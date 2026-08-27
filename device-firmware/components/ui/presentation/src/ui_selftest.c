// components/ui/presentation/src/ui_selftest.c —— 设备端产测自检界面
// 结果/进度/交互提示全画在设备屏上。状态用 ASCII OK/NG/.. 上色(避免中文点阵字缺 LVGL 符号)。
// 提示用常用汉字(字体含全 GB2312 一级字)。须在持 LVGL 锁时调用。
#include "presentation/ui_selftest.h"
#include "lvgl.h"
#include <string.h>

LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_24);

#define BG      lv_color_hex(0x0A1428)
#define CYAN    lv_color_hex(0x00E5FF)
#define GREEN   lv_color_hex(0x39FF88)
#define RED     lv_color_hex(0xFF5A5A)
#define AMBER   lv_color_hex(0xFFB020)
#define GRAY    lv_color_hex(0x8899AA)

#define MAXROW  10

static struct {
    lv_obj_t *scr, *list, *prompt, *pattern, *result, *batt;
    char      names[MAXROW][12];
    lv_obj_t *status[MAXROW];
    int       nrow;
    int       soc;
} S;

static lv_color_t batt_color(int soc) {
    if (soc < 0)  return GRAY;
    if (soc < 15) return RED;
    if (soc < 30) return AMBER;
    return GREEN;
}

static lv_obj_t *mklabel(lv_obj_t *p, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static int find_row(const char *name) {
    for (int i = 0; i < S.nrow; i++)
        if (strcmp(S.names[i], name) == 0) return i;
    return -1;
}

void ui_selftest_open(const char *sn) {
    memset(&S, 0, sizeof S);
    S.soc = -1;
    S.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(S.scr, BG, 0);
    lv_obj_set_style_pad_all(S.scr, 0, 0);
    lv_obj_remove_flag(S.scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = mklabel(S.scr, &lv_font_cn_24, CYAN);
    lv_label_set_text(title, "产测自检");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *snl = mklabel(S.scr, &lv_font_montserrat_14, GRAY);
    lv_label_set_text_fmt(snl, "SN %s", sn && sn[0] ? sn : "-");
    lv_obj_align(snl, LV_ALIGN_TOP_LEFT, 6, 38);   // 左上,给右上角电量让位

    // 结果列表(竖向 flex)
    S.list = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.list);
    lv_obj_set_size(S.list, 224, 194);
    lv_obj_align(S.list, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_flex_flow(S.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(S.list, 2, 0);
    lv_obj_remove_flag(S.list, LV_OBJ_FLAG_SCROLLABLE);

    S.prompt = mklabel(S.scr, &lv_font_cn_16, AMBER);
    lv_label_set_recolor(S.prompt, true);   // 支持 #RRGGBB 段着色(按键达标段变绿)
    lv_label_set_long_mode(S.prompt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(S.prompt, 228);
    lv_obj_set_style_text_align(S.prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(S.prompt, "");
    lv_obj_align(S.prompt, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_screen_load(S.scr);
}

// 找到行,不存在则新建;返回行号,满了返回 -1。
static int ensure_row(const char *name) {
    int i = find_row(name);
    if (i >= 0) return i;
    if (S.nrow >= MAXROW) return -1;
    i = S.nrow++;
    strncpy(S.names[i], name, sizeof S.names[i] - 1);
    lv_obj_t *row = lv_obj_create(S.list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 22);
    lv_obj_t *nm = mklabel(row, &lv_font_cn_16, lv_color_white());
    lv_label_set_text(nm, name);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 4, 0);
    S.status[i] = mklabel(row, &lv_font_cn_16, GRAY);
    lv_obj_align(S.status[i], LV_ALIGN_RIGHT_MID, -4, 0);
    return i;
}

void ui_selftest_set_item(const char *name, int state) {
    ui_selftest_set_item_text(name, state, NULL);
}

void ui_selftest_set_item_text(const char *name, int state, const char *txt) {
    if (!S.scr) return;
    int i = ensure_row(name);
    if (i < 0) return;
    const char *t = txt ? txt
                  : state == UI_ST_PASS ? "OK" : state == UI_ST_FAIL ? "NG" : "..";
    lv_color_t  col = state == UI_ST_PASS ? GREEN : state == UI_ST_FAIL ? RED : GRAY;
    lv_label_set_text(S.status[i], t);
    lv_obj_set_style_text_color(S.status[i], col, 0);
}

void ui_selftest_prompt(const char *utf8) {
    if (!S.prompt) return;
    lv_label_set_text(S.prompt, utf8 ? utf8 : "");
}

void ui_selftest_battery(int soc) {
    if (!S.scr) return;
    S.soc = soc;
    if (!S.batt) {          // 右上角电量(与 SN 同行,分色醒目)
        S.batt = mklabel(S.scr, &lv_font_cn_16, lv_color_white());
        lv_obj_align(S.batt, LV_ALIGN_TOP_RIGHT, -6, 38);
    }
    if (soc < 0) lv_label_set_text(S.batt, "电量 --");
    else         lv_label_set_text_fmt(S.batt, "电量 %d%%", soc);
    lv_obj_set_style_text_color(S.batt, batt_color(soc), 0);
    lv_obj_move_foreground(S.batt);
}

void ui_selftest_pattern(bool show) {
    if (!S.scr) return;
    if (!show) {
        if (S.pattern) { lv_obj_delete(S.pattern); S.pattern = NULL; }
        return;
    }
    if (S.pattern) return;
    S.pattern = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.pattern);
    lv_obj_set_size(S.pattern, LV_PCT(100), LV_PCT(100));
    lv_obj_center(S.pattern);
    lv_obj_remove_flag(S.pattern, LV_OBJ_FLAG_SCROLLABLE);
    static const uint32_t bands[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_obj_create(S.pattern);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(100), 65);
        lv_obj_set_y(b, i * 64);
        lv_obj_set_style_bg_color(b, lv_color_hex(bands[i]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    }
    for (int x = 0; x < 240; x += 20) {
        lv_obj_t *v = lv_obj_create(S.pattern);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, 1, LV_PCT(100)); lv_obj_set_x(v, x);
        lv_obj_set_style_bg_color(v, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_50, 0);
    }
    for (int y = 0; y < 320; y += 20) {
        lv_obj_t *h = lv_obj_create(S.pattern);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, LV_PCT(100), 1); lv_obj_set_y(h, y);
        lv_obj_set_style_bg_color(h, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_50, 0);
    }
    lv_obj_move_foreground(S.prompt);   // 提示压在图样之上
}

void ui_selftest_color(uint32_t rgb) {
    if (!S.scr) return;
    if (!S.pattern) {                       // 与色带共用同一全屏层;清除仍走 ui_selftest_pattern(false)
        S.pattern = lv_obj_create(S.scr);
        lv_obj_remove_style_all(S.pattern);
        lv_obj_set_size(S.pattern, LV_PCT(100), LV_PCT(100));
        lv_obj_center(S.pattern);
        lv_obj_remove_flag(S.pattern, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(S.pattern, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_bg_color(S.pattern, lv_color_hex(rgb), 0);
    lv_obj_move_foreground(S.pattern);
    if (S.prompt) lv_obj_move_foreground(S.prompt);   // 操作提示压在纯色之上
}

void ui_selftest_result(bool pass) {
    if (!S.scr) return;
    if (S.pattern) { lv_obj_delete(S.pattern); S.pattern = NULL; }
    S.result = lv_obj_create(S.scr);
    lv_obj_remove_style_all(S.result);
    lv_obj_set_size(S.result, LV_PCT(100), LV_PCT(100));
    lv_obj_center(S.result);
    lv_obj_set_style_bg_color(S.result, pass ? lv_color_hex(0x0E5A2A) : lv_color_hex(0x6A1414), 0);
    lv_obj_set_style_bg_opa(S.result, LV_OPA_COVER, 0);

    lv_obj_t *big = mklabel(S.result, &lv_font_montserrat_34, pass ? GREEN : RED);
    lv_label_set_text(big, pass ? "PASS" : "FAIL");
    lv_obj_align(big, LV_ALIGN_CENTER, 0, -30);

    // 注意:中文字体子集只含 ASCII + GB2312 一级汉字,不含 · 、，等标点(会显示成方框)。
    lv_obj_t *cn = mklabel(S.result, &lv_font_cn_24, lv_color_white());
    lv_label_set_text(cn, pass ? "良品" : "不良");
    lv_obj_align(cn, LV_ALIGN_CENTER, 0, 20);
}

void ui_selftest_close(void) {
    if (S.scr) { lv_obj_delete(S.scr); }
    memset(&S, 0, sizeof S);
}
