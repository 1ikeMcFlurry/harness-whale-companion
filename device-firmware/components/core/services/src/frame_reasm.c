// components/core/services/src/frame_reasm.c —— BLE 分帧重组状态机(纯逻辑)
#include "services/frame_reasm.h"
#include <string.h>

void frame_reasm_reset(frame_reasm_t *r) { r->have = 0; r->need = -1; }

frame_status_t frame_reasm_push(frame_reasm_t *r, const uint8_t *data, int len,
                                uint8_t *out_type, const uint8_t **out_payload, int *out_len) {
    for (int i = 0; i < len; i++) {
        if (r->have < (int)sizeof r->buf) r->buf[r->have] = data[i];
        r->have++;

        if (r->need < 0 && r->have == FRAME_HDR_LEN) {
            if (r->buf[0] != FRAME_VER) { frame_reasm_reset(r); return FRAME_ERR_VER; }
            int plen = (int)r->buf[2] | ((int)r->buf[3] << 8);
            if (plen > FRAME_MAX_PAYLOAD) { frame_reasm_reset(r); return FRAME_ERR_LEN; }
            r->need = FRAME_HDR_LEN + plen;
        }
        if (r->need >= 0 && r->have >= r->need) {
            *out_type    = r->buf[1];
            *out_payload = r->buf + FRAME_HDR_LEN;
            *out_len     = r->need - FRAME_HDR_LEN;
            frame_reasm_reset(r);
            return FRAME_READY;
        }
    }
    return FRAME_NEED_MORE;
}
