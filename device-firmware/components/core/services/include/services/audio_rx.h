// components/core/services/include/services/audio_rx.h —— 0x05 片段接收(带 sink,镜像 jpeg_rx);
// 以及 0x04 流 BEGIN 的解析辅助(纯逻辑)
#pragma once
#include <stdint.h>

#define AUDIO_OP_BEGIN 0x00
#define AUDIO_OP_DATA  0x01
#define AUDIO_OP_END   0x02

// 片段落盘回调(platform 实现:写临时文件,end 时原子 rename)。
// begin 返回 0=成功 -1=clip_id非法/超长 -2=打开失败;write/end 返回 0/-2。
typedef struct {
    int  (*begin)(void *user, int clip_id, uint32_t total_len);
    int  (*write)(void *user, const uint8_t *d, int n);
    int  (*end)(void *user);
    void *user;
} audio_clip_sink_t;

typedef struct { int receiving; int clip_id; uint32_t total, written; } audio_clip_rx_t;

void audio_clip_rx_init(audio_clip_rx_t *r);
// 处理一个 type=0x05 帧。返回 notify 状态码:0=OK/进行中 1=时序错 2=落盘失败 3=非法/超长。
// END 收齐且成功 → *done=1。BEGIN 载荷: [0x00][clip_id u8][total_len u32 小端]。
// (total 为 u32:音频分区 512KB,u16 会把片段限死在 64KB/~8s,不够放开机音乐。)
int  audio_clip_rx_frame(audio_clip_rx_t *r, const uint8_t *payload, int len,
                         const audio_clip_sink_t *sink, int *done);
