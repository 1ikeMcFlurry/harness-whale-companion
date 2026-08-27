// components/ui/presentation/include/presentation/ui_factory.h
#pragma once

// 产测屏幕自检:全屏纯色循环(红/绿/蓝/白/黑)+ 网格,供目视查坏点/背光/偏色。
// 叠在 lv_layer_top,每色 1s,约 5s 后自动清除,不影响下面的主界面。
// 须在持有 LVGL 锁时调用(涉及 LVGL 对象)。
void ui_factory_disp_test(void);
