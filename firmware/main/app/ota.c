#include "ota.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define BYAI_OTA_PREFIX_SIZE 288U


/** @brief 值类型快照；锁内仅复制字段，格式化、HTTP 和 Flash 操作在锁外进行。 */
typedef struct {
    char boot_id[17];
    bool boot_confirmed;
    byai_ota_state_t state;
    bool local_source;
    bool network_ready;
    char current_version[32];
    char current_elf_sha256[65];
    char running_partition[17];
    char update_partition[17];
    uint32_t max_image_size;
    bool rollback_enabled;
    char target_version[32];
    char target_elf_sha256[65];
    uint32_t received_bytes;
    uint32_t total_bytes;
    unsigned percent;
    esp_err_t error;
    char message[160];
} byai_ota_snapshot_t;

/** @brief 始终复制快照；返回 false 表示初始化尚未成功，不可对外声称状态可用。 */
static bool byai_ota_snapshot_get(byai_ota_snapshot_t *snapshot);

/**
 * @brief 非阻塞取得唯一事务，初始化、首启确认、布局和容量检查全部通过才取得。
 *
 * 成功后调用者必须提交一个异步请求，或由 HTTP close_fn 在关闭连接后释放事务。
 * worker 路径等待实际关闭的确认后才释放失败事务或开始重启延时。
 * ESP_ERR_OTA_ALREADY_IN_PROGRESS 表示 409，其余未就绪状态表示 503。
 */
static esp_err_t byai_ota_upload_claim(size_t total_bytes);

/** @brief 将已取得事务的异步请求以零等待方式交给唯一 worker；失败时所有权不变。 */
static esp_err_t byai_ota_upload_submit(httpd_req_t *request);

/** @brief 记录已取得事务的失败；不释放事务，避免旧响应被下一次上传覆盖。 */
static void byai_ota_upload_fail(esp_err_t error, const char *stage);

/** @brief worker 获得连接关闭确认后释放失败事务；HTTP 交接失败由 close_fn 释放。 */
static void byai_ota_upload_release(void);

/**
 * @brief 启动常驻 HTTP 服务并登记全部本阶段路由；登记失败停止整个服务。
 *
 * 仅在初始化路径串行调用，停止前必须确保没有已交接的异步请求。
 * 此 server 禁用 LRU；只有唯一上传 worker 可排队关闭，不允许其他 queue_work 生产者。
 * UDP 控制 mailbox 至少为 max_open_sockets + 1，保证 close_fn 中 SDK complete 有空槽。
 */
static esp_err_t byai_ota_http_start(void);
static esp_err_t byai_ota_http_stop(void);

/** @brief 以固定缓冲序列化完整快照；调用者保持 request 有效，函数不取得事务锁。 */
static esp_err_t byai_ota_http_send_snapshot(httpd_req_t *request, const char *http_status,
                                      const byai_ota_snapshot_t *snapshot);

/**
 * @brief worker 排队销毁仍固定的 session，并等待 close_fn 关闭原 socket 后确认。
 *
 * 排队失败保留 async pin 和事务所有权，只重试关闭排队；成功后不再访问 request。
 * close_fn 在 session 尚有效时恰好 complete 一次，随即 close(fd)，最后通知 worker。
 * 此函数只能由上传 worker 调用；HTTP 任务的交接失败返回 ESP_FAIL，让 close_fn
 * 直接完成异步请求并在实际关闭后释放事务，不等待自身执行关闭。
 * 静态 transport context 使用不释放内存的 free 回调，关闭确认后方可由下一事务复用。
 */
static void byai_ota_http_complete_upload(httpd_req_t *request);

static const char *TAG = "byai_ota";

_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
               sizeof(esp_app_desc_t) == BYAI_OTA_PREFIX_SIZE, "OTA prefix layout changed");

static atomic_bool s_network_ready = ATOMIC_VAR_INIT(false);
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static byai_ota_snapshot_t s_snapshot;
static char s_boot_id[17];
static bool s_initialized;
static bool s_initializing;
static bool s_owner;
static QueueHandle_t s_queue;
static TaskHandle_t s_worker;
static const esp_partition_t *s_update_partition;
static uint8_t s_receive_buffer[4096];

/** @brief 格式化均在锁外完成；临界区只发布值类型快照。 */
static void snapshot_store(const byai_ota_snapshot_t *snapshot)
{
    portENTER_CRITICAL(&s_lock);
    s_snapshot = *snapshot;
    portEXIT_CRITICAL(&s_lock);
}

static bool byai_ota_snapshot_get(byai_ota_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *snapshot = s_snapshot;
    bool initialized = s_initialized;
    portEXIT_CRITICAL(&s_lock);
    snapshot->network_ready = atomic_load_explicit(&s_network_ready, memory_order_relaxed);
    return initialized;
}

bool byai_ota_get_display_status(byai_ota_display_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    bool initialized = s_initialized;
    status->state = s_snapshot.state;
    status->percent = s_snapshot.percent;
    portEXIT_CRITICAL(&s_lock);
    return initialized;
}

void byai_ota_set_network_ready(bool ready)
{
    atomic_store_explicit(&s_network_ready, ready, memory_order_relaxed);
}

static void sha256_hex(const uint8_t digest[32], char output[65])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        output[2 * i] = hex[digest[i] >> 4];
        output[2 * i + 1] = hex[digest[i] & 15];
    }
    output[64] = '\0';
}

/** @brief 仅支持双 OTA 槽及完整 otadata；旧 factory/test 布局不能通过网页迁移。 */
static const esp_partition_t *find_update_partition(const esp_partition_t *running)
{
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    const esp_partition_t *ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *ota1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (running == NULL || running->type != ESP_PARTITION_TYPE_APP ||
        (running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
         running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) ||
        otadata == NULL || otadata->size != 0x2000 || ota0 == NULL || ota1 == NULL) {
        return NULL;
    }
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL || update->type != ESP_PARTITION_TYPE_APP ||
        (update->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
         update->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) ||
        update->address == running->address || update->size == 0) {
        return NULL;
    }
    return update;
}

static void snapshot_progress(byai_ota_snapshot_t *snapshot, size_t received)
{
    snapshot->received_bytes = received;
    snapshot->percent = snapshot->total_bytes == 0 ? 0 :
        (unsigned)((uint64_t)received * 100 / snapshot->total_bytes);
    snapshot_store(snapshot);
}

static void byai_ota_upload_fail(esp_err_t error, const char *stage)
{
    byai_ota_snapshot_t snapshot;
    byai_ota_snapshot_get(&snapshot);
    snapshot.state = BYAI_OTA_FAILED;
    snapshot.error = error;
    snprintf(snapshot.message, sizeof(snapshot.message), "%s", stage);
    snapshot_store(&snapshot);
    ESP_LOGE(TAG, "%s: %s", stage, esp_err_to_name(error));
}

static void byai_ota_upload_release(void)
{
    portENTER_CRITICAL(&s_lock);
    s_owner = false;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t byai_ota_upload_claim(size_t total_bytes)
{
    byai_ota_snapshot_t snapshot;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL(&s_lock);
    if (s_owner) {
        error = ESP_ERR_OTA_ALREADY_IN_PROGRESS;
    } else if (!s_initialized || !s_snapshot.boot_confirmed || s_update_partition == NULL) {
        error = ESP_ERR_INVALID_STATE;
    } else if (total_bytes < BYAI_OTA_PREFIX_SIZE || total_bytes > s_snapshot.max_image_size) {
        error = ESP_ERR_INVALID_SIZE;
    } else {
        s_owner = true;
        snapshot = s_snapshot;
    }
    portEXIT_CRITICAL(&s_lock);
    if (error != ESP_OK) {
        return error;
    }

    snapshot.state = BYAI_OTA_RECEIVING;
    snapshot.local_source = true;
    snapshot.target_version[0] = '\0';
    snapshot.target_elf_sha256[0] = '\0';
    snapshot.received_bytes = 0;
    snapshot.total_bytes = total_bytes;
    snapshot.percent = 0;
    snapshot.error = ESP_OK;
    snprintf(snapshot.message, sizeof(snapshot.message), "正在接收并预检应用镜像");
    snapshot_store(&snapshot);
    return ESP_OK;
}

static esp_err_t byai_ota_upload_submit(httpd_req_t *request)
{
    if (request == NULL || s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/** @brief 累计短读；仅正字节数延长无进展期限，超时不吞掉断连或其他负错误。 */
static esp_err_t receive_bytes(httpd_req_t *request, uint8_t *buffer, size_t capacity,
                               size_t *received, TickType_t *last_progress)
{
    for (;;) {
        if ((TickType_t)(xTaskGetTickCount() - *last_progress) >= pdMS_TO_TICKS(30000)) {
            return ESP_ERR_TIMEOUT;
        }
        int count = httpd_req_recv(request, (char *)buffer, capacity);
        if (count > 0) {
            if ((size_t)count > capacity) {
                return ESP_ERR_INVALID_SIZE;
            }
            *received = count;
            *last_progress = xTaskGetTickCount();
            return ESP_OK;
        }
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        return count == 0 ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
    }
}

/** @brief 通过固定前缀检查后才允许擦写；不依据版本大小拒绝重装或降级。 */
static esp_err_t validate_prefix(byai_ota_snapshot_t *snapshot)
{
    esp_image_header_t header;
    esp_image_segment_header_t segment;
    esp_app_desc_t descriptor;
    memcpy(&header, s_receive_buffer, sizeof(header));
    memcpy(&segment, s_receive_buffer + sizeof(header), sizeof(segment));
    memcpy(&descriptor, s_receive_buffer + sizeof(header) + sizeof(segment), sizeof(descriptor));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.hash_appended != 1 ||
        header.chip_id != ESP_CHIP_ID_ESP32S3 || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS || segment.data_len < sizeof(descriptor) ||
        descriptor.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        memchr(descriptor.project_name, '\0', sizeof(descriptor.project_name)) == NULL ||
        strcmp(descriptor.project_name, "BYAI") != 0 ||
        memchr(descriptor.version, '\0', sizeof(descriptor.version)) == NULL) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    esp_err_t error = esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &header, &descriptor);
    if (error != ESP_OK) {
        return error;
    }
    memcpy(snapshot->target_version, descriptor.version, sizeof(snapshot->target_version));
    sha256_hex(descriptor.app_elf_sha256, snapshot->target_elf_sha256);
    snprintf(snapshot->message, sizeof(snapshot->message), "预检通过，正在写入备用应用分区");
    snapshot_store(snapshot);
    return ESP_OK;
}

static const char *validation_http_status(esp_err_t error)
{
    return error == ESP_ERR_OTA_VALIDATE_FAILED || error == ESP_ERR_INVALID_VERSION ||
           error == ESP_ERR_OTA_SMALL_SEC_VER || error == ESP_ERR_OTA_SPI_MODE_MISMATCH ?
           "422 Unprocessable Entity" : "500 Internal Server Error";
}

/**
 * @brief worker 独占完整事务；end 后句柄无条件失效，提交后保持独占直到重启。
 *
 * 所有失败在异步请求完成之后才释放 owner；成功在响应完成后由本 worker 延时重启，
 * 不在选择新启动槽后申请额外任务或工作项。
 */
static void process_upload(httpd_req_t *request)
{
    byai_ota_snapshot_t snapshot;
    byai_ota_snapshot_get(&snapshot);
    esp_ota_handle_t handle = 0;
    bool handle_valid = false;
    size_t received = 0;
    TickType_t last_progress = xTaskGetTickCount();
    const char *stage = "接收应用镜像前缀失败";
    const char *http_status = "500 Internal Server Error";
    esp_err_t error;

    while (received < BYAI_OTA_PREFIX_SIZE) {
        size_t count = 0;
        error = receive_bytes(request, s_receive_buffer + received,
                              BYAI_OTA_PREFIX_SIZE - received, &count, &last_progress);
        if (error != ESP_OK) {
            goto failed;
        }
        received += count;
        snapshot_progress(&snapshot, received);
    }

    stage = "应用镜像前缀预检失败，未擦写备用槽";
    error = validate_prefix(&snapshot);
    if (error != ESP_OK) {
        http_status = "422 Unprocessable Entity";
        goto failed;
    }
    stage = "开始备用槽 OTA 写入失败";
    error = esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (error != ESP_OK) {
        goto failed;
    }
    handle_valid = true;
    stage = "写入应用镜像前缀失败";
    error = esp_ota_write(handle, s_receive_buffer, BYAI_OTA_PREFIX_SIZE);
    if (error != ESP_OK) {
        goto failed;
    }

    while (received < snapshot.total_bytes) {
        size_t capacity = snapshot.total_bytes - received;
        if (capacity > sizeof(s_receive_buffer)) {
            capacity = sizeof(s_receive_buffer);
        }
        size_t count = 0;
        stage = "接收应用镜像失败或 30 秒无进展";
        error = receive_bytes(request, s_receive_buffer, capacity, &count, &last_progress);
        if (error != ESP_OK) {
            goto failed;
        }
        if (count > snapshot.total_bytes - received ||
            count > s_update_partition->size - received) {
            error = ESP_ERR_INVALID_SIZE;
            goto failed;
        }
        received += count;
        stage = "写入备用应用分区失败";
        error = esp_ota_write(handle, s_receive_buffer, count);
        snapshot_progress(&snapshot, received);
        if (error != ESP_OK) {
            goto failed;
        }
    }
    if (received != snapshot.total_bytes || received > s_update_partition->size) {
        error = ESP_ERR_INVALID_SIZE;
        stage = "应用镜像实际长度与声明长度不符";
        goto failed;
    }

    snapshot.state = BYAI_OTA_VERIFYING;
    snprintf(snapshot.message, sizeof(snapshot.message), "正在校验完整镜像与附加 SHA-256");
    snapshot_store(&snapshot);
    stage = "SDK 完整镜像校验失败";
    error = esp_ota_end(handle);
    handle_valid = false;
    if (error != ESP_OK) {
        http_status = validation_http_status(error);
        goto failed;
    }
    stage = "提交下次启动分区失败";
    error = esp_ota_set_boot_partition(s_update_partition);
    if (error != ESP_OK) {
        http_status = validation_http_status(error);
        goto failed;
    }

    snapshot.state = BYAI_OTA_REBOOTING;
    snapshot.error = ESP_OK;
    snprintf(snapshot.message, sizeof(snapshot.message), "镜像已提交，设备即将重启；需核对重启后的摘要");
    snapshot_store(&snapshot);
    byai_ota_snapshot_get(&snapshot);
    error = byai_ota_http_send_snapshot(request, "200 OK", &snapshot);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Committed upload response: %s", esp_err_to_name(error));
    }
    byai_ota_http_complete_upload(request);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return;

failed:
    if (handle_valid) {
        esp_err_t abort_error = esp_ota_abort(handle);
        if (abort_error != ESP_OK) {
            ESP_LOGE(TAG, "OTA abort: %s", esp_err_to_name(abort_error));
        }
    }
    byai_ota_upload_fail(error, stage);
    byai_ota_snapshot_get(&snapshot);
    byai_ota_http_send_snapshot(request, http_status, &snapshot);
    byai_ota_http_complete_upload(request);
    byai_ota_upload_release();
}

static void ota_worker(void *argument)
{
    (void)argument;
    for (;;) {
        httpd_req_t *request = NULL;
        if (xQueueReceive(s_queue, &request, portMAX_DELAY) == pdTRUE) {
            process_upload(request);
        }
    }
}

esp_err_t byai_ota_init(void)
{
    portENTER_CRITICAL(&s_lock);
    bool initialized = s_initialized;
    bool initializing = s_initializing;
    if (!initialized && !initializing) {
        s_initializing = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (initialized || initializing) {
        return initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    byai_ota_snapshot_t snapshot = {0};
    if (s_boot_id[0] == '\0') {
        uint32_t boot_random[2];
        esp_fill_random(boot_random, sizeof(boot_random));
        snprintf(s_boot_id, sizeof(s_boot_id), "%08lx%08lx",
                 (unsigned long)boot_random[0], (unsigned long)boot_random[1]);
    }
    memcpy(snapshot.boot_id, s_boot_id, sizeof(snapshot.boot_id));
    const esp_app_desc_t *description = esp_app_get_description();
    memcpy(snapshot.current_version, description->version, sizeof(snapshot.current_version));
    snapshot.current_version[sizeof(snapshot.current_version) - 1] = '\0';
    sha256_hex(description->app_elf_sha256, snapshot.current_elf_sha256);
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        snprintf(snapshot.running_partition, sizeof(snapshot.running_partition), "%s", running->label);
    }
    s_update_partition = find_update_partition(running);
    if (s_update_partition != NULL) {
        snprintf(snapshot.update_partition, sizeof(snapshot.update_partition), "%s", s_update_partition->label);
        snapshot.max_image_size = s_update_partition->size;
    }
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    snapshot.rollback_enabled = true;
#endif
    snapshot.state = BYAI_OTA_IDLE;
    snapshot.error = s_update_partition == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
    snprintf(snapshot.message, sizeof(snapshot.message), "%s", s_update_partition == NULL ?
             "缺少可用双 OTA 槽或 otadata，需先串口迁移" : "等待本地启动确认");
    snapshot_store(&snapshot);

    esp_err_t error = ESP_ERR_NO_MEM;
    s_queue = xQueueCreate(1, sizeof(httpd_req_t *));
    if (s_queue == NULL) {
        goto failed;
    }
    if (xTaskCreate(ota_worker, "byai_ota", 8192, NULL, 5, &s_worker) != pdPASS) {
        goto failed;
    }
    error = byai_ota_http_start();
    if (error != ESP_OK) {
        goto failed;
    }
    portENTER_CRITICAL(&s_lock);
    s_initialized = true;
    s_initializing = false;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;

failed:
    /* HTTP start 在路由登记失败时先停服务；未完成初始化时写请求始终被拒绝。 */
    if (s_worker != NULL) {
        vTaskDelete(s_worker);
        s_worker = NULL;
    }
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    byai_ota_upload_fail(error, "OTA 服务初始化失败");
    portENTER_CRITICAL(&s_lock);
    s_initializing = false;
    portEXIT_CRITICAL(&s_lock);
    return error;
}

esp_err_t byai_ota_confirm_boot(esp_err_t startup_result)
{
    byai_ota_snapshot_t snapshot;
    bool initialized = byai_ota_snapshot_get(&snapshot);
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_error = running == NULL ? ESP_ERR_INVALID_STATE :
        esp_ota_get_state_partition(running, &state);
    if (startup_result == ESP_OK && !initialized) {
        startup_result = ESP_ERR_INVALID_STATE;
    }
    /* 空白 otadata 的首次串口启动没有状态记录；factory 的状态查询不受支持。 */
    if (startup_result == ESP_OK && state_error != ESP_OK &&
        state_error != ESP_ERR_NOT_FOUND && state_error != ESP_ERR_NOT_SUPPORTED) {
        startup_result = state_error;
    }
    if (startup_result != ESP_OK) {
        byai_ota_upload_fail(startup_result, "本地启动初始化失败，未确认本次启动");
        if (state_error == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t rollback_error = esp_ota_mark_app_invalid_rollback_and_reboot();
            byai_ota_upload_fail(rollback_error, "启动失败且 SDK 回退重启未完成");
            return rollback_error == ESP_OK ? startup_result : rollback_error;
        }
        return startup_result;
    }
    if (state_error == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t error = esp_ota_mark_app_valid_cancel_rollback();
        if (error != ESP_OK) {
            byai_ota_upload_fail(error, "SDK 首启确认失败，禁止新的升级");
            return error;
        }
    }
    snapshot.boot_confirmed = true;
    if (s_update_partition != NULL) {
        snapshot.error = ESP_OK;
        snprintf(snapshot.message, sizeof(snapshot.message), "本地启动已确认，可以上传应用镜像");
    }
    snapshot_store(&snapshot);
    return ESP_OK;
}

static const char *HTTP_TAG = "byai_ota_http";
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
            ESP_LOGW(HTTP_TAG, "Async completion notification: %s", esp_err_to_name(error));
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

static void byai_ota_http_complete_upload(httpd_req_t *request)
{
    httpd_handle_t server = request->handle;
    int socket_fd = httpd_req_to_sockfd(request);
    esp_err_t error;
    /* 请求保持 async 固定，排队失败只重试关闭，不完成请求或交还事务所有权。 */
    while ((error = httpd_sess_trigger_close(server, socket_fd)) != ESP_OK) {
        ESP_LOGW(HTTP_TAG, "Upload session close scheduling: %s", esp_err_to_name(error));
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

static esp_err_t byai_ota_http_send_snapshot(httpd_req_t *request, const char *http_status,
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

static esp_err_t byai_ota_http_stop(void)
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

static esp_err_t byai_ota_http_start(void)
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
                ESP_LOGE(HTTP_TAG, "HTTP initialization cleanup: %s", esp_err_to_name(stop_error));
            }
            return error;
        }
    }
    return ESP_OK;
}
