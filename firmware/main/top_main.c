/**
 * @file top_main.c
 * @brief 启动互斥的彩条或 LVGL 显示，以及正常电源采集或软件注入任务。
 *
 * @details ESP-IDF 已启动调度器；任务创建后 app_main 返回。
 *          任务创建失败只记录错误，不重启设备或修改持久化数据。
 *
 * @copyright (c) 2026 BYAI. All rights reserved.
 */

#include "esp_log.h"

#include "kernel.h"
#include "krnl_lvgl.h"
#include "st7789.h"
#include "power_state_fault_test.h"
#include "power_state_internal.h"
#include "power_test.h"
#include "sysdef.h"

static const char *TAG = "bringup";

/**
 * @brief 启动板级测试任务；ESP-IDF 已启动调度器，app_main 可直接返回。
 *
 * @note 显示与电源测试独立运行；创建失败时报告错误，不重启或修改持久化数据。
 */
void app_main(void)
{
    // 彩条与 LVGL 独占屏幕，不能同时初始化 SPI 或向同一屏幕写入。
#if CONFIG_BYAI_LCD_COLOR_BARS_TEST
    if (xTaskCreatePinnedToCore(main_test, "app_test", 8192, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display/PSRAM task");
    }
#else
    // 先初始化显示再创建 LVGL 任务；电源采集不依赖显示任务是否创建成功。
    krnl_setup(NULL);
    if (hspi2 != NULL) {
        LCD_Init();
        if (xTaskCreatePinnedToCore(main_lvgl, "app_lvgl", 32768, NULL, 5, NULL, 1) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create LVGL task");
        }
    } else {
        ESP_LOGE(TAG, "SPI initialization failed; LVGL not started");
    }
#endif
#if CONFIG_BYAI_POWER_BASIC_TEST
#if CONFIG_BYAI_POWER_ADC_TEST
    const bool adc_enabled = true;
#else
    const bool adc_enabled = false;
#endif

    // 先预留唯一生产者并发布 Q2，任务开始运行时即可使用状态机。
    esp_err_t power_err = power_state_begin(adc_enabled);
    if (power_err != ESP_OK) {
        ESP_LOGE(TAG, "Power collector already running: %s", esp_err_to_name(power_err));
    }
#if CONFIG_BYAI_POWER_STATE_FAULT_TEST
    // 诊断任务与真实采集互斥，防止合成结果和实测结果同时写入一个状态机。
    else if (xTaskCreatePinnedToCore(power_state_fault_test_task, "power_fault", 6144, NULL, 5, NULL, 1) != pdPASS) {
#else
    else if (xTaskCreate(power_test_task, "power_test", 4096, NULL, 5, NULL) != pdPASS) {
#endif
        // 预留状态成功但任务创建失败时，必须撤销运行标志并报告 Q5。
        power_state_stop(ESP_ERR_NO_MEM);
        ESP_LOGE(TAG, "Failed to create power test task");
    }
#endif
}
