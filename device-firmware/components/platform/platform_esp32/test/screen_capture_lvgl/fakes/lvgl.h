#pragma once

#include <stdint.h>

typedef struct _lv_display_t lv_display_t;
typedef struct _lv_event_t lv_event_t;
typedef struct _lv_obj_t lv_obj_t;

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} lv_area_t;

typedef struct _lv_draw_buf_t {
    uint8_t *data;
} lv_draw_buf_t;

typedef enum {
    LV_RESULT_OK = 0,
    LV_RESULT_INVALID,
} lv_result_t;

typedef enum {
    LV_COLOR_FORMAT_RGB565 = 1,
} lv_color_format_t;

typedef void (*lv_event_cb_t)(lv_event_t *e);

#define LV_EVENT_FLUSH_START 42

void lv_display_add_event_cb(lv_display_t *disp, lv_event_cb_t cb, int code, void *user);
void *lv_event_get_target(lv_event_t *e);
void *lv_event_get_param(lv_event_t *e);
lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *disp);
lv_color_format_t lv_display_get_color_format(lv_display_t *disp);
int32_t lv_display_get_horizontal_resolution(const lv_display_t *disp);
int32_t lv_display_get_vertical_resolution(const lv_display_t *disp);
lv_obj_t *lv_layer_bottom(void);
lv_obj_t *lv_screen_active(void);
lv_result_t lv_obj_invalidate(const lv_obj_t *obj);
void lv_refr_now(lv_display_t *disp);
