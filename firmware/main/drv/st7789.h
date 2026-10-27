#pragma once
#include <stdint.h>
#include "esp_err.h"


#define USE_HORIZONTAL 3  //设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W 240
#define LCD_H 320

#else
#define LCD_W 320
#define LCD_H 240
#endif



void LCD_WR_DATA8(uint8_t dat);//写入一个字节
void LCD_WR_DATA(uint16_t dat);//写入两个字节
void LCD_WR_REG(uint8_t dat);//写入一个指令
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);//设置坐标函数
void LCD_Init(void);//LCD初始化

/**
 * @brief Draw eight full-height RGB565 TV color bars, cyclically shifted.
 *
 * Call after LCD_Init from the display-owning task, without concurrent SPI
 * users. Uses one internal DMA scanline owned only for the synchronous draw;
 * no retained buffer or persistent state. Returns SPI/allocation errors.
 * @param phase Horizontal color rotation, modulo eight.
 * @return ESP_OK on completion; an esp_err_t on failure.
 */
esp_err_t lcd_draw_color_bars(unsigned int phase);




