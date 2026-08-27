// components/ui/presentation/include/presentation/ui_mario.h
#pragma once
#include <stdbool.h>

// 马里奥跳跃屏(收到心跳广播时显示)。除 ui_mario_jump() 外,涉及 LVGL 对象的
// 函数须在 LVGL 任务上下文/持锁时调用。
typedef void (*ui_mario_exit_cb_t)(void *user);

// 建马里奥屏并载入。记录当前屏为"上一屏",退出时载回。须持 LVGL 锁调用。
void ui_mario_open(ui_mario_exit_cb_t on_exit, void *user);

// 触发一次跳跃。线程安全:仅置 pending 标志,由内部 lv_timer(LVGL 任务)消费。
// 可从 NimBLE host 任务调用。
void ui_mario_jump(void);

// 是否处于马里奥屏(app 据此决定"开屏"还是"仅跳一下")。
bool ui_mario_is_active(void);
