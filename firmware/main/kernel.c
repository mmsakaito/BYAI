/**
 * @file kernel.c
 * @brief 配置显示使用的 SPI2 总线、设备及 D/C GPIO。
 * @copyright Copyright (c) <2026> <BYAI Group>
 */
#include "kernel.h"

#include <stddef.h>

#include "esp_log.h"
#include "driver/spi_master.h"
#include "sysdef.h"
#include "driver/gpio.h"
typedef void (*vptr)(void *);


void SysLoadApp() {
	
}
extern void cb_spi2_irq(spi_transaction_t *t);
spi_device_handle_t hspi2;
void krnl_setup(void *p) {
	/* SPI2 为只写主机，片选由 SPI 外设管理。 */
	esp_err_t ret;
	spi_bus_config_t scfg = {
		.miso_io_num = -1,
		.mosi_io_num = PIN_SPI2_MOSI,
		.sclk_io_num = PIN_SPI2_SCK,
		.quadhd_io_num = -1,
		.quadwp_io_num = -1,
		.max_transfer_sz = 4092,// 单次传输上限，单位为字节。
		.flags = SPICOMMON_BUSFLAG_MASTER,
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
		.post_cb = NULL,
	};
	ret = spi_bus_add_device(SPI2_HOST, &scfg2, &hspi2);
	ESP_LOGE("TAG", "2 %s", esp_err_to_name(ret));
	/* 这里只配置 D/C；复位和背光未接入 GPIO 控制。 */
	gpio_config_t gc ={
		.pull_down_en = false,
		.pull_up_en = false,
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (1ULL << PIN_SPI2_RS),
	};
	gpio_config(&gc);


}
