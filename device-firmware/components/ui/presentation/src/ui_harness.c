#include "presentation/ui_harness.h"

#include "lvgl.h"
#include "ui_harness_sprites.h"

#include <stdint.h>
#include <string.h>

#define OFFLINE_TIMEOUT_MS 6500u

#define COL_ABYSS    lv_color_hex(0x06121E)
#define COL_TRENCH   lv_color_hex(0x0A2132)
#define COL_MIDNIGHT lv_color_hex(0x12324A)
#define COL_BLUE     lv_color_hex(0x2478F2)
#define COL_CYAN     lv_color_hex(0x55E8F4)
#define COL_FOAM     lv_color_hex(0xEAFBFF)
#define COL_MUTED    lv_color_hex(0x7898AA)
#define COL_GREEN    lv_color_hex(0x62E6A7)
#define COL_AMBER    lv_color_hex(0xFFC765)
#define COL_RED      lv_color_hex(0xFF6D7E)
#define COL_VIOLET   lv_color_hex(0xB99CFF)
#define COL_LOST     lv_color_hex(0x607383)

LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_24);

typedef struct {
    lv_obj_t *home;
    lv_obj_t *scr;
    lv_obj_t *link_pill;
    lv_obj_t *link_dot;
    lv_obj_t *link_lbl;
    lv_obj_t *dive_arc;
    lv_obj_t *porthole;
    lv_obj_t *avatar;
    lv_obj_t *ambient[3];
    lv_obj_t *glyph;
    lv_obj_t *state_lbl;
    lv_obj_t *task_title_lbl;
    lv_obj_t *activity_lbl;
    lv_obj_t *elapsed_lbl;
    lv_obj_t *todo_lbl;
    lv_obj_t *todo_bar;
    lv_obj_t *balance_lbl;
    lv_timer_t *timer;
    harness_status_t status;
    uint32_t last_rx_tick;
    uint32_t phase;
    bool active;
    bool dismissed;
    bool has_snapshot;
    harness_question_t question;
    uint8_t question_index;
    bool question_submitted;
} harness_ui_t;

static harness_ui_t H;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     lv_color_t color, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const char *text,
                       const lv_font_t *font, lv_color_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    return obj;
}

static void center_label(lv_obj_t *obj, int y, int width) {
    lv_obj_set_pos(obj, 0, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}

static const char *tool_name(harness_tool_t tool) {
    switch (tool) {
        case HARNESS_TOOL_TERMINAL: return "终端";
        case HARNESS_TOOL_READ:     return "读取";
        case HARNESS_TOOL_EDIT:     return "编辑";
        case HARNESS_TOOL_SEARCH:   return "搜索";
        case HARNESS_TOOL_WEB:      return "网页";
        case HARNESS_TOOL_TASK:     return "任务";
        case HARNESS_TOOL_OTHER:    return "工具";
        default:                    return "休息";
    }
}

static const char *activity_name(const harness_status_t *status) {
    if (status->state == HARNESS_STATE_TOOL) return tool_name(status->tool);
    switch (status->state) {
        case HARNESS_STATE_THINKING: return "思考";
        case HARNESS_STATE_WAITING:  return "待确认";
        case HARNESS_STATE_DONE:     return "已完成";
        case HARNESS_STATE_ERROR:    return "出错";
        case HARNESS_STATE_STOPPED:  return "已停止";
        case HARNESS_STATE_QUESTION: return "请回电脑";
        case HARNESS_STATE_OFFLINE:  return "离线";
        default:                     return "休息";
    }
}

static lv_color_t state_color(harness_state_t state) {
    switch (state) {
        case HARNESS_STATE_IDLE:     return COL_CYAN;
        case HARNESS_STATE_THINKING: return COL_BLUE;
        case HARNESS_STATE_TOOL:     return COL_CYAN;
        case HARNESS_STATE_WAITING:  return COL_AMBER;
        case HARNESS_STATE_DONE:     return COL_GREEN;
        case HARNESS_STATE_ERROR:    return COL_RED;
        case HARNESS_STATE_STOPPED:  return COL_MUTED;
        case HARNESS_STATE_QUESTION: return COL_VIOLET;
        default:                     return COL_LOST;
    }
}

static const char *state_title(harness_state_t state) {
    switch (state) {
        case HARNESS_STATE_IDLE:     return "休息中";
        case HARNESS_STATE_THINKING: return "深度思考";
        case HARNESS_STATE_TOOL:     return "调用工具";
        case HARNESS_STATE_WAITING:  return "等待确认";
        case HARNESS_STATE_DONE:     return "任务完成";
        case HARNESS_STATE_ERROR:    return "执行出错";
        case HARNESS_STATE_STOPPED:  return "任务已停止";
        case HARNESS_STATE_QUESTION: return "等待选择";
        default:                     return "等待连接";
    }
}

static const lv_image_dsc_t *state_frame(harness_state_t state) {
    switch (state) {
        case HARNESS_STATE_IDLE:     return &ui_harness_whale_idle;
        case HARNESS_STATE_THINKING: return &ui_harness_whale_thinking;
        case HARNESS_STATE_TOOL:     return &ui_harness_whale_tool;
        case HARNESS_STATE_WAITING:  return &ui_harness_whale_waiting;
        case HARNESS_STATE_DONE:     return &ui_harness_whale_done;
        case HARNESS_STATE_ERROR:    return &ui_harness_whale_error;
        case HARNESS_STATE_STOPPED:  return &ui_harness_whale_idle;
        case HARNESS_STATE_QUESTION: return &ui_harness_whale_waiting;
        default:                     return &ui_harness_whale_offline;
    }
}

static void hide_ambient(void) {
    for (int i = 0; i < 3; ++i) lv_obj_add_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_elapsed(void) {
    uint32_t elapsed = H.status.elapsed_s;
    if (H.has_snapshot && H.status.state != HARNESS_STATE_IDLE &&
        H.status.state != HARNESS_STATE_DONE && H.status.state != HARNESS_STATE_ERROR &&
        H.status.state != HARNESS_STATE_STOPPED &&
        H.status.state != HARNESS_STATE_OFFLINE) {
        elapsed += (uint32_t)(lv_tick_get() - H.last_rx_tick) / 1000u;
    }
    uint32_t minutes = elapsed / 60u;
    if (minutes > 99u) minutes = 99u;
    lv_label_set_text_fmt(H.elapsed_lbl, "%02lu:%02lu",
                          (unsigned long)minutes, (unsigned long)(elapsed % 60u));
}

static void apply_status(void) {
    lv_color_t color = state_color(H.status.state);
    bool linked = H.status.state != HARNESS_STATE_OFFLINE;

    lv_label_set_text(H.state_lbl, state_title(H.status.state));
    lv_obj_set_style_text_color(H.state_lbl, color, 0);
    lv_obj_set_style_bg_color(H.link_dot, linked ? COL_GREEN : COL_LOST, 0);
    lv_obj_set_style_border_color(H.link_pill, linked ? COL_GREEN : COL_MIDNIGHT, 0);
    lv_label_set_text(H.link_lbl, linked ? "在线" : "离线");
    lv_obj_set_style_arc_color(H.dive_arc, color, LV_PART_INDICATOR);
    lv_image_set_src(H.avatar, state_frame(H.status.state));
    if (H.status.state == HARNESS_STATE_QUESTION && H.question.option_count > 0u) {
        if (H.question_submitted) {
            lv_label_set_text(H.activity_lbl, "已提交");
        } else {
            lv_label_set_text_fmt(H.activity_lbl, "选项 %u/%u",
                                  (unsigned)(H.question_index + 1u),
                                  (unsigned)H.question.option_count);
        }
    } else {
        lv_label_set_text(H.activity_lbl, activity_name(&H.status));
    }
    refresh_elapsed();

    if (H.status.title_len > 0) {
        lv_label_set_text_fmt(H.task_title_lbl, "任务: %s", H.status.title);
    } else {
        lv_label_set_text(H.task_title_lbl, linked ? "任务: 等待标题" : "任务: 等待 Harness 连接");
    }

    uint16_t total = H.status.todo_total;
    uint16_t done = H.status.todo_done;
    int percent = total > 0 ? (int)((uint32_t)done * 100u / total) : 0;
    lv_arc_set_value(H.dive_arc, percent);
    if (H.status.state == HARNESS_STATE_QUESTION && H.question.option_count > 0u) {
        lv_label_set_text_fmt(H.todo_lbl, "%u. %s", (unsigned)(H.question_index + 1u),
                              H.question.labels[H.question_index]);
        lv_bar_set_range(H.todo_bar, 0, H.question.option_count);
        lv_bar_set_value(H.todo_bar, H.question_index + 1u, LV_ANIM_OFF);
    } else {
        lv_label_set_text_fmt(H.todo_lbl, "进度 %u/%u", (unsigned)done, (unsigned)total);
        lv_bar_set_range(H.todo_bar, 0, total > 0 ? total : 1);
        lv_bar_set_value(H.todo_bar, total > 0 ? done : 0, LV_ANIM_OFF);
    }
    lv_obj_set_style_bg_color(H.todo_bar, color, LV_PART_INDICATOR);

    if ((H.status.flags & HARNESS_FLAG_HAS_BALANCE) != 0) {
        if ((H.status.flags & HARNESS_FLAG_BALANCE_AVAILABLE) != 0) {
            if (H.status.currency == HARNESS_CURRENCY_CNY) {
                lv_label_set_text_fmt(H.balance_lbl, "%lu.%02lu元",
                                      (unsigned long)(H.status.balance_minor / 100u),
                                      (unsigned long)(H.status.balance_minor % 100u));
            } else {
                lv_label_set_text_fmt(H.balance_lbl, "$%lu.%02lu",
                                      (unsigned long)(H.status.balance_minor / 100u),
                                      (unsigned long)(H.status.balance_minor % 100u));
            }
            lv_obj_set_style_text_color(H.balance_lbl, COL_FOAM, 0);
        } else {
            lv_label_set_text(H.balance_lbl, "余额异常");
            lv_obj_set_style_text_color(H.balance_lbl, COL_RED, 0);
        }
    } else {
        lv_label_set_text(H.balance_lbl, "余额 --");
        lv_obj_set_style_text_color(H.balance_lbl, COL_MUTED, 0);
    }

    lv_obj_set_style_opa(H.avatar, linked ? LV_OPA_COVER : LV_OPA_60, 0);
}

static void animate_character(void) {
    harness_state_t state = H.status.state;
    uint32_t p = H.phase;
    lv_image_set_src(H.avatar, state_frame(state));
    lv_obj_set_pos(H.avatar, 16, 15);
    hide_ambient();

    if (state == HARNESS_STATE_OFFLINE) {
        lv_obj_set_y(H.avatar, 17 + (int)((p / 10u) & 1u));
        lv_obj_remove_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(H.glyph, "z");
        lv_obj_set_pos(H.glyph, 128, 82 - (int)(p % 11u));
        lv_obj_set_style_text_color(H.glyph, COL_LOST, 0);
        return;
    }

    if (state == HARNESS_STATE_IDLE) {
        lv_obj_set_y(H.avatar, 14 + (int)((p / 7u) & 1u));
        if ((p % 36u) < 4u) lv_image_set_src(H.avatar, &ui_harness_whale_idle_blink);
        lv_obj_remove_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(H.glyph, "z");
        lv_obj_set_pos(H.glyph, 128, 75 - (int)(p % 12u));
        lv_obj_set_style_text_color(H.glyph, COL_CYAN, 0);
        return;
    }

    if (state == HARNESS_STATE_THINKING) {
        static const int8_t orbit_x[12] = { 76, 98, 119, 130, 119, 98, 76, 54, 34, 23, 34, 54 };
        static const int8_t orbit_y[12] = { 9, 14, 29, 50, 70, 83, 88, 83, 70, 50, 29, 14 };
        lv_obj_set_y(H.avatar, 13 + (int)((p / 3u) & 1u));
        for (int i = 0; i < 3; ++i) {
            int k = (int)((p + (uint32_t)i * 4u) % 12u);
            lv_obj_remove_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(H.ambient[i], orbit_x[k], orbit_y[k]);
            lv_obj_set_style_bg_color(H.ambient[i], i == 0 ? COL_CYAN : COL_BLUE, 0);
        }
        return;
    }

    if (state == HARNESS_STATE_TOOL) {
        lv_obj_set_pos(H.avatar, 15 + (int)((p / 3u) & 1u) * 2, 14);
        lv_obj_remove_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(H.glyph, H.status.tool == HARNESS_TOOL_TERMINAL ? ">_" : "*");
        lv_obj_set_pos(H.glyph, 126, 30 + (int)((p / 4u) & 1u) * 2);
        lv_obj_set_style_text_color(H.glyph, COL_CYAN, 0);
        return;
    }

    if (state == HARNESS_STATE_WAITING) {
        lv_obj_set_x(H.avatar, 16 + (((p / 3u) & 1u) ? 2 : -2));
        for (int i = 0; i < 2; ++i) {
            lv_obj_remove_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(H.ambient[i], 125 + i * 8, 47 + i * 10);
            lv_obj_set_style_bg_color(H.ambient[i], COL_AMBER, 0);
        }
        return;
    }

    if (state == HARNESS_STATE_DONE) {
        int beat = (int)(p % 28u);
        lv_obj_set_y(H.avatar, beat < 4 ? 9 + beat : 14);
        for (int i = 0; i < 3; ++i) {
            lv_obj_remove_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(H.ambient[i], 31 + i * 47, 94 - (int)((p * 3u + i * 8u) % 48u));
            lv_obj_set_style_bg_color(H.ambient[i], i == 1 ? COL_GREEN : COL_CYAN, 0);
        }
        return;
    }

    if (state == HARNESS_STATE_QUESTION) {
        lv_obj_set_y(H.avatar, 13 + (int)((p / 4u) & 1u));
        lv_obj_remove_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(H.glyph, "?");
        lv_obj_set_pos(H.glyph, 129, 31 + (int)((p / 4u) & 1u) * 2);
        lv_obj_set_style_text_color(H.glyph, COL_VIOLET, 0);
        for (int i = 0; i < 2; ++i) {
            lv_obj_remove_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(H.ambient[i], 27 + i * 106, 78 - (int)((p + i * 5u) % 16u));
            lv_obj_set_style_bg_color(H.ambient[i], COL_VIOLET, 0);
        }
        return;
    }

    if (state == HARNESS_STATE_STOPPED) {
        lv_obj_set_y(H.avatar, 15 + (int)((p / 10u) & 1u));
        lv_obj_remove_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(H.glyph, "-");
        lv_obj_set_pos(H.glyph, 131, 36);
        lv_obj_set_style_text_color(H.glyph, COL_MUTED, 0);
        return;
    }

    lv_obj_set_x(H.avatar, 16 + (((p / 2u) & 1u) ? 3 : -3));
    lv_obj_remove_flag(H.ambient[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(H.ambient[0], 130, 51 + (int)(p % 8u));
    lv_obj_set_style_bg_color(H.ambient[0], COL_RED, 0);
}

static void timer_cb(lv_timer_t *timer) {
    (void)timer;
    H.phase++;
    if (H.has_snapshot && H.status.state != HARNESS_STATE_OFFLINE &&
        (uint32_t)(lv_tick_get() - H.last_rx_tick) >= OFFLINE_TIMEOUT_MS) {
        H.status.state = HARNESS_STATE_OFFLINE;
        H.status.tool = HARNESS_TOOL_NONE;
        apply_status();
    }
    if (!H.active) return;
    refresh_elapsed();
    animate_character();
}

void ui_harness_create(void) {
    memset(&H, 0, sizeof H);
    H.home = lv_screen_active();
    H.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(H.scr, COL_ABYSS, 0);
    lv_obj_set_style_bg_opa(H.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(H.scr, 0, 0);
    lv_obj_remove_flag(H.scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label(H.scr, 10, 10, "DEEPSEEK HARNESS",
                            &lv_font_montserrat_12, COL_FOAM);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    H.link_pill = box(H.scr, 168, 7, 64, 25, COL_TRENCH, 13);
    lv_obj_set_style_border_width(H.link_pill, 1, 0);
    lv_obj_set_style_border_color(H.link_pill, COL_MIDNIGHT, 0);
    H.link_dot = box(H.link_pill, 7, 8, 8, 8, COL_LOST, LV_RADIUS_CIRCLE);
    H.link_lbl = label(H.link_pill, 20, 3, "离线", &lv_font_cn_16, COL_FOAM);

    H.dive_arc = lv_arc_create(H.scr);
    lv_obj_set_pos(H.dive_arc, 31, 33);
    lv_obj_set_size(H.dive_arc, 178, 178);
    lv_arc_set_range(H.dive_arc, 0, 100);
    lv_arc_set_bg_angles(H.dive_arc, 135, 405);
    lv_arc_set_value(H.dive_arc, 0);
    lv_obj_set_style_arc_width(H.dive_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(H.dive_arc, COL_MIDNIGHT, LV_PART_MAIN);
    lv_obj_set_style_arc_width(H.dive_arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(H.dive_arc, COL_LOST, LV_PART_INDICATOR);
    lv_obj_remove_style(H.dive_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(H.dive_arc, LV_OBJ_FLAG_CLICKABLE);

    H.porthole = box(H.scr, 40, 42, 160, 160, COL_TRENCH, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(H.porthole, COL_MIDNIGHT, 0);
    lv_obj_set_style_border_width(H.porthole, 1, 0);
    H.avatar = lv_image_create(H.porthole);
    lv_image_set_src(H.avatar, &ui_harness_whale_offline);
    lv_obj_set_pos(H.avatar, 16, 15);

    for (int i = 0; i < 3; ++i) {
        H.ambient[i] = box(H.porthole, 20 + i * 18, 20, 5 + i * 2, 5 + i * 2,
                           COL_CYAN, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(H.ambient[i], LV_OPA_70, 0);
        lv_obj_add_flag(H.ambient[i], LV_OBJ_FLAG_HIDDEN);
    }
    H.glyph = label(H.porthole, 128, 28, "z", &lv_font_montserrat_14, COL_CYAN);
    lv_obj_add_flag(H.glyph, LV_OBJ_FLAG_HIDDEN);

    H.state_lbl = label(H.scr, 0, 201, "等待连接", &lv_font_cn_24, COL_LOST);
    center_label(H.state_lbl, 201, 240);

    H.task_title_lbl = label(H.scr, 10, 230, "任务: 等待 Harness 连接",
                             &lv_font_cn_16, COL_MUTED);
    lv_obj_set_width(H.task_title_lbl, 220);
    lv_label_set_long_mode(H.task_title_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(H.task_title_lbl, 6000, 0);

    lv_obj_t *console = box(H.scr, 8, 254, 224, 59, COL_TRENCH, 14);
    lv_obj_set_style_border_color(console, COL_MIDNIGHT, 0);
    lv_obj_set_style_border_width(console, 1, 0);
    label(console, 10, 4, "当前", &lv_font_cn_16, COL_MUTED);
    H.activity_lbl = label(console, 47, 4, "离线", &lv_font_cn_16, COL_FOAM);
    H.elapsed_lbl = label(console, 174, 7, "00:00", &lv_font_montserrat_12, COL_FOAM);

    H.todo_lbl = label(console, 10, 28, "进度 0/0", &lv_font_cn_16, COL_MUTED);
    H.todo_bar = lv_bar_create(console);
    lv_obj_set_pos(H.todo_bar, 10, 50);
    lv_obj_set_size(H.todo_bar, 121, 5);
    lv_obj_set_style_radius(H.todo_bar, 3, 0);
    lv_obj_set_style_bg_color(H.todo_bar, COL_MIDNIGHT, 0);
    lv_obj_set_style_bg_opa(H.todo_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(H.todo_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(H.todo_bar, COL_CYAN, LV_PART_INDICATOR);

    lv_obj_t *balance_pill = box(console, 139, 28, 77, 27, COL_ABYSS, 14);
    H.balance_lbl = label(balance_pill, 0, 4, "余额 --", &lv_font_cn_16, COL_MUTED);
    lv_obj_set_width(H.balance_lbl, 77);
    lv_obj_set_style_text_align(H.balance_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(H.balance_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(H.balance_lbl, 4500, 0);

    H.status.version = HARNESS_STATUS_WIRE_VERSION;
    H.status.state = HARNESS_STATE_OFFLINE;
    H.timer = lv_timer_create(timer_cb, 250, NULL);
    apply_status();
}

void ui_harness_update(const harness_status_t *status) {
    if (!H.scr || !status) return;
    H.status = *status;
    H.last_rx_tick = lv_tick_get();
    H.has_snapshot = true;
    if (status->state != HARNESS_STATE_QUESTION) {
        H.question.option_count = 0u;
        H.question_submitted = false;
    }
    if ((status->flags & HARNESS_FLAG_NEW_TURN) != 0) H.dismissed = false;
    apply_status();
}

void ui_harness_question_update(const harness_question_t *question) {
    if (!H.scr || !question) return;
    H.question = *question;
    H.question_index = 0u;
    H.question_submitted = false;
    apply_status();
}

bool ui_harness_question_active(void) {
    return H.active && H.status.state == HARNESS_STATE_QUESTION &&
           H.question.option_count > 0u && !H.question_submitted;
}

void ui_harness_question_move(int delta) {
    if (!ui_harness_question_active()) return;
    int count = H.question.option_count;
    int next = ((int)H.question_index + delta) % count;
    if (next < 0) next += count;
    H.question_index = (uint8_t)next;
    apply_status();
}

int ui_harness_question_submit(void) {
    if (!ui_harness_question_active()) return -1;
    H.question_submitted = true;
    apply_status();
    return (int)H.question_index;
}

void ui_harness_open(void) {
    if (!H.scr || H.active) return;
    H.dismissed = false;
    H.active = true;
    lv_screen_load(H.scr);
}

void ui_harness_close(void) {
    if (!H.active || !H.home) return;
    lv_screen_load(H.home);
    H.active = false;
    H.dismissed = true;
}

bool ui_harness_is_active(void) { return H.active; }
bool ui_harness_should_auto_open(void) { return !H.dismissed; }

void ui_harness_set_capture_freeze(bool freeze) {
    if (!H.scr || !H.timer) return;
    if (freeze) {
        lv_timer_pause(H.timer);
        lv_label_set_long_mode(H.task_title_lbl, LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_long_mode(H.balance_lbl, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_x(H.task_title_lbl, 10);
        lv_obj_set_x(H.balance_lbl, 0);
        animate_character();
    } else {
        lv_label_set_long_mode(H.task_title_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(H.task_title_lbl, 6000, 0);
        lv_label_set_long_mode(H.balance_lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(H.balance_lbl, 4500, 0);
        lv_timer_resume(H.timer);
    }
}
