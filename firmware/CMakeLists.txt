cmake_minimum_required(VERSION 3.16)
# 跳过 CMake 的编译器可用性探测；编译和链接在实际构建时执行。
SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
# 加载 ESP-IDF 的项目配置与构建规则。
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED PROJECT_VER)
    execute_process(COMMAND git rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE BYAI_LOCAL_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE BYAI_GIT_RESULT)
    if(BYAI_GIT_RESULT EQUAL 0 AND BYAI_LOCAL_SHA MATCHES "^[0-9a-f]+$")
        set(PROJECT_VER "local-${BYAI_LOCAL_SHA}")
    else()
        set(PROJECT_VER "local")
    endif()
endif()
project(BYAI)

