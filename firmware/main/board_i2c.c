#include "board_i2c.h"

#include "sysdef.h"

static i2c_master_bus_handle_t board_bus;

esp_err_t board_i2c_get_bus(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_bus = NULL;
    if (board_bus == NULL) {
        const i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_I2C1_SDA,
            .scl_io_num = PIN_I2C1_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = false,
        };
        esp_err_t err = i2c_new_master_bus(&config, &board_bus);
        if (err != ESP_OK) {
            return err;
        }
    }
    *out_bus = board_bus;
    return ESP_OK;
}
