#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
test_build_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_build_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$test_dir/mocks" -I"$test_dir/../../main/drv" \
    "$test_dir/test_bq25890.c" "$test_dir/../../main/drv/bq25890.c" \
    -o "$test_build_dir/test_bq25890"
"$test_build_dir/test_bq25890"
