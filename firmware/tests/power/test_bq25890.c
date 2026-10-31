/* Host-only I2C fault injection; real ESP-IDF types are checked by the firmware build. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bq25890.h"

static int token;
static const uint8_t expected_regs[] = {0x14, 0x0b, 0x0c, 0x0c};
static uint8_t values[4];
static int reads, fail_read, added, removed;
static esp_err_t read_error, probe_error, add_error, remove_error;

static void reset_mock(void)
{
    values[0] = 0x1d; /* PN=3, revision=1, TS_PROFILE=1. */
    values[1] = 0x75; /* DCP, fast charge, PG=1, VSYSMIN=1. */
    values[2] = 0xfa;
    values[3] = 0x02;
    reads = added = removed = 0;
    fail_read = -1;
    read_error = ESP_ERR_TIMEOUT;
    probe_error = add_error = remove_error = ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    assert(bus == &token && address == 0x6a && timeout == 100);
    return probe_error;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *device)
{
    assert(bus == &token && config->device_address == 0x6a);
    assert(config->scl_speed_hz == 100000 && config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    if (add_error != ESP_OK) {
        return add_error;
    }
    ++added;
    *device = &token;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &token);
    ++removed;
    return remove_error;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *tx,
                                    size_t tx_size, uint8_t *rx, size_t rx_size, int timeout)
{
    assert(device == &token && reads < 4);
    /* A register pointer only, never a register data write or a multi-read. */
    assert(tx_size == 1 && rx_size == 1 && timeout == 100);
    assert(*tx == expected_regs[reads]);
    int index = reads++;
    if (index == fail_read) {
        return read_error;
    }
    *rx = values[index];
    return ESP_OK;
}

static void assert_empty(const bq25890_status_t *status)
{
    const bq25890_status_t empty = {0};
    assert(memcmp(status, &empty, sizeof(empty)) == 0);
}

int main(void)
{
    reset_mock();
    i2c_master_dev_handle_t device = NULL;
    assert(bq25890_init(&token, &device) == ESP_OK);
    assert(device == &token && added == 1 && reads == 1);
    assert(bq25890_deinit(&device) == ESP_OK && device == NULL && removed == 1);
    assert(bq25890_deinit(&device) == ESP_OK && removed == 1);

    reset_mock();
    bq25890_status_t status;
    assert(bq25890_read_status(&token, &status) == ESP_OK);
    assert(reads == 4 && status.valid && status.identity.part_number == 3);
    assert(status.identity.revision == 1 && status.identity.raw == 0x1d);
    assert(status.input_source == 3 && status.charge_state == 2);
    assert(status.power_good && status.system_min_regulation);
    assert(status.historical_fault.raw == 0xfa && status.current_fault.raw == 2);
    assert(status.historical_fault.watchdog && status.historical_fault.boost);
    assert(status.historical_fault.charge == 3 && status.historical_fault.battery);
    assert(status.historical_fault.ntc == 2 && !status.current_fault.watchdog);

    /* Exhaust all status/fault bit encodings, including reserved values. */
    for (unsigned raw = 0; raw < 256; ++raw) {
        reset_mock();
        values[1] = values[2] = values[3] = raw;
        assert(bq25890_read_status(&token, &status) == ESP_OK);
        assert(status.input_source == ((raw >> 5) & 7));
        assert(status.charge_state == ((raw >> 3) & 3));
        assert(status.power_good == ((raw & 4) != 0));
        assert(status.system_min_regulation == ((raw & 1) != 0));
        assert(status.current_fault.watchdog == ((raw & 128) != 0));
        assert(status.current_fault.boost == ((raw & 64) != 0));
        assert(status.current_fault.charge == ((raw >> 4) & 3));
        assert(status.current_fault.battery == ((raw & 8) != 0));
        assert(status.current_fault.ntc == (raw & 7));
    }

    /* A failed sample must erase even a previously successful output. */
    for (int index = 0; index < 4; ++index) {
        for (int nack = 0; nack < 2; ++nack) {
            reset_mock();
            fail_read = index;
            read_error = nack ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
            memset(&status, 0xff, sizeof(status));
            assert(bq25890_read_status(&token, &status) == read_error);
            assert(reads == index + 1);
            assert_empty(&status);
            reset_mock();
            assert(bq25890_read_status(&token, &status) == ESP_OK && status.valid);
        }
    }
    reset_mock();
    values[0] = 0x00;
    memset(&status, 0xff, sizeof(status));
    assert(bq25890_read_status(&token, &status) == ESP_ERR_NOT_SUPPORTED);
    assert(reads == 1);
    assert_empty(&status);
    reads = 0;
    assert(bq25890_init(&token, &device) == ESP_ERR_NOT_SUPPORTED);
    assert(device == NULL && added == 1 && removed == 1);

    reset_mock();
    probe_error = ESP_ERR_NOT_FOUND;
    assert(bq25890_init(&token, &device) == ESP_ERR_NOT_FOUND);
    assert(device == NULL && added == 0 && reads == 0);
    probe_error = ESP_ERR_TIMEOUT;
    assert(bq25890_init(&token, &device) == ESP_ERR_TIMEOUT);
    assert(device == NULL && added == 0 && reads == 0);
    probe_error = ESP_OK;
    add_error = ESP_ERR_TIMEOUT;
    assert(bq25890_init(&token, &device) == ESP_ERR_TIMEOUT && device == NULL);

    reset_mock();
    fail_read = 0;
    assert(bq25890_init(&token, &device) == ESP_ERR_TIMEOUT);
    assert(device == NULL && added == 1 && removed == 1);

    reset_mock();
    device = &token;
    remove_error = ESP_ERR_TIMEOUT;
    assert(bq25890_deinit(&device) == ESP_ERR_TIMEOUT && device == &token);
    memset(&status, 0xff, sizeof(status));
    assert(bq25890_read_status(NULL, &status) == ESP_ERR_INVALID_ARG);
    assert_empty(&status);
    assert(bq25890_read_status(&token, NULL) == ESP_ERR_INVALID_ARG);
    assert(bq25890_init(NULL, &device) == ESP_ERR_INVALID_ARG && device == NULL);
    assert(bq25890_init(&token, NULL) == ESP_ERR_INVALID_ARG);
    assert(bq25890_deinit(NULL) == ESP_ERR_INVALID_ARG);
    puts("BQ25890 host tests passed (decode, identity, NACK, timeout, stale output, cleanup)");
    return 0;
}
