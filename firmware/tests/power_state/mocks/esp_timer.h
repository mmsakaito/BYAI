/** @file esp_timer.h
 * @brief 声明由状态机测试控制并显式推进的微秒时钟。
 */
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
