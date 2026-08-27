// components/presentation/include/presentation/ui_profile.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

// 构建主页(头像 + 昵称 + token 进度条 + 底部 dock)。
// 须在持有 LVGL 锁的情况下调用(由组装层负责加锁)。
void ui_profile_create(void);

// 返回内置中文字体指针(void* 形式,实为 const lv_font_t*),供其它层叠加中文文字时复用,
// 避免调用方直接依赖 presentation 的字体符号 / lvgl 类型。
const void *ui_font_cn16(void);
const void *ui_font_cn24(void);

// 运行时更新各字段。注意:这些函数会操作 LVGL 对象,
// 若从 LVGL 任务之外调用,请用 platform_lvgl_lock()/unlock() 包起来。
void ui_profile_set_name(const char *name);         // 昵称
// 电量:5 档电池符号 + 百分比;level 1..5,<=1 红 / ==2 黄 / 其余白。
void ui_profile_set_battery(int percent, int level);
// token 余额 + 上限:进度条 = token/token_max,文本 "token / token_max"。
void ui_profile_set_token(int token, int token_max);

// 宠物功能开关:enabled 显示 dock 的 PET 磁贴(并重排/入导航),否则隐藏。须在 LVGL 锁内调用。
void ui_profile_set_pet(bool enabled, uint8_t type);
// 在头像位显示图片(contain 缩放,常驻);dsc=NULL 则清空头像位(留空)。须在 LVGL 锁内调用。
// dsc 实为 const lv_image_dsc_t*,用 void* 避免此公共头依赖 lvgl。
void ui_profile_show_image(const void *dsc);

// dock 某项被"确定"时回调(item_id: 2游戏 3看图)
typedef void (*ui_action_cb_t)(int item_id, void *user);
void ui_profile_set_on_action(ui_action_cb_t cb, void *user);
