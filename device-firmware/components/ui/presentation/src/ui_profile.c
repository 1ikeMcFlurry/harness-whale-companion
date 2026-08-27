// components/presentation/src/ui_profile.c —— 档案页 + 底部横向 dock(头像位 + 按键导航)
// 回到最初的赛博卡片布局(顶部状态栏 + 头像 + 昵称 + TOKEN + dock),去掉没有真实数据的
// 冗余占位字段(角色 NETRUNNER / 副标题 DATA STALKER / 等级·经验条)。
#include "presentation/ui_profile.h"
#include "lvgl.h"
#include <stdint.h>
#include <time.h>

// 中文字体(tools/gen_cn_font.py 生成:ASCII + GB2312 一级汉字,2bpp,数据在 flash)
LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_24);

// ---- 屏幕布局参数(ST7789P3 T200H7-C14-05:240×320 竖屏)----
// 状态栏顶部对齐、dock 底部对齐,中部内容块(头像+身份栏)在两者之间垂直居中。
#define SCREEN_W  240
#define SCREEN_H  320
#define STATUS_H  40    // 顶部状态栏占高
#define DOCK_H    76    // 底部 dock 占高

// ---- 调色板 ----
#define COL_BG      lv_color_hex(0x0E1512)
#define COL_PANEL   lv_color_hex(0x141C18)
#define COL_PANEL_F lv_color_hex(0x12261C)   // dock 焦点态背景
#define COL_BORDER  lv_color_hex(0x2A3A33)
#define COL_TRACK   lv_color_hex(0x223029)   // 进度条轨道
#define COL_GREEN   lv_color_hex(0x35E07E)
#define COL_GREEN2  lv_color_hex(0x46C77D)
#define COL_WHITE   lv_color_hex(0xF2F5F3)
#define COL_GRAY    lv_color_hex(0x707D77)
#define COL_BATT_LOW  lv_color_hex(0xF2C14E)   // 低电(黄)
#define COL_BATT_CRIT lv_color_hex(0xE0463A)   // 危急(红)

// ---- 头像位尺寸(上传的头像图缩放到此矩形内显示;没有传过图则此区域留空)----
#define AVA_W 96
#define AVA_H 156

typedef struct {
    lv_obj_t *time_lbl, *date_lbl;
    lv_obj_t *online_dot, *batt_pct;
    lv_obj_t *name_lbl;
    lv_obj_t *token_num, *token_max_lbl, *token_bar;
    lv_timer_t *clock;
    lv_obj_t *img_cont, *img;    // 头像位图片(裁剪容器+lv_image);NULL=当前空着
    lv_obj_t *ava_ph;            // 头像位默认占位(黑色半身剪影);有真实头像时隐藏
    int       ava_x, ava_y;      // 头像位左上角坐标(建界面时算好,给 show_image 定位用)
    lv_obj_t *dock_val[8];
    lv_obj_t *dock;              // dock 容器
    lv_obj_t *tile[3];           // 磁贴按钮:0=GAME 1=IMAGE 2=PET(可显隐)
    bool      pet_visible;       // PET 磁贴当前是否显示
} profile_t;
static profile_t P;

static ui_action_cb_t s_action_cb;
static void          *s_action_user;

static lv_obj_t *make_label(lv_obj_t *p, const char *txt, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}
static lv_obj_t *make_box(lv_obj_t *p, int w, int h, lv_color_t bg) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *make_dot(lv_obj_t *p, lv_color_t c, int sz) {
    lv_obj_t *d = make_box(p, sz, sz, c);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    return d;
}

// 在头像位置(96×156)显示图片,等比缩放完整装下(contain),常驻。
// dsc=NULL 则移除图片,头像位留空。须在 LVGL 锁内(或 LVGL 任务)调用。
static lv_obj_t *s_scr_ref;

// 头像位默认占位:黑色人物上半身剪影(头 + 肩,底部与两侧被头像框裁掉 → 半身)。
// show=false 隐藏(有真实头像时)。须在 LVGL 锁内调用。
static void avatar_placeholder(bool show) {
    if (!s_scr_ref) return;
    if (!show) { if (P.ava_ph) lv_obj_add_flag(P.ava_ph, LV_OBJ_FLAG_HIDDEN); return; }
    if (P.ava_ph) { lv_obj_remove_flag(P.ava_ph, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_t *box = make_box(s_scr_ref, AVA_W, AVA_H, COL_BORDER);   // 浅底衬托黑剪影
    lv_obj_set_pos(box, P.ava_x, P.ava_y);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_t *head = make_box(box, 46, 46, lv_color_black());
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *sh = make_box(box, 120, 112, lv_color_black());        // 肩:比框宽,两侧被裁成肩线
    lv_obj_set_style_radius(sh, 56, 0);
    lv_obj_align(sh, LV_ALIGN_BOTTOM_MID, 0, 34);                    // 下沉,底部被裁 → 半身
    P.ava_ph = box;
}

void ui_profile_show_image(const void *dsc_) {
    const lv_image_dsc_t *dsc = dsc_;
    if (!s_scr_ref) return;
    if (dsc == NULL) {                       // 清空头像位 → 恢复默认剪影占位
        if (P.img_cont) { lv_obj_delete(P.img_cont); P.img_cont = NULL; P.img = NULL; }
        avatar_placeholder(true);
        return;
    }
    avatar_placeholder(false);               // 有真实头像:隐藏占位
    if (!P.img_cont) {                       // 首次:建裁剪容器(超出边界的图会被容器裁掉)
        P.img_cont = lv_obj_create(s_scr_ref);
        lv_obj_remove_style_all(P.img_cont);
        lv_obj_set_size(P.img_cont, AVA_W, AVA_H);
        lv_obj_set_pos(P.img_cont, P.ava_x, P.ava_y);
        lv_obj_set_style_bg_color(P.img_cont, COL_BG, 0);
        lv_obj_set_style_bg_opa(P.img_cont, LV_OPA_COVER, 0);
        lv_obj_remove_flag(P.img_cont, LV_OBJ_FLAG_SCROLLABLE);
        P.img = lv_image_create(P.img_cont);
    }
    lv_image_set_src(P.img, dsc);
    int w = dsc->header.w, h = dsc->header.h;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    // contain:整幅完整可见、一个像素不裁,装不满的方向留背景色边。**向下取整**避免大出 1px 被裁。
    int sx = AVA_W * 256 / w;
    int sy = AVA_H * 256 / h;
    int scale = sx < sy ? sx : sy;
    if (scale < 1) scale = 1;
    lv_image_set_pivot(P.img, w / 2, h / 2);
    lv_image_set_scale(P.img, scale);
    lv_obj_center(P.img);
}

// ---- dock ----
static void dock_clicked(lv_event_t *e) {
    lv_obj_t *tile = lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(tile);
    if (s_action_cb) s_action_cb(id, s_action_user);
}
static lv_obj_t *make_dock_tile(lv_obj_t *parent, int x, int w, int id, const char *name, const char *val) {
    lv_obj_t *t = lv_button_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, w, 60);
    lv_obj_set_pos(t, x, 8);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_set_style_bg_color(t, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(t, COL_BORDER, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, COL_GREEN, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(t, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(t, COL_PANEL_F, LV_STATE_FOCUSED);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(t, (void *)(intptr_t)id);
    lv_obj_add_event_cb(t, dock_clicked, LV_EVENT_CLICKED, NULL);

    lv_group_t *g = lv_group_get_default();   // 加入默认导航 group(按键切换焦点)
    if (g) lv_group_add_obj(g, t);

    lv_obj_t *nl = make_label(t, name, &lv_font_montserrat_10, COL_GRAY);
    lv_obj_set_style_text_letter_space(nl, -1, 0);
    lv_obj_align(nl, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *vl = make_label(t, val, &lv_font_montserrat_28, COL_GREEN);   // 图标放大到 28(2 倍)
    lv_obj_align(vl, LV_ALIGN_BOTTOM_MID, 0, -4);
    if (id >= 0 && id < 8) P.dock_val[id] = vl;
    return t;
}

// 底部磁贴按可见数量居中重排(2 或 3 格)。
#define DOCK_TW  68
#define DOCK_GAP 12
static void dock_relayout(void) {
    int n = P.pet_visible ? 3 : 2;
    int total = n * DOCK_TW + (n - 1) * DOCK_GAP;
    int x0 = (SCREEN_W - total) / 2;
    int idx = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 2 && !P.pet_visible) continue;
        if (P.tile[i]) lv_obj_set_pos(P.tile[i], x0 + idx * (DOCK_TW + DOCK_GAP), 8);
        idx++;
    }
}

void ui_profile_set_on_action(ui_action_cb_t cb, void *user) {
    s_action_cb = cb; s_action_user = user;
}

const void *ui_font_cn16(void) { return &lv_font_cn_16; }
const void *ui_font_cn24(void) { return &lv_font_cn_24; }

// 读系统时钟(由 BLE 下发 "time" 经 settimeofday 设定,RTC 计时、跨深睡保留)刷新状态栏。
// 未同步(时间还停在 1970,epoch < 2020)时显示占位 --:-- / --- 。
// 时间以"本地秒当作 UTC"存(见协议),故用 gmtime 得到本地挂钟。
#define TIME_SYNCED_EPOCH 1600000000L   // 2020-09,判定是否已同步的阈值
static const char *WDAY[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static void refresh_time(void) {
    time_t now = time(NULL);
    if (now < TIME_SYNCED_EPOCH) {
        if (P.time_lbl) lv_label_set_text(P.time_lbl, "--:--");
        if (P.date_lbl) lv_label_set_text(P.date_lbl, "---");
        return;
    }
    struct tm tm;
    gmtime_r(&now, &tm);
    if (P.time_lbl) lv_label_set_text_fmt(P.time_lbl, "%02d:%02d", tm.tm_hour, tm.tm_min);
    if (P.date_lbl) lv_label_set_text(P.date_lbl, WDAY[tm.tm_wday % 7]);
}
static void clock_cb(lv_timer_t *t) {
    (void)t;
    refresh_time();   // 每秒读一次 RTC(便宜);同步后自动跳到真实时间
}

void ui_profile_create(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 状态栏 ----
    // 时间来自 BLE 同步(→系统时钟/RTC);未同步显示 --:--。电量/在线为静态占位(无真实数据源)。
    P.time_lbl = make_label(scr, "--:--", &lv_font_montserrat_24, COL_WHITE);
    lv_obj_set_pos(P.time_lbl, 12, 8);
    P.date_lbl = make_label(scr, "---", &lv_font_montserrat_12, COL_GRAY);
    lv_obj_set_style_text_letter_space(P.date_lbl, 2, 0);
    lv_obj_set_pos(P.date_lbl, 92, 16);
    // 电量:5 档电池符号 + 百分比,颜色按档变化。未接电量检测时为静态满电占位。
    P.batt_pct = make_label(scr, LV_SYMBOL_BATTERY_FULL, &lv_font_montserrat_14, COL_WHITE);
    lv_obj_align(P.batt_pct, LV_ALIGN_TOP_RIGHT, -12, 12);
    P.online_dot = make_dot(scr, COL_GREEN, 8);
    lv_obj_align_to(P.online_dot, P.batt_pct, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    // 中部内容块(头像+身份栏)整体高度以头像为准(AVA_H),在状态栏与 dock 之间垂直居中。
    // 居中后整体上移 16px:昵称离顶部状态栏更近,多出的空白让到底部 dock 一侧。
    const int mid_y0 = STATUS_H + ((SCREEN_H - DOCK_H - STATUS_H) - AVA_H) / 2 - 16;

    // ---- 头像位(左)----
    // 无默认像素小人:默认留空(背景色),仅在传过头像图后由 show_image 建图片容器填入。
    s_scr_ref = scr;
    P.ava_x = 12;
    P.ava_y = mid_y0;
    avatar_placeholder(true);   // 默认显示黑色半身剪影(传过头像后会被隐藏)

    // ---- 身份(右):只保留昵称 + TOKEN(角色/副标题/等级经验等占位信息已去除)----
    const int TEXT_X = 118, TEXT_W = SCREEN_W - 118 - 6;
    P.name_lbl = make_label(scr, "TraeWork", &lv_font_cn_24, COL_WHITE);
    lv_obj_set_size(P.name_lbl, TEXT_W, lv_font_get_line_height(&lv_font_cn_24));
    lv_label_set_long_mode(P.name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);   // 过长循环滚动,不换行撑高
    lv_obj_set_pos(P.name_lbl, TEXT_X, mid_y0 + 4);

    lv_obj_t *tok_cap = make_label(scr, "Token值", &lv_font_cn_16, COL_GREEN2);
    lv_obj_set_pos(tok_cap, TEXT_X, mid_y0 + 52);
    P.token_num = make_label(scr, "0", &lv_font_montserrat_24, COL_GREEN);
    lv_obj_set_pos(P.token_num, TEXT_X, mid_y0 + 68);
    // token 进度条(token/token_max):暗轨 + 霓虹绿指示,圆角
    P.token_bar = lv_bar_create(scr);
    lv_obj_set_size(P.token_bar, TEXT_W, 8);
    lv_obj_set_pos(P.token_bar, TEXT_X, mid_y0 + 104);
    lv_obj_set_style_radius(P.token_bar, 4, 0);
    lv_obj_set_style_bg_color(P.token_bar, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(P.token_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(P.token_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(P.token_bar, COL_GREEN, LV_PART_INDICATOR);
    P.token_max_lbl = make_label(scr, "/ 40000", &lv_font_montserrat_12, COL_GRAY);
    lv_obj_set_pos(P.token_max_lbl, TEXT_X, mid_y0 + 118);
    ui_profile_set_token(0, 40000);

    // ---- 底部横向 dock(2 格居中:GAME / IMAGE)----
    lv_obj_t *dock = lv_obj_create(scr);
    lv_obj_remove_style_all(dock);
    lv_obj_set_size(dock, SCREEN_W, DOCK_H);
    lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(dock, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(dock, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dock, COL_BORDER, 0);
    lv_obj_set_style_border_width(dock, 1, 0);
    lv_obj_remove_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    P.dock = dock;
    P.tile[0] = make_dock_tile(dock, 0, DOCK_TW, 2, "GAME",  LV_SYMBOL_PLAY);
    P.tile[1] = make_dock_tile(dock, 0, DOCK_TW, 3, "IMAGE", LV_SYMBOL_IMAGE);
    P.tile[2] = make_dock_tile(dock, 0, DOCK_TW, 4, "PET",   LV_SYMBOL_EYE_OPEN);
    // PET 默认隐藏 + 移出导航 group(由 token 广播 op=0x05 或开机持久化状态开启)
    lv_obj_add_flag(P.tile[2], LV_OBJ_FLAG_HIDDEN);
    { lv_group_t *g = lv_group_get_default(); if (g) lv_group_remove_obj(P.tile[2]); }
    P.pet_visible = false;
    dock_relayout();

    P.clock = lv_timer_create(clock_cb, 1000, NULL);
    refresh_time();
}

// ================= 运行时 setter(请在 LVGL 锁内调用)=================

void ui_profile_set_name(const char *name) {
    if (P.name_lbl && name) lv_label_set_text(P.name_lbl, name);
}
// 电量:只显示 5 档电池符号(不显示百分比);档位 <=1 红,==2 黄,其余白。
void ui_profile_set_battery(int percent, int level) {
    if (!P.batt_pct) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    const char *sym = level >= 5 ? LV_SYMBOL_BATTERY_FULL
                    : level == 4 ? LV_SYMBOL_BATTERY_3
                    : level == 3 ? LV_SYMBOL_BATTERY_2
                    : level == 2 ? LV_SYMBOL_BATTERY_1
                                 : LV_SYMBOL_BATTERY_EMPTY;
    lv_color_t col = (level <= 1) ? COL_BATT_CRIT
                   : (level == 2) ? COL_BATT_LOW
                                  : COL_WHITE;
    lv_label_set_text_fmt(P.batt_pct, "%s %d%%", sym, percent);   // 图标 + 百分比
    lv_obj_set_style_text_color(P.batt_pct, col, 0);
    lv_obj_align(P.batt_pct, LV_ALIGN_TOP_RIGHT, -12, 12);   // 贴右
    if (P.online_dot)                                         // 在线点跟随到电量左侧,避免重叠
        lv_obj_align_to(P.online_dot, P.batt_pct, LV_ALIGN_OUT_LEFT_MID, -8, 0);
}
// 宠物功能开关:显示/隐藏 dock 的 PET 磁贴并重排;同步导航 group 成员。
void ui_profile_set_pet(bool enabled, uint8_t type) {
    (void)type;   // 类型在进入宠物页时使用,dock 磁贴不区分类型
    if (!P.tile[2]) return;
    P.pet_visible = enabled;
    lv_group_t *g = lv_group_get_default();
    if (enabled) {
        lv_obj_remove_flag(P.tile[2], LV_OBJ_FLAG_HIDDEN);
        if (g) { lv_group_remove_obj(P.tile[2]); lv_group_add_obj(g, P.tile[2]); }  // 去重后加入
    } else {
        lv_obj_add_flag(P.tile[2], LV_OBJ_FLAG_HIDDEN);
        if (g) lv_group_remove_obj(P.tile[2]);
    }
    dock_relayout();
}

// token 余额 + 上限:大号数字=当前值,小号="/ 上限"。
void ui_profile_set_token(int token, int token_max) {
    if (token < 0) token = 0;
    if (token_max < 1) token_max = 1;
    if (token > token_max) token = token_max;
    if (P.token_num)     lv_label_set_text_fmt(P.token_num, "%d", token);
    if (P.token_max_lbl) lv_label_set_text_fmt(P.token_max_lbl, "/ %d", token_max);
    if (P.token_bar) {
        lv_bar_set_range(P.token_bar, 0, token_max);
        lv_bar_set_value(P.token_bar, token, LV_ANIM_OFF);
    }
}
