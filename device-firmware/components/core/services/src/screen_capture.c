#include "services/screen_capture.h"

#include <string.h>

enum {
    SC_META_LEN = 14u,
    SC_DATA_HEADER_LEN = 17u,
    SC_END_LEN = 15u,
    SC_MAX_RETRIES = 3u,
};

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool region_is_row_major(const screen_capture_t *s, uint32_t first_pixel,
                                uint16_t x, uint16_t w, uint16_t h) {
    if (first_pixel != s->crc_next_pixel) {
        return false;
    }
    return h == 1u || (x == 0u && w == s->width);
}

static void cache_packet(screen_capture_t *s, size_t n, size_t pixel_n) {
    s->cached_len = n;
    s->cached_pixel_n = pixel_n;
    s->waiting_ack = true;
    s->retransmit_pending = false;
    s->retries = 0;
}

sc_parse_result_t screen_capture_parse_control(const uint8_t *p, size_t n, sc_control_t *out) {
    if (p == NULL || out == NULL || n == 0u) {
        return SC_PARSE_BAD_LENGTH;
    }

    out->op = p[0];
    out->capture_id = 0;
    out->seq = 0;
    switch (p[0]) {
    case SC_OP_START:
        return n == 1u ? SC_PARSE_OK : SC_PARSE_BAD_LENGTH;
    case SC_OP_ACK:
        if (n != 5u) {
            return SC_PARSE_BAD_LENGTH;
        }
        out->capture_id = get_u16(p + 1);
        out->seq = get_u16(p + 3);
        return SC_PARSE_OK;
    case SC_OP_FINISH:
    case SC_OP_CANCEL:
        if (n != 3u) {
            return SC_PARSE_BAD_LENGTH;
        }
        out->capture_id = get_u16(p + 1);
        return SC_PARSE_OK;
    default:
        return SC_PARSE_BAD_OP;
    }
}

void screen_capture_init(screen_capture_t *s, uint16_t width, uint16_t height) {
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->width = width;
    s->height = height;
    s->state = SC_STATE_IDLE;
}

bool screen_capture_start(screen_capture_t *s, uint16_t capture_id) {
    uint32_t pixels;

    if (s == NULL || capture_id == 0u || s->waiting_ack || s->region_active ||
        s->state == SC_STATE_META || s->state == SC_STATE_DATA || s->state == SC_STATE_END ||
        s->width == 0u || s->height == 0u || s->width > SC_WIDTH || s->height > SC_HEIGHT) {
        return false;
    }
    pixels = (uint32_t)s->width * s->height;
    s->capture_id = capture_id;
    s->seq = 0;
    s->packet_count = 0;
    s->retries = 0;
    s->waiting_ack = false;
    s->retransmit_pending = false;
    s->region_active = false;
    s->pending_kind = 0;
    s->cached_len = 0;
    s->cached_pixel_n = 0;
    s->total_len = pixels * 2u;
    s->crc32 = 0;
    s->crc_next_pixel = 0;
    s->state = SC_STATE_META;
    return true;
}

bool screen_capture_begin_region(screen_capture_t *s, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h, const uint8_t *rgb565,
                                 size_t rgb565_len) {
    uint32_t first_pixel;
    uint32_t region_bytes;

    if (s == NULL || rgb565 == NULL || s->state != SC_STATE_DATA || s->waiting_ack ||
        s->region_active || w == 0u || h == 0u || x >= s->width || y >= s->height ||
        w > (uint16_t)(s->width - x) || h > (uint16_t)(s->height - y)) {
        return false;
    }

    region_bytes = (uint32_t)w * h * 2u;
    if ((rgb565_len & 1u) != 0u || rgb565_len != region_bytes) {
        return false;
    }
    first_pixel = (uint32_t)y * s->width + x;
    if (!region_is_row_major(s, first_pixel, x, w, h)) {
        s->state = SC_STATE_ERROR;
        return false;
    }
    s->region.x = x;
    s->region.y = y;
    s->region.w = w;
    s->region.h = h;
    s->crc32 = screen_capture_crc32_update(s->crc32, rgb565, region_bytes);
    s->crc_next_pixel += (uint32_t)w * h;
    s->region_pixels = rgb565;
    s->region_bytes = region_bytes;
    s->region_offset = 0;
    s->region_active = true;
    return true;
}

sc_packet_result_t screen_capture_next_packet(screen_capture_t *s, uint16_t mtu_payload,
                                               uint8_t *out, size_t cap, size_t *out_len) {
    size_t limit;
    size_t n;
    size_t pixel_n;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (s == NULL || out == NULL || out_len == NULL) {
        return SC_PACKET_ERROR;
    }
    if (s->state == SC_STATE_COMPLETE) {
        return SC_PACKET_COMPLETE;
    }
    if (s->state == SC_STATE_ERROR || s->state == SC_STATE_CANCELLED || s->state == SC_STATE_IDLE) {
        return SC_PACKET_ERROR;
    }
    if (s->waiting_ack) {
        if (!s->retransmit_pending) {
            return SC_PACKET_WAIT_ACK;
        }
        if ((size_t)mtu_payload < s->cached_len || cap < s->cached_len) {
            return SC_PACKET_ERROR;
        }
        memcpy(out, s->packet_cache, s->cached_len);
        *out_len = s->cached_len;
        s->retransmit_pending = false;
        return SC_PACKET_READY;
    }
    limit = mtu_payload < cap ? mtu_payload : cap;
    if (s->state == SC_STATE_META) {
        if (limit < SC_META_LEN || SC_META_LEN > sizeof(s->packet_cache)) {
            return SC_PACKET_ERROR;
        }
        out[0] = SC_PKT_META;
        put_u16(out + 1, s->capture_id);
        put_u16(out + 3, s->seq);
        put_u16(out + 5, s->width);
        put_u16(out + 7, s->height);
        out[9] = SC_FORMAT_RGB565_LE;
        put_u32(out + 10, s->total_len);
        n = SC_META_LEN;
        s->pending_kind = SC_PKT_META;
        memcpy(s->packet_cache, out, n);
        cache_packet(s, n, 0);
    } else if (s->state == SC_STATE_DATA) {
        if (!s->region_active) {
            return SC_PACKET_WAIT_ACK;
        }
        if (limit <= SC_DATA_HEADER_LEN) {
            return SC_PACKET_ERROR;
        }
        pixel_n = s->region_bytes - s->region_offset;
        if (pixel_n > limit - SC_DATA_HEADER_LEN) {
            pixel_n = limit - SC_DATA_HEADER_LEN;
        }
        if (pixel_n > sizeof(s->packet_cache) - SC_DATA_HEADER_LEN) {
            pixel_n = sizeof(s->packet_cache) - SC_DATA_HEADER_LEN;
        }
        pixel_n &= ~(size_t)1u;
        if (pixel_n == 0u) {
            return SC_PACKET_ERROR;
        }
        out[0] = SC_PKT_DATA;
        put_u16(out + 1, s->capture_id);
        put_u16(out + 3, s->seq);
        put_u16(out + 5, s->region.x);
        put_u16(out + 7, s->region.y);
        put_u16(out + 9, s->region.w);
        put_u16(out + 11, s->region.h);
        put_u32(out + 13, s->region_offset);
        memcpy(out + SC_DATA_HEADER_LEN, s->region_pixels + s->region_offset, pixel_n);
        n = SC_DATA_HEADER_LEN + pixel_n;
        s->pending_kind = SC_PKT_DATA;
        memcpy(s->packet_cache, out, n);
        cache_packet(s, n, pixel_n);
    } else if (s->state == SC_STATE_END) {
        if (limit < SC_END_LEN || SC_END_LEN > sizeof(s->packet_cache)) {
            return SC_PACKET_ERROR;
        }
        out[0] = SC_PKT_END;
        put_u16(out + 1, s->capture_id);
        put_u16(out + 3, s->seq);
        put_u16(out + 5, s->packet_count);
        put_u32(out + 7, s->total_len);
        put_u32(out + 11, s->crc32);
        n = SC_END_LEN;
        s->pending_kind = SC_PKT_END;
        memcpy(s->packet_cache, out, n);
        cache_packet(s, n, 0);
    } else {
        return SC_PACKET_ERROR;
    }
    *out_len = n;
    return SC_PACKET_READY;
}

bool screen_capture_accept_ack(screen_capture_t *s, uint16_t capture_id, uint16_t seq) {
    if (s == NULL || !s->waiting_ack || capture_id != s->capture_id || seq != s->seq) {
        return false;
    }

    s->waiting_ack = false;
    s->retransmit_pending = false;
    s->retries = 0;
    if (s->pending_kind == SC_PKT_META) {
        s->state = SC_STATE_DATA;
    } else if (s->pending_kind == SC_PKT_DATA) {
        s->region_offset += (uint32_t)s->cached_pixel_n;
        ++s->packet_count;
        if (s->region_offset == s->region_bytes) {
            s->region_active = false;
            s->region_pixels = NULL;
            s->region_bytes = 0;
            s->region_offset = 0;
        }
    } else if (s->pending_kind == SC_PKT_END) {
        s->state = SC_STATE_COMPLETE;
    }
    ++s->seq;
    return true;
}

bool screen_capture_timeout(screen_capture_t *s) {
    if (s == NULL || !s->waiting_ack) {
        return false;
    }
    if (s->retransmit_pending) {
        return true;
    }
    if (s->retries < SC_MAX_RETRIES) {
        ++s->retries;
        s->retransmit_pending = true;
        return true;
    }
    s->waiting_ack = false;
    s->retransmit_pending = false;
    s->state = SC_STATE_ERROR;
    return false;
}

void screen_capture_cancel(screen_capture_t *s) {
    if (s == NULL) {
        return;
    }
    s->waiting_ack = false;
    s->retransmit_pending = false;
    s->region_active = false;
    s->region_pixels = NULL;
    s->state = SC_STATE_CANCELLED;
}

bool screen_capture_finish(screen_capture_t *s, uint32_t *crc32) {
    uint32_t pixels;

    if (s == NULL || s->state != SC_STATE_DATA || s->waiting_ack || s->region_active) {
        return false;
    }
    pixels = (uint32_t)s->width * s->height;
    if (s->crc_next_pixel != pixels) {
        return false;
    }
    if (crc32 != NULL) {
        *crc32 = s->crc32;
    }
    s->state = SC_STATE_END;
    return true;
}

uint32_t screen_capture_crc32_update(uint32_t crc32, const uint8_t *p, size_t n) {
    size_t i;
    unsigned bit;
    uint32_t value;

    if (p == NULL && n != 0u) {
        return crc32;
    }
    value = ~crc32;
    for (i = 0; i < n; ++i) {
        value ^= p[i];
        for (bit = 0; bit < 8u; ++bit) {
            value = (value >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(value & 1u));
        }
    }
    return ~value;
}
