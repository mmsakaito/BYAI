#include "esp_log.h"
#include "sysdef.h"
#include "app_test.h"
#include <string.h>
#include "esp_heap_caps.h"

#include "kernel.h"
#include "st7789.h"

void main_test(void *p) {

	krnl_setup(NULL);
	LCD_Init();
#if CONFIG_BYAI_LCD_COLOR_BARS_TEST
    unsigned int color_bar_phase = 0;
#endif
	while (1) {
#if CONFIG_BYAI_LCD_COLOR_BARS_TEST
        esp_err_t display_err = lcd_draw_color_bars(color_bar_phase++);
        if (display_err != ESP_OK) {
            ESP_LOGE("lcd_test", "Color bars failed: %s", esp_err_to_name(display_err));
        } else if (color_bar_phase % 8 == 1) {
            ESP_LOGI("lcd_test", "Color bars running: %dx%d RGB565", LCD_W, LCD_H);
        }
#endif
		uint8_t *buf = heap_caps_malloc(12, MALLOC_CAP_SPIRAM);
		if (buf == NULL) {
			ESP_LOGI("app_test","MALLOC ERROR");
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		memset(buf, 5, 12);
		heap_caps_free(buf);
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