#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define BYAI_OTA_PREFIX_SIZE 288U

typedef enum {
    BYAI_OTA_IDLE,
    BYAI_OTA_RECEIVING,
    BYAI_OTA_VERIFYING,
    BYAI_OTA_REBOOTING,
    BYAI_OTA_FAILED,
} byai_ota_state_t;

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
bool byai_ota_snapshot_get(byai_ota_snapshot_t *snapshot);

/**
 * @brief 非阻塞取得唯一事务，初始化、首启确认、布局和容量检查全部通过才取得。
 *
 * 成功后调用者必须提交一个异步请求，或由 HTTP close_fn 在关闭连接后释放事务。
 * worker 路径等待实际关闭的确认后才释放失败事务或开始重启延时。
 * ESP_ERR_OTA_ALREADY_IN_PROGRESS 表示 409，其余未就绪状态表示 503。
 */
esp_err_t byai_ota_upload_claim(size_t total_bytes);

/** @brief 将已取得事务的异步请求以零等待方式交给唯一 worker；失败时所有权不变。 */
esp_err_t byai_ota_upload_submit(httpd_req_t *request);

/** @brief 记录已取得事务的失败；不释放事务，避免旧响应被下一次上传覆盖。 */
void byai_ota_upload_fail(esp_err_t error, const char *stage);

/** @brief worker 获得连接关闭确认后释放失败事务；HTTP 交接失败由 close_fn 释放。 */
void byai_ota_upload_release(void);

/**
 * @brief 启动常驻 HTTP 服务并登记全部本阶段路由；登记失败停止整个服务。
 *
 * 仅在初始化路径串行调用，停止前必须确保没有已交接的异步请求。
 * 此 server 禁用 LRU；只有唯一上传 worker 可排队关闭，不允许其他 queue_work 生产者。
 * UDP 控制 mailbox 至少为 max_open_sockets + 1，保证 close_fn 中 SDK complete 有空槽。
 */
esp_err_t byai_ota_http_start(void);
esp_err_t byai_ota_http_stop(void);

/** @brief 以固定缓冲序列化完整快照；调用者保持 request 有效，函数不取得事务锁。 */
esp_err_t byai_ota_http_send_snapshot(httpd_req_t *request, const char *http_status,
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
void byai_ota_http_complete_upload(httpd_req_t *request);
