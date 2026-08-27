#include "services/profile_batch_bcast.h"

#include <string.h>

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool valid_utf8(const uint8_t *s, uint8_t len) {
    uint8_t i = 0;
    while (i < len) {
        uint8_t c = s[i++];
        if (c == 0) return false;
        if (c < 0x80) continue;
        unsigned need;
        uint32_t value, minimum;
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

static bool seen_has(const profile_batch_bcast_t *s, uint8_t type, uint16_t txn) {
    for (uint8_t i = 0; i < s->seen_count; ++i)
        if (s->seen[i].type == type && s->seen[i].txn == txn) return true;
    return false;
}

static void seen_put(profile_batch_bcast_t *s, uint8_t type, uint16_t txn) {
    s->seen[s->seen_next].type = type;
    s->seen[s->seen_next].txn = txn;
    s->seen_next = (uint8_t)((s->seen_next + 1u) % PBB_SEEN_MAX);
    if (s->seen_count < PBB_SEEN_MAX) s->seen_count++;
}

static void reset_nickname(profile_batch_bcast_t *s) {
    s->nick_active = false;
    s->nick_received = 0;
    s->nick_count = 0;
    s->nick_total = 0;
    memset(s->nickname, 0, sizeof s->nickname);
}

void profile_batch_bcast_init(profile_batch_bcast_t *s, token_mac_fn mac, void *mac_user) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->mac = mac;
    s->mac_user = mac_user;
}

void profile_batch_bcast_commit(profile_batch_bcast_t *s, uint8_t type, uint16_t txn) {
    if (!s || (type != PBB_HDR_COMMON && type != PBB_HDR_NICKNAME) || seen_has(s, type, txn)) return;
    seen_put(s, type, txn);
}

pbb_status_t profile_batch_bcast_feed(profile_batch_bcast_t *s, const uint8_t frame[26],
                                      uint32_t now_ms, pbb_result_t *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!s || !frame || !out) return PBB_ERROR;
    if (frame[0] != 'H' || frame[1] != 'B' ||
        (frame[2] != PBB_HDR_COMMON && frame[2] != PBB_HDR_NICKNAME)) return PBB_IGNORE;

    uint8_t calc[8];
    // 无 target 字段，非目标设备验签失败是场地常态，必须静默忽略而非报协议错误。
    if (!s->mac || s->mac(frame, 18, calc, s->mac_user) != 0 ||
        memcmp(calc, frame + 18, 8) != 0) return PBB_IGNORE;

    uint8_t type = frame[2];
    uint16_t txn = get16(frame + 3);
    if (seen_has(s, type, txn)) return PBB_IGNORE;

    if (type == PBB_HDR_COMMON) {
        uint8_t fields = frame[5];
        if (fields == 0 || (fields & ~PBB_FIELD_ALL) != 0 || frame[17] != 0) return PBB_ERROR;
        uint32_t token = get24(frame + 6);
        uint32_t token_max = get24(frame + 9);
        uint32_t epoch = get32(frame + 12);
        uint8_t avatar = frame[16];
        if (!(fields & PBB_FIELD_TOKEN) && token != 0) return PBB_ERROR;
        if (!(fields & PBB_FIELD_TOKEN_MAX) && token_max != 0) return PBB_ERROR;
        if (!(fields & PBB_FIELD_TIME) && epoch != 0) return PBB_ERROR;
        if (!(fields & PBB_FIELD_AVATAR) && avatar != 0) return PBB_ERROR;
        if ((fields & PBB_FIELD_TOKEN_MAX) && token_max == 0) return PBB_ERROR;
        if ((fields & PBB_FIELD_TIME) && epoch == 0) return PBB_ERROR;
        if ((fields & PBB_FIELD_AVATAR) && avatar == 0) return PBB_ERROR;
        out->txn = txn;
        out->fields = fields;
        out->token = token;
        out->token_max = token_max;
        out->time_epoch = epoch;
        out->avatar_id = avatar;
        return PBB_COMMON_READY;
    }

    uint8_t index = frame[5] & 0x0f;
    uint8_t count = (uint8_t)((frame[5] >> 4) + 1u);
    uint8_t total = frame[6];
    if (total == 0 || total > PBB_NICK_MAX || count == 0 || count > PBB_NICK_MAX_FRAGMENTS ||
        index >= count || count != (uint8_t)((total + PBB_NICK_CHUNK - 1u) / PBB_NICK_CHUNK))
        return PBB_ERROR;
    uint8_t used = index == count - 1u
                 ? (uint8_t)(total - index * PBB_NICK_CHUNK) : PBB_NICK_CHUNK;
    for (uint8_t i = used; i < PBB_NICK_CHUNK; ++i)
        if (frame[7 + i] != 0) return PBB_ERROR;

    if (s->nick_active && (uint32_t)(now_ms - s->nick_last_ms) >= PBB_TIMEOUT_MS)
        reset_nickname(s);
    if (!s->nick_active || s->nick_txn != txn) {
        reset_nickname(s);
        s->nick_active = true;
        s->nick_txn = txn;
        s->nick_count = count;
        s->nick_total = total;
    } else if (s->nick_count != count || s->nick_total != total) {
        reset_nickname(s);
        return PBB_ERROR;
    }
    s->nick_last_ms = now_ms;
    uint8_t bit = (uint8_t)(1u << index);
    size_t offset = (size_t)index * PBB_NICK_CHUNK;
    if (s->nick_received & bit) {
        if (memcmp(s->nickname + offset, frame + 7, PBB_NICK_CHUNK) != 0) {
            reset_nickname(s);
            return PBB_ERROR;
        }
        return PBB_MORE;
    }
    memcpy(s->nickname + offset, frame + 7, PBB_NICK_CHUNK);
    s->nick_received |= bit;
    if (s->nick_received != (uint8_t)((1u << count) - 1u)) return PBB_MORE;
    if (!valid_utf8(s->nickname, total)) {
        reset_nickname(s);
        return PBB_ERROR;
    }
    out->txn = txn;
    out->nickname_len = total;
    memcpy(out->nickname, s->nickname, total);
    reset_nickname(s);
    return PBB_NICKNAME_READY;
}
