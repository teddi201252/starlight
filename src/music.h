#pragma once

// The music in Starlight (release 0.13.0, phase 5; design/orfeus, "Mini
// player in Starlight"): musicd plays, Starlight shows it. A pill in the top
// bar with what plays; a click on it opens the mini player (a night card
// with the cover, the progress, previous/play/next, the volume and "Open
// Orfeus"); the media keys and Super + arrows change track from anywhere,
// with a small toast at the bottom; Orfeus's dock tile keeps its dot while
// the music goes on with its window closed.

#include "starlight.h"
#include "render/blit.h"

void sl_music_init(sl_ctx_t *ctx);
// Once a frame: what musicd says, the clock of the mini player, the toast.
void sl_music_tick(sl_ctx_t *ctx);
// Once a frame, with the overlays: the mini player and the toast.
void sl_music_render(sl_ctx_t *ctx);
// The pill in the top bar, drawn by the desktop right to left: it ends at
// `right` (bar coordinates, centred on cy) and returns where it starts, or
// `right` when nothing plays.
int32_t sl_music_draw_pill(sl_ctx_t *ctx, sl_target_t *t, int32_t right, int32_t cy, uint8_t dark_bar);
// Goes up when the pill must be drawn again.
uint32_t sl_music_version(void);
// Something is playing or paused (Orfeus's tile keeps its dot).
int sl_music_active(void);
// Clicks and keys: 1 when they were the music's.
int sl_music_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
int sl_music_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev);
// The cursor over the pill and the mini player, or -1.
int sl_music_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y);
