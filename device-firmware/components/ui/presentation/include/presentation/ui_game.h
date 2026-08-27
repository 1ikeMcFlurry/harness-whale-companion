// components/presentation/include/presentation/ui_game.h
#pragma once
#include <stdbool.h>

// 三线跑酷小游戏(仅上/下/确定三键):
//   上/下      = 换道
//   确定短按   = 冲刺(游戏中) / 开始(菜单) / 重来(结束)
//   确定长按   = 退出,回到之前的界面
// 独立 LVGL 屏幕。所有涉及 LVGL 对象的函数须在 LVGL 任务上下文或持锁时调用;
// 唯一例外是 ui_game_key(),它只记录待处理输入,可从按键回调线程安全调用。

typedef enum {
    UI_GAME_KEY_UP,
    UI_GAME_KEY_DOWN,
    UI_GAME_KEY_ENTER,   // 确定短按
    UI_GAME_KEY_EXIT,    // 确定长按 → 退出
} ui_game_key_t;

typedef void (*ui_game_exit_cb_t)(void *user);
// 一局结束(game over)时回调,带本局得分。组装层据此累加总积分/更新最高分并存本地。
typedef void (*ui_game_result_cb_t)(int score, void *user);

// 注册"一局结束"回调(只需设一次)。可为 NULL 取消。
void ui_game_set_result_cb(ui_game_result_cb_t cb, void *user);
// 用本地保存的历史最高分播种 HUD 的 BEST(打开游戏前调用)。
void ui_game_set_best(int best);

// 打开游戏:创建并载入游戏屏幕,启动主循环。须在 LVGL 任务上下文/持锁时调用。
// 玩家退出时(LVGL 任务上下文)回调 on_exit,并自动载回之前的屏幕。
void ui_game_open(ui_game_exit_cb_t on_exit, void *user);

// 是否处于游戏中(组装层据此决定是否把按键转发给游戏)。
bool ui_game_is_active(void);

// 喂入一个按键。线程安全:仅记录待处理输入,由主循环(LVGL 任务)消费。
void ui_game_key(ui_game_key_t k);
