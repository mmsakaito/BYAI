/**
 * @file board_i2c.h
 * @brief 板级共享 I2C 总线的借用接口。
 */
#pragma once

#include "driver/i2c_master.h"

/**
 * @brief 获取板级共享 I2C 总线；首次调用负责初始化。
 * @param out_bus 非 NULL 输出指针；成功时返回借用句柄，初始化失败时为 NULL。
 * @return ESP_OK、空指针对应的 ESP_ERR_INVALID_ARG，或 ESP-IDF 初始化错误。
 * @note 由单一初始化任务调用，不能并发初始化；总线持有至重启，使用方不得删除。
 *       禁用内部上拉，要求板上外部上拉；设备频率由各设备配置，不修改 INT GPIO。
 */
esp_err_t board_i2c_get_bus(i2c_master_bus_handle_t *out_bus);
