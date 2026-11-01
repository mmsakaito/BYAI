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

typedef struct {
    bool valid;
    uint8_t raw_vbus;
    uint8_t raw_sys;
    uint16_t vbus_mv;
    uint16_t sys_mv;
} bq25890_voltages_t;

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
 * @brief 调试专用：清除 REG07 WATCHDOG[5:4]，回读确认整字节与预期一致。
 * @param device 已注册的 BQ25890 句柄；与其他驱动调用在同一任务串行执行。
 * @return ESP_OK、ESP_ERR_INVALID_RESPONSE（回读不符）或身份/I2C 错误。
 * @note 保留 REG07 其他位，包括充电安全计时器；写入会进入主机模式。
 *       错误时也可能已写入，调用方须停止 ADC 并报告，不能假定硬件未改变。
 *       释放句柄或 ESP32 重启不会主动恢复看门狗；须显式重配或芯片 POR。
 *       无软件失联后的自动默认配置回退，不可作为正式充电策略。
 */
esp_err_t bq25890_disable_watchdog_for_debug(i2c_master_dev_handle_t device);

/**
 * @brief USB 输入有效时开启约 1.2 秒连续 ADC 窗口，读取 VBUS/SYS 后关闭连续模式。
 * @param device 已注册设备；调用者独占 ADC，所有驱动操作在同一任务串行执行。
 * @param voltages 完整成功且停止连续模式回读成功才置 valid；失败清空全部字段。
 * @return ESP_OK、ESP_ERR_INVALID_STATE（无输入、连续模式/DPDM 忙或看门狗未关闭）、
 *         ESP_ERR_INVALID_RESPONSE 或身份/I2C 错误。
 * @note 调用前关闭并确认芯片 I2C 看门狗。本函数不喂狗，不修改充电参数。
 *       只写 REG02 ADC 位，保留其他控制位，不重放 DPDM 命令。
 *       按手册等待完整更新周期，不以连续转换中的 CONV_START 清零作为完成条件。
 *       I2C 失败时尽力关闭连续模式；清理失败报错，硬件可能仍转换，不能保证回滚。
 *       不负责判断电池是否存在，不读取电量/电流；USB 拔出时不得使用采样值。
 */
esp_err_t bq25890_read_voltages(i2c_master_dev_handle_t device, bq25890_voltages_t *voltages);

/**
 * @brief 移除本驱动设备，成功置 NULL；不会删除共享总线。
 * @note 先停止所有使用该句柄的读取，不得与读取并发调用。
 */
esp_err_t bq25890_deinit(i2c_master_dev_handle_t *device);
