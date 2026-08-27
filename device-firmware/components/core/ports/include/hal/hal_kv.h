// components/core/ports/include/hal/hal_kv.h
#pragma once
#include <stdint.h>

typedef struct hal_kv_s hal_kv_t;
typedef struct {
    // 读: 成功把值写入 buf(<=cap),*out_len 为实际长度,返回 0;不存在/出错返回 -1。
    int (*get)(hal_kv_t *self, const char *key, void *buf, int cap, int *out_len);
    // 写: 返回 0 成功,<0 失败。
    int (*set)(hal_kv_t *self, const char *key, const void *data, int len);
} hal_kv_api_t;
struct hal_kv_s { const hal_kv_api_t *api; void *impl; };

static inline int hal_kv_get(hal_kv_t *c, const char *key, void *buf, int cap, int *out_len) {
    return c->api->get(c, key, buf, cap, out_len);
}
static inline int hal_kv_set(hal_kv_t *c, const char *key, const void *data, int len) {
    return c->api->set(c, key, data, len);
}
