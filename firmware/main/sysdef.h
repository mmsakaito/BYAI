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
/*定义一些常见函数*/
#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))