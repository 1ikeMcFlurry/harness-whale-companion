// components/core/ports/include/hal/hal_identity.h —— 设备身份(只读)
// 注意:**没有 get_product()**。广播 HMAC 使用每设备 DeviceSecret，
// 业务层只暴露 hmac(),密钥不出适配器边界。
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct hal_identity_s hal_identity_t;

typedef struct {
    // 取字符串到 buf,返回实际长度(不含 '\0');缺失或 cap 不足返回 <0。始终以 '\0' 结尾。
    // get_sn 例外:SN 来自 eFuse MAC(缺失时自补),恒可用,与是否烧录 Key 无关。
    int  (*get_sn)     (hal_identity_t *self, char *buf, int cap);
    int  (*get_key)    (hal_identity_t *self, char *buf, int cap);
    int  (*get_hw_ver) (hal_identity_t *self, char *buf, int cap);

    // Key 与 ProductKey 均非空 → true。SN/HwVer 不参与判定。
    bool (*is_provisioned)(hal_identity_t *self);

    // 用每设备 DeviceSecret 对 msg[0..len) 算 HMAC-SHA256,截断写入 out。
    // out_cap 最大 32。成功返回 0;未烧录 DeviceSecret 或参数非法返回 <0。
    //
    // ⚠ 密钥编码口径(必须与网关侧逐字一致):
    //   HMAC 密钥 = DeviceSecret 字符串的**原始 ASCII 字节**,不做 hex 解码、
    //   不含结尾 '\0'、不做任何变换。
    // 网关按 SN 从受保护台账取得对应 DeviceSecret；拿错设备或做 hex 解码
    // 都会使验签失败。
    int  (*hmac)(hal_identity_t *self, const uint8_t *msg, int len,
                 uint8_t *out, int out_cap);
} hal_identity_api_t;

struct hal_identity_s { const hal_identity_api_t *api; void *impl; };

static inline int hal_identity_get_sn(hal_identity_t *s, char *b, int c) {
    return s->api->get_sn(s, b, c);
}
static inline int hal_identity_get_key(hal_identity_t *s, char *b, int c) {
    return s->api->get_key(s, b, c);
}
static inline int hal_identity_get_hw_ver(hal_identity_t *s, char *b, int c) {
    return s->api->get_hw_ver(s, b, c);
}
static inline bool hal_identity_is_provisioned(hal_identity_t *s) {
    return s->api->is_provisioned(s);
}
static inline int hal_identity_hmac(hal_identity_t *s, const uint8_t *m, int len,
                                    uint8_t *out, int out_cap) {
    return s->api->hmac(s, m, len, out, out_cap);
}
