#include "unity.h"
#include "services/ble_match.h"

TEST_CASE("exact 4-byte prefix matches", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48, 0x42};
    TEST_ASSERT_TRUE(ble_match_is_heartbeat(d, 4));
}
TEST_CASE("prefix with extra payload matches", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48, 0x42, 0x01, 0x02, 0x03};
    TEST_ASSERT_TRUE(ble_match_is_heartbeat(d, 7));
}
TEST_CASE("wrong company id rejected", "[ble_match]") {
    const uint8_t d[] = {0x4C, 0x00, 0x48, 0x42};   // Apple 0x004C
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(d, 4));
}
TEST_CASE("wrong magic rejected", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x58, 0x42};   // 'X''B'
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(d, 4));
}
TEST_CASE("too short rejected", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48};
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(d, 3));
}
TEST_CASE("null pointer rejected", "[ble_match]") {
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(NULL, 4));
}

// ---- classify:心跳 / token / 无关 三分类 ----
TEST_CASE("classify: bare 4-byte prefix is heartbeat", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48, 0x42};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_HEARTBEAT, ble_match_classify(d, 4));
}
TEST_CASE("classify: hdr 0x12 is token", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48, 0x42, 0x12, 0x01};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_TOKEN, ble_match_classify(d, 6));
}
TEST_CASE("classify: other 5th byte stays heartbeat", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48, 0x42, 0x02, 0x01};   // 旧 type 值,不再是 token
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_HEARTBEAT, ble_match_classify(d, 6));
}
TEST_CASE("classify: wrong prefix is none", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x58, 0x42, 0x12};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_NONE, ble_match_classify(d, 5));
}
TEST_CASE("classify: too short or null is none", "[ble_match]") {
    const uint8_t d[] = {0xFF, 0xFF, 0x48};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_NONE, ble_match_classify(d, 3));
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_NONE, ble_match_classify(NULL, 26));
}
TEST_CASE("is_heartbeat wrapper excludes token", "[ble_match]") {
    const uint8_t hb[]  = {0xFF, 0xFF, 0x48, 0x42};
    const uint8_t tok[] = {0xFF, 0xFF, 0x48, 0x42, 0x12};
    TEST_ASSERT_TRUE (ble_match_is_heartbeat(hb, 4));
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(tok, 5));   // token 不能再被当心跳
}

TEST_CASE("classify: exact v2 profile header is profile not heartbeat", "[ble_match]") {
    const uint8_t profile[26] = {'H','B',0x23,0x01};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_PROFILE, ble_match_classify(profile, 26));
    TEST_ASSERT_FALSE(ble_match_is_heartbeat(profile, 26));
}

TEST_CASE("classify: v3 common and nickname packets are batch profile", "[ble_match]") {
    const uint8_t common[26] = {'H','B',0x34};
    const uint8_t nickname[26] = {'H','B',0x35};
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_PROFILE_BATCH, ble_match_classify(common,26));
    TEST_ASSERT_EQUAL_INT(BLE_MATCH_PROFILE_BATCH, ble_match_classify(nickname,26));
}
