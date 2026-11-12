/**
 * @file power_state.c
 * @brief 以短临界区维护电源快照、连续失败状态与无结果超时。
 *
 * 仅管理内存状态；不拥有采集任务或硬件，不执行 I2C 与资源回收。
 */

#include "power_state_internal.h"

#include <limits.h>
#include <stddef.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define POWER_STATE_TIMEOUT_US INT64_C(10000000)
#define POWER_STATE_FAILURE_LIMIT 3

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static power_state_snapshot_t snapshot = {
    .state = POWER_STATE_Q1_DISABLED,
};

/** @brief 持锁维护派生有效性，历史数据始终与最近完整成功时间一起保留。 */
static void update_validity(void)
{
    // 有历史数据且处于 Q3 时为当前有效值，其余状态中的历史数据标记为旧值。
    snapshot.current_valid = snapshot.has_data && snapshot.state == POWER_STATE_Q3_READY;
    snapshot.is_stale = snapshot.has_data && !snapshot.current_valid;
}

/**
 * @brief 持锁检查无结果超时，锁存 Q5；记录尝试不会延后超时起点。
 *
 * @param now 本次启动后的单调微秒，调用者持有 state_lock。
 *
 * @note 只改内存状态，不更改原采样时间或失败次数，不执行硬件恢复。
 */
static void check_timeout(int64_t now)
{
    // 已停止的任务及已锁存的 Q5 保留原原因，不因查询覆盖诊断信息。
    if (snapshot.task_running && snapshot.state != POWER_STATE_Q5_UNAVAILABLE &&
        now - snapshot.last_result_us >= POWER_STATE_TIMEOUT_US) {
        snapshot.state = POWER_STATE_Q5_UNAVAILABLE;
        snapshot.reason = POWER_STATE_REASON_UPDATE_TIMEOUT;
        snapshot.last_error = ESP_ERR_TIMEOUT;
        update_validity();
    }
}

esp_err_t power_state_get_snapshot(power_state_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 超时判定与整份副本复制必须处于同一临界区，避免读到跨轮次的状态和数据。
    portENTER_CRITICAL(&state_lock);
    check_timeout(esp_timer_get_time());
    *out_snapshot = snapshot;
    portEXIT_CRITICAL(&state_lock);

    return ESP_OK;
}

esp_err_t power_state_begin(bool adc_enabled)
{
    // task_running 在创建任务前即预留；拒绝第二个生产者重置已有状态。
    portENTER_CRITICAL(&state_lock);
    if (snapshot.task_running) {
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_INVALID_STATE;
    }

    // 新生命周期不沿用旧设备会话的数据，首次超时从此刻开始计时。
    snapshot = (power_state_snapshot_t){
        .state = POWER_STATE_Q2_INITIALIZING,
        .task_running = true,
        .adc_enabled = adc_enabled,
        .last_result_us = esp_timer_get_time(),
    };
    portEXIT_CRITICAL(&state_lock);

    return ESP_OK;
}

void power_state_record_attempt(void)
{
    portENTER_CRITICAL(&state_lock);
    if (snapshot.task_running) {
        const int64_t now = esp_timer_get_time();

        // 尝试仅更新 last_attempt_us；无结果超时仍以 last_result_us 为起点。
        check_timeout(now);
        snapshot.last_attempt_us = now;
    }
    portEXIT_CRITICAL(&state_lock);
}

void power_state_publish(const power_state_sample_t *sample, esp_err_t error)
{
    portENTER_CRITICAL(&state_lock);
    if (!snapshot.task_running) {
        // 停止后忽略迟到结果，保留停止原因及历史数据。
        portEXIT_CRITICAL(&state_lock);
        return;
    }

    const int64_t now = esp_timer_get_time();

    // 先检查旧结果是否已超时，再记录新结果；迟到失败也必须保持 Q5 锁存。
    check_timeout(now);

    // 缺少必需字段的成功发布按 ESP_ERR_INVALID_ARG 计为一次失败。
    if (error == ESP_OK && (sample == NULL || (snapshot.adc_enabled && !sample->voltage_available))) {
        error = ESP_ERR_INVALID_ARG;
    }

    snapshot.last_result_us = now;
    snapshot.last_error = error;

    if (error == ESP_OK) {
        // 一次完整成功是解除 Q4/Q5 的条件；数据与成功时间在同一把锁下替换。
        snapshot.data = *sample;

        if (!snapshot.adc_enabled) {
            // ADC 关闭时，将电压标记为不可用，数值统一置为占位值 0。
            snapshot.data.voltage_available = false;
            snapshot.data.vbus_mv = 0;
            snapshot.data.sys_mv = 0;
        }

        snapshot.has_data = true;
        snapshot.last_success_us = now;
        snapshot.consecutive_failures = 0;
        snapshot.state = POWER_STATE_Q3_READY;
        snapshot.reason = POWER_STATE_REASON_NONE;
    } else {
        // 失败计数饱和，避免长时间故障后回绕为小值。
        if (snapshot.consecutive_failures < UINT32_MAX) {
            ++snapshot.consecutive_failures;
        }

        // Q5 在连续失败期间保持锁存，下一次完整成功时恢复。
        if (snapshot.state == POWER_STATE_Q5_UNAVAILABLE ||
            snapshot.consecutive_failures >= POWER_STATE_FAILURE_LIMIT) {
            snapshot.state = POWER_STATE_Q5_UNAVAILABLE;
        } else {
            snapshot.state = POWER_STATE_Q4_RETRYING;
        }
        // 失败只更新诊断信息，不覆盖上次完整数据和成功时间。
        snapshot.reason = POWER_STATE_REASON_SAMPLE_ERROR;
    }

    update_validity();
    portEXIT_CRITICAL(&state_lock);
}

void power_state_stop(esp_err_t error)
{
    // 这里只停止软件状态接收；设备释放和实际任务退出仍由采集者负责。
    portENTER_CRITICAL(&state_lock);
    snapshot.state = POWER_STATE_Q5_UNAVAILABLE;
    snapshot.reason = POWER_STATE_REASON_STARTUP_ERROR;
    snapshot.last_error = error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    snapshot.task_running = false;

    update_validity();
    portEXIT_CRITICAL(&state_lock);
}
