#pragma once

// Energia (docs/system-apps-ux-plan.md, Impostazioni, the user's design
// "Energia", 2026-11-19): the screen goes dark after a while without input
// (screen_off_battery= on the battery, screen_off_ac= on the charger or
// without a battery, in minutes, 0 never), not while music plays or an app
// shows its own full surface; a key or the mouse lights it again, and that
// first input goes no further. On the battery the light goes down to 60%
// of what it was (battery_dim=1) and back on the charger; at 10% and 5% a
// notification (battery_warn=1), once a discharge. No polling: a deadline
// from the last input.

#include "starlight_internal.h"
#include "starlight_input.h"

#include <stdint.h>

// The input of a frame, before anything else: 1 when it only woke the
// screen (then nothing else gets it).
int sl_energy_input(sl_ctx_t *ctx, const sl_input_frame_t *input);
// Every frame: the battery's news, the deadline reached.
void sl_energy_tick(sl_ctx_t *ctx);
// The screen's deadline for the idle desktop.
void sl_energy_wake(void);
// The screen is dark: nothing drawn shows (the cursor neither).
uint8_t sl_energy_screen_off(void);
// The last layer: black over all of the damage while the screen is dark.
void sl_energy_render(sl_ctx_t *ctx);
