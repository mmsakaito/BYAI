#include "esp_log.h"
#include "sysdef.h"
#include "app_test.h"

#include "kernel.h"
#include "st7789.h"

void main_test(void *p) {

	krnl_setup(NULL);
	LCD_Init();
	while (1) {
		uint8_t *buf = heap_caps_malloc(12, MALLOC_CAP_SPIRAM);
		if (buf == NULL) {
			ESP_LOGI("app_test","MALLOC ERROR");
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		memset(buf, 5, 12);
		size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
		size_t free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
		ESP_LOGI("app_test","PSRAM total=%d, free=%d\n", (int)total, (int)free);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

}

void main_test2(void *p) {
	while (1) {

	}

}