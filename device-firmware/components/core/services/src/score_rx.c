// components/core/services/src/score_rx.c —— type=0x03 分片累积到 RAM(纯逻辑)
#include "services/score_rx.h"
#include <string.h>

void score_rx_init(score_rx_t *r, uint8_t *buf, int cap) {
    r->receiving = 0; r->total = 0; r->written = 0; r->buf = buf; r->cap = cap;
}

int score_rx_frame(score_rx_t *r, const uint8_t *payload, int len, int *done) {
    if (done) *done = 0;
    if (len < 1) return 1;
    uint8_t op = payload[0];
    const uint8_t *b = payload + 1;
    int bl = len - 1;

    if (op == SCORE_OP_BEGIN) {
        if (bl < 2) return 1;
        uint32_t total = (uint32_t)b[0] | ((uint32_t)b[1] << 8);
        if (total > (uint32_t)r->cap) { r->receiving = 0; return 3; }
        r->receiving = 1; r->total = total; r->written = 0;
        return 0;
    }
    if (op == SCORE_OP_DATA) {
        if (!r->receiving) return 1;
        if (bl > 0) {
            if (r->written + (uint32_t)bl > (uint32_t)r->cap) { r->receiving = 0; return 3; }
            memcpy(r->buf + r->written, b, (size_t)bl);
            r->written += (uint32_t)bl;
        }
        return 0;
    }
    if (op == SCORE_OP_END) {
        if (!r->receiving) return 1;
        r->receiving = 0;
        if (r->written != r->total) return 1;
        if (done) *done = 1;
        return 0;
    }
    return 1;
}
