#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services/token_bcast.h"

#define PBB_FRAME_LEN 26
#define PBB_HDR_COMMON 0x34
#define PBB_HDR_NICKNAME 0x35
#define PBB_NICK_CHUNK 11u
#define PBB_NICK_MAX 47u
#define PBB_NICK_MAX_FRAGMENTS 5u
#define PBB_TIMEOUT_MS 3000u
#define PBB_SEEN_MAX 8u

#define PBB_FIELD_TOKEN     (1u << 0)
#define PBB_FIELD_TOKEN_MAX (1u << 1)
#define PBB_FIELD_TIME      (1u << 2)
#define PBB_FIELD_AVATAR    (1u << 3)
#define PBB_FIELD_ALL       (PBB_FIELD_TOKEN | PBB_FIELD_TOKEN_MAX | PBB_FIELD_TIME | PBB_FIELD_AVATAR)

typedef enum { PBB_IGNORE, PBB_MORE, PBB_COMMON_READY, PBB_NICKNAME_READY, PBB_ERROR } pbb_status_t;

typedef struct {
    uint16_t txn;
    uint8_t fields;
    uint32_t token;
    uint32_t token_max;
    uint32_t time_epoch;
    uint8_t avatar_id;
    uint8_t nickname[PBB_NICK_MAX];
    uint8_t nickname_len;
} pbb_result_t;

typedef struct {
    token_mac_fn mac;
    void *mac_user;
    struct { uint8_t type; uint16_t txn; } seen[PBB_SEEN_MAX];
    uint8_t seen_count;
    uint8_t seen_next;
    bool nick_active;
    uint16_t nick_txn;
    uint8_t nick_count;
    uint8_t nick_total;
    uint8_t nick_received;
    uint32_t nick_last_ms;
    uint8_t nickname[55];
} profile_batch_bcast_t;

void profile_batch_bcast_init(profile_batch_bcast_t *state, token_mac_fn mac, void *mac_user);
pbb_status_t profile_batch_bcast_feed(profile_batch_bcast_t *state,
                                      const uint8_t frame[PBB_FRAME_LEN],
                                      uint32_t now_ms, pbb_result_t *out);
// 只有业务副作用全部成功后才提交去重键；失败不提交，允许发送端同 txn 重试。
void profile_batch_bcast_commit(profile_batch_bcast_t *state, uint8_t type, uint16_t txn);
