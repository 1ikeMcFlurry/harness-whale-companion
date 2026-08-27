// components/core/services/src/config_json.c —— cJSON 部分更新 profile(纯逻辑)
#include "services/config_json.h"
#include "cJSON.h"
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// UTF-8 安全截断:超长时回退到字符边界,避免把一个汉字切成半个变乱码。
static void copy_trunc(char *dst, int cap, const char *src) {
    int n = (int)strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        // UTF-8 续字节形如 10xxxxxx;若切点落在续字节上,往前退到字符起始
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

// 取字符串键:存在且类型为字符串 → 回填 → 返回 1
static int take_str(cJSON *o, const char *k, char *dst, int cap, bool *flag) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsString(it) || it->valuestring == NULL) return 0;
    copy_trunc(dst, cap, it->valuestring);
    if (flag) *flag = true;
    return 1;
}

static bool valid_avatar_name(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n > 15) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) return false;
    }
    return true;
}

int config_json_apply(profile_data_t *d, const char *json, int len, cfg_changed_t *chg) {
    cfg_changed_t local = {0};
    if (chg) *chg = local;
    cJSON *root = cJSON_ParseWithLength(json, (size_t)len);
    if (!root) return -1;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }

    int n = 0;
    cfg_changed_t c = {0};

    // `nickname` is the canonical key.  `name` remains a compatibility alias,
    // but must not overwrite nickname when both are present.
    if (take_str(root, "nickname", d->name, (int)sizeof d->name, &c.name)) n++;
    else if (take_str(root, "name", d->name, (int)sizeof d->name, &c.name)) n++;
    if (take_str(root, "role",     d->role,     (int)sizeof d->role,     &c.role))     n++;
    if (take_str(root, "subtitle", d->subtitle, (int)sizeof d->subtitle, &c.subtitle)) n++;

    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(root, "avatar_name");
    if (cJSON_IsString(avatar) && avatar->valuestring != NULL &&
        valid_avatar_name(avatar->valuestring)) {
        copy_trunc(d->avatar_name, (int)sizeof d->avatar_name, avatar->valuestring);
        c.avatar_name = true;
        n++;
    }

    cJSON *it;
    it = cJSON_GetObjectItemCaseSensitive(root, "battery");
    if (cJSON_IsNumber(it)) { d->battery = clampi((int)it->valuedouble, 0, 100); c.battery = true; n++; }

    it = cJSON_GetObjectItemCaseSensitive(root, "level");
    if (cJSON_IsNumber(it)) { d->level = clampi((int)it->valuedouble, 0, 999); c.level = true; n++; }
    it = cJSON_GetObjectItemCaseSensitive(root, "xp");
    if (cJSON_IsNumber(it)) { int v=(int)it->valuedouble; d->xp = v<0?0:v; c.level = true; n++; }
    it = cJSON_GetObjectItemCaseSensitive(root, "xp_max");
    if (cJSON_IsNumber(it)) { int v=(int)it->valuedouble; d->xp_max = v<1?1:v; c.level = true; n++; }

    it = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (cJSON_IsNumber(it)) {
        double v = it->valuedouble;
        if (v < 0) v = 0;
        if (v > 1e9) v = 1e9;
        d->token = (int)v; c.token = true; n++;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "online");
    if (cJSON_IsBool(it)) { d->online = cJSON_IsTrue(it); c.online = true; n++; }

    // 时间同步:不写 profile(不持久化),仅上报 epoch 让上层设系统时钟(RTC)。
    // 取值用 valuedouble 以容纳 >2^31 的秒数(int 会溢出)。
    it = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (cJSON_IsNumber(it) && it->valuedouble > 0) {
        c.time = true; c.time_epoch = (int64_t)it->valuedouble; n++;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "token_max");
    if (cJSON_IsNumber(it)) {
        double v = it->valuedouble;
        if (v < 1) v = 1;
        if (v > 1e9) v = 1e9;
        d->token_max = (int)v; c.token_max = true; n++;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "sleep_min");
    if (cJSON_IsNumber(it)) {
        int v = (int)it->valuedouble;
        if (v < 0) v = 0;
        if (v > SLEEP_MIN_MAX) v = SLEEP_MIN_MAX;
        d->sleep_min = v; c.sleep_min = true; n++;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "volume");
    if (cJSON_IsNumber(it)) {
        int v = (int)it->valuedouble;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        d->volume = v; c.volume = true; n++;
    }

    // 游戏积分清零:动作键,不写 profile。小程序读走 …0013 后下发此键清零工牌端。
    it = cJSON_GetObjectItemCaseSensitive(root, "game_clear");
    if ((cJSON_IsBool(it) && cJSON_IsTrue(it)) || (cJSON_IsNumber(it) && it->valuedouble != 0)) {
        c.game_clear = true; n++;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "img_mode");
    if (cJSON_IsString(it) && it->valuestring) {
        if (strcmp(it->valuestring, "fullscreen") == 0) { d->img_mode = IMG_MODE_FULLSCREEN; c.img_mode = true; n++; }
        else if (strcmp(it->valuestring, "avatar") == 0) { d->img_mode = IMG_MODE_AVATAR; c.img_mode = true; n++; }
    }

    cJSON_Delete(root);
    if (chg) *chg = c;
    return n;
}
