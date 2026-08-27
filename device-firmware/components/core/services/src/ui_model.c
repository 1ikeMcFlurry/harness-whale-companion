// components/services/src/ui_model.c
#include "services/ui_model.h"
#include <string.h>

static void notify(ui_model_t *m) {
    if (m->obs) m->obs(&m->state, m->obs_user);
}

void ui_model_init(ui_model_t *m) {
    memset(m, 0, sizeof(*m));
    strncpy(m->state.status, "boot", sizeof(m->state.status) - 1);
}

void ui_model_subscribe(ui_model_t *m, ui_observer_t obs, void *user) {
    m->obs = obs;
    m->obs_user = user;
    notify(m);   // 订阅时推送一次初始状态
}

void ui_model_set_battery(ui_model_t *m, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    m->state.battery = pct;
    notify(m);
}

void ui_model_set_muted(ui_model_t *m, bool muted) {
    m->state.muted = muted;
    notify(m);
}

void ui_model_set_status(ui_model_t *m, const char *text) {
    if (!text) return;
    strncpy(m->state.status, text, sizeof(m->state.status) - 1);
    m->state.status[sizeof(m->state.status) - 1] = '\0';
    notify(m);
}
