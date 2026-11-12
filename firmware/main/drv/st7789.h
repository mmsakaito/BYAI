/**
 * @file st7789.h
 * @brief ST7789 屏幕的同步 SPI 写入接口。
 * @note 所有接口须在 krnl_setup 成功后由屏幕唯一使用方串行调用；
 *       调用方负责串行化彩条、LVGL 等屏幕操作，避免并发访问。
 *       命令和数据写入失败触发 ESP_ERROR_CHECK 终止。
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "sysdef.h"

#define USE_HORIZONTAL 3  // 0、1 为竖屏，2、3 为横屏；对应不同扫描方向。


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W DISP_VER
#define LCD_H DISP_HOR

#else
#define LCD_W DISP_HOR
#define LCD_H DISP_VER
#endif



/**
 * @brief 按当前 D/C 状态同步发送一个字节。
 * @param dat 待发送字节；发送期间保持当前命令/数据状态。
 */
void LCD_WR_DATA8(uint8_t dat);
/**
 * @brief 按高字节在前的顺序发送 16 位数据，保持当前 D/C 状态。
 * @param dat 待发送数据。
 */
void LCD_WR_DATA(uint16_t dat);
/**
 * @brief 切换 D/C 发送一字节命令，随后恢复数据状态。
 * @param dat 命令字节。
 */
void LCD_WR_REG(uint8_t dat);
/**
 * @brief 设置含首尾坐标的窗口；除方向 0 外，同时发出 RAM 写命令。
 * @param x1 起始列。
 * @param y1 起始行。
 * @param x2 结束列。
 * @param y2 结束行。
 * @note 调用方须提供有效坐标；方向 0、1 的行地址增加 20。
 */
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);
/**
 * @brief 阻塞发送当前面板初始化序列，设置初始 320×240 地址窗口。
 * @note 复位及背光辅助函数体为空；板级硬件须满足面板复位和背光条件。
 */
void LCD_Init(void);

/**
 * @brief 同步绘制八条全高 RGB565 彩条，按相位循环移色。
 * @param phase 水平方向的颜色轮换相位，按 8 取模。
 * @return 完成时 ESP_OK；设备句柄为空时 ESP_ERR_INVALID_STATE；
 *         分配失败时 ESP_ERR_NO_MEM；像素传输失败时返回 SPI 错误。
 * @note 须在 LCD_Init 后由屏幕唯一使用方调用。函数临时拥有一行内部
 *       DMA 缓冲，返回前释放。设置窗口时的 SPI 失败
 *       触发 ESP_ERROR_CHECK 终止；上述 SPI 错误返回值对应像素传输失败。
 */
esp_err_t lcd_draw_color_bars(unsigned int phase);




