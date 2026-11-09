cmake_minimum_required(VERSION 3.16)
# 跳过 CMake 的编译器可用性探测；此设置不保证后续编译或链接成功。
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
# 加载 ESP-IDF 的项目配置与构建规则。
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(BYAI)

