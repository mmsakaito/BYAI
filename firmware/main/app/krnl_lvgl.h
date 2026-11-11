/**
 * @file krnl_lvgl.h
 * @brief 单任务 LVGL 演示及显示移植入口。
 */
#pragma once
#include "sysdef.h"
#include "lvgl.h"
/**
 * @brief 创建双缓冲 partial 显示、圆弧动画和电源面板，持续处理 LVGL 定时任务。
 * @param p 未使用，传 NULL。
 * @note 调用前须完成 krnl_setup 和 LCD_Init，PSRAM 必须可用；任务独占 LVGL
 *       及屏幕 SPI，持续运行。电源采集独立运行，标签通过快照接口获取数据。
 *       动画和标签定时器随 handler 串行执行；每轮 tick 增加 5 ms，
 *       实际周期还包含绘制、同步 SPI 和任务调度耗时。
 *
 * @note 按单次启动设计，显示、对象、定时器和双缓冲保留至重启；
 *       分配及创建结果直接用于后续 LVGL 调用，资源生命周期与常驻任务一致。
 *       刷新按原 RGB565 字节序发送；多 SPI 分块会重复发送首段，详见刷新回调。
 */
void main_lvgl(void *p);