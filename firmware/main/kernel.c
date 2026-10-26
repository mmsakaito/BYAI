/**
* @file    <kernel32.c>
 * @brief   <这个文件实现所有的动态内存管理和统计、APP运行和结束，所有的内核操作都写在这里>
 * !注意这个文件的所有函数都要懂
 * @author  <蒋蒋>
 * @date    <2026-10-7>
 * @version <v1.0>
 * @copyright Copyright (c) <2026> <BYAI Group>
 */
#include "kernel.h"

#include <stddef.h>

#include "esp_log.h"
#include "driver/spi_master.h"
#include "sysdef.h"
#include "driver/gpio.h"

/* 任何任务的函数都是输入为void*输出为void，定义一个函数指针叫vptr */
typedef void (*vptr)(void *);
//---------------------------------------------------------
// 函数名称: malloc
// 函数功能: 分配大段内存
// 参数: size - 想分配的大小 字节
// 返 回 值: void * (需强制转换)
//---------------------------------------------------------
// void* malloc(size_t size)
// {
// 	return heap_caps_malloc_default(size);
// }
//
// void* realloc(void* ptr, size_t size)
// {
// 	return heap_caps_realloc_default(ptr, size);
// 	//Note: LVGL需要realloc函数 ESP32 FreeRTOS刚好提供了
// }
//
// void free(void *ptr)
// {
// 	heap_caps_free(ptr);
// }

void SysLoadApp() {
	
}
spi_device_handle_t hspi2;
void krnl_setup(void *p) {
	/*SPI2 Init*/
	esp_err_t ret;
	spi_bus_config_t scfg = {
		.miso_io_num = -1,
		.mosi_io_num = PIN_SPI2_MOSI,
		.sclk_io_num = PIN_SPI2_SCK,
		.quadhd_io_num = -1,
		.quadwp_io_num = -1,
		.max_transfer_sz = 0,//0=默认,
		.flags = SPICOMMON_BUSFLAG_MASTER,//初始化时检查总线
	};
	ret = spi_bus_initialize(SPI2_HOST, &scfg, SPI_DMA_CH_AUTO);
	if (ret != ESP_OK) {
		ESP_LOGE("TAG", "1 %s", esp_err_to_name(ret));
		return;
	}
	spi_device_interface_config_t scfg2 = {
		.mode = 0,
		.clock_speed_hz = 20000000,
		.queue_size = 8,
		.spics_io_num = PIN_SPI2_CS,
	};
	ret = spi_bus_add_device(SPI2_HOST, &scfg2, &hspi2);
	ESP_LOGE("TAG", "2 %s", esp_err_to_name(ret));
	/* gpio init*/
	gpio_config_t gc ={
		.pull_down_en = false,
		.pull_up_en = false,
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (1ULL << PIN_SPI2_RS),
	};
	gpio_config(&gc);

	gc.pin_bit_mask |= (1ULL << PIN_SPI2_CS);
	gpio_config(&gc);
}