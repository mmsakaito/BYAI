/** @file FreeRTOS.h
 * @brief 将状态机临界区映射为 pthread 互斥锁的加锁和解锁。
 */
#pragma once

#include <pthread.h>

typedef pthread_mutex_t portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) ((void)pthread_mutex_lock(lock))
#define portEXIT_CRITICAL(lock) ((void)pthread_mutex_unlock(lock))
