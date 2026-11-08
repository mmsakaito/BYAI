# Keep application sources explicit so local diagnostic files are not compiled.
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
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O0
    -g3
)
