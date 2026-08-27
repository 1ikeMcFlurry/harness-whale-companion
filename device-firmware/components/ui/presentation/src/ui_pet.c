// components/ui/presentation/src/ui_pet.c —— 宠物页 + 入住揭晓动画
// 候选宠物池(见活动方案):1 TRAE宝 / 2 向日葵小葵 / 3 旅伴博博 / 4 仙人掌刺刺 /
// 5 泰迪熊乐乐 / 6 熊猫萌萌。真实 IP 形象待设计,这里用统一风格占位吉祥物(不同配色+名字)。
// 进入时播放揭晓:倒计时 3-2-1 → 幕布左右拉开 → 宠物弹出(回弹)+ 名字淡入。
// 长按确定键退出(app 层在宠物页激活时把长按转成 ui_pet_close)。
#include "presentation/ui_pet.h"
#include "lvgl.h"

LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_24);

#define BG    lv_color_hex(0x0E1512)
#define WHITE lv_color_hex(0xF2F5F3)
#define GRAY  lv_color_hex(0x707D77)
#define CURT  lv_color_hex(0x18241E)   // 幕布色
#define GOLD  lv_color_hex(0xFFC24D)

#define SCREEN_W 240

typedef struct { const char *name; uint32_t color; bool panda; } pet_def_t;
static const pet_def_t PETS[] = {
    { "宠物",       0x00E5FF, false },  // 0 兜底
    { "TRAE宝",     0x00E5FF, false },  // 1
    { "向日葵小葵", 0xFFC24D, false },  // 2
    { "旅伴博博",   0xFF9A2E, false },  // 3
    { "仙人掌刺刺", 0x39C066, false },  // 4
    { "泰迪熊乐乐", 0xB07A3C, false },  // 5
    { "熊猫萌萌",   0xE8E8E8, true  },  // 6
};
#define PET_N ((int)(sizeof(PETS)/sizeof(PETS[0])))

static struct {
    bool        active;
    lv_obj_t   *scr, *prev, *stage, *name, *hint, *curtL, *curtR, *count;
    lv_timer_t *reveal;
    int         step;
    ui_pet_exit_cb_t on_exit;
    void       *user;
} S;

static lv_obj_t *circle(lv_obj_t *p, int d, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// —— 动画 exec 回调(只用平移/透明度:C3 无 PSRAM,对带子对象的容器做 scale/rotate
//    会强制分配整棵子树的图层缓冲(180×180 ARGB≈129KB),远超空闲堆,会把渲染卡死)——
static void a_ty   (void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }
static void a_tx   (void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }
static void a_opa  (void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void anim(void *obj, lv_anim_exec_xcb_t cb, int32_t s, int32_t e,
                 uint32_t delay, uint32_t t, lv_anim_path_cb_t path) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, s, e);
    lv_anim_set_delay(&a, delay); lv_anim_set_time(&a, t);
    if (path) lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

// 倒计时结束 → 拉幕 + 宠物弹出 + 名字淡入
static void do_reveal(void) {
    if (S.count) lv_obj_add_flag(S.count, LV_OBJ_FLAG_HIDDEN);
    anim(S.curtL, a_tx, 0, -(SCREEN_W / 2 + 2), 0, 450, lv_anim_path_ease_in_out);   // 左幕全滑出
    anim(S.curtR, a_tx, 0,  (SCREEN_W / 2 + 2), 0, 450, lv_anim_path_ease_in_out);   // 右幕全滑出
    anim(S.stage, a_ty, -30, 0, 150, 460, lv_anim_path_bounce);               // 宠物从上弹落(平移)
    anim(S.name, a_opa, 0, 255, 380, 260, lv_anim_path_linear);                // 名字淡入
    anim(S.hint, a_opa, 0, 255, 480, 260, lv_anim_path_linear);
}

static void reveal_cb(lv_timer_t *t) {
    (void)t;
    S.step++;
    if (S.step <= 3) {                 // 倒计时 3 → 2 → 1
        if (S.count) lv_label_set_text_fmt(S.count, "%d", 4 - S.step);
        return;
    }
    do_reveal();
    if (S.reveal) { lv_timer_delete(S.reveal); S.reveal = NULL; }
}

void ui_pet_open(uint8_t type, bool reveal, ui_pet_exit_cb_t on_exit, void *user) {
    if (S.active) return;
    S.on_exit = on_exit; S.user = user;
    S.prev = lv_screen_active();
    S.step = 0;
    const pet_def_t *pd = &PETS[(type < PET_N) ? type : 0];
    lv_color_t col = lv_color_hex(pd->color);
    lv_color_t ear = pd->panda ? lv_color_hex(0x2A2A2A) : col;

    S.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(S.scr, BG, 0);
    lv_obj_set_style_pad_all(S.scr, 0, 0);
    lv_obj_remove_flag(S.scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(S.scr);
    lv_label_set_text(title, "我的宠物");
    lv_obj_set_style_text_font(title, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(title, WHITE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // 吉祥物(初始缩到 0,揭晓时弹出)
    S.stage = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.stage);
    lv_obj_set_size(S.stage, 180, 180);
    lv_obj_align(S.stage, LV_ALIGN_CENTER, 0, -6);
    lv_obj_remove_flag(S.stage, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *earL = circle(S.stage, 46, ear); lv_obj_align(earL, LV_ALIGN_TOP_LEFT, 24, 6);
    lv_obj_t *earR = circle(S.stage, 46, ear); lv_obj_align(earR, LV_ALIGN_TOP_RIGHT, -24, 6);
    lv_obj_t *body = circle(S.stage, 140, col); lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (pd->panda) {
        lv_obj_t *pL = circle(body, 44, lv_color_hex(0x2A2A2A)); lv_obj_align(pL, LV_ALIGN_CENTER, -28, -6);
        lv_obj_t *pR = circle(body, 44, lv_color_hex(0x2A2A2A)); lv_obj_align(pR, LV_ALIGN_CENTER,  28, -6);
    }
    lv_obj_t *eL = circle(body, 26, WHITE); lv_obj_align(eL, LV_ALIGN_CENTER, -28, -6);
    lv_obj_t *eR = circle(body, 26, WHITE); lv_obj_align(eR, LV_ALIGN_CENTER,  28, -6);
    lv_obj_t *pupL = circle(eL, 12, lv_color_hex(0x101010)); lv_obj_center(pupL);
    lv_obj_t *pupR = circle(eR, 12, lv_color_hex(0x101010)); lv_obj_center(pupR);
    lv_obj_t *nose = circle(body, 14, lv_color_hex(0x101010)); lv_obj_align(nose, LV_ALIGN_CENTER, 0, 22);

    S.name = lv_label_create(S.scr);
    lv_label_set_text(S.name, pd->name);
    lv_obj_set_style_text_font(S.name, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(S.name, col, 0);
    lv_obj_align(S.name, LV_ALIGN_CENTER, 0, 110);

    S.hint = lv_label_create(S.scr);
    lv_label_set_text(S.hint, "长按确定键退出");
    lv_obj_set_style_text_font(S.hint, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(S.hint, GRAY, 0);
    lv_obj_align(S.hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    if (!reveal) {                 // 非揭晓:直接显示静态宠物,不建幕布/倒计时
        lv_screen_load(S.scr);
        S.active = true;
        return;
    }
    // 揭晓:名字/提示先隐藏,揭晓时淡入
    lv_obj_set_style_opa(S.name, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(S.hint, LV_OPA_TRANSP, 0);

    // —— 幕布(两片,盖住整屏,揭晓时左右拉开)——
    S.curtL = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.curtL);
    lv_obj_set_size(S.curtL, SCREEN_W / 2 + 1, LV_PCT(100));
    lv_obj_set_pos(S.curtL, 0, 0);
    lv_obj_set_style_bg_color(S.curtL, CURT, 0);
    lv_obj_set_style_bg_opa(S.curtL, LV_OPA_COVER, 0);
    S.curtR = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.curtR);
    lv_obj_set_size(S.curtR, SCREEN_W / 2 + 1, LV_PCT(100));
    lv_obj_set_pos(S.curtR, SCREEN_W / 2, 0);
    lv_obj_set_style_bg_color(S.curtR, CURT, 0);
    lv_obj_set_style_bg_opa(S.curtR, LV_OPA_COVER, 0);

    // —— 倒计时数字(压在幕布之上)——
    S.count = lv_label_create(S.scr);
    lv_label_set_text(S.count, "3");
    lv_obj_set_style_text_font(S.count, &lv_font_montserrat_34, 0);
    lv_obj_set_style_text_color(S.count, GOLD, 0);
    lv_obj_center(S.count);

    lv_screen_load(S.scr);
    S.active = true;
    S.reveal = lv_timer_create(reveal_cb, 450, NULL);   // 每 450ms:3→2→1→揭晓
}

void ui_pet_close(void) {
    if (!S.active) return;
    if (S.reveal) { lv_timer_delete(S.reveal); S.reveal = NULL; }
    if (S.prev) lv_screen_load(S.prev);
    if (S.scr) { lv_obj_delete(S.scr); S.scr = NULL; }
    S.active = false;
    if (S.on_exit) S.on_exit(S.user);
}

bool ui_pet_is_active(void) { return S.active; }
