/**
 * @file power_test.c
 * @brief 单任务采集 BQ25890 状态与可选电压，每轮发布一次完整结果。
 */

#include "power_test.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c.h"
#include "bq25890.h"
#include "power_state_internal.h"
#include "sysdef.h"

static const char *TAG = "power_test";

static const char *input_names[] = {
    "none",
    "USB_SDP",
    "USB_CDP",
    "USB_DCP",
    "HVDCP",
    "unknown_adapter",
    "nonstandard_adapter",
    "OTG",
};
static const char *charge_names[] = { "not_charging", "precharge", "fast_charge", "done" };
static const char *state_names[] = { "Q1", "Q2", "Q3", "Q4", "Q5" };

/**
 * @brief 从统一快照输出状态与历史数据，不执行额外 I2C 读取。
 *
 * @note Q4 以旧值标记显示，Q5 历史值用于诊断；charge 字段为芯片寄存器充电状态编码。
 */
static void log_snapshot(void)
{
    power_state_snapshot_t state;
    if (power_state_get_snapshot(&state) != ESP_OK) {
        return;
    }

    // 两条上层日志使用同一份副本，使状态与电压对应同一轮数据。
    ESP_LOGI(TAG,
        "state=%s reason=%u running=%u adc_enabled=%u has_data=%u stale=%u current_valid=%u consecutive_failed=%" PRIu32
        " error=%s attempt_us=%" PRId64 " result_us=%" PRId64 " success_us=%" PRId64,
        state_names[state.state],
        (unsigned)state.reason,
        (unsigned)state.task_running,
        (unsigned)state.adc_enabled,
        (unsigned)state.has_data,
        (unsigned)state.is_stale,
        (unsigned)state.current_valid,
        state.consecutive_failures,
        esp_err_to_name(state.last_error),
        state.last_attempt_us,
        state.last_result_us,
        state.last_success_us);

    if (state.has_data) {
        // Q4/Q5 历史数据随有效性和旧值标志一起输出。
        const power_state_sample_t *data = &state.data;
        ESP_LOGI(TAG,
            "snapshot current_valid=%u stale=%u id=0x%02x PN=%u rev=%u input=%s charge=%s PG=%u VSYSMIN=%u "
            "fault_history=0x%02x fault_current=0x%02x voltage_available=%u VBUS=%u mV SYS=%u mV",
            (unsigned)state.current_valid,
            (unsigned)state.is_stale,
            data->identity_raw,
            data->part_number,
            data->revision,
            input_names[data->input_source],
            charge_names[data->charge_state],
            (unsigned)data->power_good,
            (unsigned)data->system_min_regulation,
            data->fault_history_raw,
            data->fault_current_raw,
            (unsigned)data->voltage_available,
            data->vbus_mv,
            data->sys_mv);
    }
}

/**
 * @brief 严重错误立即发布 Q5，再释放任务独占设备；释放失败保留日志。
 *
 * @param device 本任务持有的设备句柄地址；成功释放后置 NULL，不删除借用总线。
 * @param error 导致任务停止的错误码。
 *
 * @note 仅由采集任务调用，不执行寄存器恢复；调用者返回后负责退出任务。
 */
static void stop_test(i2c_master_dev_handle_t *device, esp_err_t error)
{
    // 先发布停止原因，再尝试清理；清理失败时保留停止状态并记录错误。
    power_state_stop(error);
    log_snapshot();

    esp_err_t cleanup_err = bq25890_deinit(device);
    if (cleanup_err != ESP_OK) {
        ESP_LOGE(TAG, "Device cleanup failed: %s", esp_err_to_name(cleanup_err));
    }
}

void power_test_task(void *arg)
{
    (void)arg;

    // 总线由板级模块持有，设备句柄由本任务独占；成功初始化后跨轮次复用。
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t device = NULL;
    uint32_t success = 0, failure = 0;

    ESP_LOGI(TAG,
        "BQ25890H basic test: addr=0x%02x SCL=%d SDA=%d; INT=%d unused",
        BQ25890_I2C_ADDRESS,
        PIN_I2C1_SCL,
        PIN_I2C1_SDA,
        PIN_BQ25890_INT);
    ESP_LOGI(TAG, "Validation baseline: USB, battery disconnected; verify physically");
    log_snapshot();
#if CONFIG_BYAI_POWER_ADC_TEST
    uint32_t adc_success = 0, adc_failure = 0;
    ESP_LOGI(TAG,
        "ADC enabled: continuous-window VBUS/SYS (1200ms); debug I2C watchdog disabled with readback; no automatic "
        "restore");
#endif

    // 每轮只有一次状态发布。轮询周期包含 ADC 等待、I2C 开销及末尾的 1 秒间隔。
    while (true) {
        // 记录尝试起点后再进行 I/O，使查询方能识别长期没有结果的采集任务。
        power_state_record_attempt();
        esp_err_t err = board_i2c_get_bus(&bus);
        if (err == ESP_OK && device == NULL) {
            // 只在尚无设备句柄时初始化；普通通信失败不新增硬件重置流程。
            err = bq25890_init(bus, &device);
#if CONFIG_BYAI_POWER_ADC_TEST
            if (err == ESP_OK) {
                err = bq25890_disable_watchdog_for_debug(device);
                if (err == ESP_OK) {
                    err = bq25890_prepare_adc(device);
                }
                if (err != ESP_OK) {
                    // 关键配置校验失败时停止采集，发布 Q5 并尝试释放设备。
                    ESP_LOGE(TAG, "ADC startup setup failed: %s; stopping ADC test", esp_err_to_name(err));
                    stop_test(&device, err);
                    vTaskDelete(NULL);
                    return;
                }
                ESP_LOGW(TAG, "BQ25890 I2C watchdog disabled and verified; charging safety timer preserved");
            }
#endif
        }

        // 先取得身份、供电及故障信息；驱动读取 REG0C 的副作用不在上层重复执行。
        bq25890_status_t status;
        if (err == ESP_OK) {
            err = bq25890_read_status(device, &status);
        }

        // 只在局部样本中拼装本轮数据，ADC 失败时不会污染最近完整成功快照。
        power_state_sample_t sample = { 0 };
        if (err == ESP_OK) {
            ++success;
            sample = (power_state_sample_t){
                .identity_raw = status.identity.raw,
                .part_number = status.identity.part_number,
                .revision = status.identity.revision,
                .input_source = status.input_source,
                .charge_state = status.charge_state,
                .power_good = status.power_good,
                .system_min_regulation = status.system_min_regulation,
                .fault_history_raw = status.historical_fault.raw,
                .fault_current_raw = status.current_fault.raw,
            };
#if CONFIG_BYAI_POWER_ADC_TEST
            // 启用 ADC 时，完整成功要求基础读取、电压读取及收尾全部成功。
            bq25890_voltages_t voltages;
            err = bq25890_read_voltages(device, &voltages);
            if (err == ESP_OK) {
                ++adc_success;
                sample.voltage_available = true;
                sample.vbus_mv = voltages.vbus_mv;
                sample.sys_mv = voltages.sys_mv;
            } else {
                ++adc_failure;
                ESP_LOGW(TAG,
                    "ADC attempt failed=%s adc_ok=%" PRIu32 " adc_failed=%" PRIu32,
                    esp_err_to_name(err),
                    adc_success,
                    adc_failure);
            }
#endif
        } else {
            ++failure;
            ESP_LOGW(TAG,
                "Basic attempt failed=%s ok=%" PRIu32 " failed=%" PRIu32,
                esp_err_to_name(err),
                success,
                failure);
        }

        // 型号不符属于严重错误，不适用普通采样失败的“三次后不可用”容忍窗口。
        if (err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGE(TAG, "Unexpected chip identity; stopping power test");
            stop_test(&device, err);
            vTaskDelete(NULL);
            return;
        }

        // 无论失败发生在哪一阶段，每轮都只累计一次；随后日志读取统一快照。
        power_state_publish(&sample, err);
        log_snapshot();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
