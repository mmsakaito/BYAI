#include "bq25890.h"

#include <string.h>

#define BQ25890_TIMEOUT_MS 100
#define BQ25890_REG_STATUS 0x0b
#define BQ25890_REG_FAULT 0x0c
#define BQ25890_REG_IDENTITY 0x14

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
