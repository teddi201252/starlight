#pragma once

// The system menu (Technical Alpha plan, phase 1): the star of the top bar
// opens it. Informazioni su PeOS, Riavvia, Spegni; the last two ask first, in
// a dialog over a veil on the whole screen.

#include "starlight_internal.h"
#include "peos/peos.h"

// A click: 1 if the menu (or its dialog) took it.
int sl_sysmenu_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
// The cursor over the star, the menu or the dialog (-1: not ours). It also
// follows the row and the button under the mouse.
int sl_sysmenu_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y);
// Esc closes, Enter confirms: 1 if the key was ours.
int sl_sysmenu_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev);
// Drawn over everything but the cursor.
void sl_sysmenu_render(sl_ctx_t *ctx);
// The about panel, from elsewhere (Impostazioni could ask for it).
void sl_sysmenu_show_about(sl_ctx_t *ctx);
