// components/platform/platform_esp32/src/token_mac_esp.c
// 注入给 core/services 的 token_bcast 的 HMAC 实现。
//
// 存在的意义只有一个:让 token_bcast.c 保持零平台依赖、可在 host 上单测。
// 它拿到的只是一个函数指针,连 product_key 存在都不知道 —— 密钥全程不出
// identity_nvs.c,即使 token 层写错代码也拿不到。
#include "platform/platform_factory.h"
#include "hal/hal_identity.h"

int token_mac_esp(const uint8_t *msg, int len, uint8_t out[8], void *user) {
    hal_identity_t *id = (hal_identity_t *)user;
    if (id == NULL) return -1;
    return hal_identity_hmac(id, msg, len, out, 8);
}
