// components/ui/presentation/src/ui_mario.c —— 监听广播时显示的界面(炫酷 Hello World 文字)
// 收到匹配广播时进入本屏,文字跳动一下;5s 无广播自动退回。API 名沿用 ui_mario_*(保持
// 上层调用不变),内部已从"马里奥位图"改为纯文字,不再占用位图 flash。
#include "presentation/ui_mario.h"
#include "lvgl.h"
#include "ui_bcast_pixels.h"          // ui_bcast_pixels:A8 像素点画 "HELLO WORLD"(生成器见 tools/gen_pixel_text.py)

#define BG_COLOR   lv_color_hex(0x0A0E14)   // 近黑深底,衬托霓虹像素
#define TXT_COLOR  lv_color_hex(0x00E5FF)   // 赛博青(像素文字上色,A8 recolor)
#define TICK_MS    60                  // 跳跃/空闲检查周期
#define IDLE_MS    5000                // 连续无匹配广播多久自动退回
// 起跳 180 + 落地 220 = 400ms。刻意略长于顶金币音的 374ms —— 持续收广播时
// 一跳配一声,声音不会被下一次触发打断成断音(见 app.c 的播放限速)。
#define JUMP_UP    180                 // 上升时长(ms)
#define JUMP_DOWN  220                 // 下落时长(ms)
#define JUMP_H     52                  // 跳跃高度(px)
#define REST_Y     20                  // 站立位:比屏心低一点,给头顶留出起跳空间

static struct {
    bool          active;
    lv_obj_t     *scr, *prev_scr, *label;   // label:炫酷 Hello World 文字(替代原马里奥图)
    lv_timer_t   *timer;
    volatile int  pending;
    bool          animating;
    int           idle_ms;
    ui_mario_exit_cb_t on_exit;
    void         *on_exit_user;
} M;

// 平移整张图。负值向上 —— 起跳。
static void jump_cb(void *obj, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}
static void jump_done_cb(lv_anim_t *a) { (void)a; M.animating = false; }

static void jump_play(void) {
    if (M.animating) return;           // 空中不再起跳,否则连发广播会把动画抖成噪点
    M.animating = true;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, M.label);
    lv_anim_set_exec_cb(&a, jump_cb);
    lv_anim_set_values(&a, 0, -JUMP_H);
    lv_anim_set_duration(&a, JUMP_UP);
    lv_anim_set_reverse_duration(&a, JUMP_DOWN);
    // ease_out:蹬地瞬间最快、接近顶点变慢;回放段自动倒着走这条曲线,
    // 于是下落越来越快 —— 正好是重力的样子。
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, jump_done_cb);
    lv_anim_start(&a);
}

static void teardown(void) {
    if (!M.active) return;
    if (M.timer) { lv_timer_delete(M.timer); M.timer = NULL; }
    if (M.prev_scr) lv_screen_load(M.prev_scr);
    if (M.scr) { lv_obj_delete(M.scr); M.scr = NULL; M.label = NULL; }
    M.active = false;
    if (M.on_exit) M.on_exit(M.on_exit_user);
}

static void tick(lv_timer_t *t) {
    (void)t;
    if (M.pending) { M.pending = 0; M.idle_ms = 0; jump_play(); }
    else { M.idle_ms += TICK_MS; if (M.idle_ms >= IDLE_MS) teardown(); }
}

void ui_mario_open(ui_mario_exit_cb_t on_exit, void *user) {
    if (M.active) return;
    M.on_exit = on_exit; M.on_exit_user = user;
    M.prev_scr = lv_screen_active();
    M.pending = 0; M.animating = false; M.idle_ms = 0;

    M.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(M.scr, BG_COLOR, 0);
    lv_obj_set_style_border_width(M.scr, 0, 0);
    lv_obj_remove_flag(M.scr, LV_OBJ_FLAG_SCROLLABLE);

    // 炫酷像素点画 "HELLO WORLD"(A8 位图,放 flash)。A8 无自带颜色,用 image_recolor
    // 把亮点上成赛博青;跳动动画作用其上(收广播时整块像素文字弹一下)。
    M.label = lv_image_create(M.scr);
    lv_image_set_src(M.label, &ui_bcast_pixels);
    lv_obj_set_style_image_recolor(M.label, TXT_COLOR, 0);
    lv_obj_set_style_image_recolor_opa(M.label, LV_OPA_COVER, 0);
    lv_obj_align(M.label, LV_ALIGN_CENTER, 0, REST_Y);

    lv_screen_load(M.scr);
    M.active = true;
    M.timer = lv_timer_create(tick, TICK_MS, NULL);
}

void ui_mario_jump(void) { M.pending = 1; }

bool ui_mario_is_active(void) { return M.active; }
