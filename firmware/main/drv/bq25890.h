/**
 * @file bq25890.h
 * @brief BQ25890 设备生命周期、状态读取与调试 ADC 接口。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#define BQ25890_I2C_ADDRESS 0x6a

/** @brief REG14 的 PN 和修订字段；具体封装与同 PN 变体由芯片实物标识确认。 */
typedef struct {
    uint8_t raw;
    uint8_t part_number;
    uint8_t revision;
} bq25890_identity_t;

/** @brief REG0C 解码；历史与当前故障由读取次序区分。 */
typedef struct {
    uint8_t raw;
    bool watchdog;
    bool boost;
    uint8_t charge;  /**< REG0C CHRG_FAULT: 0 正常、1 输入、2 热关断、3 安全定时器。 */
    bool battery;
    uint8_t ntc;     /**< REG0C NTC_FAULT 原始编码，表示 TS 状态；电池连接状态须单独检测。 */
} bq25890_fault_t;

/** @brief 完整成功才置 valid 的状态样本；失败输出清零。 */
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

/** @brief 采样及停止连续转换均成功才有效的电压样本，单位毫伏。 */
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
 * @param out_device 成功后归调用者持有；非 NULL 输出在失败时置 NULL。
 * @return ESP_OK、ESP_ERR_NOT_SUPPORTED（型号不符）或 I2C/资源错误。
 * @note 同一设备只注册一次；身份检查失败时调用设备移除并返回身份检查错误。
 *       移除结果被忽略，移除失败会遗留设备资源。初始化仅执行探测和寄存器读取。
 */
esp_err_t bq25890_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_device);

/**
 * @brief 同步读取 REG14 身份，事务超时 100 ms；失败时清空输出。
 * @note 仅接受 PN=011 的 BQ25890；与读取/释放在同一个任务串行调用。
 */
esp_err_t bq25890_read_identity(i2c_master_dev_handle_t device, bq25890_identity_t *identity);

/**
 * @brief 读取身份、REG0B 和两次 REG0C；完整成功才置 valid。
 * @return 任意一步失败则清空整个输出并返回错误。
 * @note 单任务轮询；REG0C 第一次读取消费锁存故障，第二次为当前故障
 *       （NTC 始终为当前值），后续步骤失败时历史故障也可能已被消费。仅执行寄存器读取。
 */
esp_err_t bq25890_read_status(i2c_master_dev_handle_t device, bq25890_status_t *status);

/**
 * @brief 调试专用：清除 REG07 WATCHDOG[5:4]，回读确认整字节与预期一致。
 * @param device 已注册的 BQ25890 句柄；与其他驱动调用在同一任务串行执行。
 * @return ESP_OK、ESP_ERR_INVALID_RESPONSE（回读不符）或身份/I2C 错误。
 * @note 保留 REG07 其他位，包括充电安全计时器；写入会进入主机模式。
 *       写入阶段报错时芯片也可能已接受配置，调用方须停止 ADC 并报告。
 *       看门狗保持关闭直至显式重配或芯片 POR，该状态跨句柄释放和 ESP32 重启持续。
 *       关闭看门狗后软件失联时芯片继续使用现有配置；正式充电策略须另行实现失联处置。
 */
esp_err_t bq25890_disable_watchdog_for_debug(i2c_master_dev_handle_t device);

/**
 * @brief ADC 独占任务启动时停止复位遗留的连续转换，并回读确认。
 * @param device 已注册设备；调用者独占 ADC，与其他操作串行执行。
 * @return ESP_OK、ESP_ERR_INVALID_STATE（DPDM 正在执行）、回读或 I2C 错误。
 * @note 仅清 REG02 ADC 位，保留输入检测和其他配置；DPDM 忙时返回错误。
 *       CONV_START 可保持置位；连续模式停止及其余配置必须回读一致。
 *       写入阶段报错时硬件也可能已改变，调用者须停止测试并报告。
 */
esp_err_t bq25890_prepare_adc(i2c_master_dev_handle_t device);

/**
 * @brief USB 输入有效时开启约 1.2 秒连续 ADC 窗口，读取 VBUS/SYS 后关闭连续模式。
 * @param device 已注册设备；调用者独占 ADC，所有驱动操作在同一任务串行执行。
 * @param voltages 完整成功且停止连续模式回读成功才置 valid；失败清空全部字段。
 * @return ESP_OK、ESP_ERR_INVALID_STATE（无输入、连续模式/DPDM 忙或看门狗未关闭）、
 *         ESP_ERR_INVALID_RESPONSE 或身份/I2C 错误。
 * @note 调用前关闭并确认芯片 I2C 看门狗；窗口内保持看门狗关闭和充电参数原值。
 *       只写 REG02 ADC 位，保留其他控制位；DPDM 忙时返回错误。
 *       按手册等待完整更新周期；连续模式的完成判据为等待时长及状态检查。
 *       I2C 失败后尝试关闭连续模式；清理失败返回清理错误，连续转换可能保持开启。
 *       返回 VBUS/SYS 电压；电池连接、电量和电流须另行获取，USB 拔出时丢弃采样值。
 */
esp_err_t bq25890_read_voltages(i2c_master_dev_handle_t device, bq25890_voltages_t *voltages);

/**
 * @brief 移除本驱动设备，成功置 NULL；共享总线仍由板级模块持有。
 * @note 先停止所有使用该句柄的读取，再串行释放设备。
 */
esp_err_t bq25890_deinit(i2c_master_dev_handle_t *device);
