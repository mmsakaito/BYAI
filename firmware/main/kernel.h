/**
 * @file kernel.h
 * @brief 显示 SPI 初始化入口及共享设备句柄。
 */
#pragma once
#include "sysdef.h"

/* 历史板级内存地址及容量常量；运行时容量由堆接口查询，分配由调用方执行。 */
#define ADDR_PSRAM 0x3F800000
#define SIZE_PSRAM 8388608
#define SIZE_SRAM 64 * 1024
/** @brief SPI2 屏幕设备借用句柄；初始为 NULL，不得由使用方删除。 */
extern spi_device_handle_t hspi2;
/**
 * @brief 初始化 SPI2 为 20 MHz、模式 0，并配置屏幕 D/C 输出。
 * @param p 未使用，传 NULL。
 * @note 启动流程串行调用一次，资源保留至重启。总线初始化失败时记录错误并返回；
 *       添加设备的结果仅记录日志，GPIO 配置结果直接丢弃。
 *       调用后通过 hspi2 检查设备是否创建成功。
 */
void krnl_setup(void *p);
