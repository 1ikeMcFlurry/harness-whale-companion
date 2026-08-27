// components/ui/presentation/src/ui_reward.c —— token 加/扣奖励动画(浮层)
// 叠在 lv_layer_top,背后铺一层半透明黑蒙版衬托(否则在花哨主界面上看不清):
//   · 加分:金币跳动(bounce)+ 自旋 + 徽标 +N
//   · 扣分:礼盒弹入 + 抖动 + 盒盖飞起(开盒)+ 徽标 −N
// ~1.6s 后单次定时器统一清理。整组挂在一个 root 容器下,删 root 即连带删子对象与其上动画。
#include "presentation/ui_reward.h"
#include "lvgl.h"
#include "ui_reward_art.h"       // ui_coin / ui_gift_body / ui_gift_lid(ARGB8888,放 flash)

#define LIFE_MS   1600           // 总时长(到点清理)
#define ELEM_Y    8              // 主体相对屏心的竖直偏移
#define SCRIM_OPA 150            // 蒙版黑度(0..255)

static struct {
    bool        active;
    lv_obj_t   *root, *coin;
    lv_timer_t *life;
} R;

// —— 动画 exec 回调(值→样式)——
static void a_scale (void *o, int32_t v) { lv_obj_set_style_transform_scale  ((lv_obj_t *)o, v, 0); }
static void a_scalex(void *o, int32_t v) { lv_obj_set_style_transform_scale_x((lv_obj_t *)o, v, 0); }
static void a_tx    (void *o, int32_t v) { lv_obj_set_style_translate_x      ((lv_obj_t *)o, v, 0); }
static void a_ty    (void *o, int32_t v) { lv_obj_set_style_translate_y      ((lv_obj_t *)o, v, 0); }
static void a_opa   (void *o, int32_t v) { lv_obj_set_style_opa              ((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void a_rot   (void *o, int32_t v) { lv_obj_set_style_transform_rotation((lv_obj_t *)o, v, 0); }

// 便捷启动一条动画。repeat: 0 不重复 / <0 无限;playback: 往返。
static void anim(void *obj, lv_anim_exec_xcb_t cb, int32_t s, int32_t e,
                 uint32_t delay, uint32_t t, lv_anim_path_cb_t path, int repeat, bool playback) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, s, e);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_time(&a, t);
    if (path)     lv_anim_set_path_cb(&a, path);
    if (repeat)   lv_anim_set_repeat_count(&a, repeat < 0 ? LV_ANIM_REPEAT_INFINITE : (uint32_t)repeat);
    if (playback) lv_anim_set_playback_time(&a, t);
    lv_anim_start(&a);
}

static void teardown(void) {
    if (!R.active) return;
    if (R.life) { lv_timer_delete(R.life); R.life = NULL; }
    if (R.root) { lv_obj_delete(R.root); R.root = NULL; }
    R.coin = NULL;
    R.active = false;
}

// 生命定时器(单次):到点清理。回调里先置空句柄再删,避免与 restart 重复删。
static void life_cb(lv_timer_t *t) {
    (void)t;
    R.life = NULL;               // 该定时器 repeat=1,回调返回后自行销毁
    teardown();
}

void ui_reward_play(bool add, int delta) {
    if (R.active) teardown();    // 连续到达:重开,显示最新一次

    lv_obj_t *top = lv_layer_top();
    R.root = lv_obj_create(top);
    lv_obj_remove_style_all(R.root);
    lv_obj_set_size(R.root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(R.root);
    lv_obj_remove_flag(R.root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // —— 半透明黑蒙版(衬托主体,先淡入)——
    lv_obj_t *scrim = lv_obj_create(R.root);
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_center(scrim);
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(scrim, LV_OPA_TRANSP, 0);
    anim(scrim, a_opa, 0, SCRIM_OPA, 0, 150, lv_anim_path_linear, 0, false);

    // —— 徽标 +N / −N ——
    lv_obj_t *badge = lv_label_create(R.root);
    if (delta > 0) lv_label_set_text_fmt(badge, "%s%d", add ? "+" : "-", delta);
    else           lv_label_set_text(badge, add ? "+" : "-");
    lv_obj_set_style_text_font(badge, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(badge, add ? lv_color_hex(0x39FF88) : lv_color_hex(0xFF5A5A), 0);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, ELEM_Y - 46);
    lv_obj_set_style_opa(badge, LV_OPA_TRANSP, 0);

    if (add) {
        // ============ 加分:金币跳动 + 自旋 ============
        R.coin = lv_image_create(R.root);
        lv_image_set_src(R.coin, &ui_coin);
        lv_obj_align(R.coin, LV_ALIGN_CENTER, 0, ELEM_Y);
        lv_obj_set_style_transform_pivot_x(R.coin, 16, 0);
        lv_obj_set_style_transform_pivot_y(R.coin, 16, 0);
        lv_obj_set_style_opa(R.coin, LV_OPA_TRANSP, 0);
        anim(R.coin, a_opa,    0, 255, 120, 90,  lv_anim_path_linear, 0, false);       // 淡入
        anim(R.coin, a_scalex, 256, 32, 160, 180, lv_anim_path_ease_in_out, -1, true); // 自旋(无限)
        anim(R.coin, a_ty, 10, -46, 200, 300, lv_anim_path_ease_out, 0, false);        // 跳起
        anim(R.coin, a_ty, -46, 4, 500, 560, lv_anim_path_bounce, 0, false);           // 落地回弹
    } else {
        // ============ 扣分:礼盒弹入 + 抖动 + 开盖 ============
        lv_obj_t *gift = lv_obj_create(R.root);
        lv_obj_remove_style_all(gift);
        lv_obj_set_size(gift, 44, 46);
        lv_obj_align(gift, LV_ALIGN_CENTER, 0, ELEM_Y);
        lv_obj_set_style_transform_pivot_x(gift, 22, 0);
        lv_obj_set_style_transform_pivot_y(gift, 23, 0);
        lv_obj_t *body = lv_image_create(gift);
        lv_image_set_src(body, &ui_gift_body);
        lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_t *lid = lv_image_create(gift);
        lv_image_set_src(lid, &ui_gift_lid);
        lv_obj_align(lid, LV_ALIGN_TOP_MID, 0, 0);
        anim(gift, a_scale, 40, 256, 100, 200, lv_anim_path_overshoot, 0, false);  // 弹入
        anim(gift, a_tx, -5, 5, 320, 70, lv_anim_path_ease_in_out, 4, true);       // 抖动
        anim(lid,  a_ty,  0, -80, 720, 360, lv_anim_path_ease_out, 0, false);      // 盖飞起
        anim(lid,  a_rot, 0, -260, 720, 360, lv_anim_path_linear,  0, false);
        anim(lid,  a_opa, 255, 0, 940, 220, lv_anim_path_linear,   0, false);
    }

    // 徽标升起 + 淡出(两种模式共用)。a_ty 是相对偏移:0 → -40 表示向上升 40px。
    anim(badge, a_opa, 0, 255, 240, 120, lv_anim_path_linear, 0, false);
    anim(badge, a_ty, 0, -40, 300, 700, lv_anim_path_ease_out, 0, false);
    anim(badge, a_opa, 255, 0, 1150, 380, lv_anim_path_linear, 0, false);

    // 收尾:整组(含蒙版)淡出
    anim(R.root, a_opa, 255, 0, 1350, 250, lv_anim_path_linear, 0, false);

    R.life = lv_timer_create(life_cb, LIFE_MS, NULL);
    lv_timer_set_repeat_count(R.life, 1);
    R.active = true;
}

bool ui_reward_is_active(void) { return R.active; }
