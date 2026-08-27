#include "platform/platform_screen_capture.h"

#include "lvgl.h"
#include "platform/platform_factory.h"

#include <stddef.h>
#include <stdatomic.h>

typedef enum {
    CAPTURE_IDLE = 0,
    CAPTURE_ACTIVE,
    CAPTURE_CANCELLED,
    CAPTURE_FINISHING,
} capture_state_t;

static lv_display_t *s_disp;
static platform_capture_region_cb_t s_config_region_cb;
static platform_capture_done_cb_t s_config_done_cb;
static void *s_config_user;
static platform_capture_region_cb_t s_active_region_cb;
static platform_capture_done_cb_t s_active_done_cb;
static void *s_active_user;
static atomic_int s_state = ATOMIC_VAR_INIT(CAPTURE_IDLE);
static bool s_nav_frozen;
static bool s_nav_was_enabled;
static bool s_region_seen;
static bool s_region_failed;

static void capture_flush_start(lv_event_t *e);

static void capture_abort(void)
{
    int expected = CAPTURE_ACTIVE;
    (void)atomic_compare_exchange_strong(&s_state, &expected, CAPTURE_CANCELLED);
}

static void capture_cleanup_locked(void)
{
    atomic_store(&s_state, CAPTURE_FINISHING);
    if (s_nav_frozen) {
        s_nav_frozen = false;
        platform_lvgl_nav_enable(s_nav_was_enabled);
    }
    s_active_region_cb = NULL;
    s_active_done_cb = NULL;
    s_active_user = NULL;
    atomic_store(&s_state, CAPTURE_IDLE);
}

static bool capture_area_valid(lv_display_t *disp, const lv_area_t *area,
                               const lv_draw_buf_t *buf)
{
    if (disp == NULL || area == NULL || buf == NULL || buf->data == NULL) return false;
    if (lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565) return false;
    if (area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1) return false;
    if (area->x2 >= lv_display_get_horizontal_resolution(disp) ||
        area->y2 >= lv_display_get_vertical_resolution(disp)) return false;
    return true;
}

static void capture_flush_start(lv_event_t *e)
{
    if (atomic_load(&s_state) != CAPTURE_ACTIVE) return;

    lv_display_t *disp = lv_event_get_target(e);
    lv_area_t *area = lv_event_get_param(e);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (disp != s_disp || !capture_area_valid(disp, area, buf) || s_active_region_cb == NULL) {
        s_region_failed = true;
        capture_abort();
        return;
    }

    const int w = (int)(area->x2 - area->x1 + 1);
    const int h = (int)(area->y2 - area->y1 + 1);
    s_region_seen = true;
    if (!s_active_region_cb((int)area->x1, (int)area->y1, w, h, buf->data, s_active_user)) {
        s_region_failed = true;
        capture_abort();
    }
}

void platform_screen_capture_init(lv_display_t *disp,
                                  platform_capture_region_cb_t region_cb,
                                  platform_capture_done_cb_t done_cb,
                                  void *user)
{
    if (disp == NULL) return;
    if (!platform_lvgl_lock(0)) return;

    if (s_disp == NULL) {
        s_disp = disp;
        lv_display_add_event_cb(disp, capture_flush_start, LV_EVENT_FLUSH_START, NULL);
    } else if (s_disp != disp) {
        platform_lvgl_unlock();
        return;
    }

    s_config_region_cb = region_cb;
    s_config_done_cb = done_cb;
    s_config_user = user;
    platform_lvgl_unlock();
}

bool platform_screen_capture_start(void)
{
    if (atomic_load(&s_state) != CAPTURE_IDLE) return false;
    if (!platform_lvgl_lock(0)) return false;
    if (s_disp == NULL || s_config_region_cb == NULL) {
        platform_lvgl_unlock();
        return false;
    }

    int expected = CAPTURE_IDLE;
    if (!atomic_compare_exchange_strong(&s_state, &expected, CAPTURE_ACTIVE)) {
        platform_lvgl_unlock();
        return false;
    }

    s_active_region_cb = s_config_region_cb;
    s_active_done_cb = s_config_done_cb;
    s_active_user = s_config_user;
    s_region_seen = false;
    s_region_failed = false;
    s_nav_was_enabled = platform_lvgl_nav_enabled();
    platform_lvgl_nav_enable(false);
    s_nav_frozen = true;

    lv_obj_t *bottom = lv_layer_bottom();
    lv_obj_t *screen = lv_screen_active();
    if (bottom == NULL || screen == NULL ||
        lv_obj_invalidate(bottom) != LV_RESULT_OK ||
        lv_obj_invalidate(screen) != LV_RESULT_OK) {
        s_region_failed = true;
        capture_abort();
    } else {
        lv_refr_now(s_disp);
    }

    expected = CAPTURE_ACTIVE;
    bool ok = s_region_seen && !s_region_failed &&
              atomic_compare_exchange_strong(&s_state, &expected, CAPTURE_FINISHING);
    platform_capture_done_cb_t done_cb = s_active_done_cb;
    void *user = s_active_user;
    capture_cleanup_locked();
    platform_lvgl_unlock();

    if (done_cb != NULL) done_cb(ok, user);
    return true;
}

void platform_screen_capture_cancel(void)
{
    capture_abort();
}

bool platform_screen_capture_active(void)
{
    return atomic_load(&s_state) == CAPTURE_ACTIVE;
}
