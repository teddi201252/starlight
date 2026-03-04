#include "starlight_input.h"

static uint8_t g_prev_mouse_buttons = 0u;

void sl_input_state_reset(void) {
    g_prev_mouse_buttons = 0u;
}

int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out) {
    if (!ctx || !out) return -1;

    peos_memset(out, 0, sizeof(*out));

    peos_input_event_t evs[16];
    peos_mouse_event_t mev;
    uint64_t n = peos_input_poll(evs, 16);
    int64_t mr = peos_input_mouse(&mev);
    if ((int64_t)n < 0 || mr < 0) return -1;

    for (uint64_t i = 0; i < n; i++) {
        if (out->key_event_count < 16u) {
            out->key_events[out->key_event_count++] = evs[i];
        }
        if (evs[i].pressed && evs[i].code == PEOS_KEY_ESC) {
            out->quit_requested = 1u;
            break;
        }
    }

    if (mr >= 0) {
        out->mouse_dx = mev.dx;
        out->mouse_dy = mev.dy;
        out->mouse_wheel_x = (int32_t)mev.wheel_x;
        out->mouse_wheel_y = (int32_t)mev.wheel_y;

        uint32_t old_x = ctx->cursor_x;
        uint32_t old_y = ctx->cursor_y;

        int64_t nx = (int64_t)ctx->cursor_x + (int64_t)mev.dx;
        int64_t ny = (int64_t)ctx->cursor_y + (int64_t)mev.dy;

        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (ctx->fb.width > 0 && nx >= (int64_t)ctx->fb.width) nx = (int64_t)ctx->fb.width - 1;
        if (ctx->fb.height > 0 && ny >= (int64_t)ctx->fb.height) ny = (int64_t)ctx->fb.height - 1;

        ctx->cursor_x = (uint32_t)nx;
        ctx->cursor_y = (uint32_t)ny;
        out->mouse_moved = (ctx->cursor_x != old_x || ctx->cursor_y != old_y) ? 1u : 0u;

        out->mouse_buttons = mev.buttons;
        uint8_t left_now = (uint8_t)(mev.buttons & 0x01u);
        uint8_t left_prev = (uint8_t)(g_prev_mouse_buttons & 0x01u);
        out->mouse_left_pressed = (left_now && !left_prev) ? 1u : 0u;
        out->mouse_left_released = (!left_now && left_prev) ? 1u : 0u;

        uint8_t buttons_changed = (mev.buttons != g_prev_mouse_buttons) ? 1u : 0u;
        out->mouse_updated = (out->mouse_moved ||
                              buttons_changed ||
                              out->mouse_wheel_x != 0 ||
                              out->mouse_wheel_y != 0) ? 1u : 0u;

        g_prev_mouse_buttons = mev.buttons;
    }

    return 0;
}
