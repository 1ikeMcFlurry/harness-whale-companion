// components/core/services/include/services/audio_clip.h —— 片段枚举 + 容器头(共享)
#pragma once
#include <stdint.h>

typedef enum { CLIP_BOOT=0, CLIP_CONNECT=1, CLIP_GAMEOVER=2, CLIP_COUNT } clip_id_t;

#define AUDIO_CLIP_MAGIC 0x31504441u   // "ADP1" 小端('A'=0x41,'D'=0x44,'P'=0x50,'1'=0x31)
// 容器头(12 字节,小端);其后紧跟 ADPCM nibble 流
typedef struct {
    uint32_t magic;        // AUDIO_CLIP_MAGIC
    uint16_t sample_rate;  // 16000
    uint8_t  channels;     // 1
    uint8_t  reserved;
    uint32_t num_samples;  // 精确样本数
} audio_clip_hdr_t;
