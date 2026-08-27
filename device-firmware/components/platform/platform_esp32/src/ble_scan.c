// components/platform/platform_esp32/src/ble_scan.c —— NimBLE observer:匹配厂商广播 → app 回调
#include "platform/board_config.h"
#if PERIPH_BLE
#include "platform/platform_factory.h"
#include "hal/hal_ble_scan.h"
#include "services/ble_match.h"
#include "ble_internal.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "ble_scan";

static ble_match_cb_t s_cb;
static void          *s_user;

// disc 回调:解析广播字段,取厂商数据,命中就回调上层。
static int scan_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) return 0;
    // 粗筛:只要前缀是我们的厂商广播就原样上抛,由 app 层用 ble_match_classify()
    // 再分流到心跳 / token。这里不分类,是为了不让 core/ports 的 hal_ble_scan
    // 端口反过来依赖 core/services 的 ble_match_kind_t(分层倒置)。
    if (f.mfg_data && ble_match_classify(f.mfg_data, f.mfg_data_len) != BLE_MATCH_NONE) {
        if (s_cb) s_cb(f.mfg_data, f.mfg_data_len, s_user);
    }
    return 0;
}

// on_sync 里调用:被动扫描、关重复过滤(要连续上报以便"收到就跳")、温和占空比。
void ble_scan_start(uint8_t addr_type) {
    struct ble_gap_disc_params dp = {0};
    dp.passive = 1;
    dp.filter_duplicates = 0;
    dp.itvl = 256;    // 256 * 0.625ms = 160ms 扫描间隔
    dp.window = 179;  // 179 * 0.625ms ≈112ms(约70%占空),满足 v3 最多6种包在5秒内高概率收齐
    int rc = ble_gap_disc(addr_type, BLE_HS_FOREVER, &dp, scan_gap_event, NULL);
    if (rc != 0) ESP_LOGW(TAG, "ble_gap_disc failed rc=%d", rc);
}

static void scan_on_match(hal_ble_scan_t *self, ble_match_cb_t cb, void *user) {
    (void)self; s_cb = cb; s_user = user;
}
static const hal_ble_scan_api_t API = { .on_match = scan_on_match };
static hal_ble_scan_t s_handle = { .api = &API, .impl = NULL };

hal_ble_scan_t *platform_create_ble_scan(const board_config_t *cfg) {
    (void)cfg;
    // 仅登记句柄;实际扫描由 ble_config.c 的 on_sync → ble_scan_start 启动
    // (二者共用同一个 NimBLE host,host 已在 platform_create_ble_config 里初始化)。
    return &s_handle;
}
#endif // PERIPH_BLE
