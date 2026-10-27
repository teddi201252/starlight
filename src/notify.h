#pragma once

// Desktop notifications: the "notify" service, the pill at the top right and
// the counts on the dock tiles (Soft UI plan, phase 7).

#include "starlight.h"

void sl_notify_init(sl_ctx_t *ctx);
// Once a frame, before drawing: takes new messages, times the pill out.
void sl_notify_tick(sl_ctx_t *ctx);
// Once a frame, after the windows and the dock: the pill on top.
void sl_notify_render(sl_ctx_t *ctx);
// Notifications not yet seen for the app with this title (0 if none).
uint32_t sl_notify_badge(const char *title);
// The notification centre: a click on the time in the top bar opens it, a
// click elsewhere or Esc closes it. 1 when the event was its.
int sl_notify_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
int sl_notify_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev);
// A notification from Starlight itself (a widget's app not installed...).
void sl_notify_post(sl_ctx_t *ctx, const char *source, const char *text);
// Open or close the centre (the clock widget).
void sl_notify_toggle_center(sl_ctx_t *ctx);
