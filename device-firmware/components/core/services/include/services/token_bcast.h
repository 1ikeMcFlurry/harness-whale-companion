// components/core/services/include/services/token_bcast.h —— token 广播决策(纯逻辑)
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TOKEN_MFG_LEN         26      // 厂商数据必须恰好这么长
#define TOKEN_HDR             0x12    // mfg[4]:高4位=ver(1),低4位=type(2)
#define TOKEN_BALANCE_MAX     999999u
#define TOKEN_BAD_MAC_RATE_MS 3000u   // 签名失败音的最小间隔
#define TOKEN_MAC_LEN         8       // 签名长度,同时也是去重的键
#define TOKEN_SEEN_MAX        16      // 去重窗口:记住最近多少条消息签名

typedef enum {
    TOKEN_ACT_IGNORE = 0,   // 静默:不是给我的 / 重复 / 畸形
    TOKEN_ACT_ADD,          // 加分成功
    TOKEN_ACT_SUB,          // 扣分成功
    TOKEN_ACT_FAIL,         // 失败(网关业务失败,或签名校验失败)
    TOKEN_ACT_SYNC,         // 静默同步:刷新余额但**不响提示音**
    TOKEN_ACT_PET,          // 宠物开关(op=0x05):复用 balance 字段,0=关闭 / ≥1=开启且=宠物类型
} token_action_t;

// 仅用于日志与测试断言,不影响铃声选择
typedef enum {
    TOKEN_R_OK = 0,
    TOKEN_R_DISABLED,           // 未烧录 product_key,功能禁用
    TOKEN_R_BAD_LEN,
    TOKEN_R_BAD_MAGIC,          // 魔数错,或 hdr 低 4 位不是类型 2
    TOKEN_R_BAD_VER,            // hdr 高 4 位不是版本 1
    TOKEN_R_NOT_MINE,
    TOKEN_R_BAD_MAC,
    TOKEN_R_MAC_RATE_LIMITED,
    TOKEN_R_BAD_BALANCE,        // balance > 999999
    TOKEN_R_REPLAY,             // 签名已在去重窗口里 —— 网关重发的那些死在这里
    TOKEN_R_BAD_OP,
} token_reason_t;

// 注入的 MAC 计算:对 msg[0..len) 算 HMAC-SHA256(product_key,·),写前 8 字节到 out。
// 成功返回 0。ESP32 侧注入 mbedtls 实现;host 测试注入确定性假实现。
typedef int (*token_mac_fn)(const uint8_t *msg, int len, uint8_t out[8], void *user);

typedef struct {
    uint8_t      self_target[6];  // 本机 MAC,开机读一次
    bool         enabled;         // 未烧录 product_key → false,全部 IGNORE
    token_mac_fn mac;
    void        *mac_user;
    uint32_t     bad_mac_ms;      // 上次响签名失败音的时刻
    bool         bad_mac_seen;    // 是否已响过至少一次(见 .c 里的注释)

    // ---- 去重窗口(环形缓冲,仅 RAM)----
    // 键是消息的 8 字节签名:同一条消息重发多少次签名都一样 → 只处理一次;
    // 不同 pad 发的是不同消息 → 签名不同 → 互不干扰,**pad 之间无需任何协调**。
    //
    // 早期设计用"单调递增 seq + 持久化 last_seq",在多 pad 场景下是错的:
    // 各 pad 本地计数器互不相通,A 发到 5 之后 B 从 1 开始发就会被全部当成重复包
    // 静默丢弃。改成签名窗口后该问题消失,并且顺带消除了"一条伪造的超大 seq
    // 就能把计数器顶到顶格、永久锁死这张卡"的 DoS。
    uint8_t      seen[TOKEN_SEEN_MAX][TOKEN_MAC_LEN];
    uint8_t      seen_n;          // 已填条数(0..TOKEN_SEEN_MAX)
    uint8_t      seen_next;       // 下一个写入位置
} token_bcast_t;

typedef struct {
    token_action_t action;
    token_reason_t reason;
    uint32_t       balance;   // ADD/SUB/SYNC/FAIL(业务) 有效;FAIL(BAD_MAC) 恒为 0
    uint32_t       seq;       // 同上。仅供日志,卡片不依赖它的数值
} token_result_t;

// 初始化。enabled 置 true;调用方随后可按"是否已烧录 product_key"改写它。
void token_bcast_init(token_bcast_t *t, const uint8_t self_target[6],
                      token_mac_fn mac, void *mac_user);

// 处理一条 token 广播。now_ms 用于签名失败限速。out 必填,函数内部先清零。
// 全部校验通过后把该消息的签名记进去重窗口。
void token_bcast_handle(token_bcast_t *t, const uint8_t *mfg, int len,
                        uint32_t now_ms, token_result_t *out);
