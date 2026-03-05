#include "starlight_input.h"

static uint8_t g_prev_mouse_buttons = 0u;

#define SL_KEY_REPEAT_SLOTS 32u
#define SL_KEY_REPEAT_DELAY_MS_DEFAULT 250u
#define SL_KEY_REPEAT_DELAY_MS_MIN 100u
#define SL_KEY_REPEAT_DELAY_MS_MAX 1000u
#define SL_KEY_REPEAT_RATE_HZ_DEFAULT 30u
#define SL_KEY_REPEAT_RATE_HZ_MIN 5u
#define SL_KEY_REPEAT_RATE_HZ_MAX 60u

typedef struct {
    uint8_t down;
    uint16_t code;
    uint64_t next_repeat_ns;
} sl_key_repeat_slot_t;

static sl_key_repeat_slot_t g_key_repeat[SL_KEY_REPEAT_SLOTS];
static uint32_t g_key_repeat_delay_ms = SL_KEY_REPEAT_DELAY_MS_DEFAULT;
static uint32_t g_key_repeat_rate_hz = SL_KEY_REPEAT_RATE_HZ_DEFAULT;

static uint64_t sl_repeat_delay_ns(void) {
    return (uint64_t)g_key_repeat_delay_ms * 1000000ull;
}

static uint64_t sl_repeat_interval_ns(void) {
    uint32_t hz = g_key_repeat_rate_hz ? g_key_repeat_rate_hz : SL_KEY_REPEAT_RATE_HZ_DEFAULT;
    return 1000000000ull / (uint64_t)hz;
}

static uint16_t sl_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) return (uint16_t)(code & 0x00FFu);
    return code;
}

static uint8_t sl_is_modifier_key(uint16_t code) {
    uint16_t norm = sl_norm_keycode(code);
    return (norm == PEOS_KEY_LSHIFT || norm == PEOS_KEY_RSHIFT ||
            norm == PEOS_KEY_LCTRL || norm == PEOS_KEY_RCTRL_NORM ||
            norm == PEOS_KEY_LALT || norm == PEOS_KEY_RALT_NORM ||
            norm == PEOS_KEY_LSUPER_NORM || norm == PEOS_KEY_RSUPER_NORM) ? 1u : 0u;
}

static sl_key_repeat_slot_t *sl_find_repeat_slot(uint16_t code) {
    for (uint32_t i = 0u; i < SL_KEY_REPEAT_SLOTS; i++) {
        if (g_key_repeat[i].down && g_key_repeat[i].code == code) return &g_key_repeat[i];
    }
    return NULL;
}

static sl_key_repeat_slot_t *sl_find_free_repeat_slot(void) {
    for (uint32_t i = 0u; i < SL_KEY_REPEAT_SLOTS; i++) {
        if (!g_key_repeat[i].down) return &g_key_repeat[i];
    }
    return NULL;
}

static void sl_repeat_on_key_event(const peos_input_event_t *ev, uint64_t now_ns) {
    if (!ev) return;
    sl_key_repeat_slot_t *slot = sl_find_repeat_slot(ev->code);
    if (ev->pressed) {
        if (!slot) {
            slot = sl_find_free_repeat_slot();
            if (!slot) return;
            slot->code = ev->code;
        }
        slot->down = 1u;
        slot->next_repeat_ns = now_ns + sl_repeat_delay_ns();
    } else if (slot) {
        slot->down = 0u;
        slot->code = 0u;
        slot->next_repeat_ns = 0ull;
    }
}

static void sl_emit_synthetic_repeats(sl_input_frame_t *out, uint64_t now_ns) {
    if (!out) return;
    uint64_t interval_ns = sl_repeat_interval_ns();
    for (uint32_t i = 0u; i < SL_KEY_REPEAT_SLOTS; i++) {
        sl_key_repeat_slot_t *slot = &g_key_repeat[i];
        if (!slot->down) continue;
        if (sl_is_modifier_key(slot->code)) continue;
        while (now_ns >= slot->next_repeat_ns && out->key_event_count < 16u) {
            peos_input_event_t ev;
            ev.code = slot->code;
            ev.pressed = 1u;
            ev.time_ns = now_ns;
            out->key_events[out->key_event_count++] = ev;
            slot->next_repeat_ns += interval_ns;
        }
    }
}

void sl_input_set_key_repeat_config(uint32_t delay_ms, uint32_t rate_hz) {
    if (delay_ms < SL_KEY_REPEAT_DELAY_MS_MIN) delay_ms = SL_KEY_REPEAT_DELAY_MS_MIN;
    if (delay_ms > SL_KEY_REPEAT_DELAY_MS_MAX) delay_ms = SL_KEY_REPEAT_DELAY_MS_MAX;
    if (rate_hz < SL_KEY_REPEAT_RATE_HZ_MIN) rate_hz = SL_KEY_REPEAT_RATE_HZ_MIN;
    if (rate_hz > SL_KEY_REPEAT_RATE_HZ_MAX) rate_hz = SL_KEY_REPEAT_RATE_HZ_MAX;
    g_key_repeat_delay_ms = delay_ms;
    g_key_repeat_rate_hz = rate_hz;
}

void sl_input_state_reset(void) {
    g_prev_mouse_buttons = 0u;
    peos_memset(g_key_repeat, 0, sizeof(g_key_repeat));
}

int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out) {
    if (!ctx || !out) return -1;

    peos_memset(out, 0, sizeof(*out));

    peos_input_event_t evs[16];
    peos_mouse_event_t mev;
    uint64_t n = peos_input_poll(evs, 16);
    int64_t mr = peos_input_mouse(&mev);
    if ((int64_t)n < 0 || mr < 0) return -1;
    uint64_t now_ns = peos_time_ns();

    for (uint64_t i = 0; i < n; i++) {
        sl_repeat_on_key_event(&evs[i], now_ns);
        if (out->key_event_count < 16u) {
            out->key_events[out->key_event_count++] = evs[i];
        }
    }

    sl_emit_synthetic_repeats(out, now_ns);

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
        uint8_t right_now = (uint8_t)(mev.buttons & 0x02u);
        uint8_t right_prev = (uint8_t)(g_prev_mouse_buttons & 0x02u);
        out->mouse_left_pressed = (left_now && !left_prev) ? 1u : 0u;
        out->mouse_left_released = (!left_now && left_prev) ? 1u : 0u;
        out->mouse_right_pressed = (right_now && !right_prev) ? 1u : 0u;
        out->mouse_right_released = (!right_now && right_prev) ? 1u : 0u;

        uint8_t buttons_changed = (mev.buttons != g_prev_mouse_buttons) ? 1u : 0u;
        out->mouse_updated = (out->mouse_moved ||
                              buttons_changed ||
                              out->mouse_wheel_x != 0 ||
                              out->mouse_wheel_y != 0) ? 1u : 0u;

        g_prev_mouse_buttons = mev.buttons;
    }

    return 0;
}
