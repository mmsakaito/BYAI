#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief OTA 事务阶段；空闲时不显示升级面板。 */
typedef enum {
    BYAI_OTA_IDLE,
    BYAI_OTA_RECEIVING,
    BYAI_OTA_VERIFYING,
    BYAI_OTA_REBOOTING,
    BYAI_OTA_FAILED,
} byai_ota_state_t;

typedef struct {
    byai_ota_state_t state;
    unsigned percent;
} byai_ota_display_status_t;

/**
 * @brief 复制供显示使用的 OTA 阶段和设备接收进度。
 *
 * 可从任意任务调用；只在临界区内复制数值，不调用 LVGL 或执行 I/O。
 * @param status 调用者提供的输出缓冲；不能为 NULL。
 * @return 初始化成功且输出有效时返回 true，否则返回 false。
 */
bool byai_ota_get_display_status(byai_ota_display_status_t *status);

/**
 * @brief 初始化唯一 OTA worker、事务队列和常驻 HTTP 服务。
 *
 * 由 app_main 在网络栈本地初始化成功后串行调用，不要求已取得 IP。
 * 成功后重复调用不创建新资源。启动确认前拒绝写请求。
 * 失败时逆序释放本次取得的资源，不擦除 NVS 或修改启动分区。
 */
esp_err_t byai_ota_init(void);

/**
 * @brief 原子更新网络就绪标志；可在初始化前及网络事件回调中调用。
 *
 * 不执行网络、Flash 或阻塞操作。断网不停止 HTTP 服务。
 */
void byai_ota_set_network_ready(bool ready);

/**
 * @brief 在本地网络栈和 OTA 服务初始化结束后确认本次启动。
 *
 * startup_result 是这些初始化步骤的首个错误，不依赖路由器或外设是否可用。
 * 待确认镜像仅在初始化全部成功且 SDK 确认成功后允许升级；初始化失败时尝试
 * 回退并重启。无可回退镜像或确认失败时保留未确认状态并返回 SDK 错误。
 * 由 app_main 串行调用一次；不写入 NVS 历史。
 */
esp_err_t byai_ota_confirm_boot(esp_err_t startup_result);

#ifdef __cplusplus
}
#endif
