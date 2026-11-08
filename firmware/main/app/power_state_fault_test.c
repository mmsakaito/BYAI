#include "sdkconfig.h"

#if CONFIG_BYAI_POWER_STATE_FAULT_TEST
#include "power_state_fault_test.h"
#include "power_state_internal.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_fault_test";

/** @brief 合成标记值，绝不能当作芯片实际测量；正常采集任务在诊断模式不启动。 */
static power_state_sample_t synthetic_sample = {
    .identity_raw = 0x1f,
    .part_number = 3,
    .revision = 3,
    .input_source = 5,
    .power_good = true,
    .voltage_available = true,
    .vbus_mv = 1234,
    .sys_mv = 2345,
};

typedef struct {
    TaskHandle_t producer;
} observer_context_t;

/**
 * @brief 显式比较所有数据字段，不依赖 C 结构体填充字节。
 *
 * @param a 第一个非 NULL 的自有样本副本。
 * @param b 第二个非 NULL 的自有样本副本。
 *
 * @return 所有测量及状态字段相等时为 true。
 */
static bool same_sample(const power_state_sample_t *a, const power_state_sample_t *b)
{
    // 比较全部业务字段而非 memcmp，避免结构体填充字节影响注入测试结果。
    return a->identity_raw == b->identity_raw && a->part_number == b->part_number && a->revision == b->revision &&
           a->input_source == b->input_source && a->charge_state == b->charge_state && a->power_good == b->power_good &&
           a->system_min_regulation == b->system_min_regulation && a->fault_history_raw == b->fault_history_raw &&
           a->fault_current_raw == b->fault_current_raw && a->voltage_available == b->voltage_available &&
           a->vbus_mv == b->vbus_mv && a->sys_mv == b->sys_mv;
}

/**
 * @brief 每次注入间隔 100ms，让真实单调时钟推进；无 I2C 或电源控制操作。
 *
 * @param error ESP_OK 发布合成成功样本，其他值只发布失败结果。
 *
 * @note 仅由诊断生产者调用；可能让出调度，不改变硬件或持久化数据。
 */
static void inject_result(esp_err_t error)
{
    // 让真实 ESP32 时钟推进；这里只发布软件结果，不读取或写入充电芯片。
    vTaskDelay(pdMS_TO_TICKS(100));
    power_state_record_attempt();

    if (error == ESP_OK) {
        // 改变合成标记值，使恢复检查能区分新结果与重复发布的旧快照。
        ++synthetic_sample.vbus_mv;
        ++synthetic_sample.sys_mv;
    }

    power_state_publish(error == ESP_OK ? &synthetic_sample : NULL, error);
}

/**
 * @brief 检查输出契约，必要时验证旧值和成功时间未改变；日志始终带 synthetic=1。
 *
 * @param step 日志中的测试步骤名，不保存指针。
 * @param expected 预期状态 Q1～Q5。
 * @param failures 预期连续失败次数。
 * @param error 预期最近错误码。
 * @param reason 预期不可用或失败原因。
 * @param running 预期采集任务运行标志。
 * @param has_data 预期是否存在完整历史结果。
 * @param old 非 NULL 时要求存在且完整保留该历史快照。
 *
 * @return 所有检查均满足时为 true；否则记录 FAIL 并返回 false。
 *
 * @note 仅调用公开快照读取，不执行硬件访问，不使用 assert 绕过失败报告。
 */
static bool expect_state(const char *step,
    power_state_id_t expected,
    uint32_t failures,
    esp_err_t error,
    power_state_reason_t reason,
    bool running,
    bool has_data,
    const power_state_snapshot_t *old)
{
    power_state_snapshot_t s;
    if (power_state_get_snapshot(&s) != ESP_OK) {
        ESP_LOGE(TAG, "synthetic=1 step=%s result=FAIL snapshot_read", step);
        return false;
    }

    // 状态、运行标志及数据有效性必须同时匹配，不能只凭 Q 编号判断通过。
    bool ok = s.state == expected && s.consecutive_failures == failures && s.last_error == error &&
              s.reason == reason && s.task_running == running && s.has_data == has_data &&
              s.current_valid == (has_data && expected == POWER_STATE_Q3_READY) &&
              s.is_stale == (has_data && expected != POWER_STATE_Q3_READY);
    if (old != NULL) {
        // 失败期间既不能覆盖旧数据，也不能伪造更近的完整成功时间。
        ok = ok && s.last_success_us == old->last_success_us && same_sample(&s.data, &old->data);
    }
    ESP_LOGI(TAG,
        "synthetic=1 step=%s state=Q%u failed=%" PRIu32
        " running=%u has_data=%u stale=%u current_valid=%u reason=%u error=%s success_us=%" PRId64 " result=%s",
        step,
        (unsigned)s.state + 1,
        s.consecutive_failures,
        (unsigned)s.task_running,
        (unsigned)s.has_data,
        (unsigned)s.is_stale,
        (unsigned)s.current_valid,
        (unsigned)s.reason,
        esp_err_to_name(s.last_error),
        s.last_success_us,
        ok ? "PASS" : "FAIL");

    return ok;
}

/**
 * @brief 校验成功后时间及数据均已更新，失败计数清零。
 *
 * @param step 测试步骤名，仅供本次日志使用。
 * @param old 恢复前的非 NULL 历史快照，不保存指针。
 *
 * @return 状态、有效性、时间及数据检查通过时为 true，否则记录失败。
 */
static bool expect_recovered(const char *step, const power_state_snapshot_t *old)
{
    // 先检查恢复为 Q3 及计数清零，再确认时间和数据确实来自新的成功发布。
    if (!expect_state(step, POWER_STATE_Q3_READY, 0, ESP_OK, POWER_STATE_REASON_NONE, true, true, NULL)) {
        return false;
    }

    power_state_snapshot_t s;
    if (power_state_get_snapshot(&s) != ESP_OK || s.last_success_us <= old->last_success_us ||
        (s.adc_enabled && s.data.vbus_mv == old->data.vbus_mv)) {
        ESP_LOGE(TAG, "synthetic=1 step=%s result=FAIL fresh_data", step);
        return false;
    }
    return true;
}

/**
 * @brief 独立观察者在生产者停止发布期间查询真实 10 秒超时，不写状态或访问 I2C。
 *
 * @param arg 非 NULL 的 observer_context_t 指针，生命周期由等待通知的生产者保证。
 *
 * @note 上下文属于等待通知的生产者栈；通知前复制任务句柄，通知后不再引用上下文。
 *       结果通知 1 为通过、2 为失败；本任务随后自行退出。
 */
static void timeout_observer(void *arg)
{
    // 先复制通知目标，后续通知发出后不再访问生产者借出的栈上下文。
    const observer_context_t *context = arg;
    const TaskHandle_t producer = context->producer;
    const int64_t deadline = esp_timer_get_time() + INT64_C(12000000);
    uint32_t result = 2;
    while (esp_timer_get_time() < deadline) {
        power_state_snapshot_t s;
        if (power_state_get_snapshot(&s) != ESP_OK) {
            break;
        }

        // 查询会检查真实单调时间；生产者在此期间不发布任何结果。
        const int64_t age = esp_timer_get_time() - s.last_result_us;
        if (s.state == POWER_STATE_Q5_UNAVAILABLE) {
            if (s.reason == POWER_STATE_REASON_UPDATE_TIMEOUT && s.last_error == ESP_ERR_TIMEOUT && !s.current_valid &&
                s.has_data && s.is_stale && s.task_running && s.consecutive_failures == 0 && age >= INT64_C(10000000)) {
                result = 1;
            }
            ESP_LOGI(TAG,
                "synthetic=1 observer=independent core=%d age_us=%" PRId64 " state=Q%u result=%s",
                xPortGetCoreID(),
                age,
                (unsigned)s.state + 1,
                result == 1 ? "PASS" : "FAIL");
            break;
        }

        // 除 Q3 等待和 Q5 超时外，其他状态均意味着本用例前提不成立。
        if (s.state != POWER_STATE_Q3_READY) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 即使观察失败也通知生产者结束等待，避免诊断函数提前返回后留下悬空上下文。
    xTaskNotify(producer, result, eSetValueWithOverwrite);

    vTaskDelete(NULL);
}

/**
 * @brief 测试两次失败后恢复，以及三次失败进入 Q5 后继续失败并恢复。
 *
 * @return 全部步骤通过时为 true，首个失败返回 false 并交由入口停止套件。
 */
static bool test_failure_streaks(void)
{
    // 首先建立完整的合成历史快照；后续失败必须保留该数据，而不是读取硬件。
    if (!expect_state("initial", POWER_STATE_Q2_INITIALIZING, 0, ESP_OK, POWER_STATE_REASON_NONE, true, false, NULL)) {
        return false;
    }
    inject_result(ESP_OK);
    if (!expect_state("baseline", POWER_STATE_Q3_READY, 0, ESP_OK, POWER_STATE_REASON_NONE, true, true, NULL)) {
        return false;
    }
    power_state_snapshot_t good;
    power_state_get_snapshot(&good);

    // 两次失败都处于 Q4；随后一次成功应更新数据并清零失败计数。
    for (unsigned i = 1; i <= 2; ++i) {
        inject_result(ESP_ERR_TIMEOUT);
        if (!expect_state("two_failures",
                POWER_STATE_Q4_RETRYING,
                i,
                ESP_ERR_TIMEOUT,
                POWER_STATE_REASON_SAMPLE_ERROR,
                true,
                true,
                &good)) {
            return false;
        }
    }
    inject_result(ESP_OK);
    if (!expect_recovered("two_failures_recovered", &good)) {
        return false;
    }
    ESP_LOGI(TAG, "synthetic=1 case=two_failures result=PASS");

    // 使用新的历史快照验证三次阈值；第四次失败也不得从 Q5 回退到 Q4。
    power_state_get_snapshot(&good);
    for (unsigned i = 1; i <= 4; ++i) {
        inject_result(ESP_ERR_TIMEOUT);
        if (!expect_state("three_failures",
                i < 3 ? POWER_STATE_Q4_RETRYING : POWER_STATE_Q5_UNAVAILABLE,
                i,
                ESP_ERR_TIMEOUT,
                POWER_STATE_REASON_SAMPLE_ERROR,
                true,
                true,
                &good)) {
            return false;
        }
    }
    inject_result(ESP_OK);
    if (!expect_recovered("three_failures_recovered", &good)) {
        return false;
    }
    ESP_LOGI(TAG, "synthetic=1 case=three_failures result=PASS");
    return true;
}

/**
 * @brief 暂停生产者发布，独立观察者触发超时；后续失败不得解除 Q5。
 *
 * @return 观察者创建、超时及后续恢复均通过时为 true，否则为 false。
 *
 * @note 本函数借出栈上下文，必须等待观察者通知后才能返回，不持有设备资源。
 */
static bool test_update_timeout(void)
{
    // 保存暂停前的完整结果，由独立任务查询超时，避免生产者自己发布“超时结果”。
    power_state_snapshot_t good;
    power_state_get_snapshot(&good);
    observer_context_t context = { .producer = xTaskGetCurrentTaskHandle() };
    if (xTaskCreatePinnedToCore(timeout_observer, "power_observer", 4096, &context, 5, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "synthetic=1 case=update_timeout result=FAIL observer_create");
        return false;
    }
    ESP_LOGI(TAG, "synthetic=1 producer=paused waiting_for_real_timeout=10s");

    uint32_t notification = 0;
    // 观察者最多运行 12 秒，生产者必须保留栈上下文直到通知到达。
    xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);

    if (notification != 1 || !expect_state("timeout",
                                 POWER_STATE_Q5_UNAVAILABLE,
                                 0,
                                 ESP_ERR_TIMEOUT,
                                 POWER_STATE_REASON_UPDATE_TIMEOUT,
                                 true,
                                 true,
                                 &good)) {
        return false;
    }

    // 超时后即使收到新的失败结果，也必须保持 Q5，直到完整成功才恢复。
    inject_result(ESP_ERR_TIMEOUT);
    if (!expect_state("timeout_then_failure",
            POWER_STATE_Q5_UNAVAILABLE,
            1,
            ESP_ERR_TIMEOUT,
            POWER_STATE_REASON_SAMPLE_ERROR,
            true,
            true,
            &good)) {
        return false;
    }
    inject_result(ESP_OK);
    if (!expect_recovered("timeout_recovered", &good)) {
        return false;
    }
    ESP_LOGI(TAG, "synthetic=1 case=update_timeout result=PASS");
    return true;
}

/**
 * @brief 只模拟软件生命周期，不创建/释放实际采集设备，不执行芯片初始化。
 *
 * @return 严重错误立即停止、迟到结果被忽略的检查均通过时为 true。
 */
static bool test_startup_failure(void)
{
    // 结束上一段纯软件生命周期再重新开始；这里没有真实采集设备需要释放。
    power_state_snapshot_t s;
    power_state_get_snapshot(&s);
    const bool adc_enabled = s.adc_enabled;
    power_state_stop(ESP_FAIL);
    if (power_state_begin(adc_enabled) != ESP_OK ||
        !expect_state("restart", POWER_STATE_Q2_INITIALIZING, 0, ESP_OK, POWER_STATE_REASON_NONE, true, false, NULL)) {
        return false;
    }

    // 直接注入关键初始化校验错误，不故意制造真实寄存器或资源故障。
    power_state_stop(ESP_ERR_INVALID_RESPONSE);
    if (!expect_state("startup_failure",
            POWER_STATE_Q5_UNAVAILABLE,
            0,
            ESP_ERR_INVALID_RESPONSE,
            POWER_STATE_REASON_STARTUP_ERROR,
            false,
            false,
            NULL)) {
        return false;
    }

    // 停止后迟到的成功发布必须被忽略，不能让已停止的任务看起来又恢复正常。
    inject_result(ESP_OK);
    if (!expect_state("late_result_ignored",
            POWER_STATE_Q5_UNAVAILABLE,
            0,
            ESP_ERR_INVALID_RESPONSE,
            POWER_STATE_REASON_STARTUP_ERROR,
            false,
            false,
            NULL)) {
        return false;
    }
    ESP_LOGI(TAG, "synthetic=1 case=startup_failure result=PASS");
    return true;
}

void power_state_fault_test_task(void *arg)
{
    (void)arg;

    ESP_LOGW(TAG,
        "DIAGNOSTIC ONLY synthetic=1; normal power collector disabled; NO BQ I2C; voltages are NOT measurements");
    // 按顺序执行，首个失败即停止后续用例；不要用后续成功掩盖先前失败。
    const bool passed = test_failure_streaks() && test_update_timeout() && test_startup_failure();
    if (!passed) {
        power_state_stop(ESP_FAIL);
    }
    // 诊断任务退出后不会接替为真实采集，必须烧录关闭注入的正常固件。
    ESP_LOGI(TAG,
        "synthetic=1 SUITE result=%s; diagnostic task stopping; flash normal firmware to resume sampling",
        passed ? "PASS" : "FAIL");

    vTaskDelete(NULL);
}
#endif
