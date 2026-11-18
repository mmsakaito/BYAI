/**
 * @file    wm8978_playback.h
 * @brief   WM8978 播放通路：I2C 初始化 + 1 kHz 正弦波循环送 I2S。
 *
 * @note    总线由 board_i2c.c 创建，本模块不创建、不删除总线。
 *          本模块只做播放，不含录音通路。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 I2C：借用板级共享总线并挂载 WM8978（7 位地址 0x1A）。
 * @return ESP_OK 表示总线已就绪且设备挂载成功。
 * @note  成功时不发起任何总线事务。重复调用会产生重复的设备句柄。
 */
esp_err_t wm8978_i2c_init(void);

/**
 * @brief 启动播放：写寄存器 → 打开 I2S TX → 建任务循环输出 1 kHz 正弦波。
 * @return ESP_OK 表示任务已创建；寄存器或 I2S 初始化失败时返回对应错误码。
 * @note  需先调用 wm8978_i2c_init()。本函数先把 codec 配好再开 I2S——顺序不能
 *        颠倒：WM8978 未初始化时收到 MCLK 会进入异常状态，控制口随即静默。
 *        只走耳机通路（LOUT1/ROUT1），扬声器输出级不使能。
 *        48 kHz / 16 bit / 立体声，MCLK = 12.288 MHz，数据由 ESP32-S3 内部生成。
 */
esp_err_t wm8978_playback_start(void);

#ifdef __cplusplus
}
#endif
