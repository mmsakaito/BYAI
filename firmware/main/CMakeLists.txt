# 应用仅编译下列显式列出的源文件；LVGL 源码递归收集。
file(GLOB_RECURSE LVGL_C "lib/lvgl/src/*.c")

idf_component_register(
    SRCS
        "top_main.c"
        "kernel.c"
        "gui.c"
        "board_i2c.c"
        "drv/st7789.c"
        "drv/bq25890.c"
        "app/app_test.c"
        "app/krnl_lvgl.c"
        "app/power_test.c"
        "app/power_state.c"
        "app/power_state_fault_test.c"
        "app/network.c"
        "app/ota.c"
        ${LVGL_C}
    INCLUDE_DIRS
        "."
        "app"
        "drv"
        "lib/lvgl"
        "lib/lvgl/src"
    REQUIRES
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
        esp_timer
        esp_netif
        esp_wifi
        nvs_flash
        esp_wifi_service
        esp_config_manager
        esp_service
        adf_event_hub
        app_update
        esp_http_server
        espressif__cjson
    EMBED_TXTFILES "web/ota_web.html"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O0
    -g3
)
