// components/core/services/src/jpeg_rx.c —— JPG 分片子协议状态机(纯逻辑)
#include "services/jpeg_rx.h"

void jpeg_rx_init(jpeg_rx_t *r) { r->receiving = 0; r->total = 0; r->written = 0; }

int jpeg_rx_frame(jpeg_rx_t *r, const uint8_t *payload, int len,
                  const jpeg_rx_sink_t *sink, int *done) {
    if (done) *done = 0;
    if (len < 1) return 1;
    uint8_t op = payload[0];
    const uint8_t *body = payload + 1;
    int blen = len - 1;

    if (op == JPEG_RX_OP_BEGIN) {
        if (blen < 4) return 1;
        uint32_t total = (uint32_t)body[0] | ((uint32_t)body[1] << 8)
                       | ((uint32_t)body[2] << 16) | ((uint32_t)body[3] << 24);
        int rc = sink->begin(sink->user, total);
        if (rc == -1) { r->receiving = 0; return 3; }   // 超长
        if (rc < 0)   { r->receiving = 0; return 2; }   // flash 失败
        r->receiving = 1; r->total = total; r->written = 0;
        return 0;
    }
    if (op == JPEG_RX_OP_DATA) {
        if (!r->receiving) return 1;
        if (blen > 0) {
            if (sink->write(sink->user, body, blen) < 0) { r->receiving = 0; return 2; }
            r->written += (uint32_t)blen;
        }
        return 0;
    }
    if (op == JPEG_RX_OP_END) {
        if (!r->receiving) return 1;
        r->receiving = 0;
        if (r->written != r->total) return 1;           // 长度不符
        if (sink->end(sink->user) < 0) return 2;
        if (done) *done = 1;
        return 0;
    }
    return 1;   // 未知 op
}
