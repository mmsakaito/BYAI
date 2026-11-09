/**
 * @file app_test.h
 * @brief 显示与 PSRAM 板级诊断任务入口。
 */
#pragma once

/**
 * @brief 初始化显示并持续执行 PSRAM 诊断；按配置每秒绘制一次彩条。
 * @param p 未使用，传 NULL。
 * @note 任务不返回，独占屏幕和 SPI 初始化，不得与 LVGL 任务同时运行。
 *       临时 PSRAM 缓冲每轮释放；分配失败时记录日志并延时重试。
 *       显示初始化中的 SPI 错误由 ESP_ERROR_CHECK 终止。
 */
void main_test(void *p);
