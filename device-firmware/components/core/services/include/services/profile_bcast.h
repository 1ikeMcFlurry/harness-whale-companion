#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services/token_bcast.h"

#define PROFILE_BCAST_LEN 26
#define PROFILE_BCAST_TIMEOUT_MS 3000u
#define PROFILE_BCAST_MAX_DATA 47u
#define PROFILE_BCAST_SEEN_MAX 8u

typedef enum {
    PB_FIELD_TOKEN = 1,
    PB_FIELD_TOKEN_MAX = 2,
    PB_FIELD_TIME = 3,
    PB_FIELD_NICKNAME = 4,
    PB_FIELD_AVATAR_NAME = 5,
} pb_field_t;

typedef enum { PB_MORE, PB_COMPLETE, PB_IGNORE, PB_ERROR } pb_status_t;

typedef struct {
    pb_field_t field;
    uint8_t data[PROFILE_BCAST_MAX_DATA];
    uint8_t len;
    uint16_t txn;
} pb_result_t;

typedef struct {
    uint8_t self_target[6];
    token_mac_fn mac;
    void *mac_user;
    bool active;
    uint8_t field;
    uint16_t txn;
    uint8_t fragment_count;
    uint8_t total_len;
    uint16_t received;
    uint32_t last_ms;
    uint8_t data[48];
    struct { uint8_t field; uint16_t txn; } seen[PROFILE_BCAST_SEEN_MAX];
    uint8_t seen_count;
    uint8_t seen_next;
} profile_bcast_t;

void profile_bcast_init(profile_bcast_t *state, const uint8_t target[6],
                        token_mac_fn mac, void *mac_user);
pb_status_t profile_bcast_feed(profile_bcast_t *state,
                               const uint8_t frame[PROFILE_BCAST_LEN],
                               uint32_t now_ms, pb_result_t *out);
