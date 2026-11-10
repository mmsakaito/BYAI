/**
 * @file power_state_internal.h
 * @brief 启动者与唯一采集任务使用的状态发布接口。
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
 * @note 在任务上下文调用，短临界区内清除历史状态；任务及设备由调用者管理。
 *       重新启动前须完成旧任务退出。
 */
esp_err_t power_state_begin(bool adc_enabled);

/**
 * @brief 唯一采集任务在每轮开始时记录尝试时间；不会延后无结果超时。
 *
 * @note 在任务上下文调用，内部持短临界区；停止后忽略调用，操作限于内存和时钟。
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
 *       在任务上下文调用，短临界区内复制 sample 至内部存储；采集者须在调用前结束全部 I2C。
 *       设备、任务及持久化数据由调用者管理。
 */
void power_state_publish(const power_state_sample_t *sample, esp_err_t error);

/**
 * @brief 发布严重错误，立即进入 Q5 并标记任务停止；调用者随后回收设备并退出。
 *
 * @param error 严重错误码；误传 ESP_OK 时转为 ESP_ERR_INVALID_STATE。
 *
 * @note 在任务上下文调用，短临界区内更新状态及有效性，采样失败次数保持原值。
 *       停止后仍可查询历史数据；设备释放、看门狗配置和实际任务退出由调用者负责。
 */
void power_state_stop(esp_err_t error);
