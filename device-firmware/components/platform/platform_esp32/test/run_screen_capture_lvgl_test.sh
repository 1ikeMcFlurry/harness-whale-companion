#!/usr/bin/env bash
set -euo pipefail

task3_test_dir="$(cd "$(dirname "$0")/screen_capture_lvgl" && pwd)"
task3_component_dir="$(cd "$task3_test_dir/../.." && pwd)"
task3_test_bin="/tmp/trae_card_screen_capture_lvgl_test"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$task3_test_dir/fakes" \
  -I"$task3_component_dir/include" \
  "$task3_test_dir/test_screen_capture_lvgl.c" \
  -o "$task3_test_bin"

for task3_case in hook nav-enabled nav-disabled callback-snapshot cancel; do
  "$task3_test_bin" "$task3_case"
done
