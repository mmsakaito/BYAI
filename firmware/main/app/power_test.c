#include "power_test.h"

#include <inttypes.h>
#include "board_i2c.h"
#include "bq25890.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sysdef.h"

static const char *TAG = "power_test";

static const char *input_names[] = {
    "none", "USB_SDP", "USB_CDP", "USB_DCP", "HVDCP",
    "unknown_adapter", "nonstandard_adapter", "OTG",
};
static const char *charge_names[] = {"not_charging", "precharge", "fast_charge", "done"};

/** @brief 故障保留编码及各字段；watchdog 在芯片默认模式可置位，不等同供电失败。 */
static void log_fault(const char *kind, const bq25890_fault_t *fault)
{
    ESP_LOGI(TAG, "%s=0x%02x watchdog=%u boost=%u charge=%u battery=%u ntc=%u",
             kind, fault->raw, (unsigned)fault->watchdog, (unsigned)fault->boost,
             fault->charge, (unsigned)fault->battery, fault->ntc);
}

void power_test_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t device = NULL;
    uint32_t success = 0, failure = 0;
    ESP_LOGI(TAG, "BQ25890RTWT read-only test: addr=0x%02x SCL=%d SDA=%d; INT=%d unused",
             BQ25890_I2C_ADDRESS, PIN_I2C1_SCL, PIN_I2C1_SDA, PIN_BQ25890_INT);
    ESP_LOGI(TAG, "Validation baseline: USB, battery disconnected; verify physically");

    while (true) {
        esp_err_t err = board_i2c_get_bus(&bus);
        if (err == ESP_OK && device == NULL) {
            err = bq25890_init(bus, &device);
        }
        bq25890_status_t status;
        if (err == ESP_OK) {
            err = bq25890_read_status(device, &status);
        }
        if (err == ESP_OK) {
            ++success;
            ESP_LOGI(TAG, "valid=1 id=0x%02x PN=%u rev=%u status=0x%02x input=%s charge=%s PG=%u VSYSMIN=%u ok=%" PRIu32 " failed=%" PRIu32,
                     status.identity.raw, status.identity.part_number, status.identity.revision,
                     status.raw_status, input_names[status.input_source], charge_names[status.charge_state],
                     (unsigned)status.power_good, (unsigned)status.system_min_regulation, success, failure);
            log_fault("fault_history", &status.historical_fault);
            log_fault("fault_current", &status.current_fault);
        } else {
            ++failure;
            ESP_LOGW(TAG, "valid=0 error=%s ok=%" PRIu32 " failed=%" PRIu32,
                     esp_err_to_name(err), success, failure);
            if (err == ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGE(TAG, "Unexpected chip identity; stopping power test");
                esp_err_t cleanup_err = bq25890_deinit(&device);
                if (cleanup_err != ESP_OK) {
                    ESP_LOGE(TAG, "Device cleanup failed: %s", esp_err_to_name(cleanup_err));
                }
                vTaskDelete(NULL);
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
