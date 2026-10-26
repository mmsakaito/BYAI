#CMakeLists.txt
#！！！在此处按照格式添加.C源文件
#如果文件在当前目录直接输入文件名
#如果文件在二级目录以./开头 如./drv/i2c.c
idf_component_register(
        SRCS
        "top_main.c"
        "kernel.c"
        "gui.c"
        "./drv/st7789.c"
        "./app/app_test.c"
        "./app/krnl_lvgl.c"
        INCLUDE_DIRS
        "."
        "./app"
<<<<<<< Updated upstream
    REQUIRES
=======
        "./drv"
        REQUIRES
>>>>>>> Stashed changes
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
)
