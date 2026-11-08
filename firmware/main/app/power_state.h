#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 数据可用性状态；Q3 不代表芯片故障字段均正常。 */
typedef enum {
    POWER_STATE_Q1_DISABLED,     /**< 未启用，无采集任务。 */
    POWER_STATE_Q2_INITIALIZING, /**< 初始化并等待首次完整结果。 */
    POWER_STATE_Q3_READY,        /**< 最新完整结果可用。 */
    POWER_STATE_Q4_RETRYING,     /**< 前两次连续失败，保留旧值并重试。 */
    POWER_STATE_Q5_UNAVAILABLE,  /**< 不可用；结合 reason 和 task_running 判断原因。 */
} power_state_id_t;

/** @brief 当前状态的诊断原因，与芯片自身的故障寄存器独立。 */
typedef enum {
    POWER_STATE_REASON_NONE,
    POWER_STATE_REASON_SAMPLE_ERROR,
    POWER_STATE_REASON_UPDATE_TIMEOUT,
    POWER_STATE_REASON_STARTUP_ERROR,
} power_state_reason_t;

/**
 * @brief 最近一次完整成功的测量；字段编码沿用 BQ25890H 寄存器定义。
 *
 * @note 不推断电池存在、电量、真实充电或真实温度。voltage_available=false 时
 *       vbus_mv/sys_mv 为 0，不能把 0 解释为实际电压。
 */
typedef struct {
    uint8_t identity_raw;
    uint8_t part_number;
    uint8_t revision;

    uint8_t input_source;
    uint8_t charge_state;
    bool power_good;
    bool system_min_regulation;

    uint8_t fault_history_raw;
    uint8_t fault_current_raw;

    bool voltage_available;
    uint16_t vbus_mv;
    uint16_t sys_mv;
} power_state_sample_t;

/**
 * @brief 一致性快照，data 为自有副本，不包含设备句柄或借用指针。
 *
 * @note has_data 表示存在完整历史结果；仅 current_valid=true 时可作为当前数据。
 *       Q4/Q5 中历史 data 保留但 is_stale=true，Q5 中仅用于诊断。
 *       所有时间均为本次启动后的单调微秒，不是墙上时间；0 表示尚无该事件，
 *       数据存在性仍以 has_data 为准。last_result_us 是最后结果或初始化起点。
 */
typedef struct {
    power_state_id_t state;
    power_state_reason_t reason;
    esp_err_t last_error;

    bool task_running;
    bool adc_enabled;

    bool has_data;
    bool is_stale;
    bool current_valid;

    uint32_t consecutive_failures; /**< 成功清零，失败递增并在 UINT32_MAX 饱和。 */

    int64_t last_attempt_us; /**< 最近一次采样尝试开始的单调时间，单位微秒。 */
    int64_t last_result_us;  /**< 最近结果时间；尚未返回结果时为初始化起点。 */
    int64_t last_success_us; /**< 最近完整成功结果的发布时间，单位微秒。 */

    power_state_sample_t data; /**< 只有 has_data=true 时才存在历史完整结果。 */
} power_state_snapshot_t;

/**
 * @brief 获取状态副本，并将达到 10 秒无结果的活动状态锁存为 Q5。
 *
 * @param out_snapshot 调用者提供的输出空间，成功后独立于内部存储。
 *
 * @return ESP_OK 或 ESP_ERR_INVALID_ARG；设备错误在快照 last_error 中表达。
 *
 * @note 可由多个任务查询；只在短临界区内读取/更新内存和单调时钟，不执行 I2C、
 *       等待、日志或硬件恢复。不能从 ISR 调用。Q5 仅完整成功或重新启动才能解除。
 *       功能未启动时返回 Q1，无寄存器访问、持久化或动态资源分配。
 */
esp_err_t power_state_get_snapshot(power_state_snapshot_t *out_snapshot);
