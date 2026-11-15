#include "byai_ota.h"
#include "byai_ota_internal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

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

bool byai_ota_snapshot_get(byai_ota_snapshot_t *snapshot)
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

void byai_ota_upload_fail(esp_err_t error, const char *stage)
{
    byai_ota_snapshot_t snapshot;
    byai_ota_snapshot_get(&snapshot);
    snapshot.state = BYAI_OTA_FAILED;
    snapshot.error = error;
    snprintf(snapshot.message, sizeof(snapshot.message), "%s", stage);
    snapshot_store(&snapshot);
    ESP_LOGE(TAG, "%s: %s", stage, esp_err_to_name(error));
}

void byai_ota_upload_release(void)
{
    portENTER_CRITICAL(&s_lock);
    s_owner = false;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t byai_ota_upload_claim(size_t total_bytes)
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

esp_err_t byai_ota_upload_submit(httpd_req_t *request)
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
