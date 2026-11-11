/**
 * @file sysdef.h
 * @brief 板级引脚、显示尺寸及任务延时宏。
 */
#pragma once
#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"
#include "driver/spi_master.h"
#include "app_test.h"

/* 显示 SPI2：只接时钟、MOSI、片选和 D/C，不配置 MISO。 */
#define PIN_SPI2_SCK GPIO_NUM_41
#define PIN_SPI2_MOSI GPIO_NUM_42
#define PIN_SPI2_CS GPIO_NUM_5
#define PIN_SPI2_RS GPIO_NUM_2
/* 20261001 板卡：I2C 与音频/触控共用；INT 定义对应板级接线假设。 */
#define PIN_I2C1_SCL GPIO_NUM_38
#define PIN_I2C1_SDA GPIO_NUM_39
#define PIN_BQ25890_INT GPIO_NUM_4
/* 将毫秒换算为 RTOS tick 后挂起当前任务；恢复执行时间随 tick 精度及任务调度变化。 */
#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

/* 当前横屏逻辑尺寸，单位为像素。 */
#define DISP_HOR 320
#define DISP_VER 240
