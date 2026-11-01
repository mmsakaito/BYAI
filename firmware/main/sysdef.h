#pragma once
#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"
#include "driver/spi_master.h"
/*include app*/
#include "app_test.h"

/*定义外设*/
#define PIN_SPI2_SCK GPIO_NUM_41
#define PIN_SPI2_MOSI GPIO_NUM_42
#define PIN_SPI2_CS GPIO_NUM_5
#define PIN_SPI2_RS GPIO_NUM_2
/* 20261001 板卡：I2C 与音频/触控共用；INT 接线由成员确认。 */
#define PIN_I2C1_SCL GPIO_NUM_38
#define PIN_I2C1_SDA GPIO_NUM_39
#define PIN_BQ25890_INT GPIO_NUM_4
/*定义一些常见函数*/
#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))
