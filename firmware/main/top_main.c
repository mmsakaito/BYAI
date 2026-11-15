/**
 * @file top_main.c
 * @brief 启动显示、电源采集、唯一 Wi-Fi 服务及本地网页 OTA。
 *
 * @details 显示/电源任务创建失败只记录错误。网络与 OTA 本地初始化失败时，
 *          待验证镜像请求 SDK 回退；成功才确认本次启动，不等待联网或外设可用。
 *
 * @copyright (c) 2026 BYAI. All rights reserved.
 */

#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"

#include "kernel.h"
#include "krnl_lvgl.h"
#include "st7789.h"
#include "power_state_fault_test.h"
#include "power_state_internal.h"
#include "power_test.h"
#include "sysdef.h"
#include "ota.h"

static const char *TAG = "bringup";

esp_err_t byai_network_start(void (*on_ready)(bool ready));

/**
 * @brief 初始化网络及 OTA，保留板级测试路径，最后确认待验证镜像。
 *
 * @note 仅汇总网络与 OTA 本地初始化错误；显示/电源不参与首启确认。
 */
void app_main(void)
{
    esp_err_t startup_result = byai_network_start(byai_ota_set_network_ready);
    if (startup_result == ESP_OK) {
        startup_result = byai_ota_init();
    }
    if (startup_result != ESP_OK) {
        ESP_LOGE(TAG, "Network/OTA startup: %s", esp_err_to_name(startup_result));
    }

    // 彩条与 LVGL 互斥启动，各自独占屏幕及 SPI 初始化。
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
    esp_err_t confirm_result = byai_ota_confirm_boot(startup_result);
    if (confirm_result != ESP_OK) {
        ESP_LOGE(TAG, "Boot confirmation: %s", esp_err_to_name(confirm_result));
    }
}
