/*****
 * @file    top_main.c
 * @brief   程序主文件
 *
 * @details
 * 开始写代码了
 * BYAI项目正式打响了 20261025 凌晨002000时开始代码了
 * 六个星星代表六六大顺
 *
 * @author  BYAI三巨头
 * @version 1.0.0
 * @date    2026-10-25
 * @copyright (c) BYAI. All rights reserved.
 *
 * @note
 * 慢慢写 多用AI
 *
 * @warning
 * 时间多
 *
 *******/

/*开始你的头文件Show*/
#include "esp_log.h"
#include "kernel.h"
#include "krnl_lvgl.h"
#include "st7789.h"
#include "sysdef.h"
/*写代码了*/
void app_main(void) {
	/*项目的开始*/
	krnl_setup(NULL);
	LCD_Init();
	TaskHandle_t Handle_lvgl;
	xTaskCreatePinnedToCore(main_lvgl, "app_lvgl", 32768, NULL, 5, &Handle_lvgl, 1);

	/*Core 0 处理所有中断
	 *不可以使用ESPLOGI中断不安全函数
	 *并且必须至少有一个任务
	 *给个while(0)*/
	TaskHandle_t Handle_test2;
	xTaskCreatePinnedToCore(main_test2, "app_test", 8192, NULL, 5, &Handle_test2, 0);
	/*代码底 FreeRTOS启动*/
	vTaskStartScheduler();

	/*运行不到这里 写了也没用*/
	while (1);
}