// components/ui/presentation/src/ui_factory.c —— 产测屏幕自检(一屏全色带,单眼可判)
// 一次性铺满 红/绿/蓝/白/黑 五条横色带 + 白色细网格,工人一眼看全(查偏色/背光/坏点/断线),
// 无需逐色等待。叠在 lv_layer_top,~15s 后自动清除(够工人判定),不影响下方界面。
// 须在持 LVGL 锁时调用。
#include "presentation/ui_factory.h"
#include "lvgl.h"

static const uint32_t BANDS[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
#define N_BANDS   (int)(sizeof(BANDS) / sizeof(BANDS[0]))
#define HOLD_MS   15000        // 图样停留时长(够工人判定后自动撤)

static lv_obj_t   *s_obj;
static lv_timer_t *s_timer;

static void cleanup(void) {
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_obj)   { lv_obj_delete(s_obj); s_obj = NULL; }
}

static void hold_done(lv_timer_t *t) { (void)t; cleanup(); }

void ui_factory_disp_test(void) {
    cleanup();                       // 重入:先清旧的
    s_obj = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_obj);
    lv_obj_set_size(s_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_obj);
    lv_obj_set_style_bg_color(s_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE);

    // 五条等高横色带,一次铺满整屏
    for (int i = 0; i < N_BANDS; i++) {
        lv_obj_t *band = lv_obj_create(s_obj);
        lv_obj_remove_style_all(band);
        lv_obj_set_size(band, LV_PCT(100), LV_PCT(100 / N_BANDS + 1));
        lv_obj_set_y(band, i * (320 / N_BANDS));
        lv_obj_set_style_bg_color(band, lv_color_hex(BANDS[i]), 0);
        lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    }
    // 白色细网格(查坏点/断线):20px 间距,叠在色带上
    for (int x = 0; x < 240; x += 20) {
        lv_obj_t *v = lv_obj_create(s_obj);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, 1, LV_PCT(100));
        lv_obj_set_x(v, x);
        lv_obj_set_style_bg_color(v, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_50, 0);
    }
    for (int y = 0; y < 320; y += 20) {
        lv_obj_t *h = lv_obj_create(s_obj);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, LV_PCT(100), 1);
        lv_obj_set_y(h, y);
        lv_obj_set_style_bg_color(h, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_50, 0);
    }

    s_timer = lv_timer_create(hold_done, HOLD_MS, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}
