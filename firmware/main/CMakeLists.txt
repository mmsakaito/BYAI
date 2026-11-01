#CMakeLists.txt
#！！！在此处按照格式添加.C源文件
#如果文件在当前目录直接输入文件名
#如果文件在二级目录以./开头 如./drv/i2c.c

#自动检索单层文件夹
file(GLOB SRC_C
        "*.c"
        "app/*.c"
        "drv/*.c"
)
#file(GLOB LVGL_C
#        "lib/lvgl/*.c"
#        "lib/lvgl/src/*.c"
#        "lib/lvgl/src/core/*.c"
#        "lib/lvgl/src/display/*.c"
#        "lib/lvgl/src/draw/*.c"
#        "lib/lvgl/src/drivers/*.c"
#        "lib/lvgl/src/font/*.c"
#        "lib/lvgl/src/indev/*.c"
#        "lib/lvgl/src/layouts/*.c"
#        "lib/lvgl/src/libs/*.c"
#        "lib/lvgl/src/misc/*.c"
#        "lib/lvgl/src/osal/*.c"
#        "lib/lvgl/src/others/*.c"
#        "lib/lvgl/src/stdlib/*.c"
#        "lib/lvgl/src/themes/*.c"
#        "lib/lvgl/src/tick/*.c"
#        "lib/lvgl/src/widgets/*.c"
#        "lib/lvgl/src/layouts/flex/*.c"
#        "lib/lvgl/src/draw/sw/*.c"
#        "lib/lvgl/src/themes/default/*.c"
#        "lib/lvgl/src/image/*.c"
#)
file(GLOB_RECURSE LVGL_C "lib/lvgl/src/*.c")
idf_component_register(
        SRCS
        ${SRC_C}
        ${LVGL_C}
        INCLUDE_DIRS
        "."
        "./app"
        "./drv"
        "lib/lvgl/"
        "lib/lvgl/src/"
        "lib/lvgl/src/core/"
        "lib/lvgl/src/display/"
        "lib/lvgl/src/draw/"
        "lib/lvgl/src/drivers/"
        "lib/lvgl/src/font/"
        "lib/lvgl/src/indev/"
        "lib/lvgl/src/layouts/"
        "lib/lvgl/src/libs/"
        "lib/lvgl/src/misc/"
        "lib/lvgl/src/osal/"
        "lib/lvgl/src/others/"
        "lib/lvgl/src/stdlib/"
        "lib/lvgl/src/themes/"

        "lib/lvgl/src/tick/"
        "lib/lvgl/src/widgets/"
        "lib/lvgl/src/layouts/flex/"
        "lib/lvgl/src/draw/sw/"
        "lib/lvgl/src/themes/default"
        "lib/lvgl/src/image/"
        REQUIRES
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
)

target_compile_options(${COMPONENT_LIB} PRIVATE
        -O0
        -g3
)