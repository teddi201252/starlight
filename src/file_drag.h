#pragma once

// Files dragged from one app to another (docs/system-apps-ux-plan.md, 1.7;
// SLUI SDK 1.0.21). The app the drag starts in says which files
// ("HOST DRAG_FILE <path>", one line each) while the left button is held;
// from then on Starlight shows them by the pointer, and when the button is
// let go over another app's window that app gets "EVENT DROP_FILE" for
// each. Esc cancels. The apps keep getting the mouse as before.

#include "starlight_internal.h"
#include "starlight_input.h"

#include <stdint.h>

#define SL_FILE_DRAG_MAX 16u

// A file of the drag the app `source` (its host's userdata) starts, or adds
// to: ignored when the left button is not held (the release came first).
void sl_file_drag_add(const void *source, const char *path);
uint8_t sl_file_drag_active(void);
// The mouse and Esc, before anything else sees them: moves the files, lets
// them go, or cancels. 1 when the input was the drag's alone (Esc).
int sl_file_drag_input(sl_ctx_t *ctx, sl_input_frame_t *input);
// The files by the pointer, where the damage is.
void sl_file_drag_render(sl_ctx_t *ctx);
