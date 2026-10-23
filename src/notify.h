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
