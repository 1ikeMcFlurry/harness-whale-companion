// components/ports/include/hal/hal_button.h
#pragma once

// 注:HAL_BTN_PRESS 为“按下即触发”(防抖后立即,低延迟);HAL_BTN_CLICK 是
// 松手后再等短按窗口排除双击才触发(有 ~180ms 延迟)。新值置于末尾,兼容按序号
// 做上界判断的消费者。
typedef enum {
    HAL_BTN_CLICK,
    HAL_BTN_DOUBLE,
    HAL_BTN_LONG,
    HAL_BTN_PRESS,
    HAL_BTN_RELEASE,
} hal_btn_event_t;

// 回调带按钮索引(0..N-1),便于一套回调区分多个按钮。
typedef void (*hal_btn_cb_t)(int index, hal_btn_event_t e, void *user);

typedef struct hal_button_s hal_button_t;

typedef struct {
    void (*on_event)(hal_button_t *self, hal_btn_cb_t cb, void *user);
    int  (*count)(hal_button_t *self);   // 按钮数量
} hal_button_api_t;

struct hal_button_s { const hal_button_api_t *api; void *impl; };

static inline void hal_button_on_event(hal_button_t *b, hal_btn_cb_t cb, void *user) {
    b->api->on_event(b, cb, user);
}
static inline int hal_button_count(hal_button_t *b) {
    return b->api->count(b);
}
