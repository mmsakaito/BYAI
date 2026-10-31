#pragma once

#include "driver/i2c_master.h"

/**
 * @brief 获取板级共享 I2C 总线；首次调用负责初始化。
 * @param out_bus 成功时返回借用句柄，失败时为 NULL。
 * @return ESP_OK 或 ESP-IDF 初始化错误。
 * @note 由单一初始化任务调用，不能并发初始化；总线持有至重启，使用方不得删除。
 *       使用图纸中的外部上拉，100 kHz 设备频率由各设备配置；不修改 INT GPIO。
 */
esp_err_t board_i2c_get_bus(i2c_master_bus_handle_t *out_bus);
