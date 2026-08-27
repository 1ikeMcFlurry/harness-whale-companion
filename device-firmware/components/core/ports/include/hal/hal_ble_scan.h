// components/core/ports/include/hal/hal_ble_scan.h
#pragma once
#include <stdint.h>

// 一次匹配命中:命中的厂商数据原始字节(含公司 ID 两字节)+ 长度。
// 回调在 NimBLE host 任务上下文触发,实现方须保证跨线程安全(参考 on_cfg_write)。
typedef void (*ble_match_cb_t)(const uint8_t *data, int len, void *user);

typedef struct hal_ble_scan_s hal_ble_scan_t;
typedef struct {
    void (*on_match)(hal_ble_scan_t *self, ble_match_cb_t cb, void *user);
} hal_ble_scan_api_t;
struct hal_ble_scan_s { const hal_ble_scan_api_t *api; void *impl; };

static inline void hal_ble_scan_on_match(hal_ble_scan_t *s, ble_match_cb_t cb, void *user) {
    s->api->on_match(s, cb, user);
}
