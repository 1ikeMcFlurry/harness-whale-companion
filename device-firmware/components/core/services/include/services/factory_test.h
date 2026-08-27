// components/core/services/include/services/factory_test.h —— 产测命令解析与判据(纯逻辑)
#pragma once
#include <stdbool.h>

// 产测子命令。解析只认以 "AT+TEST" 开头的行;其余一律 FT_NONE(交回 AT+CARDID 解析)。
typedef enum {
    FT_NONE = 0,   // 非产测命令
    FT_AUTO,       // AT+TEST?  或  AT+TEST=AUTO  —— 跑全部自动项
    FT_INFO,       // AT+TEST=INFO   固件版本/SN/堆/复位原因
    FT_I2C,        // AT+TEST=I2C    ES8311(0x18)/CW2017(0x63) 在位
    FT_AUDIO,      // AT+TEST=AUDIO  声学回环(播音→录音→测峰值)
    FT_BATT,       // AT+TEST=BATT   CW2017 SOC/电压
    FT_BLE,        // AT+TEST=BLE    BLE 就绪 + MAC/SN
    FT_ID,         // AT+TEST=ID     身份已烧录 + pk 指纹
    FT_DISP,       // AT+TEST=DISP   屏幕色块扫描(供目视)
    FT_BTN,        // AT+TEST=BTN    逐键捕获(上/下/确定)
} ft_cmd_t;

// —— PASS 判据阈值(真机可标定后调整)——
// 音频判据用"声学回环在 1kHz 处的能量(tone_mag)":麦克风听自己扬声器的音,该频点有能量
// 才算扬声器+麦克风都好。已真机标定(降幅 amp=2000 不削顶):好板 tone_mag≈1329,
// 拔喇叭≈2(掉到基线,证明确为声学而非电气串扰),堵麦≈390。阈值取 300:好板 4.4× 余量,
// 喇叭坏/麦克风死(≈2)判 FAIL。换板/改音量后请复测重标。
#define FT_TONE_MAG_MIN   300      // 回环 1kHz 能量下限:低于此判扬声器不出声或麦克风通路断
#define FT_HEAP_MIN       20000    // 空闲堆下限(低于此说明 RAM 余量异常)
#define FT_BATT_SOC_MIN   1
#define FT_BATT_SOC_MAX   100

// 解析一行(len 为不含结尾符的长度)。前缀大小写不敏感。
ft_cmd_t ft_parse(const char *line, int len);

// —— 纯逻辑判据(便于单测)——
bool ft_audio_pass(int ret, int tone_mag);   // ret==0 且回环 1kHz 能量达标
bool ft_batt_pass(bool present, int soc);     // 在位且 SOC∈[1,100]
bool ft_heap_pass(int free_heap);             // 堆余量达标

// 子命令 → 报告用短名("AUTO"/"I2C"/...)。
const char *ft_cmd_name(ft_cmd_t c);
