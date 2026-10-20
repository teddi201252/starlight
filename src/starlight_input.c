#include "starlight_input.h"

static uint8_t g_prev_mouse_buttons = 0u;

#define SL_KEY_REPEAT_SLOTS 32u
#define SL_KEY_REPEAT_DELAY_MS_DEFAULT 250u
#define SL_KEY_REPEAT_DELAY_MS_MIN 100u
#define SL_KEY_REPEAT_DELAY_MS_MAX 1000u
#define SL_KEY_REPEAT_RATE_HZ_DEFAULT 30u
#define SL_KEY_REPEAT_RATE_HZ_MIN 5u
#define SL_KEY_REPEAT_RATE_HZ_MAX 60u
#define SL_ACTIONMOD_TAP_NS 180000000ull
#define SL_ACTIONMOD_HOLD_MS_DEFAULT 400u
#define SL_ACTIONMOD_HOLD_MS_MIN 100u
#define SL_ACTIONMOD_HOLD_MS_MAX 3000u

typedef enum {
    SL_ACTIONMOD_IDLE = 0,
    SL_ACTIONMOD_PRESSED,
    SL_ACTIONMOD_CHORD_USED,
    SL_ACTIONMOD_MANAGER_OPENED,
} sl_actionmod_state_t;

typedef struct {
    sl_actionmod_state_t state;
    uint64_t press_ns;
    uint8_t caps_down;
    uint8_t caps_lock_on;
    uint8_t swallow_a;
    uint8_t swallow_s;
} sl_actionmod_tracker_t;

typedef struct {
    uint8_t down;
    uint16_t code;
    uint64_t next_repeat_ns;
} sl_key_repeat_slot_t;

static sl_key_repeat_slot_t g_key_repeat[SL_KEY_REPEAT_SLOTS];
static uint32_t g_key_repeat_delay_ms = SL_KEY_REPEAT_DELAY_MS_DEFAULT;
static uint32_t g_key_repeat_rate_hz = SL_KEY_REPEAT_RATE_HZ_DEFAULT;
static uint32_t g_actionmod_hold_ms = SL_ACTIONMOD_HOLD_MS_DEFAULT;
static sl_actionmod_tracker_t g_actionmod;

static uint64_t sl_repeat_delay_ns(void) {
    return (uint64_t)g_key_repeat_delay_ms * 1000000ull;
}

static uint64_t sl_repeat_interval_ns(void) {
    uint32_t hz = g_key_repeat_rate_hz ? g_key_repeat_rate_hz : SL_KEY_REPEAT_RATE_HZ_DEFAULT;
    return 1000000000ull / (uint64_t)hz;
}

static uint64_t sl_actionmod_hold_ns(void) {
    uint32_t hold_ms = g_actionmod_hold_ms ? g_actionmod_hold_ms : SL_ACTIONMOD_HOLD_MS_DEFAULT;
    return (uint64_t)hold_ms * 1000000ull;
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
            norm == PEOS_KEY_LSUPER_NORM || norm == PEOS_KEY_RSUPER_NORM ||
            norm == PEOS_KEY_CAPSLOCK) ? 1u : 0u;
}

static void sl_append_key_event(sl_input_frame_t *out, uint16_t code, uint8_t pressed, uint64_t now_ns) {
    peos_input_event_t ev;
    if (!out || out->key_event_count >= 16u) return;
    ev.code = code;
    ev.pressed = pressed ? 1u : 0u;
    ev.reserved = 0u;
    ev.time_ns = now_ns;
    out->key_events[out->key_event_count++] = ev;
}

static uint8_t sl_actionmod_filter_event(sl_input_frame_t *out,
                                         const peos_input_event_t *ev,
                                         uint64_t now_ns) {
    uint16_t code;
    if (!out || !ev) return 0u;
    code = sl_norm_keycode(ev->code);
    if (code == PEOS_KEY_CAPSLOCK) {
        if (ev->pressed) {
            g_actionmod.state = SL_ACTIONMOD_PRESSED;
            g_actionmod.press_ns = now_ns;
            g_actionmod.caps_down = 1u;
            g_actionmod.swallow_a = 0u;
            g_actionmod.swallow_s = 0u;
        } else {
            if (g_actionmod.state == SL_ACTIONMOD_PRESSED &&
                (now_ns - g_actionmod.press_ns) <= SL_ACTIONMOD_TAP_NS) {
                g_actionmod.caps_lock_on ^= 1u;
                sl_append_key_event(out, PEOS_KEY_CAPSLOCK, 1u, now_ns);
                sl_append_key_event(out, PEOS_KEY_CAPSLOCK, 0u, now_ns);
            }
            g_actionmod.state = SL_ACTIONMOD_IDLE;
            g_actionmod.caps_down = 0u;
            g_actionmod.swallow_a = 0u;
            g_actionmod.swallow_s = 0u;
        }
        return 1u;
    }
    if (!g_actionmod.caps_down) return 0u;
    if (code == PEOS_KEY_A) {
        if (ev->pressed) {
            out->clipboard_cmds |= SL_CLIP_CMD_COPY;
            g_actionmod.state = SL_ACTIONMOD_CHORD_USED;
            g_actionmod.swallow_a = 1u;
        } else if (!g_actionmod.swallow_a) {
            return 0u;
        } else {
            g_actionmod.swallow_a = 0u;
        }
        return 1u;
    }
    if (code == PEOS_KEY_S) {
        if (ev->pressed) {
            out->clipboard_cmds |= SL_CLIP_CMD_PASTE;
            g_actionmod.state = SL_ACTIONMOD_CHORD_USED;
            g_actionmod.swallow_s = 1u;
        } else if (!g_actionmod.swallow_s) {
            return 0u;
        } else {
            g_actionmod.swallow_s = 0u;
        }
        return 1u;
    }
    return 0u;
}

static void sl_actionmod_tick(sl_input_frame_t *out, uint64_t now_ns) {
    if (!out || g_actionmod.state != SL_ACTIONMOD_PRESSED || !g_actionmod.caps_down) return;
    if ((now_ns - g_actionmod.press_ns) < sl_actionmod_hold_ns()) return;
    out->clipboard_cmds |= SL_CLIP_CMD_OPEN_MANAGER;
    g_actionmod.state = SL_ACTIONMOD_MANAGER_OPENED;
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

void sl_input_set_actionmod_hold_config(uint32_t hold_ms) {
    if (hold_ms < SL_ACTIONMOD_HOLD_MS_MIN) hold_ms = SL_ACTIONMOD_HOLD_MS_MIN;
    if (hold_ms > SL_ACTIONMOD_HOLD_MS_MAX) hold_ms = SL_ACTIONMOD_HOLD_MS_MAX;
    g_actionmod_hold_ms = hold_ms;
}

void sl_input_state_reset(void) {
    g_prev_mouse_buttons = 0u;
    peos_memset(g_key_repeat, 0, sizeof(g_key_repeat));
    peos_memset(&g_actionmod, 0, sizeof(g_actionmod));
}

static uint8_t g_endpoint_buttons = 0u;

// Pending keys (up to `cap`) and the mouse motion since the last call: from the
// input endpoint when Starlight has one, else from the legacy poll calls.
static int sl_input_read(sl_ctx_t *ctx, peos_input_event_t *evs, uint64_t cap,
                         uint64_t *n_out, peos_mouse_event_t *mev) {
    if (ctx->input_fd < 0) {
        uint64_t n = peos_input_poll(evs, cap);
        int64_t mr = peos_input_mouse(mev);
        if ((int64_t)n < 0 || mr < 0) return -1;
        *n_out = n;
        return 0;
    }

    peos_memset(mev, 0, sizeof(*mev));
    int32_t wheel_x = 0, wheel_y = 0;
    uint64_t n = 0;
    peos_input_record_t rec[16];
    while (n < cap) {
        // never take more keys than fit: what is left stays queued for the next frame
        uint64_t want = cap - n;
        if (want > sizeof(rec) / sizeof(rec[0])) want = sizeof(rec) / sizeof(rec[0]);
        uint64_t got = peos_fread_nb((uint64_t)ctx->input_fd, rec, (size_t)want * sizeof(rec[0]));
        if (got == (uint64_t)-1 || got < sizeof(rec[0])) break;
        got /= sizeof(rec[0]);
        for (uint64_t i = 0; i < got; i++) {
            if (rec[i].type == PEOS_INPUT_KEY) {
                evs[n].code = rec[i].code;
                evs[n].pressed = rec[i].pressed;
                evs[n].reserved = 0;
                evs[n].time_ns = rec[i].time_ns;
                n++;
            } else if (rec[i].type == PEOS_INPUT_MOUSE) {
                mev->dx += rec[i].dx;
                mev->dy += rec[i].dy;
                wheel_x += rec[i].wheel_x;
                wheel_y += rec[i].wheel_y;
                g_endpoint_buttons = rec[i].buttons;
                mev->time_ns = rec[i].time_ns;
            }
        }
        if (got < want) break;
    }
    if (wheel_x > 127) wheel_x = 127;
    if (wheel_x < -128) wheel_x = -128;
    if (wheel_y > 127) wheel_y = 127;
    if (wheel_y < -128) wheel_y = -128;
    mev->wheel_x = (int8_t)wheel_x;
    mev->wheel_y = (int8_t)wheel_y;
    mev->buttons = g_endpoint_buttons;
    *n_out = n;
    return 0;
}

int sl_input_poll(sl_ctx_t *ctx, sl_input_frame_t *out) {
    if (!ctx || !out) return -1;

    peos_memset(out, 0, sizeof(*out));
    out->caps_lock_on = g_actionmod.caps_lock_on;

    peos_input_event_t evs[16];
    peos_mouse_event_t mev;
    uint64_t n = 0;
    int64_t mr = sl_input_read(ctx, evs, 16, &n, &mev);
    if (mr < 0) return -1;
    uint64_t now_ns = peos_time_ns();

    for (uint64_t i = 0; i < n; i++) {
        if (sl_actionmod_filter_event(out, &evs[i], now_ns)) {
            continue;
        }
        sl_repeat_on_key_event(&evs[i], now_ns);
        if (out->key_event_count < 16u) {
            out->key_events[out->key_event_count++] = evs[i];
        }
    }

    sl_actionmod_tick(out, now_ns);
    out->caps_lock_on = g_actionmod.caps_lock_on;

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
