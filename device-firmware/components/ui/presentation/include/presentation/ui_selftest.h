// components/ui/presentation/include/presentation/ui_selftest.h
// 设备端产测自检界面:结果/进度/交互提示全画在设备屏上,工人只碰设备。
// 所有函数须在持有 LVGL 锁时调用。
#pragma once
#include <stdbool.h>

// 自检项状态
enum { UI_ST_RUN = 0, UI_ST_PASS = 1, UI_ST_FAIL = 2 };

// 建自检屏(标题 + SN + 空的结果列表 + 底部提示区),载入为当前屏。
void ui_selftest_open(const char *sn);

// 更新/新增一行自检项:name 为项名(如 "AUDIO"),state 见上枚举。
void ui_selftest_set_item(const char *name, int state);

// 同上,但右侧状态显示自定义短文本 txt(而非 OK/NG),颜色仍按 state 分。
// 用于需要展示细节的项,如 ID 行显示 cardid 字段存在标记 "SKPH"/"S--H"(缺字段用 '-')。
// txt 传 NULL 时等价于 ui_selftest_set_item。txt 应为 ASCII 短串(≤8 字符)。
void ui_selftest_set_item_text(const char *name, int state, const char *txt);

// 底部大字提示(交互步骤用,如"按 确定=通过 上=不通过");NULL 清空。
void ui_selftest_prompt(const char *utf8);

// 醒目显示电池剩余电量(右上角大字,按电量分色);soc<0 显示 "--"。
void ui_selftest_battery(int soc);

// 全屏色带图样(屏幕自检):show=true 铺满 红/绿/蓝/白/黑 + 网格并把提示置顶;false 撤除。
void ui_selftest_pattern(bool show);

// 全屏纯色(屏幕自检逐色检查用):铺满整屏为 rgb(0xRRGGBB),提示压在最上。
// 复用与 pattern 同一层,清除用 ui_selftest_pattern(false)。
#include <stdint.h>
void ui_selftest_color(uint32_t rgb);

// 终判:整屏大 ✓(绿)/ ✗(红)+ 文案。
void ui_selftest_result(bool pass);

// 关闭自检屏(一般产测不需要,设备测完即拔电)。
void ui_selftest_close(void);
