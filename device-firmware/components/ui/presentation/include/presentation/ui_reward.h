// components/ui/presentation/include/presentation/ui_reward.h
#pragma once
#include <stdbool.h>

// token 加/扣奖励动画:礼盒弹出→盒盖飞起→金币旋转跳动 + 徽标(+N / −N)。
// 浮层实现(叠在 lv_layer_top,不切换当前屏);~1.4s 后自动清理。
// 须在持有 LVGL 锁时调用(涉及 LVGL 对象)。
//   add   : true=加分(绿色 +),false=扣分(红色 −)。
//   delta : 变化量的绝对值;<=0 时不显示数字,只显示符号动画。
void ui_reward_play(bool add, int delta);

// 是否正在播放奖励动画。
bool ui_reward_is_active(void);
