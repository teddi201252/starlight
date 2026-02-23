#pragma once

#include "starlight.h"

#include <stdint.h>

typedef struct {
    uint8_t quit_requested;
    uint8_t mouse_updated;
    uint8_t mouse_moved;
    uint8_t mouse_left_pressed;
    uint8_t mouse_left_released;
    uint8_t mouse_buttons;
    uint8_t key_event_count;
    int32_t mouse_dx;
    int32_t mouse_dy;
    peos_input_event_t key_events[16];
} sl_input_frame_t;

void sl_input_state_reset(void);
int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out);
