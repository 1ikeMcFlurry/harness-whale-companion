// components/platform_esp32/src/ble_config.c —— NimBLE GATT:结构化 JSON 配置通道
// 命令写特征(重组分帧) + notify 状态特征 + 身份只读 + 游戏积分只读。
#include "platform/board_config.h"
#if PERIPH_BLE
#include "platform/platform_factory.h"
#include "hal/hal_config.h"
#include "services/frame_reasm.h"
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"   // ble_att_mtu / ble_att_set_preferred_mtu
#include "host/ble_gatt.h"  // ble_gattc_exchange_mtu(主动发起 MTU 协商)
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_internal.h"

static const char *TAG = "ble_config";

// UUID 基址:ASCII "TRAECARD" + 末字节字段号。NimBLE 用小端字节序存储。
#define TRAE_UUID128(nn) BLE_UUID128_INIT( \
    (nn),0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x44,0x52,0x41,0x43,0x45,0x41,0x52,0x54)

static const ble_uuid128_t g_svc_uuid    = TRAE_UUID128(0x00);
static const ble_uuid128_t g_cmd_uuid    = TRAE_UUID128(0x10);   // 命令写特征
static const ble_uuid128_t g_notify_uuid = TRAE_UUID128(0x11);   // notify 状态特征
static const ble_uuid128_t g_ident_uuid  = TRAE_UUID128(0x12);   // 身份只读特征
static const ble_uuid128_t g_game_uuid   = TRAE_UUID128(0x13);   // 游戏积分只读特征
static const ble_uuid128_t g_capture_uuid = TRAE_UUID128(0x14);  // 屏幕截图 notify 特征

// 广播用的设备名。改为运行时传入(内容是本机 SN),这样网关扫一下就知道是哪张卡,
// 不必再靠台账反查 —— 也就绕开了"广播地址 = WIFI MAC + 2"那个坑。
// ble_config.c 仍然不认识身份模块,名字由组装根 app.c 传进来。
static char s_dev_name[32] = BLE_DEVICE_NAME;

static const uint8_t *s_ident_ptr;
static int            s_ident_len;
static const uint8_t *s_game_ptr;
static int            s_game_len;

static cfg_msg_cb_t   s_msg_cb;
static void          *s_msg_user;
static cfg_conn_cb_t  s_conn_cb;
static void          *s_conn_user;
static cfg_disconnect_cb_t s_disconnect_cb;
static void          *s_disconnect_user;
static frame_reasm_t  s_reasm;
static uint16_t       s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t       s_notify_handle = 0;   // notify 特征 val handle(GATT 注册时回填)
static bool           s_subscribed = false;  // 客户端是否已订阅通知(写了 CCCD)
static uint16_t       s_capture_handle = 0;  // 截图 notify 特征 val handle(GATT 注册时回填)
static bool           s_capture_subscribed = false;
#define REASM_TIMEOUT_US  2000000            // 一帧内相邻写包间隔上限(2s)
static int64_t        s_last_write_us = 0;
static uint8_t        s_addr_type;

static int gap_event(struct ble_gap_event *event, void *arg);   // 前置声明(start_adv 用)

// 身份读:返回 app 推入的 72 字节 blob(ver/flags/SN/Key/HwVer)。
// blob 由 app 组装 —— app 层只有 get_sn/get_key/get_hw_ver,拿不到 ProductKey,
// 所以结构上不可能把 ProductKey 泄漏到这里。
static int ident_read(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (s_ident_ptr == NULL || s_ident_len <= 0) return 0;
    return os_mbuf_append(ctxt->om, s_ident_ptr, s_ident_len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// 游戏积分读:返回 app 推入的 blob([ver][game_total u32][game_best u32])。
static int game_read(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (s_game_ptr == NULL || s_game_len <= 0) return 0;
    return os_mbuf_append(ctxt->om, s_game_ptr, s_game_len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// notify 特征不可读写(值靠主动 notify 推送),但 NimBLE 要求每个特征 access_cb 非空
// (ble_gatts_chr_is_sane 检查,NULL 会使 ble_gatts_start 返回 EINVAL → host 启动 assert)。
// NOTIFY flag 下 att 层不含 read/write,客户端读会被 ATT 拒,此桩实际不会被调用。
static int notify_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

// 从 host task 发 notify [type][status]。未连接或无句柄则静默跳过。
static void cfg_notify(uint8_t type, uint8_t status) {
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "notify 跳过: 未连接 (type=0x%02X status=%d)", type, status);
        return;
    }
    if (s_notify_handle == 0) {
        ESP_LOGW(TAG, "notify 跳过: 通知特征句柄为0 (GATT 未注册?)");
        return;
    }
    if (!s_subscribed) {   // 仅告警,仍照发:避免订阅检测误判把正常通知拦掉
        ESP_LOGW(TAG, "客户端尚未订阅CCCD(仍尝试发送)。小程序需对 ...0011 特征调用 "
                      "notifyBLECharacteristicValueChange 订阅,否则收不到通知");
    }
    uint8_t pkt[2] = { type, status };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof pkt);
    if (!om) { ESP_LOGE(TAG, "notify: mbuf 分配失败"); return; }
    int rc = ble_gatts_notify_custom(s_conn, s_notify_handle, om);
    if (rc == 0) ESP_LOGD(TAG, "notify → type=0x%02X status=%d (conn=%d handle=%d)",  // 每片 ACK,降 DEBUG
                          type, status, s_conn, s_notify_handle);
    else         ESP_LOGE(TAG, "notify 发送失败 rc=%d (type=0x%02X status=%d)", rc, type, status);
}

// 截图分包走独立的 notify 特征。必须已连接且中央已订阅,避免把高频数据送入无消费者的链路。
static bool cfg_notify_capture(hal_config_t *self, const uint8_t *data, size_t len) {
    (void)self;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || s_capture_handle == 0 || !s_capture_subscribed) {
        return false;
    }
    if (data == NULL || len == 0) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGE(TAG, "capture notify: mbuf 分配失败");
        return false;
    }
    int rc = ble_gatts_notify_custom(s_conn, s_capture_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "capture notify 发送失败 rc=%d", rc);
        return false;
    }
    return true;
}

// 重组超时看门狗:小程序若只发了半帧就停,不会再有写入触发被动检测,
// 故用周期定时器主动发现并回报,避免小程序一直等不到回应。
static esp_timer_handle_t s_reasm_timer;
static void reasm_timeout_cb(void *arg) {
    (void)arg;
    if (s_reasm.have == 0) return;
    if (esp_timer_get_time() - s_last_write_us <= REASM_TIMEOUT_US) return;
    ESP_LOGW(TAG, "重组超时:丢弃半帧(%d 字节)并回报小程序", s_reasm.have);
    frame_reasm_reset(&s_reasm);
    cfg_notify(CFG_MSG_FRAME, CFG_ST_ERR_TIMEOUT);
}

// 命令写:累积字节 → 重组 → 完整帧交给上层消息回调。运行于 NimBLE host task。
static int cmd_write(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t seg[FRAME_HDR_LEN + FRAME_MAX_PAYLOAD];   // 容纳单次最大 ATT 写(可能整帧一次到)
    uint16_t got = 0;
    uint16_t avail = OS_MBUF_PKTLEN(ctxt->om);
    if (avail > sizeof seg) avail = sizeof seg;
    if (ble_hs_mbuf_to_flat(ctxt->om, seg, avail, &got) != 0) return BLE_ATT_ERR_UNLIKELY;

    int64_t now = esp_timer_get_time();
    if (s_reasm.have > 0 && (now - s_last_write_us) > REASM_TIMEOUT_US) {
        ESP_LOGW(TAG, "重组超时,丢弃半帧(已累计 %d 字节)并回报", s_reasm.have);
        frame_reasm_reset(&s_reasm);
        cfg_notify(CFG_MSG_FRAME, CFG_ST_ERR_TIMEOUT);
    }
    s_last_write_us = now;

    uint8_t type; const uint8_t *pl; int pl_len;
    ESP_LOGD(TAG, "收到写入 %d 字节(此前已累计 %d)", got, s_reasm.have);
    frame_status_t st = frame_reasm_push(&s_reasm, seg, got, &type, &pl, &pl_len);
    if (st == FRAME_READY) {
        // 音频流(0x04)每秒几十帧,逐帧 INFO 日志会占满串口并阻塞 host 任务拖慢收包 → 降为 DEBUG。
        // 批量分片(图片0x02/乐谱0x03/音频流0x04/音频片段0x05)每秒几十帧,逐帧 INFO 会刷屏
        // 并拖慢 host 任务 → 降 DEBUG;仅配置类(0x01)保留 INFO。结果由上层"显示/播放结果"给出。
        if (type == 0x01) ESP_LOGI(TAG, "◀ 完整帧: type=0x01 payload=%d 字节", pl_len);
        else              ESP_LOGD(TAG, "◀ 完整帧: type=0x%02X payload=%d 字节", type, pl_len);
        if (s_msg_cb) s_msg_cb(type, pl, pl_len, s_msg_user);
        else ESP_LOGW(TAG, "上层未注册消息回调,帧被丢弃");
    } else if (st == FRAME_ERR_VER || st == FRAME_ERR_LEN) {
        ESP_LOGE(TAG, "帧头非法: %s", st == FRAME_ERR_LEN ? "len 超过 256" : "ver 不是 0x01");
        cfg_notify(CFG_MSG_FRAME,
                   st == FRAME_ERR_LEN ? CFG_ST_ERR_TOO_LONG : CFG_ST_ERR_FRAME);
    }
    return 0;
}

static struct ble_gatt_chr_def g_chrs[6];    // cmd, notify, identity, gamescore, capture, 结束哨兵
static struct ble_gatt_svc_def g_svcs[2];

static void build_gatt_table(void) {
    // 0: 命令写
    g_chrs[0].uuid = &g_cmd_uuid.u;
    g_chrs[0].access_cb = cmd_write;
    g_chrs[0].arg = NULL;
    // 同时允许"带响应写"(配置/图片/乐谱,保证有序可靠)与"无响应写"(音频流,免每帧往返 ACK,
    // 吞吐从 ~2KB/s 提到满足 16k ADPCM 的 8KB/s)。两种写都进同一 access_cb,处理一致。
    g_chrs[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
    g_chrs[0].descriptors = NULL;
    // 1: notify 状态
    g_chrs[1].uuid = &g_notify_uuid.u;
    g_chrs[1].access_cb = notify_access;      // NimBLE 要求非空桩(NOTIFY 不会被读写)
    g_chrs[1].arg = NULL;
    g_chrs[1].flags = BLE_GATT_CHR_F_NOTIFY;
    g_chrs[1].val_handle = &s_notify_handle;  // 注册后回填 val handle
    g_chrs[1].descriptors = NULL;
    // 2: 身份只读
    g_chrs[2].uuid = &g_ident_uuid.u;
    g_chrs[2].access_cb = ident_read;
    g_chrs[2].arg = NULL;
    g_chrs[2].flags = BLE_GATT_CHR_F_READ;
    g_chrs[2].descriptors = NULL;
    // 3: 游戏积分只读
    g_chrs[3].uuid = &g_game_uuid.u;
    g_chrs[3].access_cb = game_read;
    g_chrs[3].arg = NULL;
    g_chrs[3].flags = BLE_GATT_CHR_F_READ;
    g_chrs[3].descriptors = NULL;
    // 4: 屏幕截图 notify(独立 CCCD,不影响旧客户端的状态通知订阅)
    g_chrs[4].uuid = &g_capture_uuid.u;
    g_chrs[4].access_cb = notify_access;
    g_chrs[4].arg = NULL;
    g_chrs[4].flags = BLE_GATT_CHR_F_NOTIFY;
    g_chrs[4].val_handle = &s_capture_handle;
    g_chrs[4].descriptors = NULL;
    memset(&g_chrs[5], 0, sizeof g_chrs[5]);  // 结束哨兵

    g_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    g_svcs[0].uuid = &g_svc_uuid.u;
    g_svcs[0].characteristics = g_chrs;
    memset(&g_svcs[1], 0, sizeof g_svcs[1]);
}

static void start_adv(void) {
    struct ble_gap_adv_params advp;
    struct ble_hs_adv_fields f;
    memset(&advp, 0, sizeof(advp));
    memset(&f, 0, sizeof(f));
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)s_dev_name;
    f.name_len = strlen(s_dev_name);
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &advp, gap_event, NULL);
}

// 主动 MTU 协商:卡片作为 GATT 客户端主动发起 Exchange MTU,从高档位依次试常见 MTU;
// 失败则降到下一档重试。很多中心端不主动协商 MTU,卡片主动发起可把 MTU 从默认 23 顶到 256。
// 档位不超过 256(msys 缓冲按 256 配的,再高会吃 RAM,C3 无 PSRAM 慎行)。
static const uint16_t MTU_LADDER[] = {256, 185, 23 };
#define MTU_LADDER_N ((int)(sizeof(MTU_LADDER) / sizeof(MTU_LADDER[0])))
static int s_mtu_step;

static int mtu_exch_cb(uint16_t conn, const struct ble_gatt_error *error, uint16_t mtu, void *arg) {
    (void)arg;
    if (error->status == 0) {
        ESP_LOGI(TAG, "主动 MTU 协商成功 = %d(请求档 %d,单次写上限约 %d 字节)",
                 mtu, MTU_LADDER[s_mtu_step], mtu - 3);
        return 0;
    }
    if (error->status == BLE_HS_EALREADY) {            // 对端已抢先协商 → 直接采用
        ESP_LOGI(TAG, "MTU 已由对端协商 = %d", ble_att_mtu(conn));
        return 0;
    }
    if (s_mtu_step + 1 < MTU_LADDER_N) {               // 降到下一常见档位重试
        s_mtu_step++;
        ble_att_set_preferred_mtu(MTU_LADDER[s_mtu_step]);
        ESP_LOGW(TAG, "MTU 协商失败(status=%d),降档到 %d 重试", error->status, MTU_LADDER[s_mtu_step]);
        ble_gattc_exchange_mtu(conn, mtu_exch_cb, NULL);
    } else {
        ESP_LOGW(TAG, "MTU 各档均失败,沿用当前 %d", ble_att_mtu(conn));
    }
    return 0;
}

static void start_mtu_negotiation(uint16_t conn) {
    s_mtu_step = 0;
    ble_att_set_preferred_mtu(MTU_LADDER[0]);          // 每次连接从最高档重置起
    int rc = ble_gattc_exchange_mtu(conn, mtu_exch_cb, NULL);
    if (rc != 0)                                       // 已协商过 / 忙 → 直接读当前值
        ESP_LOGI(TAG, "MTU 未发起主动协商(rc=%d),当前 %d", rc, ble_att_mtu(conn));
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                // 本传输端口仅服务一个中央。拒绝额外连接可使截图 CCCD 和 MTU
                // 始终对应同一 conn_handle,不会把 A 的订阅泄漏给 B。
                if (s_conn != BLE_HS_CONN_HANDLE_NONE && s_conn != event->connect.conn_handle) {
                    ESP_LOGW(TAG, "拒绝额外连接 conn=%d (活动连接=%d)",
                             event->connect.conn_handle, s_conn);
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    break;
                }
                s_conn = event->connect.conn_handle;
                ESP_LOGI(TAG, "已连接 conn=%d, MTU=%d(此刻多为默认23,协商后见 MTU 事件)", s_conn, ble_att_mtu(s_conn));
                // 请求更短的连接间隔:音频流需 8KB/s,默认间隔(常 ~30-50ms)只够 ~4KB/s → 卡顿。
                // 请求 15-30ms、latency=0,让每秒连接事件更多,吞吐翻倍以上。中心可能协商到别的值。
                struct ble_gap_upd_params up = {
                    .itvl_min = 12,   // 12 * 1.25ms = 15ms
                    .itvl_max = 24,   // 24 * 1.25ms = 30ms
                    .latency = 0,
                    .supervision_timeout = 400,   // 400 * 10ms = 4s
                };
                ble_gap_update_params(s_conn, &up);
                start_mtu_negotiation(s_conn);           // 主动发起 MTU 协商(高→低依次)
                if (s_conn_cb) s_conn_cb(s_conn_user);   // 通知上层:已连接(播提示片段等)
            } else {
                ESP_LOGW(TAG, "连接失败 status=%d", event->connect.status);
            }
            break;
        case BLE_GAP_EVENT_CONN_UPDATE: {
            struct ble_gap_conn_desc d;
            if (ble_gap_conn_find(s_conn, &d) == 0)
                ESP_LOGI(TAG, "连接参数更新 status=%d,间隔=%.2fms latency=%d timeout=%dms",
                         event->conn_update.status, d.conn_itvl * 1.25,
                         d.conn_latency, d.supervision_timeout * 10);
            else
                ESP_LOGI(TAG, "连接参数更新 status=%d", event->conn_update.status);
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            if (event->disconnect.conn.conn_handle != s_conn) {
                ESP_LOGD(TAG, "忽略非活动连接断开 conn=%d", event->disconnect.conn.conn_handle);
                break;
            }
            ESP_LOGI(TAG, "已断开 reason=%d, 重新广播", event->disconnect.reason);
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_subscribed = false;
            s_capture_subscribed = false;
            frame_reasm_reset(&s_reasm);
            if (s_disconnect_cb) s_disconnect_cb(s_disconnect_user);
            start_adv();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            // 客户端写 CCCD 订阅/退订。只有订阅了 notify,卡片发的通知才收得到。
            ESP_LOGI(TAG, "订阅事件: attr=%d notify=%d indicate=%d (通知特征句柄=%d)",
                     event->subscribe.attr_handle, event->subscribe.cur_notify,
                     event->subscribe.cur_indicate, s_notify_handle);
            if (event->subscribe.attr_handle == s_notify_handle) {
                s_subscribed = (event->subscribe.cur_notify != 0);
                ESP_LOGI(TAG, "通知特征订阅状态 → %s", s_subscribed ? "已订阅✓" : "未订阅✗");
            } else if (event->subscribe.attr_handle == s_capture_handle) {
                s_capture_subscribed = (event->subscribe.cur_notify != 0);
                ESP_LOGI(TAG, "截图特征订阅状态 → %s", s_capture_subscribed ? "已订阅✓" : "未订阅✗");
            }
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU 协商 = %d (单次写上限约 %d 字节)",
                     event->mtu.value, event->mtu.value - 3);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_adv();
            break;
        default: break;
    }
    return 0;
}

static void on_sync(void) {
    ble_hs_id_infer_auto(0, &s_addr_type);
    start_adv();
    ble_scan_start(s_addr_type);   // 广播启动后并行启动扫描(observer)
}
static void on_reset(int reason) { ESP_LOGW(TAG, "nimble reset, reason=%d", reason); }
static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void cfg_on_message(hal_config_t *self, cfg_msg_cb_t cb, void *user) {
    (void)self; s_msg_cb = cb; s_msg_user = user;
}
static void cfg_on_connect(hal_config_t *self, cfg_conn_cb_t cb, void *user) {
    (void)self; s_conn_cb = cb; s_conn_user = user;
}
static void cfg_on_disconnect(hal_config_t *self, cfg_disconnect_cb_t cb, void *user) {
    (void)self; s_disconnect_cb = cb; s_disconnect_user = user;
}
static void cfg_notify_status(hal_config_t *self, uint8_t type, uint8_t status) {
    (void)self; cfg_notify(type, status);
}
static bool cfg_capture_subscribed(hal_config_t *self) {
    (void)self;
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_capture_subscribed;
}
static uint16_t cfg_get_mtu_payload(hal_config_t *self) {
    (void)self;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) return 20;
    uint16_t mtu = ble_att_mtu(s_conn);
    if (mtu <= 3) return 20;
    uint16_t payload = (uint16_t)(mtu - 3);
    return payload > 244 ? 244 : payload;
}
static bool cfg_capture_transport_hold(hal_config_t *self, bool hold) {
    (void)self;
    esp_err_t err = hold ? esp_bt_sleep_disable() : esp_bt_sleep_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "截图传输%s BLE modem sleep 失败: %s",
                 hold ? "暂停" : "恢复", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "截图传输 BLE modem sleep → %s", hold ? "暂停" : "恢复");
    return true;
}
static void cfg_set_identity(hal_config_t *self, const uint8_t *data, int len) {
    (void)self; s_ident_ptr = data; s_ident_len = len;
}
static void cfg_set_gamescore(hal_config_t *self, const uint8_t *data, int len) {
    (void)self; s_game_ptr = data; s_game_len = len;
}
static const hal_config_api_t API = {
    .on_message = cfg_on_message,
    .on_connect = cfg_on_connect,
    .on_disconnect = cfg_on_disconnect,
    .notify_status = cfg_notify_status,
    .notify_capture = cfg_notify_capture,
    .capture_subscribed = cfg_capture_subscribed,
    .get_mtu_payload = cfg_get_mtu_payload,
    .capture_transport_hold = cfg_capture_transport_hold,
    .set_identity = cfg_set_identity,
    .set_gamescore = cfg_set_gamescore,
};
static hal_config_t s_handle = { .api = &API, .impl = NULL };

hal_config_t *platform_create_ble_config(const board_config_t *cfg, const char *dev_name) {
    // 名字为空时回落到编译期常量,保证任何情况下都能被搜到
    if (dev_name && dev_name[0]) {
        strncpy(s_dev_name, dev_name, sizeof s_dev_name - 1);
        s_dev_name[sizeof s_dev_name - 1] = '\0';
    }
    (void)cfg;
    // BLE/PHY 需要 NVS 存 RF 校准数据(否则日志报 "Call nvs_flash_init before starting BT")。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    // 防御:host 初始化可能因堆不足失败(如 esp_nimble_hci_init 的 ble_buf_alloc
    // 返回 NO_MEM)。此时必须提前返回,否则后面的 ble_gatts_count_cfg 会在未初始化的
    // host 上解引用空指针 → Load access fault。返回句柄让上层照常注册回调(不会被调用)。
    esp_err_t ble_err = nimble_port_init();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed (0x%x); BLE disabled", ble_err);
        return &s_handle;
    }
    frame_reasm_reset(&s_reasm);
    {   // 重组超时看门狗:周期检查,发现半帧超时则丢弃并主动回报小程序
        const esp_timer_create_args_t targs = { .callback = reasm_timeout_cb, .name = "reasm_to" };
        if (esp_timer_create(&targs, &s_reasm_timer) == ESP_OK)
            esp_timer_start_periodic(s_reasm_timer, 500000);   // 每 500ms 检查
    }
    // NimBLE 主机每次 notify/GAP 动作都打 INFO("GATT procedure initiated: notify; att_handle=..")
    // —— 图片传输时每片一条,严重刷屏。压到 WARN,只留告警/错误;我方 ble_config 日志不受影响。
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    build_gatt_table();
    ble_gatts_count_cfg(g_svcs);
    ble_gatts_add_svcs(g_svcs);
    ble_svc_gap_device_name_set(s_dev_name);
    nimble_port_freertos_init(host_task);
    return &s_handle;
}
#endif // PERIPH_BLE
