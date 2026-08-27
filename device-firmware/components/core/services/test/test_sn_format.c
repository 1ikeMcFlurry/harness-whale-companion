#include "unity.h"
#include "services/sn_format.h"
#include <string.h>

TEST_CASE("typical mac formats lowercase without colons", "[sn_format]") {
    const uint8_t mac[6] = {0x8c, 0xbf, 0xea, 0x89, 0xf2, 0x2c};
    char sn[SN_STR_LEN + 1];
    sn_from_mac(mac, sn);
    TEST_ASSERT_EQUAL_STRING("8cbfea89f22c", sn);
}

TEST_CASE("zero and high bytes are zero-padded", "[sn_format]") {
    const uint8_t mac[6] = {0x00, 0x0f, 0xff, 0x10, 0xa0, 0x01};
    char sn[SN_STR_LEN + 1];
    sn_from_mac(mac, sn);
    TEST_ASSERT_EQUAL_STRING("000fff10a001", sn);
}

TEST_CASE("output is always 12 chars and NUL terminated", "[sn_format]") {
    const uint8_t mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    char sn[SN_STR_LEN + 2];
    memset(sn, 'X', sizeof sn);
    sn_from_mac(mac, sn);
    TEST_ASSERT_EQUAL_INT(SN_STR_LEN, (int)strlen(sn));
    TEST_ASSERT_EQUAL_CHAR('\0', sn[SN_STR_LEN]);
    TEST_ASSERT_EQUAL_CHAR('X', sn[SN_STR_LEN + 1]);   // 不越界写
}

TEST_CASE("byte order is not reversed", "[sn_format]") {
    const uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    char sn[SN_STR_LEN + 1];
    sn_from_mac(mac, sn);
    TEST_ASSERT_EQUAL_STRING("010203040506", sn);   // 不是 "060504030201"
}
