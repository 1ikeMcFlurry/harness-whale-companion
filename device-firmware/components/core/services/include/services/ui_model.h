// components/services/include/services/ui_model.h —— 无 LVGL
#pragma once
#include <stdbool.h>

typedef struct { int battery; bool muted; char status[32]; } ui_state_t;
typedef void (*ui_observer_t)(const ui_state_t *, void *user);

typedef struct {
    ui_state_t state;
    ui_observer_t obs; void *obs_user;
} ui_model_t;

void ui_model_init(ui_model_t *);
void ui_model_subscribe(ui_model_t *, ui_observer_t, void *user);
void ui_model_set_battery(ui_model_t *, int pct);   // 内部触发 observer
void ui_model_set_muted(ui_model_t *, bool muted);
void ui_model_set_status(ui_model_t *, const char *text);
