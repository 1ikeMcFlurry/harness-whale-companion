// components/hal/include/hal/hal_types.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 通用返回码:0 表示成功,负值表示失败。
// hal 层刻意不引用 esp_err_t,保持零依赖,便于在 PC 上单测。
typedef int hal_err_t;
