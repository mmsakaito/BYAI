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

#include "esp_log.h"
#include "sysdef.h"
#include "power_test.h"

static const char *TAG = "bringup";

/**
 * @brief 启动板级测试任务；ESP-IDF 已启动调度器，app_main 可直接返回。
 * @note 显示与电源测试独立运行；创建失败时报告错误，不重启或修改持久化数据。
 */
void app_main(void)
{
    if (xTaskCreatePinnedToCore(main_test, "app_test", 8192, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display/PSRAM task");
    }
#if CONFIG_BYAI_POWER_BASIC_TEST
    if (xTaskCreate(power_test_task, "power_test", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power test task");
    }
#endif
}
