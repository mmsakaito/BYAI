#!/usr/bin/env bash
# 在临时目录编译真实驱动与 I2C 替身，退出时清理产物。
# 真实驱动仍依赖 ESP-IDF 平台接口；当前替身未补齐，不能独立编译完整驱动。
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
test_build_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_build_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$test_dir/mocks" -I"$test_dir/../../main/drv" \
    "$test_dir/test_bq25890.c" "$test_dir/../../main/drv/bq25890.c" \
    -o "$test_build_dir/test_bq25890"
"$test_build_dir/test_bq25890"
