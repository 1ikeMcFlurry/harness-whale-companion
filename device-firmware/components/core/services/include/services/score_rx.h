// components/core/services/include/services/score_rx.h
#pragma once
#include <stdint.h>

#define SCORE_OP_BEGIN 0x00   // [0x00][total_len:2 小端]
#define SCORE_OP_DATA  0x01   // [0x01][chunk...]
#define SCORE_OP_END   0x02   // [0x02]

typedef struct { int receiving; uint32_t total, written; uint8_t *buf; int cap; } score_rx_t;

void score_rx_init(score_rx_t *r, uint8_t *buf, int cap);
int  score_rx_frame(score_rx_t *r, const uint8_t *payload, int len, int *done);
