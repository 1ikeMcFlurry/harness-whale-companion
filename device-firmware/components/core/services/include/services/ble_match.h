// components/core/services/include/services/ble_match.h —— 纯逻辑,无 NimBLE/LVGL
#pragma once
#include <stddef.h>   // NULL
#include <stdint.h>
#include <stdbool.h>

// 目标广播的厂商数据前缀:公司 ID 0xFFFF(小端 FF FF) + 魔数 "HB"(0x48 0x42)。
#define BLE_MATCH_PREFIX_0 0xFF
#define BLE_MATCH_PREFIX_1 0xFF
#define BLE_MATCH_PREFIX_2 0x48   // 'H'
#define BLE_MATCH_PREFIX_3 0x42   // 'B'
#define BLE_MATCH_PREFIX_LEN 4

// token 广播的第 5 字节:高 4 位=版本(1),低 4 位=类型(2)。心跳广播没有这个字节。
#define BLE_MATCH_HDR_TOKEN 0x12
#define BLE_MATCH_HDR_PROFILE 0x23
#define BLE_MATCH_HDR_PROFILE_COMMON 0x34
#define BLE_MATCH_HDR_PROFILE_NICKNAME 0x35

typedef enum {
    BLE_MATCH_NONE = 0,    // 不是我们的广播
    BLE_MATCH_HEARTBEAT,   // 心跳(爱心屏)
    BLE_MATCH_TOKEN,       // token 余额消息
    BLE_MATCH_PROFILE,     // v2 档案字段分片
    BLE_MATCH_PROFILE_BATCH, // v3 常用字段一包 / 昵称专用分片
} ble_match_kind_t;

// mfg = 广播里 Manufacturer Specific Data 的原始字节(含公司 ID 两字节),len 为其长度。
//   前 4 字节不是 FF FF 'H' 'B' ........... NONE
//   len >= 5 且 mfg[4] == 0x12 ............ TOKEN
//   否则 .................................. HEARTBEAT
//
// 心跳与 token 共用前 4 字节,必须靠 mfg[4] 区分:否则每条 token 广播都会被
// 当成心跳去触发爱心屏跳动。心跳发送方须只发 4 字节,或保证第 5 字节不是 0x12。
ble_match_kind_t ble_match_classify(const uint8_t *mfg, int len);

// 兼容包装,等价于 classify()==BLE_MATCH_HEARTBEAT。
bool ble_match_is_heartbeat(const uint8_t *mfg, int len);
