/**
 * @file power_state_internal.h
 * @brief 启动者与唯一采集任务使用的状态发布接口，不供普通消费者写入。
 */

#pragma once

#include "power_state.h"

/**
 * @brief 启动者在创建任务前预留单一采集者并发布 Q2，停止后允许重新启动。
 *
 * @param adc_enabled 是否要求基础状态和电压同时成功才发布完整结果。
 *
 * @return 已有活动采集者时返回 ESP_ERR_INVALID_STATE，否则返回 ESP_OK。
 *
 * @note 清除历史状态；内部持短临界区，不可从 ISR 调用。不创建任务、不访问芯片。
 *       必须先退出旧任务再重新启动。
 */
esp_err_t power_state_begin(bool adc_enabled);

/**
 * @brief 唯一采集任务在每轮开始时记录尝试时间；不会延后无结果超时。
 *
 * @note 内部持短临界区；停止后忽略调用，不访问硬件，不可从 ISR 调用。
 */
void power_state_record_attempt(void);

/**
 * @brief 唯一采集者发布本轮结果，每轮最多调用一次；停止后忽略迟到结果。
 *
 * @param sample 完整成功时必须非 NULL；内容复制，失败时不读取且保留旧快照。
 * @param error ESP_OK 表示完整成功，否则累计一次失败并保存错误。
 *
 * @note ADC 开启时要求 sample->voltage_available；不合格结果按失败处理。
 *       ESP_OK 配合 NULL 或缺少所需电压的样本时，按 ESP_ERR_INVALID_ARG 累计失败。
 *       内部持短临界区，复制而不持有 sample；不可从 ISR 调用，无持久化行为。
 *       采集者必须在调用前结束全部 I2C；本模块不拥有硬件或线程生命周期。
 */
void power_state_publish(const power_state_sample_t *sample, esp_err_t error);

/**
 * @brief 发布严重错误，立即进入 Q5 并标记任务停止；调用者随后回收设备并退出。
 *
 * @param error 严重错误码；误传 ESP_OK 时转为 ESP_ERR_INVALID_STATE。
 *
 * @note 内部持短临界区，不可从 ISR 调用。不释放硬件，不恢复看门狗，
 *       不增加采样失败次数；停止后仍可查询历史数据，不代表旧任务已完全退出。
 */
void power_state_stop(esp_err_t error);
