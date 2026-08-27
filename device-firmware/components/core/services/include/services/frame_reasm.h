// components/core/services/include/services/frame_reasm.h
#pragma once
#include <stdint.h>

#define FRAME_MAX_PAYLOAD 256
#define FRAME_HDR_LEN     4          // [ver:1][type:1][len:2 小端]
#define FRAME_VER         1

typedef enum {
    FRAME_NEED_MORE = 0,   // 尚未凑齐一帧
    FRAME_READY     = 1,   // 凑齐一帧(见 out_type/out_payload/out_len)
    FRAME_ERR_VER   = -1,  // 版本号非法
    FRAME_ERR_LEN   = -2,  // 声明 len 超过 FRAME_MAX_PAYLOAD
} frame_status_t;

typedef struct {
    uint8_t  buf[FRAME_HDR_LEN + FRAME_MAX_PAYLOAD];
    int      have;         // 已累积字节数
    int      need;         // 整帧总长(头凑齐后确定); -1 表示头未凑齐
} frame_reasm_t;

void frame_reasm_reset(frame_reasm_t *r);

// 追加一段写入字节。返回 FRAME_READY 时,out_* 指向本帧;错误码见 frame_status_t。
// FRAME_READY / 错误 返回后内部自动 reset。约定单次 BLE 写不跨帧。
frame_status_t frame_reasm_push(frame_reasm_t *r, const uint8_t *data, int len,
                                uint8_t *out_type, const uint8_t **out_payload, int *out_len);
