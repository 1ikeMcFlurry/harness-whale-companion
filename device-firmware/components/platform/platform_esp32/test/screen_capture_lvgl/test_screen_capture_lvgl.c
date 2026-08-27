#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

struct _lv_display_t { int unused; };
struct _lv_event_t {
    lv_display_t *target;
    lv_area_t *param;
};
struct _lv_obj_t { int id; };

static lv_display_t g_disp;
static lv_obj_t g_bottom = { .id = 1 };
static lv_obj_t g_screen = { .id = 2 };
static lv_area_t g_area = { .x1 = 0, .y1 = 20, .x2 = 239, .y2 = 39 };
static uint8_t g_pixels[240 * 20 * 2];
static lv_draw_buf_t g_buf = { .data = g_pixels };
static lv_event_cb_t g_flush_cb;

static int g_lock_depth;
static int g_lock_count;
static int g_unlock_count;
static int g_hook_count;
static bool g_hook_was_locked;
static int g_invalidate_count;
static int g_refresh_count;
static bool g_nav_enabled = true;
static bool g_nav_history[8];
static int g_nav_history_count;

static int g_user_a;
static int g_user_b;
static int g_region_a_count;
static int g_region_b_count;
static int g_done_a_count;
static int g_done_b_count;
static bool g_done_a_ok;
static bool g_done_b_ok;

typedef enum {
    REGION_NORMAL,
    REGION_REINIT,
    REGION_CANCEL,
} region_mode_t;
static region_mode_t g_region_mode;

void lv_display_add_event_cb(lv_display_t *disp, lv_event_cb_t cb, int code, void *user)
{
    assert(disp == &g_disp);
    assert(code == LV_EVENT_FLUSH_START);
    assert(user == NULL);
    g_hook_was_locked = g_lock_depth > 0;
    g_flush_cb = cb;
    g_hook_count++;
}

void *lv_event_get_target(lv_event_t *e) { return e->target; }
void *lv_event_get_param(lv_event_t *e) { return e->param; }

lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *disp)
{
    assert(disp == &g_disp);
    return &g_buf;
}

lv_color_format_t lv_display_get_color_format(lv_display_t *disp)
{
    assert(disp == &g_disp);
    return LV_COLOR_FORMAT_RGB565;
}

int32_t lv_display_get_horizontal_resolution(const lv_display_t *disp)
{
    assert(disp == &g_disp);
    return 240;
}

int32_t lv_display_get_vertical_resolution(const lv_display_t *disp)
{
    assert(disp == &g_disp);
    return 320;
}

lv_obj_t *lv_layer_bottom(void) { return &g_bottom; }
lv_obj_t *lv_screen_active(void) { return &g_screen; }

lv_result_t lv_obj_invalidate(const lv_obj_t *obj)
{
    assert(obj == &g_bottom || obj == &g_screen);
    g_invalidate_count++;
    return LV_RESULT_OK;
}

void lv_refr_now(lv_display_t *disp)
{
    assert(disp == &g_disp);
    assert(g_lock_depth > 0);
    g_refresh_count++;
    lv_event_t event = { .target = disp, .param = &g_area };
    g_flush_cb(&event);
}

bool platform_lvgl_lock(int timeout_ms)
{
    assert(timeout_ms == 0);
    g_lock_count++;
    g_lock_depth++;
    return true;
}

void platform_lvgl_unlock(void)
{
    assert(g_lock_depth > 0);
    g_unlock_count++;
    g_lock_depth--;
}

void platform_lvgl_nav_enable(bool en)
{
    assert(g_nav_history_count < (int)(sizeof g_nav_history / sizeof g_nav_history[0]));
    g_nav_history[g_nav_history_count++] = en;
    g_nav_enabled = en;
}

bool platform_lvgl_nav_enabled(void) { return g_nav_enabled; }

#include "../../src/screen_capture_lvgl.c"

static bool region_b(int x, int y, int w, int h, const uint8_t *rgb565, void *user);
static void done_b(bool ok, void *user);

static void assert_region(int x, int y, int w, int h, const uint8_t *rgb565)
{
    assert(x == 0);
    assert(y == 20);
    assert(w == 240);
    assert(h == 20);
    assert(rgb565 == g_pixels);
    assert(platform_screen_capture_active());
}

static bool region_a(int x, int y, int w, int h, const uint8_t *rgb565, void *user)
{
    assert(user == &g_user_a);
    assert_region(x, y, w, h, rgb565);
    g_region_a_count++;
    if (g_region_mode == REGION_REINIT) {
        platform_screen_capture_init(&g_disp, region_b, done_b, &g_user_b);
    } else if (g_region_mode == REGION_CANCEL) {
        platform_screen_capture_cancel();
        platform_screen_capture_cancel();
        assert(!platform_screen_capture_active());
        return false;
    }
    return true;
}

static bool region_b(int x, int y, int w, int h, const uint8_t *rgb565, void *user)
{
    assert(user == &g_user_b);
    assert_region(x, y, w, h, rgb565);
    g_region_b_count++;
    return true;
}

static void done_a(bool ok, void *user)
{
    assert(user == &g_user_a);
    assert(!platform_screen_capture_active());
    g_done_a_count++;
    g_done_a_ok = ok;
}

static void done_b(bool ok, void *user)
{
    assert(user == &g_user_b);
    assert(!platform_screen_capture_active());
    g_done_b_count++;
    g_done_b_ok = ok;
}

static void setup_a(void)
{
    platform_screen_capture_init(&g_disp, NULL, NULL, NULL);
    platform_screen_capture_init(&g_disp, region_a, done_a, &g_user_a);
    assert(g_hook_count == 1);
}

static void reset_nav_history(void) { g_nav_history_count = 0; }

static void test_hook(void)
{
    setup_a();
    platform_screen_capture_init(&g_disp, region_b, done_b, &g_user_b);
    assert(g_hook_count == 1);
    assert(g_hook_was_locked);
    assert(g_lock_count == g_unlock_count);
    assert(g_lock_depth == 0);
}

static void test_delivery(void)
{
    setup_a();
    reset_nav_history();
    assert(platform_screen_capture_start());
    assert(g_region_a_count == 1);
    assert(g_done_a_count == 1 && g_done_a_ok);
    assert(g_invalidate_count == 2);
    assert(g_refresh_count == 1);
    assert(g_nav_history_count == 2);
    assert(!g_nav_history[0] && g_nav_history[1]);
}

static void test_nav_disabled(void)
{
    setup_a();
    platform_lvgl_nav_enable(false);
    reset_nav_history();
    assert(platform_screen_capture_start());
    assert(!platform_lvgl_nav_enabled());
    assert(g_nav_history_count == 2);
    assert(!g_nav_history[0] && !g_nav_history[1]);
}

static void test_callback_snapshot(void)
{
    setup_a();
    g_region_mode = REGION_REINIT;
    assert(platform_screen_capture_start());
    assert(g_region_a_count == 1);
    assert(g_done_a_count == 1 && g_done_a_ok);
    assert(g_region_b_count == 0 && g_done_b_count == 0);

    g_region_mode = REGION_NORMAL;
    assert(platform_screen_capture_start());
    assert(g_region_a_count == 1 && g_done_a_count == 1);
    assert(g_region_b_count == 1);
    assert(g_done_b_count == 1 && g_done_b_ok);
}

static void test_cancel(void)
{
    setup_a();
    g_region_mode = REGION_CANCEL;
    reset_nav_history();
    assert(platform_screen_capture_start());
    assert(g_region_a_count == 1);
    assert(g_done_a_count == 1 && !g_done_a_ok);
    assert(!platform_screen_capture_active());
    assert(platform_lvgl_nav_enabled());
    assert(g_nav_history_count == 2);
    assert(!g_nav_history[0] && g_nav_history[1]);
    platform_screen_capture_cancel();
    assert(g_nav_history_count == 2);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "hook") == 0) test_hook();
    else if (strcmp(argv[1], "nav-enabled") == 0) test_delivery();
    else if (strcmp(argv[1], "nav-disabled") == 0) test_nav_disabled();
    else if (strcmp(argv[1], "callback-snapshot") == 0) test_callback_snapshot();
    else if (strcmp(argv[1], "cancel") == 0) test_cancel();
    else assert(!"unknown test name");
    printf("PASS %s\n", argv[1]);
    return 0;
}
