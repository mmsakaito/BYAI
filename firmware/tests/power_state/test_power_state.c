/**
 * @file test_power_state.c
 * @brief 使用可控时钟和 pthread 锁检查电源状态机的状态转换与并发快照。
 */

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "power_state_internal.h"

static atomic_int_fast64_t now_us;

/** @brief 主机时钟替身；测试通过原子变量显式推进时间。 */
int64_t esp_timer_get_time(void)
{
    return atomic_load(&now_us);
}

/** @brief 查询快照并断言成功，供各场景检查返回的值副本。 */
static power_state_snapshot_t get(void)
{
    power_state_snapshot_t result;
    assert(power_state_get_snapshot(&result) == ESP_OK);
    return result;
}

/** @brief 重置上一个软件采集生命周期，以固定时间开始独立测试场景。 */
static void begin(bool adc_enabled)
{
    power_state_stop(ESP_FAIL);
    atomic_store(&now_us, 100);
    assert(power_state_begin(adc_enabled) == ESP_OK);
}

/** @brief 构造关联字段可校验的合成数据，用于检测旧值污染和并发撕裂。 */
static power_state_sample_t sample(unsigned value)
{
    return (power_state_sample_t){
        .identity_raw = (uint8_t)value,
        .input_source = (uint8_t)(value & 7),
        .voltage_available = true,
        .vbus_mv = (uint16_t)value,
        .sys_mv = (uint16_t)~value,
    };
}

/** @brief 推进替身时钟，发布一次完整成功结果。 */
static void publish_ok(unsigned value)
{
    power_state_sample_t data = sample(value);
    atomic_fetch_add(&now_us, 10);
    power_state_record_attempt();
    power_state_publish(&data, ESP_OK);
}

/** @brief 发布失败及故意不同的部分数据，检查失败不会覆盖完整历史值。 */
static void publish_failure(void)
{
    atomic_fetch_add(&now_us, 10);
    power_state_record_attempt();

    power_state_sample_t partial = sample(999);
    power_state_publish(&partial, ESP_ERR_TIMEOUT);
}

/** @brief 覆盖 Q1、首次启动、拒绝重复启动，以及首次完整成功进入 Q3。 */
static void test_disabled_and_startup(void)
{
    // 首次查询应保持 Q1；启动后先是 Q2，完整成功前不得暴露有效数据。
    power_state_snapshot_t s = get();
    assert(s.state == POWER_STATE_Q1_DISABLED && !s.has_data && !s.task_running);
    assert(power_state_get_snapshot(NULL) == ESP_ERR_INVALID_ARG);

    begin(true);
    s = get();
    assert(s.state == POWER_STATE_Q2_INITIALIZING && s.task_running && s.adc_enabled);
    assert(power_state_begin(false) == ESP_ERR_INVALID_STATE);

    publish_ok(5100);
    s = get();
    assert(s.state == POWER_STATE_Q3_READY && s.current_valid && !s.is_stale);
    assert(s.has_data && s.data.vbus_mv == 5100 && s.consecutive_failures == 0);
}

/** @brief 验证前两次失败重试、第三次不可用、后续失败锁存与成功恢复。 */
static void test_failure_threshold_and_recovery(void)
{
    begin(true);
    publish_ok(5100);
    const power_state_snapshot_t good = get();

    // 同时覆盖前三次失败阈值和第四次继续失败；每轮都检查旧数据未被污染。
    for (unsigned n = 1; n <= 4; ++n) {
        publish_failure();
        power_state_snapshot_t s = get();
        assert(s.state == (n < 3 ? POWER_STATE_Q4_RETRYING : POWER_STATE_Q5_UNAVAILABLE));
        assert(s.consecutive_failures == n && s.has_data && s.is_stale && !s.current_valid);
        assert(s.last_error == ESP_ERR_TIMEOUT && s.reason == POWER_STATE_REASON_SAMPLE_ERROR);
        assert(s.last_success_us == good.last_success_us);
        assert(memcmp(&s.data, &good.data, sizeof(s.data)) == 0);
        assert(s.task_running);
    }

    // 完整成功解除 Q5，下一轮失败应重新从 1 计数，而非沿用旧故障计数。
    publish_ok(5200);
    power_state_snapshot_t s = get();
    assert(s.state == POWER_STATE_Q3_READY && s.current_valid && s.consecutive_failures == 0);
    assert(s.last_error == ESP_OK && s.reason == POWER_STATE_REASON_NONE && s.data.vbus_mv == 5200);
    publish_failure();
    assert(get().state == POWER_STATE_Q4_RETRYING && get().consecutive_failures == 1);
    publish_ok(5300);
    assert(get().state == POWER_STATE_Q3_READY && get().consecutive_failures == 0);
}

/** @brief 验证首次失败无历史数据，以及不完整成功样本按失败处理。 */
static void test_first_failure_and_invalid_publish(void)
{
    begin(true);
    publish_failure();
    power_state_snapshot_t s = get();
    assert(s.state == POWER_STATE_Q4_RETRYING && !s.has_data && !s.current_valid && !s.is_stale);
    assert(s.data.vbus_mv == 0 && s.data.sys_mv == 0 && s.last_success_us == 0);

    // 调用者误报成功时也不能产生有效快照，必须沿用同一失败计数规则。
    power_state_publish(NULL, ESP_OK);
    assert(get().consecutive_failures == 2 && get().last_error == ESP_ERR_INVALID_ARG);
    power_state_sample_t incomplete = { 0 };
    power_state_publish(&incomplete, ESP_OK);
    assert(get().state == POWER_STATE_Q5_UNAVAILABLE && !get().has_data);
}

/** @brief 检查指定活动状态的 10 秒边界、超时锁存和完整成功恢复。 */
static void assert_timeout_from(power_state_id_t from)
{
    // 通过正常发布到达指定活动状态，不直接篡改模块内部状态。
    begin(true);
    if (from != POWER_STATE_Q2_INITIALIZING) {
        publish_ok(5100);
    }
    if (from == POWER_STATE_Q4_RETRYING) {
        publish_failure();
    }
    const power_state_snapshot_t before = get();
    assert(before.state == from);

    // 分别检查边界前一微秒与达到 10 秒的结果，避免只有远超阈值才生效。
    atomic_store(&now_us, before.last_result_us + 9999999);
    assert(get().state == from);
    atomic_fetch_add(&now_us, 1);
    power_state_snapshot_t s = get();
    assert(s.state == POWER_STATE_Q5_UNAVAILABLE && !s.current_valid);
    assert(s.reason == POWER_STATE_REASON_UPDATE_TIMEOUT && s.last_error == ESP_ERR_TIMEOUT);
    assert(s.consecutive_failures == before.consecutive_failures && s.task_running);

    // 超时保持采样失败计数；后续失败维持 Q5，完整成功恢复 Q3。
    publish_failure();
    assert(get().state == POWER_STATE_Q5_UNAVAILABLE);
    publish_ok(5200);
    assert(get().state == POWER_STATE_Q3_READY);
}

/** @brief 验证尝试时间不延后超时，迟到失败也不能绕过超时判定。 */
static void test_timeout_and_attempts(void)
{
    assert_timeout_from(POWER_STATE_Q2_INITIALIZING);
    assert_timeout_from(POWER_STATE_Q3_READY);
    assert_timeout_from(POWER_STATE_Q4_RETRYING);

    // 生产者开始尝试却一直没有结果时，尝试时间不得替代结果时间续期。
    begin(true);
    publish_ok(5100);
    const int64_t result_us = get().last_result_us;
    atomic_store(&now_us, result_us + 9000000);
    power_state_record_attempt();
    atomic_store(&now_us, result_us + 10000000);
    assert(get().state == POWER_STATE_Q5_UNAVAILABLE);
    // 即使没有中途查询，迟到失败也不能绕过超时锁存。
    begin(true);
    atomic_store(&now_us, get().last_result_us + 10000000);
    power_state_publish(NULL, ESP_FAIL);
    assert(get().state == POWER_STATE_Q5_UNAVAILABLE);
}

/** @brief 覆盖停止后忽略结果、重新启动、ADC 关闭和快照副本独立性。 */
static void test_stop_restart_adc_disabled_and_copy(void)
{
    begin(true);
    power_state_stop(ESP_ERR_NOT_SUPPORTED);
    power_state_snapshot_t s = get();
    assert(s.state == POWER_STATE_Q5_UNAVAILABLE && !s.task_running);
    assert(s.reason == POWER_STATE_REASON_STARTUP_ERROR && s.last_error == ESP_ERR_NOT_SUPPORTED);

    // 严重错误停止后，即使收到成功结果也不能重新成为有效状态。
    publish_ok(5100);
    assert(!get().has_data && get().state == POWER_STATE_Q5_UNAVAILABLE);

    // 新生命周期关闭 ADC 后，不允许调用者传来的电压被解释为实际测量。
    assert(power_state_begin(false) == ESP_OK);
    power_state_sample_t data = { 0 };
    power_state_publish(&data, ESP_OK);
    s = get();
    assert(s.state == POWER_STATE_Q3_READY && s.current_valid && !s.adc_enabled);
    assert(!s.data.voltage_available && s.data.vbus_mv == 0 && s.data.sys_mv == 0);
    publish_ok(5100); // ADC 关闭时，传入电压仍归零并保持不可用。
    s = get();
    assert(!s.data.voltage_available && s.data.vbus_mv == 0);

    // 消费者只能修改自己的副本，不能借返回值改写模块内部数据。
    s.data.identity_raw = 255;
    assert(get().data.identity_raw != 255);
    power_state_stop(ESP_FAIL);
    assert(get().has_data && get().is_stale && !get().current_valid);
}

/** @brief 并发生产者；发布关联字段一致的样本，暴露快照撕裂风险。 */
static void *writer(void *arg)
{
    (void)arg;
    for (unsigned i = 1; i <= 20000; ++i) {
        publish_ok(i);
    }
    return NULL;
}

/** @brief 并发消费者；每次独立快照中的数据与时间必须属于同一结果。 */
static void *reader(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 30000; ++i) {
        power_state_snapshot_t s = get();
        if (s.has_data) {
            // 这些字段由同一合成值关联；任一跨轮混合都会破坏关联或时间一致性。
            assert(s.state == POWER_STATE_Q3_READY && s.current_valid && !s.is_stale);
            assert(s.data.identity_raw == (uint8_t)s.data.vbus_mv);
            const uint16_t expected = (uint16_t)~s.data.vbus_mv;
            assert(s.data.sys_mv == expected);
            assert(s.last_success_us == s.last_result_us);
        }
    }
    return NULL;
}

/** @brief 启动一写两读 pthread 线程，全部结束后检查最终结果。 */
static void test_concurrent_snapshots(void)
{
    begin(true);
    pthread_t threads[3];

    assert(pthread_create(&threads[0], NULL, writer, NULL) == 0);
    assert(pthread_create(&threads[1], NULL, reader, NULL) == 0);
    assert(pthread_create(&threads[2], NULL, reader, NULL) == 0);
    for (unsigned i = 0; i < 3; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(get().data.vbus_mv == 20000);
}

/** @brief 执行由合成样本、可控时钟和 pthread 锁驱动的主机状态机检查。 */
int main(void)
{
    test_disabled_and_startup();
    test_failure_threshold_and_recovery();
    test_first_failure_and_invalid_publish();
    test_timeout_and_attempts();
    test_stop_restart_adc_disabled_and_copy();
    test_concurrent_snapshots();
    puts("power_state: Q1-Q5 transitions, timeout, stale data, restart, ADC off and concurrent snapshots passed (no "
         "I2C linked)");
    return 0;
}
