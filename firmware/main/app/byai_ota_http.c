#include "byai_ota_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "byai_ota_http";
static httpd_handle_t s_http_server;

#define OTA_HTTP_MAX_SOCKETS 4

/* 同步交接失败可能在一轮 session 遍历中完成四次；再保留一个正在执行的控制槽。 */
_Static_assert(CONFIG_LWIP_UDP_RECVMBOX_SIZE >= OTA_HTTP_MAX_SOCKETS + 1,
               "OTA HTTP close completion requires at least five UDP mailbox slots");

typedef struct {
    httpd_req_t *request;
    bool release_owner_on_close;
} upload_session_t;

static upload_session_t s_upload_session;
static StaticSemaphore_t s_upload_closed_storage;
static SemaphoreHandle_t s_upload_closed;

/** @brief transport context 为静态单事务存储，SDK 清理 session 时不得释放它。 */
static void upload_session_free(void *context)
{
    (void)context;
}

/**
 * @brief 由 HTTP 任务销毁仍被异步请求固定的 session；完成请求后立即关闭原 socket。
 *
 * 此服务不使用 LRU 清理或其他 queue_work 生产者。SDK complete 会在 HTTP 任务中
 * 占一个控制 mailbox 槽，因此必须满足上面的容量约束，且运行事务期间不得 stop。
 */
static void session_close(httpd_handle_t server, int socket_fd)
{
    upload_session_t *session = httpd_sess_get_transport_ctx(server, socket_fd);
    if (session != &s_upload_session) {
        /* 先发送 FIN，避免 lwIP 因未读请求体在 close 时用 RST 丢弃已排队的响应。 */
        shutdown(socket_fd, SHUT_WR);
        close(socket_fd);
        return;
    }

    httpd_req_t *request = session->request;
    bool release_owner = session->release_owner_on_close;
    session->request = NULL;
    if (request != NULL) {
        esp_err_t error = httpd_req_async_handler_complete(request);
        if (error != ESP_OK) {
            /* SDK 已释放请求；通知失败也不得再次 complete。 */
            ESP_LOGW(TAG, "Async completion notification: %s", esp_err_to_name(error));
        }
    }
    /* 保留已发送的成功/失败响应；不在 HTTP 任务内同步排空未读上传体。 */
    shutdown(socket_fd, SHUT_WR);
    close(socket_fd);

    /* 最后才交还所有权或唤醒 worker，此后不再访问可被下次事务复用的 context。 */
    if (release_owner) {
        byai_ota_upload_release();
    } else {
        xSemaphoreGive(s_upload_closed);
    }
}

void byai_ota_http_complete_upload(httpd_req_t *request)
{
    httpd_handle_t server = request->handle;
    int socket_fd = httpd_req_to_sockfd(request);
    esp_err_t error;
    /* 请求保持 async 固定，排队失败只重试关闭，不完成请求或交还事务所有权。 */
    while ((error = httpd_sess_trigger_close(server, socket_fd)) != ESP_OK) {
        ESP_LOGW(TAG, "Upload session close scheduling: %s", esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* 排队成功后 close_fn 可能已释放 request；这里只等待关闭确认，不再解引用它。 */
    xSemaphoreTake(s_upload_closed, portMAX_DELAY);
}

extern const unsigned char ota_web_start[] asm("_binary_ota_web_html_start");
extern const unsigned char ota_web_end[] asm("_binary_ota_web_html_end");

/** @brief 返回固件内嵌页面，不依赖文件系统或外部资源。 */
static esp_err_t page_handler(httpd_req_t *request)
{
    size_t length = ota_web_end - ota_web_start;
    if (length <= 1) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA page unavailable");
    }
    esp_err_t error = httpd_resp_set_type(request, "text/html; charset=utf-8");
    if (error == ESP_OK) {
        error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    }
    return error == ESP_OK ? httpd_resp_send(request, (const char *)ota_web_start, length - 1) : error;
}
typedef struct {
    char data[2048];
    size_t length;
    bool failed;
} json_buffer_t;

/** @brief 固定容量 JSON 编码，不为每秒状态轮询分配堆对象。 */
static void json_append(json_buffer_t *json, const char *text, size_t length)
{
    if (json->failed || length >= sizeof(json->data) - json->length) {
        json->failed = true;
        return;
    }
    memcpy(json->data + json->length, text, length);
    json->length += length;
    json->data[json->length] = '\0';
}

static void json_string(json_buffer_t *json, const char *text)
{
    static const char hex[] = "0123456789abcdef";
    json_append(json, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            char escaped[2] = {'\\', (char)*p};
            json_append(json, escaped, sizeof(escaped));
        } else if (*p < 0x20) {
            char escaped[6] = {'\\', 'u', '0', '0', hex[*p >> 4], hex[*p & 15]};
            json_append(json, escaped, sizeof(escaped));
        } else {
            json_append(json, (const char *)p, 1);
        }
    }
    json_append(json, "\"", 1);
}

static void json_string_field(json_buffer_t *json, const char *name, const char *value)
{
    json_string(json, name);
    json_append(json, ":", 1);
    json_string(json, value);
    json_append(json, ",", 1);
}

esp_err_t byai_ota_http_send_snapshot(httpd_req_t *request, const char *http_status,
                                      const byai_ota_snapshot_t *snapshot)
{
    static const char *const states[] = {
        "idle", "receiving", "verifying", "rebooting", "failed",
    };
    json_buffer_t json = {0};
    json_append(&json, "{", 1);
    json_string_field(&json, "boot_id", snapshot->boot_id);
    json_string_field(&json, "state", states[snapshot->state]);
    json_string_field(&json, "source", snapshot->local_source ? "local" : "");
    json_string_field(&json, "current_version", snapshot->current_version);
    json_string_field(&json, "current_elf_sha256", snapshot->current_elf_sha256);
    json_string_field(&json, "running_partition", snapshot->running_partition);
    json_string_field(&json, "update_partition", snapshot->update_partition);
    json_string_field(&json, "target_version", snapshot->target_version);
    json_string_field(&json, "target_elf_sha256", snapshot->target_elf_sha256);
    json_string_field(&json, "error", snapshot->error == ESP_OK ? "" : esp_err_to_name(snapshot->error));
    json_string_field(&json, "message", snapshot->message);

    char numbers[320];
    int count = snprintf(numbers, sizeof(numbers),
                         "\"boot_confirmed\":%s,\"network_ready\":%s,"
                         "\"max_image_size\":%" PRIu32 ",\"rollback_enabled\":%s,"
                         "\"received_bytes\":%" PRIu32 ",\"total_bytes\":%" PRIu32 ",\"percent\":%u}",
                         snapshot->boot_confirmed ? "true" : "false",
                         snapshot->network_ready ? "true" : "false", snapshot->max_image_size,
                         snapshot->rollback_enabled ? "true" : "false", snapshot->received_bytes,
                         snapshot->total_bytes, snapshot->percent);
    if (count < 0 || (size_t)count >= sizeof(numbers)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA status encoding failed");
    }
    json_append(&json, numbers, count);
    if (json.failed) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA status exceeds buffer");
    }

    esp_err_t error = httpd_resp_set_status(request, http_status);
    if (error == ESP_OK) {
        error = httpd_resp_set_type(request, "application/json; charset=utf-8");
    }
    if (error == ESP_OK) {
        error = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    }
    if (error == ESP_OK && request->method == HTTP_POST) {
        error = httpd_resp_set_hdr(request, "Connection", "close");
    }
    return error == ESP_OK ? httpd_resp_send(request, json.data, json.length) : error;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    byai_ota_snapshot_t snapshot;
    bool initialized = byai_ota_snapshot_get(&snapshot);
    if (!initialized) {
        snapshot.error = ESP_ERR_INVALID_STATE;
        snprintf(snapshot.message, sizeof(snapshot.message), "OTA 状态尚不可用");
    }
    return byai_ota_http_send_snapshot(request,
                                      initialized ? "200 OK" : "503 Service Unavailable", &snapshot);
}

/** @brief 拒绝未交接请求后返回失败，让 httpd 关闭连接而非同步排空上传 body。 */
static esp_err_t reject_upload(httpd_req_t *request, const char *http_status,
                               esp_err_t error, const char *message)
{
    byai_ota_snapshot_t snapshot;
    byai_ota_snapshot_get(&snapshot);
    snapshot.error = error;
    snprintf(snapshot.message, sizeof(snapshot.message), "%s", message);
    byai_ota_http_send_snapshot(request, http_status, &snapshot);
    return ESP_FAIL;
}

/** @brief 必须有十进制 Content-Length，禁止 multipart、chunked 或隐式长度。 */
static bool upload_headers_valid(httpd_req_t *request)
{
    char transfer_encoding[2];
    if (httpd_req_get_hdr_value_str(request, "Transfer-Encoding", transfer_encoding,
                                   sizeof(transfer_encoding)) != ESP_ERR_NOT_FOUND) {
        return false;
    }
    size_t type_length = httpd_req_get_hdr_value_len(request, "Content-Type");
    char content_type[64];
    if (type_length == 0 || type_length >= sizeof(content_type) ||
        httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcasecmp(content_type, "application/octet-stream") != 0) {
        return false;
    }
    size_t length_length = httpd_req_get_hdr_value_len(request, "Content-Length");
    char content_length[24];
    if (length_length == 0 || length_length >= sizeof(content_length) ||
        httpd_req_get_hdr_value_str(request, "Content-Length", content_length, sizeof(content_length)) != ESP_OK) {
        return false;
    }
    size_t length = 0;
    for (const char *p = content_length; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9' || length > (SIZE_MAX - (unsigned)(*p - '0')) / 10) {
            return false;
        }
        length = length * 10 + (unsigned)(*p - '0');
    }
    return length == request->content_len && length >= BYAI_OTA_PREFIX_SIZE;
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    if (!upload_headers_valid(request)) {
        return reject_upload(request, "400 Bad Request", ESP_ERR_INVALID_ARG,
                             "需要 application/octet-stream、Content-Length 及至少 288 字节的应用镜像");
    }
    esp_err_t error = byai_ota_upload_claim(request->content_len);
    if (error != ESP_OK) {
        const char *http_status = "503 Service Unavailable";
        const char *message = "本地启动尚未确认或 OTA 服务尚未就绪";
        if (error == ESP_ERR_OTA_ALREADY_IN_PROGRESS) {
            http_status = "409 Conflict";
            message = "另一个升级事务正在接收、校验或等待重启";
        } else if (error == ESP_ERR_INVALID_SIZE) {
            http_status = "413 Content Too Large";
            message = "应用镜像超过备用分区容量";
        } else {
            byai_ota_snapshot_t snapshot;
            if (byai_ota_snapshot_get(&snapshot) && snapshot.max_image_size == 0) {
                message = "缺少可用双 OTA 槽或 otadata，需先串口迁移";
            }
        }
        return reject_upload(request, http_status, error, message);
    }
    /* ESP-IDF waits for the first body byte before dispatching the URI handler.
     * Keep the worker's receive timeout short after allowing curl's Expect delay. */
    const struct timeval upload_timeout = {.tv_sec = 1, .tv_usec = 0};
    if (setsockopt(httpd_req_to_sockfd(request), SOL_SOCKET, SO_RCVTIMEO,
                   &upload_timeout, sizeof(upload_timeout)) != 0) {
        byai_ota_upload_fail(ESP_FAIL, "设置上传接收超时失败");
        reject_upload(request, "503 Service Unavailable", ESP_FAIL, "设置上传接收超时失败");
        byai_ota_upload_release();
        return ESP_FAIL;
    }


    s_upload_session.request = NULL;
    s_upload_session.release_owner_on_close = true;
    httpd_sess_set_transport_ctx(request->handle, httpd_req_to_sockfd(request),
                                 &s_upload_session, upload_session_free);
    httpd_req_t *async_request = NULL;
    error = httpd_req_async_handler_begin(request, &async_request);
    if (error != ESP_OK) {
        byai_ota_upload_fail(error, "创建异步上传请求失败");
        reject_upload(request, "503 Service Unavailable", error, "创建异步上传请求失败");
        return ESP_FAIL;
    }
    s_upload_session.request = async_request;
    s_upload_session.release_owner_on_close = false;
    error = byai_ota_upload_submit(async_request);
    if (error != ESP_OK) {
        s_upload_session.release_owner_on_close = true;
        byai_ota_upload_fail(error, "上传工作队列交接失败");
        byai_ota_snapshot_t snapshot;
        byai_ota_snapshot_get(&snapshot);
        byai_ota_http_send_snapshot(async_request, "503 Service Unavailable", &snapshot);
        /* HTTP 任务不可等待自己执行 close_fn；失败返回直接销毁原 session。 */
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t byai_ota_http_stop(void)
{
    if (s_http_server == NULL) {
        return ESP_OK;
    }
    esp_err_t error = httpd_stop(s_http_server);
    if (error == ESP_OK) {
        s_http_server = NULL;
        vSemaphoreDelete(s_upload_closed);
        s_upload_closed = NULL;
    }
    return error;
}

esp_err_t byai_ota_http_start(void)
{
    if (s_http_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = OTA_HTTP_MAX_SOCKETS;
    config.max_uri_handlers = 4;
    config.recv_wait_timeout = 2;
    config.stack_size = 8192;
    /* 仅上传 worker 排队关闭，避免 LRU 的延迟 session 指针在复用后关闭其他连接。 */
    config.lru_purge_enable = false;
    config.close_fn = session_close;
    s_upload_closed = xSemaphoreCreateBinaryStatic(&s_upload_closed_storage);
    esp_err_t error = httpd_start(&s_http_server, &config);
    if (error != ESP_OK) {
        vSemaphoreDelete(s_upload_closed);
        s_upload_closed = NULL;
        return error;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/ota", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/ota/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = upload_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        error = httpd_register_uri_handler(s_http_server, &routes[i]);
        if (error != ESP_OK) {
            esp_err_t stop_error = byai_ota_http_stop();
            if (stop_error != ESP_OK) {
                ESP_LOGE(TAG, "HTTP initialization cleanup: %s", esp_err_to_name(stop_error));
            }
            return error;
        }
    }
    return ESP_OK;
}
