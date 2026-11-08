#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
test_build_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_build_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$test_dir/mocks" -I"$test_dir/../../main/app" \
    "$test_dir/test_power_state.c" "$test_dir/../../main/app/power_state.c" \
    -o "$test_build_dir/test_power_state"
"$test_build_dir/test_power_state"
