// components/core/services/include/services/adpcm.h —— IMA-ADPCM 单声道连续流解码(纯逻辑)
#pragma once
#include <stdint.h>

typedef struct { int16_t predictor; int16_t index; } adpcm_state_t;

void adpcm_state_reset(adpcm_state_t *st);           // predictor=0, index=0

// 解码 nbytes 字节(每字节 2 个 4bit 样本,低 nibble 在前)。
// out 需能容纳 2*nbytes 个 int16。返回写入的样本数(=2*nbytes)。
int  adpcm_decode(adpcm_state_t *st, const uint8_t *in, int nbytes, int16_t *out);
