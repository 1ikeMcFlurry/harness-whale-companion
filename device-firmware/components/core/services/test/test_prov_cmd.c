#include "unity.h"
#include "services/prov_cmd.h"
#include <string.h>

static void parse(const char *s, prov_cmd_t *c) { prov_parse(s, (int)strlen(s), c); }

TEST_CASE("set with three fields", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=B2,pk=C3,hw=D4", &c);
    TEST_ASSERT_EQUAL_INT(PROV_CMD_SET, c.kind);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_NONE, c.err);
    TEST_ASSERT_TRUE(c.has_key); TEST_ASSERT_EQUAL_STRING("B2", c.key);
    TEST_ASSERT_TRUE(c.has_pk);  TEST_ASSERT_EQUAL_STRING("C3", c.pk);
    TEST_ASSERT_TRUE(c.has_hw);  TEST_ASSERT_EQUAL_STRING("D4", c.hw);
}

TEST_CASE("partial update: only key given", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=B2", &c);
    TEST_ASSERT_EQUAL_INT(PROV_CMD_SET, c.kind);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_NONE, c.err);
    TEST_ASSERT_TRUE(c.has_key);
    TEST_ASSERT_FALSE(c.has_pk);      // 局部更新:未给的字段不写,不擦分区
    TEST_ASSERT_FALSE(c.has_hw);
}

TEST_CASE("param order does not matter", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=hw=D4,key=B2,pk=C3", &c);
    TEST_ASSERT_EQUAL_STRING("B2", c.key);
    TEST_ASSERT_EQUAL_STRING("C3", c.pk);
    TEST_ASSERT_EQUAL_STRING("D4", c.hw);
}

TEST_CASE("no writable field at all is missing_param", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_MISSING_PARAM, c.err);
}

TEST_CASE("empty value is missing_param", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=,pk=C3", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_MISSING_PARAM, c.err);
}

TEST_CASE("sn parameter is rejected loudly, not ignored", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=sn=deadbeef,key=B2", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_SN_READONLY, c.err);
    TEST_ASSERT_FALSE(c.has_key);     // 整条指令拒绝,不做部分生效
}

TEST_CASE("command prefix is case insensitive but values are not", "[prov_cmd]") {
    prov_cmd_t c; parse("at+cardid=key=AbCd", &c);
    TEST_ASSERT_EQUAL_INT(PROV_CMD_SET, c.kind);
    TEST_ASSERT_EQUAL_STRING("AbCd", c.key);   // 不能被小写化
}

TEST_CASE("over-long key is too_long", "[prov_cmd]") {
    // 33 个字符,超过 PROV_KEY_MAX=32
    prov_cmd_t c; parse("AT+CARDID=key=123456789012345678901234567890123", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_TOO_LONG, c.err);
}

TEST_CASE("key of exactly max length is accepted", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=12345678901234567890123456789012", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_NONE, c.err);
    TEST_ASSERT_EQUAL_INT(32, (int)strlen(c.key));
}

TEST_CASE("query command", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID?", &c);
    TEST_ASSERT_EQUAL_INT(PROV_CMD_QUERY, c.kind);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_NONE, c.err);
}

TEST_CASE("trailing CRLF is stripped", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=B2\r\n", &c);
    TEST_ASSERT_EQUAL_STRING("B2", c.key);
}

TEST_CASE("control chars in value are rejected", "[prov_cmd]") {
    // 操作员按退格键的典型场景。串口不回显 key,这里不拦就永远发现不了。
    prov_cmd_t c; parse("AT+CARDID=key=a1b2\x08""c3", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BAD_CHAR, c.err);
    TEST_ASSERT_FALSE(c.has_key);
}

TEST_CASE("space in value is rejected", "[prov_cmd]") {
    prov_cmd_t c; parse("AT+CARDID=key=a b", &c);
    TEST_ASSERT_EQUAL_INT(PROV_ERR_BAD_CHAR, c.err);
}

TEST_CASE("garbage inputs do not crash", "[prov_cmd]") {
    prov_cmd_t c;
    parse("", &c);        TEST_ASSERT_EQUAL_INT(PROV_CMD_NONE, c.kind);
    parse("AT+", &c);     TEST_ASSERT_EQUAL_INT(PROV_ERR_UNKNOWN, c.err);
    parse("hello", &c);   TEST_ASSERT_EQUAL_INT(PROV_ERR_UNKNOWN, c.err);
    parse("\r\n", &c);    TEST_ASSERT_EQUAL_INT(PROV_CMD_NONE, c.kind);
    // 老指令 AT+TOKENSEQ 已作废,现在必须落到"不认识的指令",而不是假装成功。
    parse("AT+TOKENSEQ=0", &c); TEST_ASSERT_EQUAL_INT(PROV_ERR_UNKNOWN, c.err);
}
