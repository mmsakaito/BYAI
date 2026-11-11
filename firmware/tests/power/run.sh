#!/usr/bin/env bash
# 在临时目录编译真实驱动与 I2C 替身，退出时清理产物。
# 当前替身提供状态读取 I2C 声明和部分错误码；编译完整驱动还需补齐 esp_timer、
# esp_log、FreeRTOS、寄存器写入及 ESP_ERR_INVALID_STATE 的主机定义。
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
test_build_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_build_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$test_dir/mocks" -I"$test_dir/../../main/drv" \
    "$test_dir/test_bq25890.c" "$test_dir/../../main/drv/bq25890.c" \
    -o "$test_build_dir/test_bq25890"
"$test_build_dir/test_bq25890"
