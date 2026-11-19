#pragma once

#include "starlight.h"

#include <stdint.h>

typedef struct {
    uint8_t quit_requested;
    uint8_t mouse_updated;
    uint8_t mouse_moved;
    uint8_t mouse_left_pressed;
    uint8_t mouse_left_released;
    uint8_t mouse_right_pressed;
    uint8_t mouse_right_released;
    uint8_t mouse_buttons;
    uint8_t caps_lock_on;
    uint8_t key_event_count;
    int32_t mouse_dx;
    int32_t mouse_dy;
    int32_t mouse_wheel_x;
    int32_t mouse_wheel_y;
    uint32_t clipboard_cmds;
    uint32_t window_cmds;           // SL_WIN_CMD_*: Caps with a key, for the windows
    peos_input_event_t key_events[16];
} sl_input_frame_t;

// Caps with a key, for the windows (docs/system-apps-ux-plan.md, phase 4;
// the user's choice of 2026-11-18: Caps for the clipboard and the windows).
#define SL_WIN_CMD_NEXT          (1u << 0)   // Caps+Tab: the next window up, a minimized one too
#define SL_WIN_CMD_CLOSE         (1u << 1)   // Caps+W: close the window in front (the app may ask first)
#define SL_WIN_CMD_MIN           (1u << 2)   // Caps+M: minimize it
#define SL_WIN_CMD_MANAGER       (1u << 3)   // Caps+Space: open or close the app manager

#define SL_CLIP_CMD_COPY         (1u << 0)
#define SL_CLIP_CMD_PASTE        (1u << 1)
#define SL_CLIP_CMD_OPEN_MANAGER (1u << 2)
// Caps+Z and Caps+Y (release 0.14.0, phase 3): undo and redo in a text field.
#define SL_CLIP_CMD_UNDO         (1u << 3)
#define SL_CLIP_CMD_REDO         (1u << 4)
// Caps+D (docs/system-apps-ux-plan.md, phase 3; the user's choice of
// 2026-11-18): cut, in a text field or in an app (Curiosity: a file).
#define SL_CLIP_CMD_CUT          (1u << 5)

void sl_input_state_reset(void);
void sl_input_set_key_repeat_config(uint32_t delay_ms, uint32_t rate_hz);
void sl_input_set_actionmod_hold_config(uint32_t hold_ms);
// Caps held alone toward the clipboard manager: 0..1000 of the hold (0 when
// not), and how long it has been held.
uint32_t sl_input_caps_hold_permille(uint64_t now_ns, uint64_t *held_ns);
int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out);
// A key held (its next repeat) or Caps held (the clipboard manager opens): when.
void sl_input_wake(void);
