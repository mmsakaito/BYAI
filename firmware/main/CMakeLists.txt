# 应用源文件显式列出，避免编入本地诊断文件；仅 LVGL 源码递归收集。
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
        "app/wm8978_playback.c"
        ${LVGL_C}
    INCLUDE_DIRS
        "."
        "app"
        "drv"
        "lib/lvgl"
        "lib/lvgl/src"
    REQUIRES
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_driver_gpio
        esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O0
    -g3
)
