#include "bq25890.h"

#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bq25890";

#define BQ25890_TIMEOUT_MS 100
#define BQ25890_REG_STATUS 0x0b
#define BQ25890_REG_FAULT 0x0c
#define BQ25890_REG_IDENTITY 0x14
#define BQ25890_REG_ADC_CONTROL 0x02
#define BQ25890_REG_TIMER_CONTROL 0x07
#define BQ25890_REG_SYS_VOLTAGE 0x0f
#define BQ25890_REG_VBUS_VOLTAGE 0x11
#define BQ25890_CONV_START 0x80
#define BQ25890_CONV_RATE 0x40
#define BQ25890_FORCE_DPDM 0x02
#define BQ25890_WATCHDOG_MASK 0x30
#define BQ25890_PG_STAT 0x04
#define BQ25890_VBUS_GD 0x80
#define BQ25890_ADC_WINDOW_MS 1200

/** @brief 发送一字节寄存器地址后重复起始读取；不会写入寄存器数据。 */
static esp_err_t read_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(device, &reg, 1, value, 1, BQ25890_TIMEOUT_MS);
}

static bq25890_fault_t decode_fault(uint8_t raw)
{
    return (bq25890_fault_t) {
        .raw = raw,
        .watchdog = (raw & 0x80) != 0,
        .boost = (raw & 0x40) != 0,
        .charge = (raw >> 4) & 0x03,
        .battery = (raw & 0x08) != 0,
        .ntc = raw & 0x07,
    };
}

esp_err_t bq25890_read_identity(i2c_master_dev_handle_t device, bq25890_identity_t *identity)
{
    if (identity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(identity, 0, sizeof(*identity));
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t raw;
    esp_err_t err = read_register(device, BQ25890_REG_IDENTITY, &raw);
    if (err != ESP_OK) {
        return err;
    }
    if (((raw >> 3) & 0x07) != 0x03) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *identity = (bq25890_identity_t) {
        .raw = raw,
        .part_number = (raw >> 3) & 0x07,
        .revision = raw & 0x03,
    };
    return ESP_OK;
}

esp_err_t bq25890_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_device)
{
    if (out_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_device = NULL;
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = i2c_master_probe(bus, BQ25890_I2C_ADDRESS, BQ25890_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ25890_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t device;
    err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) {
        return err;
    }
    bq25890_identity_t identity;
    err = bq25890_read_identity(device, &identity);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(device);
        return err;
    }
    *out_device = device;
    return ESP_OK;
}

esp_err_t bq25890_read_status(i2c_master_dev_handle_t device, bq25890_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    bq25890_status_t sample = {0};
    esp_err_t err = bq25890_read_identity(device, &sample.identity);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(device, BQ25890_REG_STATUS, &sample.raw_status);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t historical, current;
    err = read_register(device, BQ25890_REG_FAULT, &historical);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(device, BQ25890_REG_FAULT, &current);
    if (err != ESP_OK) {
        return err;
    }
    sample.input_source = (sample.raw_status >> 5) & 0x07;
    sample.charge_state = (sample.raw_status >> 3) & 0x03;
    sample.power_good = (sample.raw_status & 0x04) != 0;
    sample.system_min_regulation = (sample.raw_status & 0x01) != 0;
    sample.historical_fault = decode_fault(historical);
    sample.current_fault = decode_fault(current);
    sample.valid = true;
    *status = sample;
    return ESP_OK;
}

esp_err_t bq25890_deinit(i2c_master_dev_handle_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*device == NULL) {
        return ESP_OK;
    }
    esp_err_t err = i2c_master_bus_rm_device(*device);
    if (err == ESP_OK) {
        *device = NULL;
    }
    return err;
}

/** @brief 写入控制寄存器；调用方须确认身份并串行读改写，保留其他控制位。 */
static esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), BQ25890_TIMEOUT_MS);
}

/**
 * @brief ADC 诊断读取：失败时记录阶段和地址，不输出未初始化的寄存器值。
 * @note 不额外读取 REG0C，避免诊断日志消费锁存故障；单任务调用。
 */
static esp_err_t read_adc_register(i2c_master_dev_handle_t device, uint8_t reg,
                                 uint8_t *value, const char *stage)
{
    esp_err_t err = read_register(device, reg, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC stage=%s read reg=0x%02x error=%s", stage, reg, esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 诊断快照：逐个读取输入/ADC 寄存器，跳过会消费锁存故障的 REG0C。
 * @note 仅用于排查，转换完成未确认时原始 ADC 值不作为有效测量返回。
 */
static void log_adc_snapshot(i2c_master_dev_handle_t device, const char *stage)
{
    static const uint8_t registers[] = {0x00, 0x02, 0x03, 0x07, 0x0b, 0x0d,
                                        0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14};
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        uint8_t value;
        esp_err_t err = read_adc_register(device, registers[i], &value, stage);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "ADC snapshot=%s reg=0x%02x raw=0x%02x diagnostic_only=1", stage, registers[i], value);
        }
    }
}

esp_err_t bq25890_disable_watchdog_for_debug(i2c_master_dev_handle_t device)
{
    bq25890_identity_t identity;
    esp_err_t err = bq25890_read_identity(device, &identity);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t original, actual;
    err = read_register(device, BQ25890_REG_TIMER_CONTROL, &original);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t expected = original & ~BQ25890_WATCHDOG_MASK;
    if (original != expected) {
        err = write_register(device, BQ25890_REG_TIMER_CONTROL, expected);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = read_register(device, BQ25890_REG_TIMER_CONTROL, &actual);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "watchdog setup REG07 before=0x%02x expected=0x%02x actual=0x%02x WATCHDOG=%u EN_TIMER=%u",
             original, expected, actual, (actual & BQ25890_WATCHDOG_MASK) >> 4,
             (unsigned)((actual & 0x08) != 0));
    return actual == expected ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t bq25890_read_voltages(i2c_master_dev_handle_t device, bq25890_voltages_t *voltages)
{
    if (voltages == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(voltages, 0, sizeof(*voltages));
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    bq25890_identity_t identity;
    esp_err_t err = bq25890_read_identity(device, &identity);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC stage=identity error=%s", esp_err_to_name(err));
        return err;
    }
    uint8_t status, control, timer;
    err = read_adc_register(device, BQ25890_REG_STATUS, &status, "input_before");
    if (err != ESP_OK) {
        return err;
    }
    if (!(status & BQ25890_PG_STAT)) {
        ESP_LOGW(TAG, "ADC rejected: input_not_good REG0B=0x%02x", status);
        return ESP_ERR_INVALID_STATE;
    }
    err = read_adc_register(device, BQ25890_REG_ADC_CONTROL, &control, "control_before");
    if (err != ESP_OK) {
        return err;
    }
    /* 不接管其他使用者的连续转换，也不重放 DPDM 命令。 */
    if (control & (BQ25890_CONV_RATE | BQ25890_FORCE_DPDM)) {
        ESP_LOGW(TAG, "ADC rejected: control_busy REG02=0x%02x", control);
        return ESP_ERR_INVALID_STATE;
    }
    err = read_adc_register(device, BQ25890_REG_TIMER_CONTROL, &timer, "watchdog_check");
    if (err != ESP_OK) {
        return err;
    }
    if (timer & BQ25890_WATCHDOG_MASK) {
        ESP_LOGW(TAG, "ADC rejected: watchdog_enabled REG07=0x%02x", timer);
        return ESP_ERR_INVALID_STATE;
    }
    log_adc_snapshot(device, "trial2_before");
    /* Rev D §9.2.8：CONV_RATE=1 每秒更新结果，CONV_START 不作为完成条件。
     * 只在本次调用内启用连续转换，成功或失败均尝试清除；不改输入检测或充电位。
     */
    const uint8_t start = (control & ~BQ25890_CONV_START) | BQ25890_CONV_RATE;
    ESP_LOGI(TAG, "ADC trial2 continuous_window start REG02_write=0x%02x wait_ms=%u", start, BQ25890_ADC_WINDOW_MS);
    const int64_t started = esp_timer_get_time();
    err = write_register(device, BQ25890_REG_ADC_CONTROL, start);
    bq25890_voltages_t sample = {0};
    uint8_t active = 0;
    if (err == ESP_OK) {
        err = read_adc_register(device, BQ25890_REG_ADC_CONTROL, &active, "continuous_start_readback");
    }
    if (err == ESP_OK && !(active & BQ25890_CONV_RATE)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        /* 向上取整，避免低 tick 频率时等待不足一个转换周期。 */
        vTaskDelay(pdMS_TO_TICKS(BQ25890_ADC_WINDOW_MS) + 1);
        err = read_adc_register(device, BQ25890_REG_ADC_CONTROL, &active, "continuous_after_wait");
    }
    if (err == ESP_OK && !(active & BQ25890_CONV_RATE)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = read_adc_register(device, BQ25890_REG_SYS_VOLTAGE, &sample.raw_sys, "result_sys");
    }
    if (err == ESP_OK) {
        err = read_adc_register(device, BQ25890_REG_VBUS_VOLTAGE, &sample.raw_vbus, "result_vbus");
    }
    if (err == ESP_OK) {
        err = read_adc_register(device, BQ25890_REG_STATUS, &status, "input_after");
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ADC trial2 result REG02=0x%02x REG0B=0x%02x REG0F=0x%02x REG11=0x%02x elapsed_ms=%" PRId64,
                 active, status, sample.raw_sys, sample.raw_vbus, (esp_timer_get_time() - started) / 1000);
        if (!(status & BQ25890_PG_STAT) || !(sample.raw_vbus & BQ25890_VBUS_GD)) {
            err = ESP_ERR_INVALID_STATE;
        } else if (sample.raw_sys & 0x80) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    /* 即使开始写入返回错误，也可能已改变芯片，仍尝试收尾。
     * 仅清 ADC 两位及自清命令 FORCE_DPDM，避免回放命令；保留其他最新位。
     */
    uint8_t stop_control;
    esp_err_t stop_err = read_adc_register(device, BQ25890_REG_ADC_CONTROL, &stop_control, "continuous_stop_read");
    if (stop_err == ESP_OK) {
        stop_err = write_register(device, BQ25890_REG_ADC_CONTROL,
                                  stop_control & ~(BQ25890_CONV_RATE | BQ25890_CONV_START | BQ25890_FORCE_DPDM));
    }
    if (stop_err == ESP_OK) {
        stop_err = read_adc_register(device, BQ25890_REG_ADC_CONTROL, &stop_control, "continuous_stop_verify");
        if (stop_err == ESP_OK && (stop_control & BQ25890_CONV_RATE)) {
            stop_err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (stop_err != ESP_OK) {
        ESP_LOGE(TAG, "ADC continuous cleanup failed=%s; hardware may still be converting", esp_err_to_name(stop_err));
        return stop_err;
    }
    ESP_LOGI(TAG, "ADC continuous stopped REG02=0x%02x error=%s", stop_control, esp_err_to_name(err));
    if (err != ESP_OK) {
        log_adc_snapshot(device, "trial2_failed");
        return err;
    }
    sample.vbus_mv = 2600 + (sample.raw_vbus & 0x7f) * 100;
    sample.sys_mv = 2304 + (sample.raw_sys & 0x7f) * 20;
    sample.valid = true;
    *voltages = sample;
    return ESP_OK;
}
