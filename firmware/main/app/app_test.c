#include "esp_log.h"
#include "sysdef.h"
#include "app_test.h"
void main_test(void *p) {
	while (1) {
		ESP_LOGI("app_test","tick_ok");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

}

void main_test2(void *p) {
	while (1) {

	}

}