#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct _lv_display_t lv_display_t;

typedef bool (*platform_capture_region_cb_t)(int x, int y, int w, int h,
                                             const uint8_t *rgb565, void *user);
typedef void (*platform_capture_done_cb_t)(bool ok, void *user);

/*
 * Register the display flush hook, or update the callbacks for the next capture
 * when the same display is already registered. An in-flight capture keeps its
 * own callback/user snapshot. The region callback runs synchronously while LVGL
 * owns the RGB565 flush buffer and must not return until the buffer can be reused.
 */
void platform_screen_capture_init(lv_display_t *disp,
                                  platform_capture_region_cb_t region_cb,
                                  platform_capture_done_cb_t done_cb,
                                  void *user);

bool platform_screen_capture_start(void);

/*
 * Cancellation cannot release a wait owned by the application. Before calling
 * this function, the application must wake any ACK wait used by region_cb so it
 * can return false and allow the synchronous redraw to unwind.
 */
void platform_screen_capture_cancel(void);
bool platform_screen_capture_active(void);
