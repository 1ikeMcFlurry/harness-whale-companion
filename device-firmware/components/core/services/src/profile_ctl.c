// components/core/services/src/profile_ctl.c —— profile 默认值 + 定长 blob 序列化(纯逻辑)
#include "services/profile_ctl.h"
#include <string.h>

typedef struct {
    char name[48];
    char role[40];
    char subtitle[56];
    int battery;
    int level;
    int xp;
    int xp_max;
    bool online;
    int token;
    int token_max;
    int sleep_min;
    int game_total;
    int game_best;
    uint8_t img_mode;
    int volume;
    bool pet_enabled;
    uint8_t pet_type;
} profile_v13_t;

static void copy_trunc(char *dst, int cap, const char *src, int len) {
    if (len < 0) len = 0;
    if (len > cap - 1) len = cap - 1;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
}

void profile_ctl_init(profile_data_t *d) {
    memset(d, 0, sizeof(*d));
    copy_trunc(d->name,     sizeof d->name,     "TraeWork", 8);
    copy_trunc(d->role,     sizeof d->role,     "NETRUNNER", 9);
    copy_trunc(d->subtitle, sizeof d->subtitle, "DATA STALKER", 12);
    d->battery = 82; d->level = 1; d->xp = 0; d->xp_max = 100; d->online = true;
    d->token = 0; d->token_max = TOKEN_MAX_DEFAULT;
    d->sleep_min = SLEEP_MIN_DEFAULT;
    d->game_total = 0; d->game_best = 0;
    d->img_mode = IMG_MODE_AVATAR;   // 默认头像位显示
    d->volume = VOLUME_DEFAULT;      // codec 输出音量默认 95%
    d->pet_enabled = false; d->pet_type = 0;   // 宠物默认关闭(由 token 广播 op=0x05 开启)
    copy_trunc(d->avatar_name, sizeof d->avatar_name, "default", 7);
}

int profile_serialize(const profile_data_t *d, uint8_t *buf, int cap) {
    if (cap < PROFILE_BLOB_SIZE) return -1;
    buf[0] = PROFILE_BLOB_VER;
    memcpy(buf + 1, d, sizeof(*d));
    return PROFILE_BLOB_SIZE;
}

int profile_deserialize(profile_data_t *d, const uint8_t *buf, int len) {
    if (d == NULL || buf == NULL || len < 1) return -1;
    if (buf[0] == PROFILE_BLOB_VER && len == PROFILE_BLOB_SIZE) {
        memcpy(d, buf + 1, sizeof(*d));
        return 0;
    }
    if (buf[0] != 13 || len != 1 + (int)sizeof(profile_v13_t)) return -1;
    profile_v13_t old;
    memcpy(&old, buf + 1, sizeof(old));
    profile_ctl_init(d);
    memcpy(d->name, old.name, sizeof old.name);
    memcpy(d->role, old.role, sizeof old.role);
    memcpy(d->subtitle, old.subtitle, sizeof old.subtitle);
    d->battery = old.battery;
    d->level = old.level;
    d->xp = old.xp;
    d->xp_max = old.xp_max;
    d->online = old.online;
    d->token = old.token;
    d->token_max = old.token_max;
    d->sleep_min = old.sleep_min;
    d->game_total = old.game_total;
    d->game_best = old.game_best;
    d->img_mode = old.img_mode;
    d->volume = old.volume;
    d->pet_enabled = old.pet_enabled;
    d->pet_type = old.pet_type;
    return 0;
}
