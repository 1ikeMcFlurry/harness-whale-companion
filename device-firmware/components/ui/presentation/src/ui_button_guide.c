// 开机外围按键指引:左上电源键，右侧依次为上/下/确定键。
#include "presentation/ui_button_guide.h"
#include "lvgl.h"

LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_24);

#define BG       lv_color_hex(0x0B1117)
#define CARD     lv_color_hex(0x1A2730)
#define BORDER   lv_color_hex(0x354852)
#define GREEN    lv_color_hex(0x3ECF8E)
#define PINK     lv_color_hex(0xFF3D8B)
#define WHITE    lv_color_hex(0xF2F6F8)
#define MUTED    lv_color_hex(0x8EA0AA)

// 240x320 固定布局网格。右侧三组共用同一左右边界，顶部两组严格齐顶。
#define EDGE_W       8
#define EDGE_H       38
#define EDGE_TOP_Y   18
#define EDGE_MID_Y   141
#define EDGE_BOTTOM_Y 264
#define CARD_LEFT_X  20
#define CARD_RIGHT_X 128
#define CARD_RIGHT_W 92
#define CARD_GAP      4

static lv_obj_t *s_scr;
static lv_obj_t *s_prev;

static lv_obj_t *rect(lv_obj_t *parent, int w, int h, int radius, lv_color_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

static void add_edge_callout(bool left, int y, int card_w, int card_h,
                             const char *name, lv_color_t accent) {
    // 纯色短条贴住屏幕边缘，避免阴影触发 LVGL 大型离屏绘制层。
    lv_obj_t *edge = rect(s_scr, EDGE_W, EDGE_H, 4, accent);
    lv_obj_set_pos(edge, left ? 0 : 240 - EDGE_W, y);

    lv_obj_t *line = rect(s_scr, left ? 12 : 10, 2, 1, accent);
    lv_obj_set_pos(line, left ? EDGE_W : CARD_RIGHT_X + CARD_RIGHT_W + 2,
                   y + EDGE_H / 2 - 1);

    lv_obj_t *card = rect(s_scr, card_w, card_h, 10, CARD);
    int card_y = y;   // 框顶与对应边缘亮条严格对齐
    if (card_y < 4) card_y = 4;
    if (card_y + card_h > 316) card_y = 316 - card_h;
    lv_obj_set_pos(card, left ? CARD_LEFT_X : CARD_RIGHT_X, card_y);
    lv_obj_set_style_border_color(card, BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);

    lv_obj_t *text = label(card, name, &lv_font_cn_16, WHITE);
    lv_obj_update_layout(text);
    if (left) {
        // 按实际文字尺寸加内边距收紧外框，首行与“上选择键”文字齐顶。
        int max_w = CARD_RIGHT_X - CARD_LEFT_X - CARD_GAP;
        int content_w = lv_obj_get_width(text) + 20;
        int fitted_w = content_w < max_w ? content_w : max_w;
        if (content_w > max_w) {
            lv_obj_set_width(text, fitted_w - 20);
            lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
            lv_obj_update_layout(text);
        }
        int text_y = (EDGE_H - lv_font_get_line_height(&lv_font_cn_16)) / 2;
        lv_obj_set_size(card, fitted_w,
                        text_y + lv_obj_get_height(text) + 10);
        lv_obj_align(text, LV_ALIGN_TOP_MID, 0, text_y);
    } else {
        // 右侧保持统一宽度；多行说明按内容增高，避免文字超出外框。
        int content_h = lv_obj_get_height(text) + 12;
        if (content_h > card_h) {
            lv_obj_set_height(card, content_h);
            int adjusted_y = y;
            if (adjusted_y + content_h > 316) adjusted_y = 316 - content_h;
            lv_obj_set_y(card, adjusted_y);
        }
        lv_obj_center(text);
    }
}

void ui_button_guide_open(void) {
    if (s_scr) return;
    s_prev = lv_screen_active();
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    add_edge_callout(true, EDGE_TOP_Y, 104, 98,
                     "电源键\n开机:\n短按0.5s\n关机:\n长按2.0s", PINK); // 左侧最上方
    add_edge_callout(false, EDGE_TOP_Y, CARD_RIGHT_W, EDGE_H, "上键", GREEN); // 右侧上段
    add_edge_callout(false, EDGE_MID_Y, CARD_RIGHT_W, EDGE_H, "下键", GREEN); // 右侧中段
    add_edge_callout(false, EDGE_BOTTOM_Y, CARD_RIGHT_W, 50,
                     "确定键\n短按选中\n长按返回", GREEN); // 距底部 18px，与上键上下对称

    lv_obj_t *exit_hint = label(s_scr, "按任意功能键开启体验", &lv_font_cn_16, MUTED);
    lv_obj_align(exit_hint, LV_ALIGN_TOP_MID, 0, 216);

    lv_screen_load(s_scr);
    // 平台的 SCREEN_LOADED 回调会给所有 screen 开启整屏圆角裁剪。该效果需要约一整条
    // 240px 宽的离屏 layer，引导页对象较多时会耗尽 LVGL builtin 池并卡住 TLSF。
    // 本页背景接近黑色，关闭裁剪对外观无明显影响，同时彻底避免大型临时 layer。
    lv_obj_set_style_clip_corner(s_scr, false, 0);
    lv_obj_set_style_radius(s_scr, 0, 0);
}

void ui_button_guide_close(void) {
    if (!s_scr) return;
    lv_obj_t *old = s_scr;
    if (s_prev) lv_screen_load(s_prev);
    s_scr = NULL;
    s_prev = NULL;
    // 切屏后的重绘仍可能引用旧 screen；交给 LVGL 下一轮安全删除。
    lv_obj_delete_async(old);
}
