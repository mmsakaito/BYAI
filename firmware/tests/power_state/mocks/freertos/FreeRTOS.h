/** @file FreeRTOS.h
 * @brief 用 pthread 互斥锁模拟状态机临界区，不模拟 FreeRTOS 中断屏蔽。
 */
#pragma once

#include <pthread.h>

typedef pthread_mutex_t portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) ((void)pthread_mutex_lock(lock))
#define portEXIT_CRITICAL(lock) ((void)pthread_mutex_unlock(lock))
