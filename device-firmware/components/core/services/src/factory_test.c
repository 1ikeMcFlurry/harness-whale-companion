// components/core/services/src/factory_test.c —— 产测命令解析与判据(纯逻辑,无硬件依赖)
#include "services/factory_test.h"
#include <string.h>
#include <ctype.h>

// 大小写不敏感比较 a[0..n) 与以 NUL 结尾的 b,要求长度相等。
static bool ieq(const char *a, int n, const char *b) {
    int bl = (int)strlen(b);
    if (n != bl) return false;
    for (int i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

ft_cmd_t ft_parse(const char *line, int len) {
    if (!line || len <= 0) return FT_NONE;
    // 跳过前导空白
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    const char *p = line + i;
    int n = len - i;
    // 必须以 "AT+TEST" 开头(大小写不敏感)
    const char *pfx = "AT+TEST";
    int pl = 7;
    if (n < pl) return FT_NONE;
    for (int k = 0; k < pl; k++)
        if (tolower((unsigned char)p[k]) != tolower((unsigned char)pfx[k])) return FT_NONE;
    p += pl; n -= pl;
    // 去掉尾部空白
    while (n > 0 && (p[n-1] == ' ' || p[n-1] == '\t')) n--;

    if (n == 0) return FT_NONE;          // 光 "AT+TEST" 不成命令,交回原解析
    if (n == 1 && p[0] == '?') return FT_AUTO;
    if (p[0] != '=') return FT_NONE;
    const char *arg = p + 1; int al = n - 1;
    while (al > 0 && (arg[0] == ' ' || arg[0] == '\t')) { arg++; al--; }
    if (al <= 0) return FT_NONE;

    if (ieq(arg, al, "AUTO"))  return FT_AUTO;
    if (ieq(arg, al, "INFO"))  return FT_INFO;
    if (ieq(arg, al, "I2C"))   return FT_I2C;
    if (ieq(arg, al, "AUDIO")) return FT_AUDIO;
    if (ieq(arg, al, "BATT"))  return FT_BATT;
    if (ieq(arg, al, "BLE"))   return FT_BLE;
    if (ieq(arg, al, "ID"))    return FT_ID;
    if (ieq(arg, al, "DISP"))  return FT_DISP;
    if (ieq(arg, al, "BTN"))   return FT_BTN;
    return FT_NONE;                       // 未知子命令:交回原解析 → +ERR=unknown
}

bool ft_audio_pass(int ret, int tone_mag) {
    return ret == 0 && tone_mag >= FT_TONE_MAG_MIN;
}
bool ft_batt_pass(bool present, int soc) {
    return present && soc >= FT_BATT_SOC_MIN && soc <= FT_BATT_SOC_MAX;
}
bool ft_heap_pass(int free_heap) {
    return free_heap >= FT_HEAP_MIN;
}

const char *ft_cmd_name(ft_cmd_t c) {
    switch (c) {
        case FT_AUTO:  return "AUTO";
        case FT_INFO:  return "INFO";
        case FT_I2C:   return "I2C";
        case FT_AUDIO: return "AUDIO";
        case FT_BATT:  return "BATT";
        case FT_BLE:   return "BLE";
        case FT_ID:    return "ID";
        case FT_DISP:  return "DISP";
        case FT_BTN:   return "BTN";
        default:       return "NONE";
    }
}
