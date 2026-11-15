#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动应用唯一的 Wi-Fi 服务及持久化 profile 管理器。
 *
 * 由 app_main 串行调用一次，不另建网络栈或重连任务。已有 profile 时复用
 * 服务内建选网；无 profile 时可从私有 wifi.local.h 导入配置并写入 NVS。
 * 首次实际运行前须取得凭据使用及持久化写入授权。本接口不擦除 NVS。
 * 成功取得的资源由本模块持有至设备重启；失败逆序清理，保留原始错误。
 * 若服务销毁失败，则保留其依赖，禁止再次启动。
 *
 * @param on_ready 可为 NULL；在服务事件线程同步调用，仅传递 IP 就绪状态。
 *                 回调必须快速返回，不访问 Flash、不等待 HTTP 或执行阻塞操作。
 *                 CONNECTED 不代表 IP 就绪，失去 IP 或断开连接时传入 false。
 * @return ESP_OK 表示本地初始化成功，不表示已联网；重复启动返回
 *         ESP_ERR_INVALID_STATE，其余情况返回首次初始化或连接请求的错误。
 */
esp_err_t byai_network_start(void (*on_ready)(bool ready));

#ifdef __cplusplus
}
#endif
