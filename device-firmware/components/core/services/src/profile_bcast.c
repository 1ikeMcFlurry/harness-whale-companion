#include "services/profile_bcast.h"

#include <string.h>

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reset_active(profile_bcast_t *state) {
    state->active = false;
    state->received = 0;
    state->fragment_count = 0;
    state->total_len = 0;
    memset(state->data, 0, sizeof(state->data));
}

static bool field_shape(uint8_t field, uint8_t total) {
    switch (field) {
        case PB_FIELD_TOKEN:
        case PB_FIELD_TOKEN_MAX:
        case PB_FIELD_TIME:
            return total == 4;
        case PB_FIELD_NICKNAME:
            return total >= 1 && total <= 47;
        case PB_FIELD_AVATAR_NAME:
            return total >= 1 && total <= 15;
        default:
            return false;
    }
}

static bool valid_utf8(const uint8_t *s, uint8_t len) {
    uint8_t i = 0;
    while (i < len) {
        uint8_t c = s[i++];
        if (c == 0) return false;
        if (c < 0x80) continue;
        unsigned need;
        uint32_t value;
        uint32_t minimum;
        if ((c & 0xe0) == 0xc0) { need=1; value=c&0x1f; minimum=0x80; }
        else if ((c & 0xf0) == 0xe0) { need=2; value=c&0x0f; minimum=0x800; }
        else if ((c & 0xf8) == 0xf0) { need=3; value=c&0x07; minimum=0x10000; }
        else return false;
        if ((unsigned)(len - i) < need) return false;
        while (need--) {
            uint8_t next = s[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

static bool valid_value(uint8_t field, const uint8_t *data, uint8_t len) {
    if (field == PB_FIELD_TOKEN) return len == 4;
    if (field == PB_FIELD_TOKEN_MAX) return len == 4 && get32(data) >= 1;
    if (field == PB_FIELD_TIME) return len == 4 && get32(data) > 0;
    if (field == PB_FIELD_NICKNAME) return valid_utf8(data, len);
    if (field == PB_FIELD_AVATAR_NAME) {
        for (uint8_t i = 0; i < len; ++i) {
            uint8_t c = data[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-')) return false;
        }
        return true;
    }
    return false;
}

static bool seen_has(const profile_bcast_t *state, uint8_t field, uint16_t txn) {
    for (uint8_t i = 0; i < state->seen_count; ++i) {
        if (state->seen[i].field == field && state->seen[i].txn == txn) return true;
    }
    return false;
}

static void seen_put(profile_bcast_t *state, uint8_t field, uint16_t txn) {
    state->seen[state->seen_next].field = field;
    state->seen[state->seen_next].txn = txn;
    state->seen_next = (uint8_t)((state->seen_next + 1) % PROFILE_BCAST_SEEN_MAX);
    if (state->seen_count < PROFILE_BCAST_SEEN_MAX) state->seen_count++;
}

void profile_bcast_init(profile_bcast_t *state, const uint8_t target[6],
                        token_mac_fn mac, void *mac_user) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    if (target != NULL) memcpy(state->self_target, target, 6);
    state->mac = mac;
    state->mac_user = mac_user;
}

pb_status_t profile_bcast_feed(profile_bcast_t *state, const uint8_t frame[26],
                               uint32_t now_ms, pb_result_t *out) {
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (state == NULL || frame == NULL || out == NULL) return PB_ERROR;
    if (frame[0] != 'H' || frame[1] != 'B' || frame[2] != 0x23) return PB_IGNORE;
    if (memcmp(frame + 4, state->self_target, 6) != 0) return PB_IGNORE;
    uint8_t calculated[8];
    if (state->mac == NULL || state->mac(frame, 18, calculated, state->mac_user) != 0 ||
        memcmp(calculated, frame + 18, 8) != 0) return PB_ERROR;

    uint8_t field = frame[3];
    uint16_t txn = get16(frame + 10);
    uint8_t index = frame[12];
    uint8_t count = frame[13];
    uint8_t total = frame[14];
    if (!field_shape(field, total) || count == 0 || count > 16 || index >= count ||
        count != (uint8_t)((total + 2u) / 3u)) return PB_ERROR;
    uint8_t used = (index == count - 1) ? (uint8_t)(total - index * 3u) : 3u;
    if (used == 0 || used > 3) return PB_ERROR;
    for (uint8_t i = used; i < 3; ++i) if (frame[15 + i] != 0) return PB_ERROR;

    if (seen_has(state, field, txn)) return PB_IGNORE;
    if (state->active && (uint32_t)(now_ms - state->last_ms) >= PROFILE_BCAST_TIMEOUT_MS)
        reset_active(state);
    if (!state->active || state->field != field || state->txn != txn) {
        reset_active(state);
        state->active = true;
        state->field = field;
        state->txn = txn;
        state->fragment_count = count;
        state->total_len = total;
    } else if (state->fragment_count != count || state->total_len != total) {
        reset_active(state);
        return PB_ERROR;
    }
    state->last_ms = now_ms;

    uint16_t bit = (uint16_t)(1u << index);
    size_t offset = (size_t)index * 3u;
    if ((state->received & bit) != 0) {
        if (memcmp(state->data + offset, frame + 15, 3) != 0) {
            reset_active(state);
            return PB_ERROR;
        }
        return PB_MORE;
    }
    memcpy(state->data + offset, frame + 15, 3);
    state->received |= bit;
    uint16_t complete = count == 16 ? 0xffffu : (uint16_t)((1u << count) - 1u);
    if (state->received != complete) return PB_MORE;

    if (!valid_value(field, state->data, total)) {
        reset_active(state);
        return PB_ERROR;
    }
    out->field = (pb_field_t)field;
    out->txn = txn;
    out->len = total;
    memcpy(out->data, state->data, total);
    seen_put(state, field, txn);
    reset_active(state);
    return PB_COMPLETE;
}
