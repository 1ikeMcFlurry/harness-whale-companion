#include "unity.h"
#include "hal/hal_config.h"
#include "services/screen_capture.h"

#include <string.h>

TEST_CASE("screen capture uses the dedicated BLE message type", "[screen_capture]") {
    TEST_ASSERT_EQUAL_HEX8(0x06, CFG_MSG_SCREEN_CAPTURE);
}

TEST_CASE("screen capture state leaves internal RAM for the BLE controller", "[screen_capture]") {
    /* ESP32-C3 has no PSRAM. The capture task already owns its stack and packet
     * cache, so protocol bookkeeping must stay below 1 KiB of permanent RAM. */
    TEST_ASSERT_LESS_THAN_UINT32(1024u, sizeof(screen_capture_t));
}

static void start_and_ack_meta(screen_capture_t *s, uint16_t width, uint16_t height,
                               uint16_t capture_id) {
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(s, width, height);
    TEST_ASSERT_TRUE(screen_capture_start(s, capture_id));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(s, capture_id, 0));
}

static void send_region(screen_capture_t *s, uint16_t capture_id) {
    uint8_t out[SC_PACKET_CACHE_BYTES];
    size_t n = 0;

    while (s->region_active) {
        TEST_ASSERT_EQUAL(SC_PACKET_READY,
                          screen_capture_next_packet(s, 244, out, sizeof out, &n));
        TEST_ASSERT_TRUE(screen_capture_accept_ack(s, capture_id, s->seq));
    }
}

static bool capture_is_active(const screen_capture_t *s) {
    return s->state == SC_STATE_META || s->state == SC_STATE_DATA ||
           s->state == SC_STATE_END;
}

static void assert_capture_terminal(screen_capture_t *s, uint16_t old_capture_id) {
    uint8_t out[SC_PACKET_CACHE_BYTES];
    size_t n = 0;

    TEST_ASSERT_FALSE(capture_is_active(s));
    TEST_ASSERT_FALSE(screen_capture_accept_ack(s, old_capture_id, s->seq));
    TEST_ASSERT_NOT_EQUAL(SC_PACKET_READY,
                          screen_capture_next_packet(s, 244, out, sizeof out, &n));
}

TEST_CASE("screen capture parses control commands", "[screen_capture]") {
    sc_control_t c;
    TEST_ASSERT_EQUAL(SC_PARSE_OK,
                      screen_capture_parse_control((uint8_t[]){SC_OP_START}, 1, &c));
    TEST_ASSERT_EQUAL(SC_OP_START, c.op);
    TEST_ASSERT_EQUAL(SC_PARSE_OK, screen_capture_parse_control(
        (uint8_t[]){SC_OP_ACK, 0x34, 0x12, 0x02, 0x00}, 5, &c));
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.capture_id);
    TEST_ASSERT_EQUAL_UINT16(2, c.seq);
    TEST_ASSERT_EQUAL(SC_PARSE_BAD_LENGTH,
        screen_capture_parse_control((uint8_t[]){SC_OP_ACK, 1}, 2, &c));
}

TEST_CASE("screen capture emits deterministic meta", "[screen_capture]") {
    screen_capture_t s;
    uint8_t out[32]; size_t n = 0;
    screen_capture_init(&s, 240, 320);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x1234));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_EQUAL_UINT8(14, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        ((uint8_t[]){SC_PKT_META,0x34,0x12,0x00,0x00,0xF0,0x00,0x40,0x01,
                     SC_FORMAT_RGB565_LE,0x00,0x58,0x02,0x00}), out, 14);
}

TEST_CASE("screen capture rejects an ACK with another capture id or sequence", "[screen_capture]") {
    screen_capture_t s;
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x1234));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_FALSE(screen_capture_accept_ack(&s, 0x1235, 0));
    TEST_ASSERT_FALSE(screen_capture_accept_ack(&s, 0x1234, 1));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x1234, 0));
}

TEST_CASE("screen capture retransmits the exact cached packet three times then aborts", "[screen_capture]") {
    screen_capture_t s;
    uint8_t first[32];
    uint8_t retry[32];
    size_t n = 0;
    size_t retry_n = 0;
    unsigned i;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 7));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, first, sizeof first, &n));
    TEST_ASSERT_EQUAL(SC_PACKET_WAIT_ACK,
                      screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
    for (i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(screen_capture_timeout(&s));
        TEST_ASSERT_EQUAL(SC_PACKET_READY,
                          screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
        TEST_ASSERT_EQUAL_UINT(n, retry_n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(first, retry, n);
        TEST_ASSERT_EQUAL(SC_PACKET_WAIT_ACK,
                          screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
    }
    TEST_ASSERT_FALSE(screen_capture_timeout(&s));
    TEST_ASSERT_EQUAL(SC_PACKET_ERROR,
                      screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
}

TEST_CASE("screen capture refuses a retry that no longer fits the MTU", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixels[] = { 0x34, 0x12, 0x78, 0x56 };
    uint8_t first[32];
    uint8_t retry[32];
    size_t n = 0;
    size_t retry_n = 0;

    start_and_ack_meta(&s, 2, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 2, 1, pixels, sizeof pixels));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, first, sizeof first, &n));
    TEST_ASSERT_TRUE(screen_capture_timeout(&s));
    TEST_ASSERT_EQUAL(SC_PACKET_ERROR,
                      screen_capture_next_packet(&s, (uint16_t)(n - 1u), retry, sizeof retry, &retry_n));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
    TEST_ASSERT_EQUAL_UINT(n, retry_n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, retry, n);
    TEST_ASSERT_EQUAL(SC_PACKET_WAIT_ACK,
                      screen_capture_next_packet(&s, 244, retry, sizeof retry, &retry_n));
}

TEST_CASE("screen capture can restart after cancellation", "[screen_capture]") {
    screen_capture_t s;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 1));
    screen_capture_cancel(&s);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 2));
}

TEST_CASE("screen capture rejects odd and short region buffers", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixel[] = { 0x34, 0x12 };

    start_and_ack_meta(&s, 1, 1, 1);
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, 1));
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel - 1u));
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
}

TEST_CASE("screen capture accepts the bottom-right pixel and rejects out-of-bounds regions", "[screen_capture]") {
    screen_capture_t s;
    uint8_t row[SC_WIDTH * 2u] = { 0 };
    uint8_t final_prefix[(SC_WIDTH - 1u) * 2u] = { 0 };
    const uint8_t pixel[] = { 0x34, 0x12 };
    uint16_t y;

    start_and_ack_meta(&s, SC_WIDTH, SC_HEIGHT, 1);
    for (y = 0; y < SC_HEIGHT - 1u; ++y) {
        TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, y, SC_WIDTH, 1, row, sizeof row));
        send_region(&s, 1);
    }
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, SC_HEIGHT - 1u, SC_WIDTH - 1u, 1,
                                                  final_prefix, sizeof final_prefix));
    send_region(&s, 1);
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 239, 319, 1, 1, pixel, sizeof pixel));

    start_and_ack_meta(&s, SC_WIDTH, SC_HEIGHT, 2);
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 239, 319, 2, 1, pixel, sizeof pixel));
}

TEST_CASE("screen capture rejects out-of-order regions before changing coverage", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixel[] = { 0x34, 0x12 };
    uint8_t out[32];
    size_t n = 0;

    start_and_ack_meta(&s, 2, 1, 1);
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 1, 0, 1, 1, pixel, sizeof pixel));
    TEST_ASSERT_EQUAL(SC_PACKET_ERROR,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
}

TEST_CASE("screen capture rejects a second or overlapping active region", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixel[] = { 0x34, 0x12 };
    uint8_t out[32];
    size_t n = 0;

    start_and_ack_meta(&s, 1, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 1, 1));
    TEST_ASSERT_FALSE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
}

TEST_CASE("screen capture will not finish with missing pixels", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixel[] = { 0x34, 0x12 };
    uint8_t out[32];
    size_t n = 0;
    uint32_t crc = 0;

    start_and_ack_meta(&s, 2, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 1, 1));
    TEST_ASSERT_FALSE(screen_capture_finish(&s, &crc));
}

TEST_CASE("screen capture emits exact data and end packets", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixels[] = { 0x34, 0x12, 0x78, 0x56 };
    uint8_t out[32];
    size_t n = 0;
    uint32_t crc = 0;

    start_and_ack_meta(&s, 2, 1, 0x1234);
    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 2, 1, pixels, sizeof pixels));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 19, out, sizeof out, &n));
    TEST_ASSERT_EQUAL_UINT8(19, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        ((uint8_t[]){SC_PKT_DATA,0x34,0x12,0x01,0x00,0x00,0x00,0x00,0x00,
                     0x02,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x34,0x12}), out, 19);
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x1234, 1));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 19, out, sizeof out, &n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        ((uint8_t[]){SC_PKT_DATA,0x34,0x12,0x02,0x00,0x00,0x00,0x00,0x00,
                     0x02,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x78,0x56}), out, 19);
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x1234, 2));
    TEST_ASSERT_TRUE(screen_capture_finish(&s, &crc));
    TEST_ASSERT_EQUAL_HEX32(0x2441C0CB, crc);
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_EQUAL_UINT8(15, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        ((uint8_t[]){SC_PKT_END,0x34,0x12,0x03,0x00,0x02,0x00,0x04,0x00,
                     0x00,0x00,0xCB,0xC0,0x41,0x24}), out, 15);
}

TEST_CASE("screen capture crc32 helper matches the standard ASCII vector", "[screen_capture]") {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926,
                            screen_capture_crc32_update(0, (const uint8_t *)"123456789", 9));
}

TEST_CASE("screen capture accepts an ACK at the timeout boundary", "[screen_capture]") {
    screen_capture_t s;
    const uint8_t pixel[] = { 0x34, 0x12 };
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x101));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x101, 0));
    TEST_ASSERT_FALSE(screen_capture_timeout(&s));
    TEST_ASSERT_TRUE(capture_is_active(&s));

    TEST_ASSERT_TRUE(screen_capture_begin_region(&s, 0, 0, 1, 1, pixel, sizeof pixel));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x101, 1));
    TEST_ASSERT_TRUE(screen_capture_finish(&s, NULL));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x101, 2));
    assert_capture_terminal(&s, 0x101);
}

TEST_CASE("screen capture cancellation wakes a packet wait into a terminal state",
          "[screen_capture]") {
    screen_capture_t s;
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x201));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    screen_capture_cancel(&s);
    assert_capture_terminal(&s, 0x201);
}

TEST_CASE("screen capture disconnect cleanup after META is terminal", "[screen_capture]") {
    screen_capture_t s;
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x301));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_TRUE(screen_capture_accept_ack(&s, 0x301, 0));
    screen_capture_cancel(&s); /* the app maps disconnect to the same core cleanup */
    assert_capture_terminal(&s, 0x301);
}

TEST_CASE("screen capture rejects a stale ACK after allocating a new capture id",
          "[screen_capture]") {
    screen_capture_t s;
    uint8_t out[32];
    size_t n = 0;

    screen_capture_init(&s, 1, 1);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x401));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    screen_capture_cancel(&s);
    TEST_ASSERT_TRUE(screen_capture_start(&s, 0x402));
    TEST_ASSERT_EQUAL(SC_PACKET_READY,
                      screen_capture_next_packet(&s, 244, out, sizeof out, &n));
    TEST_ASSERT_FALSE(screen_capture_accept_ack(&s, 0x401, 0));
    TEST_ASSERT_TRUE(capture_is_active(&s));
    TEST_ASSERT_TRUE(s.waiting_ack);

    screen_capture_cancel(&s);
    assert_capture_terminal(&s, 0x401);
    TEST_ASSERT_FALSE(screen_capture_accept_ack(&s, 0x402, 0));
}
