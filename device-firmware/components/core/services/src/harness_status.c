#include "services/harness_status.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int tool_is_valid(uint8_t tool) {
    return tool <= HARNESS_TOOL_TASK || tool == HARNESS_TOOL_OTHER;
}

static int utf8_is_valid(const uint8_t *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i++];
        if (c < 0x80) continue;
        size_t trail;
        uint32_t code;
        if ((c & 0xE0u) == 0xC0u) { trail = 1; code = c & 0x1Fu; }
        else if ((c & 0xF0u) == 0xE0u) { trail = 2; code = c & 0x0Fu; }
        else if ((c & 0xF8u) == 0xF0u) { trail = 3; code = c & 0x07u; }
        else return 0;
        if (i + trail > len) return 0;
        for (size_t n = 0; n < trail; ++n) {
            uint8_t t = s[i++];
            if ((t & 0xC0u) != 0x80u) return 0;
            code = (code << 6) | (uint32_t)(t & 0x3Fu);
        }
        if ((trail == 1 && code < 0x80u) ||
            (trail == 2 && code < 0x800u) ||
            (trail == 3 && code < 0x10000u) ||
            code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) return 0;
    }
    return 1;
}

int harness_status_parse(const uint8_t *payload, size_t len, harness_status_t *out) {
    if (!payload || !out || len < HARNESS_STATUS_WIRE_BASE_SIZE) {
        return HARNESS_PARSE_BAD_LENGTH;
    }
    if (payload[0] != HARNESS_STATUS_WIRE_VERSION_V1 &&
        payload[0] != HARNESS_STATUS_WIRE_VERSION) {
        return HARNESS_PARSE_BAD_VERSION;
    }

    uint8_t title_len = 0;
    if (payload[0] == HARNESS_STATUS_WIRE_VERSION_V1) {
        if (len != HARNESS_STATUS_WIRE_BASE_SIZE) return HARNESS_PARSE_BAD_LENGTH;
    } else {
        if (len < HARNESS_STATUS_WIRE_BASE_SIZE + 1u) return HARNESS_PARSE_BAD_LENGTH;
        title_len = payload[HARNESS_STATUS_WIRE_BASE_SIZE];
        if (title_len > HARNESS_STATUS_TITLE_MAX_BYTES ||
            len != HARNESS_STATUS_WIRE_BASE_SIZE + 1u + title_len) {
            return HARNESS_PARSE_BAD_LENGTH;
        }
        if (!utf8_is_valid(&payload[HARNESS_STATUS_WIRE_BASE_SIZE + 1u], title_len)) {
            return HARNESS_PARSE_BAD_VALUE;
        }
    }

    harness_status_t parsed = {
        .version = payload[0],
        .state = (harness_state_t)payload[1],
        .tool = (harness_tool_t)payload[2],
        .flags = payload[3],
        .seq = read_le32(&payload[4]),
        .elapsed_s = read_le32(&payload[8]),
        .todo_done = read_le16(&payload[12]),
        .todo_total = read_le16(&payload[14]),
        .balance_minor = read_le32(&payload[16]),
        .currency = (harness_currency_t)payload[20],
        .title_len = title_len,
    };
    if (title_len > 0) {
        memcpy(parsed.title, &payload[HARNESS_STATUS_WIRE_BASE_SIZE + 1u], title_len);
    }
    parsed.title[title_len] = '\0';

    const uint8_t known_flags = HARNESS_FLAG_HAS_BALANCE |
                                HARNESS_FLAG_BALANCE_AVAILABLE |
                                HARNESS_FLAG_NEW_TURN;
    if (parsed.state > HARNESS_STATE_QUESTION ||
        !tool_is_valid((uint8_t)parsed.tool) ||
        (parsed.flags & (uint8_t)~known_flags) != 0 ||
        parsed.todo_done > parsed.todo_total ||
        parsed.currency > HARNESS_CURRENCY_USD) {
        return HARNESS_PARSE_BAD_VALUE;
    }
    if ((parsed.flags & HARNESS_FLAG_HAS_BALANCE) == 0) {
        if (parsed.balance_minor != 0 || parsed.currency != HARNESS_CURRENCY_NONE ||
            (parsed.flags & HARNESS_FLAG_BALANCE_AVAILABLE) != 0) {
            return HARNESS_PARSE_BAD_VALUE;
        }
    } else if (parsed.currency == HARNESS_CURRENCY_NONE) {
        return HARNESS_PARSE_BAD_VALUE;
    }

    *out = parsed;
    return HARNESS_PARSE_OK;
}

int harness_question_parse(const uint8_t *payload, size_t len, harness_question_t *out) {
    if (!payload || !out || len < 2u || payload[0] != HARNESS_QUESTION_WIRE_VERSION) {
        return !payload || !out || len < 2u ? HARNESS_PARSE_BAD_LENGTH : HARNESS_PARSE_BAD_VERSION;
    }
    uint8_t count = payload[1];
    if (count > HARNESS_QUESTION_MAX_OPTIONS) return HARNESS_PARSE_BAD_VALUE;
    harness_question_t parsed = { .option_count = count };
    size_t offset = 2u;
    for (uint8_t i = 0; i < count; ++i) {
        if (offset >= len) return HARNESS_PARSE_BAD_LENGTH;
        uint8_t label_len = payload[offset++];
        if (label_len == 0u || label_len > HARNESS_QUESTION_LABEL_MAX_BYTES ||
            offset + label_len > len) {
            return HARNESS_PARSE_BAD_LENGTH;
        }
        if (!utf8_is_valid(&payload[offset], label_len)) return HARNESS_PARSE_BAD_VALUE;
        memcpy(parsed.labels[i], &payload[offset], label_len);
        parsed.labels[i][label_len] = '\0';
        offset += label_len;
    }
    if (offset != len) return HARNESS_PARSE_BAD_LENGTH;
    *out = parsed;
    return HARNESS_PARSE_OK;
}
