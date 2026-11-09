/**
 * @file krnl_lvgl.h
 * @brief 单任务 LVGL 演示及显示移植入口。
 */
#pragma once
#include "sysdef.h"
#include "lvgl.h"
/**
 * @brief 创建双缓冲 partial 显示和圆弧动画，持续处理 LVGL 定时任务。
 * @param p 未使用，传 NULL。
 * @note 调用前须完成 krnl_setup 和 LCD_Init；任务独占 LVGL 及屏幕 SPI，
 *       不返回。显示和缓冲保留至重启，当前未检查分配或创建失败。
 *       刷新仅按 RGB565 发送，未转换字节序；多 SPI 分块路径存在限制，
 *       详见刷新回调说明，不可视为通用 partial 刷新实现。
 */
void main_lvgl(void *p);