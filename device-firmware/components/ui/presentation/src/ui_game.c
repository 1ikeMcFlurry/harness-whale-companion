// components/presentation/src/ui_game.c —— 三线跑酷(上/下换道,确定冲刺)
#include "presentation/ui_game.h"
#include "lvgl.h"

// ---- 屏幕/布局参数(与 ST7789P3 240×320 一致)----
#define GW        240
#define GH        320
#define HUD_H     32
#define LANES     3
#define PLAY_Y    HUD_H
#define PLAY_H    (GH - HUD_H)          // 288
#define LANE_H    (PLAY_H / LANES)      // 96
#define HERO_CELL 2                      // 每个精灵格 = 2px
#define PLAYER_W  (HERO_W * HERO_CELL)   // 30
#define PLAYER_H  (HERO_H * HERO_CELL)   // 22
#define PLAYER_X  26
#define N_OBST    5
#define OBST_W    18
#define OBST_H    84
#define OBST_RADIUS 5
#define OBST_BORDER 3
#define OBST_HILITE_W 4
#define OBST_HILITE_H 66
#define SPAWN_GAP 120                   // 相邻障碍水平间距(px)
#define TICK_MS   40
#define DASH_MS   500                   // 冲刺无敌时长
#define COOL_MS   2500                  // 冲刺冷却
#define SPD_BASE  3                     // 基础速度(px/tick)
#define SPD_MAX   7

// ---- 调色板(Supabase 风:中性深色底 + 细边框卡片 + 翠绿强调)----
#define COL_BG      lv_color_hex(0x1C1C1C)   // 中性近黑底
#define COL_SURFACE lv_color_hex(0x242424)   // 卡片/面板
#define COL_SURFACE2 lv_color_hex(0x2A2A2A)  // 略亮面板(车道)
#define COL_BORDER  lv_color_hex(0x323232)   // 细边框
#define COL_PLAYER  lv_color_hex(0x3ECF8E)   // Supabase 绿(主角)
#define COL_DASH    lv_color_hex(0xC7F9E5)   // 冲刺:更亮的薄荷绿
#define COL_OBST    lv_color_hex(0xFF3D8B)   // 障碍墙粉色
#define COL_OBST_CORE lv_color_hex(0x24242C) // 障碍墙深色内芯
#define COL_OBST_HILITE lv_color_hex(0xFFB3D1) // 中央浅粉高光

// ---- 主角像素精灵(照 下载.png 还原)----
// 绿色描边"显示器脸" + 两只菱形眼 + 左下角缺口(底边右移错开)。单一亮绿,
// 冲刺时整体变青蓝。1=实心,0=透明。由脚本从 PNG 裁剪降采样生成。
#define HERO_W 16
#define HERO_H 12
static const uint8_t HERO[HERO_H][HERO_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1},
    {1,1,0,0,0,1,0,0,0,0,1,0,0,0,1,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,0,0,1,1},
    {1,1,0,0,0,1,0,0,0,0,1,0,0,0,1,1},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1},   // ← 左下缺角
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1},
};
static uint8_t hero_buf[PLAYER_W * PLAYER_H * 4] __attribute__((aligned(4))); // ARGB8888
#define COL_WHITE   lv_color_hex(0xEDEDED)
#define COL_GRAY    lv_color_hex(0x8B8B8B)
#define COL_GREEN   lv_color_hex(0x3ECF8E)

typedef enum { ST_MENU, ST_PLAYING, ST_OVER } gstate_t;

typedef struct { lv_obj_t *o; int x; int w; int lane; bool active; bool passed; } obst_t;

static struct {
    bool       active;                  // 游戏屏幕是否存在
    gstate_t   state;
    lv_obj_t  *scr, *prev_scr;
    lv_obj_t  *player;
    lv_obj_t  *score_lbl, *best_lbl, *dash_lbl;
    lv_obj_t  *overlay, *overlay_lbl;   // 菜单/结束卡片 + 卡内文字
    obst_t     ob[N_OBST];
    int        player_lane;
    bool       hero_dash_drawn;          // 当前精灵是否以冲刺色绘制(避免每帧重绘)
    int        score, best;
    int        speed;
    int        dash_ms, cool_ms;
    uint32_t   rng;                     // 简易 LCG 随机数
    uint32_t   open_tick;               // 开局时刻,用于吞掉"进入游戏那一下确定"
    lv_timer_t *timer;
    ui_game_exit_cb_t on_exit;
    void      *on_exit_user;
} G;

// 待处理输入(按键回调线程写,主循环读);ESP32-C3 单核,int 读写原子。
static volatile int  s_move;            // 累积换道:+1 下 / -1 上
static volatile bool s_enter;
static volatile bool s_exit;
static ui_game_result_cb_t s_result_cb;  // 一局结束回调(上报本局得分给组装层)
static void               *s_result_user;

void ui_game_set_result_cb(ui_game_result_cb_t cb, void *user) {
    s_result_cb = cb; s_result_user = user;
}
void ui_game_set_best(int best) { G.best = best; }

// ---- 工具 ----
static int rng_next(int mod) {
    G.rng = G.rng * 1103515245u + 12345u;
    return (int)((G.rng >> 16) % (uint32_t)mod);
}
static int lane_center(int l) { return PLAY_Y + l * LANE_H + LANE_H / 2; }

static lv_obj_t *make_rect(lv_obj_t *p, int w, int h, lv_color_t c, int radius) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// 霓虹障碍柱:粉色外框 + 深色内芯 + 居中的浅粉竖向高光。
// 移动物体不使用阴影，避免扩大脏区和申请离屏绘制层造成撕裂/卡顿。
static lv_obj_t *make_obstacle(lv_obj_t *p) {
    lv_obj_t *o = make_rect(p, OBST_W, OBST_H, COL_OBST_CORE, OBST_RADIUS);
    lv_obj_set_style_border_color(o, COL_OBST, 0);
    lv_obj_set_style_border_width(o, OBST_BORDER, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_t *hilite = make_rect(o, OBST_HILITE_W, OBST_HILITE_H,
                                 COL_OBST_HILITE, OBST_HILITE_W / 2);
    lv_obj_align(hilite, LV_ALIGN_CENTER, 0, 0);
    return o;
}

// Supabase 卡片:填充面板色 + 1px 细边框 + 圆角
static void style_card(lv_obj_t *o, lv_color_t bg, int radius) {
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, COL_BORDER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, radius, 0);
}

// ---- HUD 刷新 ----
static void hud_refresh(void) {
    lv_label_set_text_fmt(G.score_lbl, "#8B8B8B SCORE# %d", G.score);
    lv_label_set_text_fmt(G.best_lbl, "BEST %d", G.best);
    // 冲刺状态胶囊:就绪=绿字、冲刺中=亮底深字、冷却=灰字
    lv_color_t pill_bg, pill_txt;
    const char *txt;
    if (G.dash_ms > 0)      { txt = "DASH"; pill_bg = COL_GREEN;   pill_txt = COL_BG;   }
    else if (G.cool_ms > 0) { txt = "WAIT"; pill_bg = COL_SURFACE; pill_txt = COL_GRAY; }
    else                    { txt = "DASH"; pill_bg = COL_SURFACE; pill_txt = COL_GREEN;}
    lv_label_set_text(G.dash_lbl, txt);
    lv_obj_set_style_bg_color(G.dash_lbl, pill_bg, 0);
    lv_obj_set_style_text_color(G.dash_lbl, pill_txt, 0);
}

// 把像素精灵画进 canvas。col=主体色(平时绿,冲刺青蓝);0 格保持透明(含缺角)。
static void hero_draw(lv_color_t col) {
    lv_canvas_fill_bg(G.player, col, LV_OPA_TRANSP);    // 清为透明
    for (int r = 0; r < HERO_H; r++)
        for (int c = 0; c < HERO_W; c++) {
            if (!HERO[r][c]) continue;
            for (int dy = 0; dy < HERO_CELL; dy++)
                for (int dx = 0; dx < HERO_CELL; dx++)
                    lv_canvas_set_px(G.player, c * HERO_CELL + dx, r * HERO_CELL + dy, col, LV_OPA_COVER);
        }
}

static void player_render(void) {
    lv_obj_set_pos(G.player, PLAYER_X, lane_center(G.player_lane) - PLAYER_H / 2);
    bool dashing = G.dash_ms > 0;                        // 仅在冲刺状态切换时重绘
    if (dashing != G.hero_dash_drawn) {
        hero_draw(dashing ? COL_DASH : COL_PLAYER);
        G.hero_dash_drawn = dashing;
    }
}

static void obstacles_hide_all(void) {
    for (int i = 0; i < N_OBST; i++) {
        G.ob[i].active = false;
        lv_obj_add_flag(G.ob[i].o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void overlay_show(const char *txt) {
    lv_label_set_text(G.overlay_lbl, txt);
    lv_obj_remove_flag(G.overlay, LV_OBJ_FLAG_HIDDEN);
}
static void overlay_hide(void) { lv_obj_add_flag(G.overlay, LV_OBJ_FLAG_HIDDEN); }

// ---- 状态切换 ----
static void game_start(void) {
    G.state = ST_PLAYING;
    G.score = 0; G.speed = SPD_BASE;
    G.player_lane = 1;
    G.dash_ms = 0; G.cool_ms = 0;
    obstacles_hide_all();
    overlay_hide();
    player_render();
    hud_refresh();
}

static void game_over(void) {
    G.state = ST_OVER;
    if (G.score > G.best) G.best = G.score;
    if (s_result_cb) s_result_cb(G.score, s_result_user);   // 上报本局得分:累加总分/存最高分到本地
    hud_refresh();
    char buf[128];
    lv_snprintf(buf, sizeof(buf),
        "#FF3D8B GAME OVER#\n\n#EDEDED SCORE %d#\n\n"
        "#3ECF8E OK#  retry\n#8B8B8B HOLD  exit#", G.score);
    overlay_show(buf);
}

static void spawn_maybe(void) {
    int max_x = -10000; int free = -1;
    for (int i = 0; i < N_OBST; i++) {
        if (G.ob[i].active) { if (G.ob[i].x > max_x) max_x = G.ob[i].x; }
        else if (free < 0) free = i;
    }
    if (free < 0) return;                       // 池满
    if (max_x > GW - SPAWN_GAP) return;         // 间距不够,先不生成
    obst_t *b = &G.ob[free];
    b->active = true; b->passed = false;
    b->x = GW; b->lane = rng_next(LANES);
    b->w = OBST_W;
    lv_obj_set_pos(b->o, b->x, lane_center(b->lane) - OBST_H / 2);
    lv_obj_remove_flag(b->o, LV_OBJ_FLAG_HIDDEN);
}

// ---- 主循环 ----
// 消费换道/确定输入(退出 s_exit 由 game_tick 优先处理,这里不碰)。
static void consume_input(void) {
    int mv = s_move; s_move = 0;
    bool en = s_enter; s_enter = false;

    if (G.state == ST_PLAYING) {
        if (mv) {
            G.player_lane += (mv > 0) ? 1 : -1;
            if (G.player_lane < 0) G.player_lane = 0;
            if (G.player_lane > LANES - 1) G.player_lane = LANES - 1;
        }
        if (en && G.dash_ms <= 0 && G.cool_ms <= 0) {   // 触发冲刺
            G.dash_ms = DASH_MS; G.cool_ms = COOL_MS;
        }
    } else {                                    // MENU / OVER
        if (en) game_start();
    }
}

static void teardown(void) {
    if (!G.active) return;
    if (G.timer) { lv_timer_delete(G.timer); G.timer = NULL; }
    if (G.prev_scr) lv_screen_load(G.prev_scr);
    if (G.scr) { lv_obj_delete(G.scr); G.scr = NULL; }
    G.active = false;
    G.state = ST_MENU;
    if (G.on_exit) G.on_exit(G.on_exit_user);
}

static void game_tick(lv_timer_t *t) {
    (void)t;
    if (s_exit) { s_exit = false; teardown(); return; }   // 长按确定 → 退出
    consume_input();

    if (G.state != ST_PLAYING) return;

    // 冲刺/冷却计时
    if (G.dash_ms > 0) { G.dash_ms -= TICK_MS; if (G.dash_ms < 0) G.dash_ms = 0; }
    if (G.cool_ms > 0) { G.cool_ms -= TICK_MS; if (G.cool_ms < 0) G.cool_ms = 0; }

    spawn_maybe();

    for (int i = 0; i < N_OBST; i++) {
        obst_t *b = &G.ob[i];
        if (!b->active) continue;
        b->x -= G.speed;
        // 计分:越过玩家
        if (!b->passed && b->x + b->w < PLAYER_X) {
            b->passed = true;
            G.score++;
            if (G.score % 5 == 0 && G.speed < SPD_MAX) G.speed++;
        }
        // 碰撞:与玩家同道且 x 重叠,非无敌
        if (G.dash_ms <= 0 && b->lane == G.player_lane &&
            b->x < PLAYER_X + PLAYER_W && b->x + b->w > PLAYER_X) {
            game_over();
            return;
        }
        // 出屏回收
        if (b->x + b->w < 0) { b->active = false; lv_obj_add_flag(b->o, LV_OBJ_FLAG_HIDDEN); }
        else lv_obj_set_x(b->o, b->x);
    }

    player_render();
    hud_refresh();
}

// ---- 构建屏幕 ----
static void build_scene(void) {
    G.scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(G.scr);
    lv_obj_set_style_bg_color(G.scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(G.scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(G.scr, LV_OBJ_FLAG_SCROLLABLE);

    // 车道卡片(3 条):略亮面板 + 细边框 + 圆角
    for (int l = 0; l < LANES; l++) {
        lv_obj_t *lane = lv_obj_create(G.scr);
        lv_obj_remove_style_all(lane);
        lv_obj_set_size(lane, GW - 12, LANE_H - 8);
        lv_obj_set_pos(lane, 6, PLAY_Y + l * LANE_H + 4);
        style_card(lane, COL_SURFACE2, 10);
        lv_obj_remove_flag(lane, LV_OBJ_FLAG_SCROLLABLE);
    }

    // HUD 底部 hairline 分隔线
    lv_obj_t *hair = make_rect(G.scr, GW, 1, COL_BORDER, 0);
    lv_obj_set_pos(hair, 0, HUD_H - 1);

    // HUD:SCORE(灰标+白数,recolor) / BEST(灰) / DASH 胶囊
    G.score_lbl = lv_label_create(G.scr);
    lv_label_set_recolor(G.score_lbl, true);
    lv_obj_set_style_text_font(G.score_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.score_lbl, COL_WHITE, 0);
    lv_obj_align(G.score_lbl, LV_ALIGN_TOP_LEFT, 10, 9);

    G.best_lbl = lv_label_create(G.scr);
    lv_obj_set_style_text_font(G.best_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(G.best_lbl, COL_GRAY, 0);
    lv_obj_align(G.best_lbl, LV_ALIGN_TOP_MID, 6, 10);

    G.dash_lbl = lv_label_create(G.scr);
    lv_obj_set_style_text_font(G.dash_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(G.dash_lbl, 8, 0);          // 胶囊圆角
    lv_obj_set_style_bg_opa(G.dash_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(G.dash_lbl, 8, 0);
    lv_obj_set_style_pad_ver(G.dash_lbl, 3, 0);
    lv_obj_align(G.dash_lbl, LV_ALIGN_TOP_RIGHT, -8, 6);

    // 障碍池:固定尺寸的粉色圆角矩形墙。
    for (int i = 0; i < N_OBST; i++) {
        lv_obj_t *o = make_obstacle(G.scr);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        G.ob[i].o = o;
        G.ob[i].active = false;
    }

    // 玩家(像素精灵,LVGL canvas 逐像素绘制,ARGB8888 带透明)
    G.player = lv_canvas_create(G.scr);
    lv_canvas_set_buffer(G.player, hero_buf, PLAYER_W, PLAYER_H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_remove_flag(G.player, LV_OBJ_FLAG_SCROLLABLE);
    G.hero_dash_drawn = false;
    hero_draw(COL_PLAYER);

    // 菜单/结束:居中卡片 + 卡内文字(recolor 给标题上色)
    G.overlay = lv_obj_create(G.scr);
    lv_obj_remove_style_all(G.overlay);
    lv_obj_set_size(G.overlay, 204, LV_SIZE_CONTENT);
    style_card(G.overlay, COL_SURFACE, 14);
    lv_obj_set_style_pad_all(G.overlay, 18, 0);
    lv_obj_remove_flag(G.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(G.overlay);

    G.overlay_lbl = lv_label_create(G.overlay);
    lv_label_set_recolor(G.overlay_lbl, true);
    lv_obj_set_style_text_font(G.overlay_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(G.overlay_lbl, COL_GRAY, 0);
    lv_obj_set_style_text_align(G.overlay_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(G.overlay_lbl, lv_pct(100));
}

void ui_game_open(ui_game_exit_cb_t on_exit, void *user) {
    if (G.active) return;
    G.on_exit = on_exit; G.on_exit_user = user;
    G.prev_scr = lv_screen_active();
    G.open_tick = lv_tick_get();
    G.rng = G.open_tick ^ 0xA5A5u;
    s_move = 0; s_enter = false; s_exit = false;

    build_scene();
    lv_screen_load(G.scr);
    // 平台会给每个 screen 自动开启整屏圆角裁剪；游戏含持续移动对象，裁剪会创建
    // 大型临时 layer 并拖慢局部刷新。游戏页关闭它，以降低刷新延迟和撕裂。
    lv_obj_set_style_clip_corner(G.scr, false, 0);
    lv_obj_set_style_radius(G.scr, 0, 0);

    G.active = true;
    G.state = ST_MENU;
    G.player_lane = 1;
    player_render();
    hud_refresh();
    overlay_show("#3ECF8E LANE RUNNER#\n\n"
                 "#EDEDED UP / DOWN#  move\n"
                 "#EDEDED OK#  dash\n\n"
                 "#3ECF8E PRESS OK TO START#");

    G.timer = lv_timer_create(game_tick, TICK_MS, NULL);
}

bool ui_game_is_active(void) { return G.active; }

void ui_game_key(ui_game_key_t k) {
    switch (k) {
        case UI_GAME_KEY_UP:    s_move -= 1; break;
        case UI_GAME_KEY_DOWN:  s_move += 1; break;
        case UI_GAME_KEY_ENTER:
            // 吞掉"点 GAME 磁贴进入游戏"那一下的确定(经 hal_button 异步到达)
            if (lv_tick_get() - G.open_tick > 300) s_enter = true;
            break;
        case UI_GAME_KEY_EXIT:  s_exit = true;  break;
    }
}
