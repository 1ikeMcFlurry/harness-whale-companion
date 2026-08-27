#include "unity.h"
#include "hal/hal_config.h"
#include "services/harness_status.h"

TEST_CASE("Harness status uses a compact dedicated BLE message", "[harness_status]") {
    TEST_ASSERT_EQUAL_HEX8(0x07, CFG_MSG_HARNESS_STATUS);
    TEST_ASSERT_EQUAL_UINT32(21u, HARNESS_STATUS_WIRE_BASE_SIZE);
    TEST_ASSERT_EQUAL_UINT32(94u, HARNESS_STATUS_WIRE_MAX_SIZE);
    TEST_ASSERT_LESS_THAN_UINT32(128u, sizeof(harness_status_t));
}

TEST_CASE("Harness status parses the canonical little-endian payload", "[harness_status]") {
    const uint8_t payload[HARNESS_STATUS_WIRE_BASE_SIZE] = {
        HARNESS_STATUS_WIRE_VERSION_V1, HARNESS_STATE_TOOL, HARNESS_TOOL_EDIT,
        HARNESS_FLAG_HAS_BALANCE | HARNESS_FLAG_BALANCE_AVAILABLE | HARNESS_FLAG_NEW_TURN,
        0x78, 0x56, 0x34, 0x12,
        0x2A, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x07, 0x00,
        0xF4, 0x01, 0x00, 0x00,
        HARNESS_CURRENCY_CNY,
    };
    harness_status_t status;

    TEST_ASSERT_EQUAL(HARNESS_PARSE_OK,
                      harness_status_parse(payload, sizeof payload, &status));
    TEST_ASSERT_EQUAL(HARNESS_STATE_TOOL, status.state);
    TEST_ASSERT_EQUAL(HARNESS_TOOL_EDIT, status.tool);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, status.seq);
    TEST_ASSERT_EQUAL_UINT32(42, status.elapsed_s);
    TEST_ASSERT_EQUAL_UINT16(3, status.todo_done);
    TEST_ASSERT_EQUAL_UINT16(7, status.todo_total);
    TEST_ASSERT_EQUAL_UINT32(500, status.balance_minor);
    TEST_ASSERT_EQUAL(HARNESS_CURRENCY_CNY, status.currency);
}

TEST_CASE("Harness v2 status carries a bounded UTF-8 task title", "[harness_status]") {
    const uint8_t payload[] = {
        HARNESS_STATUS_WIRE_VERSION, HARNESS_STATE_THINKING, HARNESS_TOOL_NONE,
        HARNESS_FLAG_NEW_TURN,
        1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        HARNESS_CURRENCY_NONE,
        12, 0xE5, 0x86, 0x99, 0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87, 0xE6, 0xA0, 0x87,
    };
    harness_status_t status;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_OK, harness_status_parse(payload, sizeof payload, &status));
    TEST_ASSERT_EQUAL_UINT8(12, status.title_len);
    TEST_ASSERT_EQUAL_STRING("写中文标", status.title);
}

TEST_CASE("Harness status rejects malformed values", "[harness_status]") {
    uint8_t payload[HARNESS_STATUS_WIRE_BASE_SIZE] = {0};
    harness_status_t status;

    payload[0] = HARNESS_STATUS_WIRE_VERSION_V1;
    payload[1] = HARNESS_STATE_IDLE;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_BAD_LENGTH,
                      harness_status_parse(payload, sizeof payload - 1u, &status));

    payload[0] = 3;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_BAD_VERSION,
                      harness_status_parse(payload, sizeof payload, &status));

    payload[0] = HARNESS_STATUS_WIRE_VERSION_V1;
    payload[12] = 2;
    payload[14] = 1;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_BAD_VALUE,
                      harness_status_parse(payload, sizeof payload, &status));

    payload[12] = payload[14] = 0;
    payload[20] = HARNESS_CURRENCY_CNY;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_BAD_VALUE,
                      harness_status_parse(payload, sizeof payload, &status));
}

TEST_CASE("Harness question parses options and clear", "[harness_status]") {
    const uint8_t payload[] = {
        HARNESS_QUESTION_WIRE_VERSION, 2,
        3, 'Y', 'e', 's',
        2, 'N', 'o',
    };
    harness_question_t question;
    TEST_ASSERT_EQUAL(HARNESS_PARSE_OK,
                      harness_question_parse(payload, sizeof payload, &question));
    TEST_ASSERT_EQUAL_UINT8(2, question.option_count);
    TEST_ASSERT_EQUAL_STRING("Yes", question.labels[0]);
    TEST_ASSERT_EQUAL_STRING("No", question.labels[1]);

    const uint8_t clear[] = { HARNESS_QUESTION_WIRE_VERSION, 0 };
    TEST_ASSERT_EQUAL(HARNESS_PARSE_OK,
                      harness_question_parse(clear, sizeof clear, &question));
    TEST_ASSERT_EQUAL_UINT8(0, question.option_count);
}
