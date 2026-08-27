// components/platform/platform_esp32/src/kv_nvs.c —— hal_kv 的 NVS 实现
#include "platform/platform_factory.h"
#include "hal/hal_kv.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "kv_nvs";
#define KV_NS "trae_cfg"

static int kv_get(hal_kv_t *self, const char *key, void *buf, int cap, int *out_len) {
    (void)self;
    nvs_handle_t h;
    esp_err_t oe = nvs_open(KV_NS, NVS_READONLY, &h);
    if (oe != ESP_OK) {
        // 命名空间还不存在(从没写过)是正常的;但 NVS 未初始化就是 bug —— 明确报出来。
        if (oe == ESP_ERR_NVS_NOT_INITIALIZED)
            ESP_LOGW(TAG, "kv_get(%s): NVS 未初始化,读取失败(检查 create_kv 是否先于读取)", key);
        return -1;
    }
    size_t sz = (size_t)cap;
    esp_err_t e = nvs_get_blob(h, key, buf, &sz);
    nvs_close(h);
    if (e != ESP_OK) return -1;
    if (out_len) *out_len = (int)sz;
    return 0;
}

static int kv_set(hal_kv_t *self, const char *key, const void *data, int len) {
    (void)self;
    nvs_handle_t h;
    if (nvs_open(KV_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_set_blob(h, key, data, (size_t)len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) { ESP_LOGW(TAG, "kv_set(%s) failed: 0x%x", key, e); return -1; }
    return 0;
}

static const hal_kv_api_t API = { .get = kv_get, .set = kv_set };
static hal_kv_t s_kv = { .api = &API, .impl = NULL };

hal_kv_t *platform_create_kv(const board_config_t *cfg) {
    (void)cfg;
    // 自己确保默认 nvs 分区已初始化 —— 否则开机紧接着的 profile 读取会打在未初始化的 NVS 上
    // (曾是 bug:load 早于 ble_config 的 nvs_flash_init,导致重启后 name/token 全回默认)。
    // nvs_flash_init 幂等,ble_config 之后再调返回已初始化,无副作用。
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 仅默认 nvs 分区(存 RF 校准 + trae_cfg 配置),与产线身份 cardid 分区无关,擦除安全。
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "nvs_flash_init 失败 0x%x —— 配置持久化不可用", e);
    return &s_kv;
}
