// components/hal/include/hal/hal_display.h
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct hal_display_s hal_display_t;

typedef struct {
    void (*flush)(hal_display_t *self, int x1, int y1, int x2, int y2,
                  const void *px565);          // RGB565 区域刷新
    void (*set_backlight)(hal_display_t *self, uint8_t percent);
    void (*get_size)(hal_display_t *self, uint16_t *w, uint16_t *h);
} hal_display_api_t;

struct hal_display_s {
    const hal_display_api_t *api;   // 函数表
    void *impl;                     // 适配器私有数据
};

static inline void hal_display_flush(hal_display_t *d, int x1, int y1,
                                     int x2, int y2, const void *px) {
    d->api->flush(d, x1, y1, x2, y2, px);
}
static inline void hal_display_set_backlight(hal_display_t *d, uint8_t p) {
    d->api->set_backlight(d, p);
}
static inline void hal_display_get_size(hal_display_t *d, uint16_t *w, uint16_t *h) {
    d->api->get_size(d, w, h);
}
