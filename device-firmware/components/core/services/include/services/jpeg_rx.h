// components/core/services/include/services/jpeg_rx.h
#pragma once
#include <stdint.h>

#define JPEG_RX_OP_BEGIN 0x00   // [0x00][total_len:4 小端]
#define JPEG_RX_OP_DATA  0x01   // [0x01][chunk...]
#define JPEG_RX_OP_END   0x02   // [0x02]

// flash 落盘回调(由 platform 实现)。返回 0 成功;begin 返回 -1=超长 -2=失败;write/end 返回 -2=失败。
typedef struct {
    int  (*begin)(void *user, uint32_t total_len);
    int  (*write)(void *user, const uint8_t *d, int n);
    int  (*end)(void *user);
    void *user;
} jpeg_rx_sink_t;

typedef struct {
    int      receiving;   // 是否已 BEGIN
    uint32_t total;
    uint32_t written;
} jpeg_rx_t;

void jpeg_rx_init(jpeg_rx_t *r);

// 处理一个 type=0x02 帧。返回 notify 状态码:
//   0=OK/进行中  1=时序错(未BEGIN先DATA/END,或END长度不符,或未知op)  2=flash失败  3=超长
// END 收齐且成功 → *done=1(其余情形 *done=0)。
int  jpeg_rx_frame(jpeg_rx_t *r, const uint8_t *payload, int len,
                   const jpeg_rx_sink_t *sink, int *done);
