#标识CMake版本 用处不大
cmake_minimum_required(VERSION 3.16)
#两行神秘代码supress编译器报错
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
#ESP32自动配置项
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#项目名！
project(BYAI)

