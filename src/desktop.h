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
// The battery as the poller last read it: percent (-1: none) and PEOS_BATTERY_*.
void sl_desk_battery(int32_t *pct, uint32_t *flags);
// The clock's next minute, the pill's end, the first start's hello.
void sl_desk_wake(const sl_ctx_t *ctx);
void sl_desk_free(sl_ctx_t *ctx);
// The local time as "hh:mm" (the timezone of settings.cfg).
void sl_desk_local_hhmm(const sl_ctx_t *ctx, char *out, size_t cap);

// The widgets answer the mouse on the desktop (outside windows and dock):
// hover lights them, a click is their shortcut, the volume follows a drag
// and the wheel. covered: something else is under the mouse.
void sl_desk_hover(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t covered);
int sl_desk_press(sl_ctx_t *ctx, uint32_t x, uint32_t y);       // 1: a widget took it
uint8_t sl_desk_held(void);                                      // a widget is held down
void sl_desk_drag(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_desk_release(sl_ctx_t *ctx, uint32_t x, uint32_t y);
int sl_desk_wheel(sl_ctx_t *ctx, uint32_t x, uint32_t y, int32_t delta);   // 1: taken
// The volume, mute and screen light keys: 1 when taken.
int sl_desk_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev);
// The system volume (0..100), and setting it as the volume widget does
// (save: written to audio.cfg, at the end of a drag).
int32_t sl_desk_volume_level(void);
void sl_desk_volume_set_level(sl_ctx_t *ctx, int32_t v, uint8_t save);
// The pill those keys show at the bottom of the screen, for a moment (a layer).
void sl_desk_pill_render(sl_ctx_t *ctx);
// The hand over a widget, or -1.
int sl_desk_cursor(uint32_t x, uint32_t y);

// The top bar's height: the time at its right opens the notification centre.
#define SL_DESK_BAR_HEIGHT 44u
// Where the time in the top bar starts (its left edge); 0 before it is drawn.
uint32_t sl_desk_time_x(void);
