/** @file esp_timer.h
 * @brief 声明由状态机测试控制的微秒时钟，不使用真实运行时间。
 */
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
