#include "unity.h"
#include "services/token_bcast.h"
#include <string.h>

// 覆盖全部 18 字节输入的确定性假签名(FNV-1a)。签名同时也是去重的键,
// 假签名如果只依赖前几个字节,不同内容的消息会撞成同一个键,测试就没意义了。
static int fake_mac(const uint8_t *m, int n, uint8_t o[8], void *u) {
    (void)u;
    unsigned long long h = 1469598103934665603ULL;   // FNV-1a
    for (int i = 0; i < n; i++) { h ^= m[i]; h *= 1099511628211ULL; }
    for (int i = 0; i < 8; i++) o[i] = (unsigned char)(h >> (8 * i));
    return 0;
}

static void wr_u24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
}

// 构造一条合法帧(含正确签名),target/seq/balance/op 可指定
static void build_frame(uint8_t out[TOKEN_MFG_LEN], const uint8_t target[6],
                         uint32_t seq, uint32_t balance, uint8_t op) {
    memset(out, 0, TOKEN_MFG_LEN);
    out[0] = 0xFF; out[1] = 0xFF;
    out[2] = 0x48; out[3] = 0x42;
    out[4] = TOKEN_HDR;
    out[5] = op;
    memcpy(&out[6], target, 6);
    wr_u24(&out[12], seq);
    wr_u24(&out[15], balance);
    uint8_t mac[8];
    fake_mac(out, 18, mac, NULL);
    memcpy(&out[18], mac, 8);
}

static const uint8_t SELF[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

// ---------------- 结构 ----------------

TEST_CASE("bad len 25/27/0 -> BAD_LEN", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 100, 0x01);

    token_bcast_handle(&t, f, 25, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_LEN, r.reason);

    token_bcast_handle(&t, f, 27, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_LEN, r.reason);

    token_bcast_handle(&t, f, 0, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_LEN, r.reason);
}

TEST_CASE("bad magic each byte -> BAD_MAGIC", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];

    build_frame(f, SELF, 1, 100, 0x01); f[0] = 0x00;
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAGIC, r.reason);

    build_frame(f, SELF, 1, 100, 0x01); f[1] = 0x00;
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAGIC, r.reason);

    build_frame(f, SELF, 1, 100, 0x01); f[2] = 0x00;
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAGIC, r.reason);

    build_frame(f, SELF, 1, 100, 0x01); f[3] = 0x00;
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAGIC, r.reason);
}

TEST_CASE("hdr=0x11 -> BAD_MAGIC; hdr=0x22 -> BAD_VER", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];

    build_frame(f, SELF, 1, 100, 0x01);
    f[4] = 0x11; // ver=1 type=1 (类型错)
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAGIC, r.reason);

    build_frame(f, SELF, 1, 100, 0x01);
    f[4] = 0x22; // ver=2 type=2 (版本错)
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_VER, r.reason);
}

// ---------------- 目标 ----------------

TEST_CASE("target only last byte wrong -> NOT_MINE", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t almost[6]; memcpy(almost, SELF, 6); almost[5] ^= 0x01;
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, almost, 1, 100, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_NOT_MINE, r.reason);
}

// ---------------- 顺序约束 ----------------

TEST_CASE("order: not-mine AND bad-mac -> NOT_MINE, never BAD_MAC", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t other[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, other, 1, 100, 0x01);
    f[18] ^= 0xFF; // 同时破坏签名
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_NOT_MINE, r.reason);
}

// ---------------- 签名 ----------------

TEST_CASE("mac byte wrong -> FAIL/BAD_MAC; correct -> pass", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 100, 0x01);
    f[18] ^= 0x01; // 破坏签名首字节
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 1000, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);

    // 正确签名放行
    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 100, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 1000, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
}

TEST_CASE("bad mac does not enter dedup window; legit packet after it passes", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 100, 0x01);
    f[18] ^= 0xFF; // 篡改签名

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);
    TEST_ASSERT_EQUAL_UINT8(0, t.seen_n);

    // 随后喂对应的合法包(同样的内容,正确签名),必须正常通过
    uint8_t g[TOKEN_MFG_LEN];
    build_frame(g, SELF, 1, 100, 0x01);
    token_bcast_handle(&t, g, TOKEN_MFG_LEN, 10000, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
}

// ---------------- 值域 ----------------

TEST_CASE("balance 999999 passes; 1000000 -> BAD_BALANCE", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 6, 999999, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_UINT32(999999, r.balance);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 6, 1000000, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_BALANCE, r.reason);
}

TEST_CASE("seq==0 is legal (nonce only) -> passes normally", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 0, 100, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(0, r.seq);
}

// ---------------- 去重(本次改动的核心) ----------------

TEST_CASE("multi-pad: pad A at seq=500 then pad B at seq=1 (different balance) both pass", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];

    // pad A: seq=500
    build_frame(f, SELF, 500, 100, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(100, r.balance);

    // pad B: 独立计数器,从 seq=1 开始,余额不同 —— 旧的 last_seq 设计会把它当重复丢弃
    build_frame(f, SELF, 1, 200, 0x02);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_SUB, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(200, r.balance);
}

TEST_CASE("same packet resent 30x: only first is not IGNORE, rest REPLAY", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 100, 0x01);

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);

    for (int i = 0; i < 29; i++) {
        token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
        TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
        TEST_ASSERT_EQUAL_INT(TOKEN_R_REPLAY, r.reason);
    }
}

TEST_CASE("op=0x04 -> SYNC with correct balance; resend deduped", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 777, 0x04);

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_SYNC, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(777, r.balance);

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_REPLAY, r.reason);
}

TEST_CASE("same balance different seq -> both pass (seq as nonce)", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];

    build_frame(f, SELF, 1, 300, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);

    build_frame(f, SELF, 2, 300, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
}

TEST_CASE("window rotation: earliest entry evicted after TOKEN_SEEN_MAX fills, can pass again", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t first[TOKEN_MFG_LEN];
    build_frame(first, SELF, 1, 1, 0x01);

    // 第一条:填进窗口
    token_bcast_handle(&t, first, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);

    // 再灌 TOKEN_SEEN_MAX 条不同内容的包,把第一条挤出窗口
    uint8_t f[TOKEN_MFG_LEN];
    for (int i = 0; i < TOKEN_SEEN_MAX; i++) {
        build_frame(f, SELF, (uint32_t)(1000 + i), 1, 0x01);
        token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
        TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    }

    // 第一条现在应该已经被挤出窗口,能再次通过
    token_bcast_handle(&t, first, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
}

// ---------------- op ----------------

TEST_CASE("op 0x01 ADD, 0x02 SUB, 0x03 FAIL, 0x04 SYNC", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    uint8_t f[TOKEN_MFG_LEN];

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x02);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_SUB, r.action);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x03);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(1, r.seq);
    TEST_ASSERT_EQUAL_UINT32(55, r.balance);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x04);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_SYNC, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
}

TEST_CASE("op 0x06/0x00 -> BAD_OP, does not enter dedup window", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    uint8_t f[TOKEN_MFG_LEN];

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x00);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_OP, r.reason);
    TEST_ASSERT_EQUAL_UINT8(0, t.seen_n);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 55, 0x06);   // 0x06 未定义 → BAD_OP(0x05 已是 PET)
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_OP, r.reason);
    TEST_ASSERT_EQUAL_UINT8(0, t.seen_n);
}

// op 0x05 = 宠物开关:balance 复用为宠物载荷(0=关 / ≥1=开且=类型),进入去重窗口
TEST_CASE("op 0x05 -> PET, balance carries pet type", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    uint8_t f[TOKEN_MFG_LEN];

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 1, 3, 0x05);          // 开启,宠物类型=3
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_PET, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_OK, r.reason);
    TEST_ASSERT_EQUAL_UINT32(3, r.balance);
    TEST_ASSERT_EQUAL_UINT8(1, t.seen_n);

    token_bcast_init(&t, SELF, fake_mac, NULL);
    build_frame(f, SELF, 2, 0, 0x05);          // 关闭(balance=0)
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_PET, r.action);
    TEST_ASSERT_EQUAL_UINT32(0, r.balance);
}

// ---------------- 限速 ----------------

TEST_CASE("rate limit: 1000 FAIL, 2000 RATE_LIMITED, 4500 FAIL", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 1, 0x01);
    f[18] ^= 0xFF;

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 1000, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 2000, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_MAC_RATE_LIMITED, r.reason);

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 4500, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);
}

TEST_CASE("rate limit: fresh init first bad-mac at now_ms=500 must FAIL", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 1, 0x01);
    f[18] ^= 0xFF;

    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 500, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);
}

TEST_CASE("rate limit: wraparound must not falsely rate-limit", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    t.bad_mac_ms = 0xFFFFF000u;
    t.bad_mac_seen = true;
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 1, 0x01);
    f[18] ^= 0xFF;

    // 实际间隔 = 0x00000100 - 0xFFFFF000 (mod 2^32) = 0x1100 = 4352ms > 3000ms
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0x00000100u, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_FAIL, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_BAD_MAC, r.reason);
}

// ---------------- enabled=false ----------------

TEST_CASE("disabled: legit packet -> IGNORE/DISABLED", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    t.enabled = false;
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 100, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_IGNORE, r.action);
    TEST_ASSERT_EQUAL_INT(TOKEN_R_DISABLED, r.reason);
}

// ---------------- balance 边界 ----------------

TEST_CASE("balance==0 is read correctly", "[token_bcast]") {
    token_bcast_t t; token_result_t r;
    token_bcast_init(&t, SELF, fake_mac, NULL);
    uint8_t f[TOKEN_MFG_LEN];
    build_frame(f, SELF, 1, 0, 0x01);
    token_bcast_handle(&t, f, TOKEN_MFG_LEN, 0, &r);
    TEST_ASSERT_EQUAL_INT(TOKEN_ACT_ADD, r.action);
    TEST_ASSERT_EQUAL_UINT32(0, r.balance);
}
