// components/core/services/include/services/sn_format.h —— MAC → SN 字符串(纯逻辑)
#pragma once
#include <stdint.h>

#define SN_STR_LEN 12   // 不含结尾 '\0'

// 把 6 字节 MAC 转成 SN 字符串:小写十六进制、无冒号、恒 12 字符 + '\0'。
// 顺序按 mac[0] 在最左(与 esptool.py read_mac 的打印顺序一致),不反转。
void sn_from_mac(const uint8_t mac[6], char out[SN_STR_LEN + 1]);
