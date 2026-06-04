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
    peos_input_event_t key_events[16];
} sl_input_frame_t;

#define SL_CLIP_CMD_COPY         (1u << 0)
#define SL_CLIP_CMD_PASTE        (1u << 1)
#define SL_CLIP_CMD_OPEN_MANAGER (1u << 2)

void sl_input_state_reset(void);
void sl_input_set_key_repeat_config(uint32_t delay_ms, uint32_t rate_hz);
int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out);
