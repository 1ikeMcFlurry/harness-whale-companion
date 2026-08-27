#pragma once

#include <stdbool.h>

#include "services/harness_status.h"

/* Create the persistent Harness screen while the home screen is active. */
void ui_harness_create(void);

/* Apply one validated BLE snapshot. Call only while holding the LVGL lock. */
void ui_harness_update(const harness_status_t *status);
void ui_harness_question_update(const harness_question_t *question);
bool ui_harness_question_active(void);
void ui_harness_question_move(int delta);
int ui_harness_question_submit(void);

/* Screen lifecycle. A user close suppresses heartbeats until a NEW_TURN packet. */
void ui_harness_open(void);
void ui_harness_close(void);
bool ui_harness_is_active(void);
bool ui_harness_should_auto_open(void);

/* Freeze motion and title scrolling around the synchronous QA redraw. */
void ui_harness_set_capture_freeze(bool freeze);
