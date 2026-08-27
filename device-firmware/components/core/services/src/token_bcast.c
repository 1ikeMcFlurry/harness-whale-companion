// components/core/services/src/token_bcast.c
#include "services/token_bcast.h"
#include <string.h>

static uint32_t rd_u24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

// 签名是否已在去重窗口里
static bool seen_has(const token_bcast_t *t, const uint8_t sig[TOKEN_MAC_LEN]) {
    for (uint8_t i = 0; i < t->seen_n; i++)
        if (memcmp(t->seen[i], sig, TOKEN_MAC_LEN) == 0) return true;
    return false;
}

// 记入去重窗口(环形覆盖最旧的一条)
static void seen_put(token_bcast_t *t, const uint8_t sig[TOKEN_MAC_LEN]) {
    memcpy(t->seen[t->seen_next], sig, TOKEN_MAC_LEN);
    t->seen_next = (uint8_t)((t->seen_next + 1) % TOKEN_SEEN_MAX);
    if (t->seen_n < TOKEN_SEEN_MAX) t->seen_n++;
}

void token_bcast_init(token_bcast_t *t, const uint8_t self_target[6],
                      token_mac_fn mac, void *mac_user) {
    memset(t, 0, sizeof *t);
    if (self_target) memcpy(t->self_target, self_target, 6);
    t->mac      = mac;
    t->mac_user = mac_user;
    t->enabled  = true;
}

void token_bcast_handle(token_bcast_t *t, const uint8_t *mfg, int len,
                        uint32_t now_ms, token_result_t *out) {
    memset(out, 0, sizeof *out);
    out->action = TOKEN_ACT_IGNORE;
    out->reason = TOKEN_R_OK;

    if (!t->enabled) { out->reason = TOKEN_R_DISABLED; return; }

    // ① 结构校验
    if (mfg == NULL || len != TOKEN_MFG_LEN) { out->reason = TOKEN_R_BAD_LEN; return; }
    if (mfg[0] != 0xFF || mfg[1] != 0xFF || mfg[2] != 0x48 || mfg[3] != 0x42) {
        out->reason = TOKEN_R_BAD_MAGIC; return;
    }
    if ((mfg[4] & 0x0F) != (TOKEN_HDR & 0x0F)) { out->reason = TOKEN_R_BAD_MAGIC; return; }
    if ((mfg[4] >> 4)   != (TOKEN_HDR >> 4))   { out->reason = TOKEN_R_BAD_VER;   return; }

    // ② 目标筛选 —— 必须排在验签之前,否则场地里别人的广播都会让本机响失败音
    if (memcmp(&mfg[6], t->self_target, 6) != 0) { out->reason = TOKEN_R_NOT_MINE; return; }

    // ③ 签名校验 —— 必须排在去重之前。签名同时也是去重的键,没验过的签名不能进窗口,
    //    否则攻击者随便发几条伪造包就能把窗口刷满,把真实消息挤出去。
    const uint8_t *sig = &mfg[18];
    uint8_t calc[TOKEN_MAC_LEN];
    if (t->mac == NULL || t->mac(mfg, 18, calc, t->mac_user) != 0 ||
        memcmp(calc, sig, TOKEN_MAC_LEN) != 0) {
        // 限速:一个持续发坏包的干扰源不该让卡片不停叫。
        // bad_mac_seen 不能省 —— init 把 bad_mac_ms 清零,而开机头几秒 now_ms 本身
        // 就很小,只判 (now_ms - 0) < 3000 会把该响的**第一声**也吃掉。
        if (t->bad_mac_seen &&
            (uint32_t)(now_ms - t->bad_mac_ms) < TOKEN_BAD_MAC_RATE_MS) {
            out->reason = TOKEN_R_MAC_RATE_LIMITED;   // action 保持 IGNORE
            return;
        }
        t->bad_mac_ms   = now_ms;
        t->bad_mac_seen = true;
        out->action = TOKEN_ACT_FAIL;
        out->reason = TOKEN_R_BAD_MAC;
        // balance/seq 保持 0:这个包整体不可信,一个字节都不采信
        return;
    }

    // ④ 值域校验 —— 放在验签之后,这样"别人家网关配错"不会让全场卡刷 WARN 日志。
    //    seq 不再做单调性/非零校验:它现在只是 nonce,作用是让"内容相同但确实是
    //    两次不同操作"的消息产生不同签名,数值本身卡片不关心。
    uint32_t seq = rd_u24(&mfg[12]);
    uint32_t bal = rd_u24(&mfg[15]);
    if (bal > TOKEN_BALANCE_MAX) { out->reason = TOKEN_R_BAD_BALANCE; return; }

    // ⑤ 去重 —— 按消息签名查最近 TOKEN_SEEN_MAX 条。
    //    网关为了送达会把同一条消息重发几十次,它们签名相同,只有第一条被处理。
    //    不同 pad 发的是不同消息 → 签名不同 → 都能通过,无需任何跨设备协调。
    if (seen_has(t, sig)) { out->reason = TOKEN_R_REPLAY; return; }

    // ⑥ 分发
    token_action_t act;
    switch (mfg[5]) {
        case 0x01: act = TOKEN_ACT_ADD;  break;
        case 0x02: act = TOKEN_ACT_SUB;  break;
        case 0x03: act = TOKEN_ACT_FAIL; break;   // 网关下发的业务失败:可信,照常落地
        case 0x04: act = TOKEN_ACT_SYNC; break;   // 周期性余额同步:刷新但不响音
        case 0x05: act = TOKEN_ACT_PET;  break;   // 宠物开关:balance=0 关 / ≥1 开且=宠物类型
        default:   out->reason = TOKEN_R_BAD_OP; return;   // 不记进窗口
    }

    seen_put(t, sig);
    out->action  = act;
    out->reason  = TOKEN_R_OK;
    out->balance = bal;
    out->seq     = seq;
}
