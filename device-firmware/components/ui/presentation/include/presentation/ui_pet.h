// components/ui/presentation/include/presentation/ui_pet.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

// 宠物页:按宠物类型展示对应宠物(占位吉祥物,真实形象待设计)。
// 由 dock 的 PET 磁贴进入;长按确定键退出(退出回调里载回上一屏)。
// 涉及 LVGL 对象的函数须在 LVGL 任务上下文/持锁时调用。
typedef void (*ui_pet_exit_cb_t)(void *user);

// 建宠物屏并载入(记录当前屏为"上一屏",退出时载回)。type 为宠物类型(1..N)。须持 LVGL 锁。
// reveal=true 播放入住揭晓(倒计时+拉幕+弹落),仅在收到广播自动进入时用一次;
// reveal=false 直接显示静态宠物(从 dock 再次进入时用)。
void ui_pet_open(uint8_t type, bool reveal, ui_pet_exit_cb_t on_exit, void *user);

// 主动关闭(长按确定触发)。须持 LVGL 锁。
void ui_pet_close(void);

// 是否处于宠物屏。
bool ui_pet_is_active(void);
