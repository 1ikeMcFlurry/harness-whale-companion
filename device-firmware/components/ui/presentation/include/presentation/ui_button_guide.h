// 开机外围按键指引页。所有函数须在持有 LVGL 锁时调用。
#pragma once

// 显示指引页并记住当前页面。
void ui_button_guide_open(void);

// 返回进入指引前的页面并销毁指引页。
void ui_button_guide_close(void);
