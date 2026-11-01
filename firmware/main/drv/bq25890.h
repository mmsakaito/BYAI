#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#define BQ25890_I2C_ADDRESS 0x6a

typedef struct {
    uint8_t raw;
    uint8_t part_number;
    uint8_t revision;
} bq25890_identity_t;

typedef struct {
    uint8_t raw;
    bool watchdog;
    bool boost;
    uint8_t charge;  /**< REG0C CHRG_FAULT: 0 正常、1 输入、2 热关断、3 安全定时器。 */
    bool battery;
    uint8_t ntc;     /**< 保留 NTC_FAULT 原始编码，不能据此推断电池是否连接。 */
} bq25890_fault_t;

typedef struct {
    bool valid;
    bq25890_identity_t identity;
    uint8_t raw_status;
    uint8_t input_source; /**< REG0B VBUS_STAT，按 BQ25890 编码。 */
    uint8_t charge_state; /**< REG0B CHRG_STAT: 0 未充电、1 预充、2 快充、3 完成。 */
    bool power_good;
    bool system_min_regulation;
    bq25890_fault_t historical_fault;
    bq25890_fault_t current_fault;
} bq25890_status_t;

/**
 * @brief 在借用总线上注册 0x6A 设备并检查 BQ25890 身份。
 * @param bus 板级总线，调用者保持其生命周期。
 * @param out_device 成功后归调用者持有；失败为 NULL，无有效设备遗留。
 * @return ESP_OK、ESP_ERR_NOT_SUPPORTED（型号不符）或 I2C/资源错误。
 * @note 同一设备只注册一次；失败回收设备，不写芯片配置。
 */
esp_err_t bq25890_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_device);

/**
 * @brief 同步读取 REG14 身份，事务超时 100 ms；失败时清空输出。
 * @note 仅接受 PN=011 的 BQ25890；与读取/释放在同一个任务串行调用。
 */
esp_err_t bq25890_read_identity(i2c_master_dev_handle_t device, bq25890_identity_t *identity);

/**
 * @brief 读取身份、REG0B 和两次 REG0C；完整成功才置 valid。
 * @return 任意一步失败则清空整个输出并返回错误，不返回上次成功数据。
 * @note 单任务轮询；REG0C 第一次读取消费锁存故障，第二次为当前故障
 *       （NTC 始终为当前值），失败也可能已经消费历史故障。无寄存器数据写入。
 */
esp_err_t bq25890_read_status(i2c_master_dev_handle_t device, bq25890_status_t *status);

/**
 * @brief 移除本驱动设备，成功置 NULL；不会删除共享总线。
 * @note 先停止所有使用该句柄的读取，不得与读取并发调用。
 */
esp_err_t bq25890_deinit(i2c_master_dev_handle_t *device);
