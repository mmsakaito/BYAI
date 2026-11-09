/**
 * @file kernel.h
 * @brief 显示 SPI 初始化入口及共享设备句柄。
 */
#pragma once
#include "sysdef.h"

/* 历史板级内存常量；不代表运行时探测结果，也不负责分配内存。 */
#define ADDR_PSRAM 0x3F800000
#define SIZE_PSRAM 8388608
#define SIZE_SRAM 64 * 1024
/** @brief SPI2 屏幕设备借用句柄；初始为 NULL，不得由使用方删除。 */
extern spi_device_handle_t hspi2;
/**
 * @brief 初始化 SPI2 为 20 MHz、模式 0，并配置屏幕 D/C 输出。
 * @param p 未使用，传 NULL。
 * @note 由启动流程串行调用一次，不支持并发或重复初始化。总线初始化失败
 *       时记录错误并返回；添加设备的结果仅记录日志，GPIO 配置结果未检查。
 *       无返回状态，调用方需检查 hspi2；资源保留至重启。
 */
void krnl_setup(void *p);
