#pragma once

#include <stdbool.h>

bool platform_lvgl_lock(int timeout_ms);
void platform_lvgl_unlock(void);
void platform_lvgl_nav_enable(bool en);
bool platform_lvgl_nav_enabled(void);
