// components/core/ports/include/hal/hal_config.h
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 收到一条完整应用层帧(已重组)。payload 为原始字节(可能无 NUL)。
typedef void (*cfg_msg_cb_t)(uint8_t type, const uint8_t *payload, int len, void *user);

// BLE 连接建立(status ok)。运行在 NimBLE host 任务;回调实现须非阻塞。
typedef void (*cfg_conn_cb_t)(void *user);

// BLE 断开连接。运行在 NimBLE host 任务;回调实现须非阻塞。
typedef void (*cfg_disconnect_cb_t)(void *user);

typedef struct hal_config_s hal_config_t;
typedef struct {
    void (*on_message)(hal_config_t *self, cfg_msg_cb_t cb, void *user);
    void (*on_connect)(hal_config_t *self, cfg_conn_cb_t cb, void *user);      // 连接建立回调
    void (*notify_status)(hal_config_t *self, uint8_t type, uint8_t status);  // 回报处理结果
    void (*set_identity)(hal_config_t *self, const uint8_t *data, int len);   // 身份只读 blob
    void (*set_gamescore)(hal_config_t *self, const uint8_t *data, int len);  // 游戏积分只读 blob
    void (*on_disconnect)(hal_config_t *self, cfg_disconnect_cb_t cb, void *user);
    bool (*notify_capture)(hal_config_t *self, const uint8_t *data, size_t len);
    bool (*capture_subscribed)(hal_config_t *self);
    uint16_t (*get_mtu_payload)(hal_config_t *self);
    bool (*capture_transport_hold)(hal_config_t *self, bool hold);
} hal_config_api_t;
struct hal_config_s { const hal_config_api_t *api; void *impl; };

static inline void hal_config_on_message(hal_config_t *c, cfg_msg_cb_t cb, void *user) {
    if (c && c->api && c->api->on_message) c->api->on_message(c, cb, user);
}
static inline void hal_config_on_connect(hal_config_t *c, cfg_conn_cb_t cb, void *user) {
    if (c && c->api && c->api->on_connect) c->api->on_connect(c, cb, user);
}
static inline void hal_config_on_disconnect(hal_config_t *c, cfg_disconnect_cb_t cb, void *user) {
    if (c && c->api && c->api->on_disconnect) c->api->on_disconnect(c, cb, user);
}
static inline void hal_config_notify_status(hal_config_t *c, uint8_t type, uint8_t status) {
    if (c && c->api && c->api->notify_status) c->api->notify_status(c, type, status);
}
static inline bool hal_config_notify_capture(hal_config_t *c, const uint8_t *data, size_t len) {
    return c && c->api && c->api->notify_capture && c->api->notify_capture(c, data, len);
}
static inline bool hal_config_capture_subscribed(hal_config_t *c) {
    return c && c->api && c->api->capture_subscribed && c->api->capture_subscribed(c);
}
static inline uint16_t hal_config_get_mtu_payload(hal_config_t *c) {
    return c && c->api && c->api->get_mtu_payload ? c->api->get_mtu_payload(c) : 20;
}
static inline bool hal_config_capture_transport_hold(hal_config_t *c, bool hold) {
    return c && c->api && c->api->capture_transport_hold &&
           c->api->capture_transport_hold(c, hold);
}
// 身份只读 blob(BLE 特征 …0012)。由 app 层组装后推入 —— app 层只有 get_sn/get_key/
// get_hw_ver,拿不到 ProductKey,所以结构上不可能把 ProductKey 泄漏进这个 blob。
static inline void hal_config_set_identity(hal_config_t *c, const uint8_t *data, int len) {
    if (c && c->api && c->api->set_identity) c->api->set_identity(c, data, len);
}
// 游戏积分只读 blob(BLE 特征 …0013)。小程序读取后再下发 game_clear 清零。
static inline void hal_config_set_gamescore(hal_config_t *c, const uint8_t *data, int len) {
    if (c && c->api && c->api->set_gamescore) c->api->set_gamescore(c, data, len);
}

// ==================== 通知协议 ====================
// 每个下发动作都有回应,通知格式: [type(1)][status(1)]
// 原则:分片进行中回 ACK;每个动作最终必有一条 DONE 或 0x1X 错误作为终结。

// 消息/通知类型
#define CFG_MSG_FRAME    0x00   // 帧层问题(帧头非法/重组超时,判断不出业务类型)
#define CFG_MSG_JSON     0x01   // 配置(JSON)
#define CFG_MSG_JPEG     0x02   // 图片(JPG)
#define CFG_MSG_SCORE    0x03   // 乐谱(RTTTL)
// 0x04 曾为"音频流(实时播放)",因 BLE 中心强制 60ms 间隔带宽不足、效果差已移除,号段保留不复用。
#define CFG_MSG_AUDIO_CLIP 0x05   // 音频片段写入/替换(存 SPIFFS,离线播放)
#define CFG_MSG_SCREEN_CAPTURE 0x06   // 屏幕截图分包通知
#define CFG_MSG_HARNESS_STATUS 0x07   // DeepSeek Harness 状态副屏(固定 21 字节快照)
#define CFG_MSG_HARNESS_QUESTION 0x08 // Harness 设备端选择题；notify status=选项序号
#define CFG_HARNESS_ANSWER_BASE  0x40 // 设备选择回复：status - BASE = 选项下标

// 状态码
#define CFG_ST_ACK             0x00   // 该帧已收到(分片传输进行中,非终结)
#define CFG_ST_DONE            0x01   // 整体完成(配置生效/图片已显示/乐谱已开始播放)
#define CFG_ST_ERR_FRAME       0x10   // 帧格式错(ver 非 0x01 / len 非法)
#define CFG_ST_ERR_TIMEOUT     0x11   // 重组超时,半帧被丢弃
#define CFG_ST_ERR_SEQ         0x12   // 时序错(未 BEGIN 先 DATA/END、END 长度对不上)
#define CFG_ST_ERR_PARSE       0x13   // 内容解析失败(JSON/RTTTL 格式错)
#define CFG_ST_ERR_NO_FIELD    0x14   // JSON 无任何已知合法键
#define CFG_ST_ERR_TOO_LONG    0x15   // 超长(超过 payload / 存储上限)
#define CFG_ST_ERR_STORAGE     0x16   // 存储失败(flash/NVS 写失败)
#define CFG_ST_ERR_DECODE      0x17   // 图片解码失败(尺寸不合规/坏图)
#define CFG_ST_ERR_UNSUPPORTED 0x18   // 不支持的消息类型
#define CFG_ST_ERR_JPEG_FORMAT 0x19   // JPG 格式不受支持(渐进式/算术编码等),需发送方重新编码
#define CFG_ST_ERR_JPEG_BROKEN 0x1A   // JPG 数据不完整(缺 SOS/EOI)或不是合法 JPEG
#define CFG_ST_ERR_BUSY        0x1B   // 设备忙(上一张图正在解码),稍等 1 秒重试
#define CFG_ST_ERR_NOT_READY   0x1C   // 传输通道未连接或未订阅
#define CFG_ST_ERR_AVATAR      0x1D   // 头像名称不存在或内置资源损坏
