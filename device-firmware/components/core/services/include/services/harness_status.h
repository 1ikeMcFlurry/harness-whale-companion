#pragma once

#include <stddef.h>
#include <stdint.h>

#define HARNESS_STATUS_WIRE_VERSION_V1 1u
#define HARNESS_STATUS_WIRE_VERSION    2u
#define HARNESS_STATUS_WIRE_BASE_SIZE  21u
#define HARNESS_STATUS_TITLE_MAX_BYTES 72u
#define HARNESS_STATUS_WIRE_MAX_SIZE   (HARNESS_STATUS_WIRE_BASE_SIZE + 1u + HARNESS_STATUS_TITLE_MAX_BYTES)
#define HARNESS_QUESTION_WIRE_VERSION  1u
#define HARNESS_QUESTION_MAX_OPTIONS   4u
#define HARNESS_QUESTION_LABEL_MAX_BYTES 36u

typedef enum {
    HARNESS_STATE_OFFLINE = 0,
    HARNESS_STATE_IDLE,
    HARNESS_STATE_THINKING,
    HARNESS_STATE_TOOL,
    HARNESS_STATE_WAITING,
    HARNESS_STATE_DONE,
    HARNESS_STATE_ERROR,
    HARNESS_STATE_STOPPED,
    HARNESS_STATE_QUESTION,
} harness_state_t;

typedef enum {
    HARNESS_TOOL_NONE = 0,
    HARNESS_TOOL_TERMINAL,
    HARNESS_TOOL_READ,
    HARNESS_TOOL_EDIT,
    HARNESS_TOOL_SEARCH,
    HARNESS_TOOL_WEB,
    HARNESS_TOOL_TASK,
    HARNESS_TOOL_OTHER = 0xFF,
} harness_tool_t;

typedef enum {
    HARNESS_CURRENCY_NONE = 0,
    HARNESS_CURRENCY_CNY,
    HARNESS_CURRENCY_USD,
} harness_currency_t;

enum {
    HARNESS_FLAG_HAS_BALANCE       = 1u << 0,
    HARNESS_FLAG_BALANCE_AVAILABLE = 1u << 1,
    HARNESS_FLAG_NEW_TURN          = 1u << 2,
};

typedef struct {
    uint8_t version;
    harness_state_t state;
    harness_tool_t tool;
    uint8_t flags;
    uint32_t seq;
    uint32_t elapsed_s;
    uint16_t todo_done;
    uint16_t todo_total;
    uint32_t balance_minor;
    harness_currency_t currency;
    uint8_t title_len;
    char title[HARNESS_STATUS_TITLE_MAX_BYTES + 1u];
} harness_status_t;

typedef struct {
    uint8_t option_count;
    char labels[HARNESS_QUESTION_MAX_OPTIONS][HARNESS_QUESTION_LABEL_MAX_BYTES + 1u];
} harness_question_t;

enum {
    HARNESS_PARSE_OK = 0,
    HARNESS_PARSE_BAD_LENGTH = -1,
    HARNESS_PARSE_BAD_VERSION = -2,
    HARNESS_PARSE_BAD_VALUE = -3,
};

int harness_status_parse(const uint8_t *payload, size_t len, harness_status_t *out);
int harness_question_parse(const uint8_t *payload, size_t len, harness_question_t *out);
