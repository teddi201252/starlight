#pragma once

// The desktop layer (Soft UI plan, phase 6): the baked background with the
// top bar, the clock and the month calendar drawn on it, kept in a buffer as
// big as the screen. Restoring the desktop under a window is a row copy from
// it; each region is redrawn only when what it shows changes (the clock once
// a minute, the calendar at midnight, the bar on focus, address or volume).

#include "starlight.h"

// Build the layer from ctx->bg_frame (after it is baked) and point
// ctx->desk_frame at it. Safe to call again after the background changes.
void sl_desk_build(sl_ctx_t *ctx);
// Once a frame: redraws the regions whose data changed and marks them dirty.
void sl_desk_tick(sl_ctx_t *ctx);
void sl_desk_free(sl_ctx_t *ctx);
