#!/usr/bin/env bash
# Compile the actual firmware OTA source against SDK-boundary host substitutes.
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf -- "$build_dir"' EXIT
"${CC:-cc}" -std=gnu11 -O0 -ffunction-sections -fdata-sections \
    -I"$test_dir/mocks" -I"$test_dir/../../main/app" \
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter \
    "$test_dir/test_ota.c" -Wl,--gc-sections -o "$build_dir/test_ota"
"$build_dir/test_ota"
