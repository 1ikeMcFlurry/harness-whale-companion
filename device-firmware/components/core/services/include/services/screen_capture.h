#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SC_WIDTH 240u
#define SC_HEIGHT 320u
#define SC_TOTAL_BYTES (SC_WIDTH * SC_HEIGHT * 2u)

#define SC_OP_START  0x01u
#define SC_OP_ACK    0x02u
#define SC_OP_FINISH 0x03u
#define SC_OP_CANCEL 0x04u

#define SC_PKT_META 0x01u
#define SC_PKT_DATA 0x02u
#define SC_PKT_END  0x03u

#define SC_FORMAT_RGB565_LE 0x01u

/* Bluetooth LE notification payloads are at most 517 bytes. */
#define SC_PACKET_CACHE_BYTES 517u

typedef enum {
    SC_PARSE_OK,
    SC_PARSE_BAD_OP,
    SC_PARSE_BAD_LENGTH,
} sc_parse_result_t;

typedef enum {
    SC_PACKET_READY,
    SC_PACKET_WAIT_ACK,
    SC_PACKET_COMPLETE,
    SC_PACKET_ERROR,
} sc_packet_result_t;

typedef struct {
    uint8_t op;
    uint16_t capture_id;
    uint16_t seq;
} sc_control_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} sc_region_t;

typedef enum {
    SC_STATE_IDLE,
    SC_STATE_META,
    SC_STATE_DATA,
    SC_STATE_END,
    SC_STATE_COMPLETE,
    SC_STATE_ERROR,
    SC_STATE_CANCELLED,
} sc_state_t;

typedef struct screen_capture_s {
    uint16_t width;
    uint16_t height;
    uint16_t capture_id;
    uint16_t seq;
    uint16_t packet_count;
    uint8_t retries;
    sc_state_t state;
    bool waiting_ack;
    bool retransmit_pending;
    bool region_active;
    uint8_t pending_kind;
    size_t cached_len;
    size_t cached_pixel_n;
    uint32_t total_len;
    uint32_t crc32;
    uint32_t crc_next_pixel;
    sc_region_t region;
    const uint8_t *region_pixels;
    uint32_t region_bytes;
    uint32_t region_offset;
    uint8_t packet_cache[SC_PACKET_CACHE_BYTES];
} screen_capture_t;

sc_parse_result_t screen_capture_parse_control(const uint8_t *p, size_t n, sc_control_t *out);
void screen_capture_init(screen_capture_t *s, uint16_t width, uint16_t height);
bool screen_capture_start(screen_capture_t *s, uint16_t capture_id);
bool screen_capture_begin_region(screen_capture_t *s, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h, const uint8_t *rgb565,
                                 size_t rgb565_len);
sc_packet_result_t screen_capture_next_packet(screen_capture_t *s, uint16_t mtu_payload,
                                               uint8_t *out, size_t cap, size_t *out_len);
bool screen_capture_accept_ack(screen_capture_t *s, uint16_t capture_id, uint16_t seq);
bool screen_capture_timeout(screen_capture_t *s); /* true means retransmit, false means abort */
void screen_capture_cancel(screen_capture_t *s);
bool screen_capture_finish(screen_capture_t *s, uint32_t *crc32);

/* `crc32` is a prior return value; pass zero for a new standard CRC-32. */
uint32_t screen_capture_crc32_update(uint32_t crc32, const uint8_t *p, size_t n);
