#include "ui_sdk.h"

static int sl_ui_point_in_rect(uint32_t px, uint32_t py,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released,
                 int32_t wheel_x, int32_t wheel_y) {
    if (!ui) return;
    ui->ctx = ctx;
    ui->win = win;
    ui->cursor_x = cursor_x;
    ui->cursor_y = cursor_y;
    ui->mouse_down = mouse_down;
    ui->mouse_pressed = mouse_pressed;
    ui->mouse_released = mouse_released;
    ui->wheel_x = wheel_x;
    ui->wheel_y = wheel_y;
    ui->wheel_consumed = 0u;
}

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || !text) return;
    sl_draw_text(ui->ctx, x, y, text, r, g, b);
}

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text) {
    return sl_ui_button_state(ui, id, x, y, w, h, text, 1u);
}

uint8_t sl_ui_button_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !text || w < 8u || h < 8u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) {
        ui->active_id = id;
    }

    int active = (ui->active_id == id);
    uint8_t br = 100u, bg = 100u, bb = 100u;
    if (!enabled) {
        br = 58u; bg = 58u; bb = 58u;
    } else {
        if (hovered) { br = 120u; bg = 120u; bb = 120u; }
        if (active && ui->mouse_down) { br = 80u; bg = 80u; bb = 80u; }
    }

    sl_fill_rect(ui->ctx, x, y, w, h, br, bg, bb);
    if (enabled) {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 220u, 220u, 220u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 40u, 40u, 40u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 220u, 220u, 220u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 40u, 40u, 40u);
        sl_draw_text(ui->ctx, x + 6u, y + ((h > 12u) ? ((h - 8u) / 2u) : 0u), text, 255u, 255u, 255u);
    } else {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 100u, 100u, 100u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 28u, 28u, 28u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 100u, 100u, 100u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 28u, 28u, 28u);
        sl_draw_text(ui->ctx, x + 6u, y + ((h > 12u) ? ((h - 8u) / 2u) : 0u), text, 170u, 170u, 170u);
        if (active) ui->active_id = 0u;
    }

    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }

    return 0u;
}

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value) {
    return sl_ui_slider_u32_state(ui, id, x, y, w, min_v, max_v, value, 1u);
}

uint8_t sl_ui_slider_u32_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w,
                               uint32_t min_v, uint32_t max_v, uint32_t *value,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 24u || min_v > max_v) return 0u;

    uint32_t h = 16u;
    uint32_t knob_w = 10u;
    uint32_t track_x = x + 2u;
    uint32_t track_w = w - 4u;

    if (enabled && sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h) && ui->mouse_pressed) {
        ui->active_id = id;
    }

    uint8_t changed = 0u;
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t cx = ui->cursor_x;
        if (cx < track_x) cx = track_x;
        if (cx > track_x + track_w) cx = track_x + track_w;

        uint32_t range = max_v - min_v;
        uint32_t nv = min_v;
        if (range > 0u && track_w > 0u) {
            nv = min_v + (uint32_t)(((uint64_t)(cx - track_x) * (uint64_t)range) / (uint64_t)track_w);
        }
        if (nv != *value) {
            *value = nv;
            changed = 1u;
        }
    }

    if (ui->mouse_released && ui->active_id == id) {
        ui->active_id = 0u;
    }

    sl_fill_rect(ui->ctx, x, y + 6u, w, 4u, enabled ? 70u : 52u, enabled ? 70u : 52u, enabled ? 70u : 52u);

    uint32_t knob_x = track_x;
    if (max_v > min_v) {
        uint32_t range = max_v - min_v;
        uint32_t rel = *value;
        if (rel < min_v) rel = min_v;
        if (rel > max_v) rel = max_v;
        rel -= min_v;
        knob_x = track_x + (uint32_t)(((uint64_t)rel * (uint64_t)track_w) / (uint64_t)range);
    }
    if (knob_x + knob_w > x + w) knob_x = (x + w > knob_w) ? (x + w - knob_w) : x;

    if (enabled) {
        sl_fill_rect(ui->ctx, knob_x, y + 2u, knob_w, 12u, 190u, 190u, 80u);
    } else {
        sl_fill_rect(ui->ctx, knob_x, y + 2u, knob_w, 12u, 120u, 120u, 120u);
    }
    return changed;
}

static uint16_t sl_ui_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) {
        return (uint16_t)(code & 0x00FFu);
    }
    return code;
}

static uint32_t sl_ui_strlen(const char *s) {
    uint32_t n = 0u;
    if (!s) return 0u;
    while (s[n]) n++;
    return n;
}

static char sl_ui_key_to_char(uint16_t code, uint8_t shift) {
    switch (code) {
        case PEOS_KEY_A: return shift ? 'A' : 'a';
        case PEOS_KEY_B: return shift ? 'B' : 'b';
        case PEOS_KEY_C: return shift ? 'C' : 'c';
        case PEOS_KEY_D: return shift ? 'D' : 'd';
        case PEOS_KEY_E: return shift ? 'E' : 'e';
        case PEOS_KEY_F: return shift ? 'F' : 'f';
        case PEOS_KEY_G: return shift ? 'G' : 'g';
        case PEOS_KEY_H: return shift ? 'H' : 'h';
        case PEOS_KEY_I: return shift ? 'I' : 'i';
        case PEOS_KEY_J: return shift ? 'J' : 'j';
        case PEOS_KEY_K: return shift ? 'K' : 'k';
        case PEOS_KEY_L: return shift ? 'L' : 'l';
        case PEOS_KEY_M: return shift ? 'M' : 'm';
        case PEOS_KEY_N: return shift ? 'N' : 'n';
        case PEOS_KEY_O: return shift ? 'O' : 'o';
        case PEOS_KEY_P: return shift ? 'P' : 'p';
        case PEOS_KEY_Q: return shift ? 'Q' : 'q';
        case PEOS_KEY_R: return shift ? 'R' : 'r';
        case PEOS_KEY_S: return shift ? 'S' : 's';
        case PEOS_KEY_T: return shift ? 'T' : 't';
        case PEOS_KEY_U: return shift ? 'U' : 'u';
        case PEOS_KEY_V: return shift ? 'V' : 'v';
        case PEOS_KEY_W: return shift ? 'W' : 'w';
        case PEOS_KEY_X: return shift ? 'X' : 'x';
        case PEOS_KEY_Y: return shift ? 'Y' : 'y';
        case PEOS_KEY_Z: return shift ? 'Z' : 'z';
        case PEOS_KEY_SPACE: return ' ';
        case 0x0002u: return shift ? '!' : '1';
        case 0x0003u: return shift ? '@' : '2';
        case 0x0004u: return shift ? '#' : '3';
        case 0x0005u: return shift ? '$' : '4';
        case 0x0006u: return shift ? '%' : '5';
        case 0x0007u: return shift ? '^' : '6';
        case 0x0008u: return shift ? '&' : '7';
        case 0x0009u: return shift ? '*' : '8';
        case 0x000Au: return shift ? '(' : '9';
        case 0x000Bu: return shift ? ')' : '0';
        case 0x000Cu: return shift ? '_' : '-';
        case 0x000Du: return shift ? '+' : '=';
        case 0x001Au: return shift ? '{' : '[';
        case 0x001Bu: return shift ? '}' : ']';
        case 0x0027u: return shift ? ':' : ';';
        case 0x0028u: return shift ? '"' : '\'';
        case 0x0029u: return shift ? '~' : '`';
        case 0x002Bu: return shift ? '|' : '\\';
        case 0x0033u: return shift ? '<' : ',';
        case 0x0034u: return shift ? '>' : '.';
        case 0x0035u: return shift ? '?' : '/';
        default: return '\0';
    }
}

uint8_t sl_ui_toggle(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint8_t *value, const char *text) {
    return sl_ui_toggle_state(ui, id, x, y, w, h, value, text, 1u);
}

uint8_t sl_ui_toggle_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint8_t *value, const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 20u || h < 12u) return 0u;
    if (!text) text = "";

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            *value = *value ? 0u : 1u;
            changed = 1u;
        }
    }

    uint8_t br = hovered ? 88u : 72u;
    if (active && ui->mouse_down) br = 58u;
    if (!enabled) br = 56u;
    sl_fill_rect(ui->ctx, x, y, w, h, br, br, br);
    if (enabled) {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 200u, 200u, 200u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 30u, 30u, 30u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 200u, 200u, 200u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 30u, 30u, 30u);
    } else {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 110u, 110u, 110u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 24u, 24u, 24u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 110u, 110u, 110u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 24u, 24u, 24u);
    }

    uint32_t sw_h = (h > 6u) ? (h - 6u) : 6u;
    uint32_t sw_w = sw_h * 2u;
    if (sw_w < 20u) sw_w = 20u;
    if (sw_w + 8u > w) sw_w = (w > 8u) ? (w - 8u) : w;
    uint32_t sw_x = x + w - sw_w - 4u;
    uint32_t sw_y = y + ((h > sw_h) ? ((h - sw_h) / 2u) : 0u);

    if (*value) sl_fill_rect(ui->ctx, sw_x, sw_y, sw_w, sw_h, enabled ? 70u : 80u, enabled ? 150u : 80u, enabled ? 80u : 80u);
    else sl_fill_rect(ui->ctx, sw_x, sw_y, sw_w, sw_h, 90u, 90u, 90u);

    uint32_t knob_w = (sw_w > 4u) ? (sw_w / 2u - 1u) : sw_w;
    uint32_t knob_x = *value ? (sw_x + sw_w - knob_w - 1u) : (sw_x + 1u);
    sl_fill_rect(ui->ctx, knob_x, sw_y + 1u, knob_w, (sw_h > 2u) ? (sw_h - 2u) : sw_h,
                 enabled ? 230u : 170u, enabled ? 230u : 170u, enabled ? 230u : 170u);

    if (text[0]) {
        uint32_t text_x = x + 6u;
        uint32_t text_y = y + ((h > 12u) ? ((h - 8u) / 2u) : 0u);
        if (text_x + 8u < sw_x) {
            sl_draw_text(ui->ctx, text_x, text_y, text, enabled ? 245u : 170u, enabled ? 245u : 170u, enabled ? 245u : 170u);
        }
    }
    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

uint8_t sl_ui_select_cycle(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t option_count, uint32_t *selected,
                           const char *current_text) {
    return sl_ui_select_cycle_state(ui, id, x, y, w, h, option_count, selected, current_text, 1u);
}

uint8_t sl_ui_select_cycle_state(sl_ui_ctx_t *ui, uint32_t id,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 uint32_t option_count, uint32_t *selected,
                                 const char *current_text, uint8_t enabled) {
    if (!ui || !ui->ctx || !selected || !current_text || w < 16u || h < 12u) return 0u;
    if (option_count == 0u) return 0u;
    if (*selected >= option_count) *selected = 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            *selected = (*selected + 1u) % option_count;
            changed = 1u;
        }
    }

    uint8_t br = hovered ? 92u : 74u;
    if (active && ui->mouse_down) br = 60u;
    if (!enabled) br = 56u;
    sl_fill_rect(ui->ctx, x, y, w, h, br, br, br);
    if (enabled) {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 210u, 210u, 210u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 30u, 30u, 30u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 210u, 210u, 210u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 30u, 30u, 30u);
    } else {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 110u, 110u, 110u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 24u, 24u, 24u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 110u, 110u, 110u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 24u, 24u, 24u);
    }

    uint32_t text_y = y + ((h > 12u) ? ((h - 8u) / 2u) : 0u);
    sl_draw_text(ui->ctx, x + 6u, text_y, current_text,
                 enabled ? 255u : 170u, enabled ? 255u : 170u, enabled ? 255u : 170u);
    sl_draw_text(ui->ctx, x + w - 10u, text_y, ">", enabled ? 220u : 140u, enabled ? 220u : 140u, enabled ? 220u : 140u);
    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

uint8_t sl_ui_text_input(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         char *text, uint32_t text_cap,
                         const peos_input_event_t *events, uint8_t event_count) {
    return sl_ui_text_input_state(ui, id, x, y, w, h, text, text_cap, events, event_count, 1u);
}

uint8_t sl_ui_text_input_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               char *text, uint32_t text_cap,
                               const peos_input_event_t *events, uint8_t event_count,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !text || text_cap < 2u || w < 16u || h < 12u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && ui->mouse_pressed) {
        if (hovered) ui->focused_id = id;
        else if (ui->focused_id == id) ui->focused_id = 0u;
    }
    if (!enabled && ui->focused_id == id) ui->focused_id = 0u;
    int focused = (ui->focused_id == id);

    uint32_t len = sl_ui_strlen(text);
    if (len >= text_cap) {
        len = text_cap - 1u;
        text[len] = '\0';
    }

    uint8_t changed = 0u;
    for (uint8_t i = 0u; i < event_count; i++) {
        uint16_t code = sl_ui_norm_keycode(events[i].code);
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
            ui->shift_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (!enabled || !focused || !events[i].pressed) continue;
        if (code == 0x000Eu) {
            if (len > 0u) {
                len--;
                text[len] = '\0';
                changed = 1u;
            }
            continue;
        }
        if (code == PEOS_KEY_ESC) {
            ui->focused_id = 0u;
            focused = 0;
            continue;
        }
        if (code == PEOS_KEY_ENTER) {
            ui->focused_id = 0u;
            focused = 0;
            continue;
        }

        char c = sl_ui_key_to_char(code, ui->shift_down);
        if (c && (len + 1u < text_cap)) {
            text[len++] = c;
            text[len] = '\0';
            changed = 1u;
        }
    }

    uint8_t bg = focused ? 42u : 26u;
    if (!enabled) bg = 24u;
    sl_fill_rect(ui->ctx, x, y, w, h, bg, bg, bg);
    if (focused && enabled) {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 120u, 200u, 255u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 120u, 200u, 255u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 120u, 200u, 255u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 120u, 200u, 255u);
    } else {
        sl_fill_rect(ui->ctx, x, y, w, 1u, 130u, 130u, 130u);
        sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 30u, 30u, 30u);
        sl_fill_rect(ui->ctx, x, y, 1u, h, 130u, 130u, 130u);
        sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 30u, 30u, 30u);
    }

    uint32_t vis_chars = (w > 12u) ? ((w - 12u) / 8u) : 0u;
    if (vis_chars > 0u) {
        uint32_t start = (len > vis_chars) ? (len - vis_chars) : 0u;
        char draw_buf[192];
        uint32_t n = 0u;
        while (text[start + n] && n + 2u < (uint32_t)sizeof(draw_buf)) {
            draw_buf[n] = text[start + n];
            n++;
        }
        if (enabled && focused && ((peos_time_ns() / 500000000ull) & 1ull) && n + 2u < (uint32_t)sizeof(draw_buf)) {
            draw_buf[n++] = '_';
        }
        draw_buf[n] = '\0';
        sl_draw_text(ui->ctx, x + 6u, y + ((h > 12u) ? ((h - 8u) / 2u) : 0u), draw_buf,
                     enabled ? 240u : 150u, enabled ? 240u : 150u, enabled ? 240u : 150u);
    }
    return changed;
}

uint8_t sl_ui_scroll_v(sl_ui_ctx_t *ui, uint32_t id,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t content_h, uint32_t *offset,
                       uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h) {
    return sl_ui_scroll_v_state(ui, id, x, y, w, h, content_h, offset, hit_x, hit_y, hit_w, hit_h, 1u);
}

uint8_t sl_ui_scroll_v_state(sl_ui_ctx_t *ui, uint32_t id,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t content_h, uint32_t *offset,
                             uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h,
                             uint8_t enabled) {
    if (!ui || !ui->ctx || !offset || w < 8u || h < 20u) return 0u;

    uint32_t max_off = 0u;
    if (content_h > h) max_off = content_h - h;
    if (*offset > max_off) *offset = max_off;

    uint32_t track_x = x + 2u;
    uint32_t track_y = y + 2u;
    uint32_t track_w = (w > 4u) ? (w - 4u) : w;
    uint32_t track_h = (h > 4u) ? (h - 4u) : h;
    if (track_w == 0u || track_h < 8u) return 0u;

    uint32_t knob_h = track_h;
    if (content_h > h && content_h > 0u) {
        knob_h = (uint32_t)(((uint64_t)track_h * (uint64_t)h) / (uint64_t)content_h);
        if (knob_h < 12u) knob_h = 12u;
        if (knob_h > track_h) knob_h = track_h;
    }
    uint32_t travel = (track_h > knob_h) ? (track_h - knob_h) : 0u;
    uint32_t knob_y = track_y;
    if (travel > 0u && max_off > 0u) {
        knob_y = track_y + (uint32_t)(((uint64_t)(*offset) * (uint64_t)travel) / (uint64_t)max_off);
    }

    if (hit_w == 0u || hit_h == 0u) {
        hit_x = x;
        hit_y = y;
        hit_w = w;
        hit_h = h;
    }

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, hit_x, hit_y, hit_w, hit_h);
    int bar_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    int knob_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, track_x, knob_y, track_w, knob_h);
    if (enabled && bar_hovered && ui->mouse_pressed) {
        ui->active_id = id;
        if (!knob_hovered && travel > 0u && max_off > 0u) {
            uint32_t cy = ui->cursor_y;
            if (cy < track_y) cy = track_y;
            if (cy > track_y + track_h) cy = track_y + track_h;
            uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
            if (knob_pos > travel) knob_pos = travel;
            *offset = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        }
    }

    uint8_t changed = 0u;
    if (enabled && hovered && !ui->wheel_consumed && max_off > 0u) {
        int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
        if (wheel != 0) {
            int32_t step = (int32_t)(h / 5u);
            if (step < 12) step = 12;
            if (step > 64) step = 64;

            int64_t nv = (int64_t)(*offset) - ((int64_t)wheel * (int64_t)step);
            if (nv < 0) nv = 0;
            if (nv > (int64_t)max_off) nv = (int64_t)max_off;
            if ((uint32_t)nv != *offset) {
                *offset = (uint32_t)nv;
                changed = 1u;
            }
            ui->wheel_consumed = 1u;
        }
    }

    if (enabled && ui->active_id == id && ui->mouse_down && travel > 0u && max_off > 0u) {
        uint32_t cy = ui->cursor_y;
        if (cy < track_y) cy = track_y;
        if (cy > track_y + track_h) cy = track_y + track_h;
        uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
        if (knob_pos > travel) knob_pos = travel;
        uint32_t nv = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        if (nv != *offset) {
            *offset = nv;
            changed = 1u;
        }
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    sl_fill_rect(ui->ctx, x, y, w, h, enabled ? 36u : 30u, enabled ? 36u : 30u, enabled ? 36u : 30u);
    sl_fill_rect(ui->ctx, x, y, w, 1u, 140u, 140u, 140u);
    sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 30u, 30u, 30u);
    sl_fill_rect(ui->ctx, x, y, 1u, h, 140u, 140u, 140u);
    sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 30u, 30u, 30u);

    sl_fill_rect(ui->ctx, track_x, track_y, track_w, track_h, enabled ? 54u : 44u, enabled ? 54u : 44u, enabled ? 54u : 44u);
    uint8_t kb = (ui->active_id == id && ui->mouse_down) ? 170u : (knob_hovered ? 155u : 140u);
    if (!enabled) kb = 112u;
    sl_fill_rect(ui->ctx, track_x, knob_y, track_w, knob_h, kb, kb, kb);
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    return changed;
}

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ui || !ui->ctx || !rgba || w == 0u || h == 0u || src_w == 0u || src_h == 0u) return;
    sl_blit_rgba_stretch(ui->ctx, x, y, w, h, rgba, src_w, src_h, src_stride);
}

void sl_ui_separator(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w) {
    if (!ui || !ui->ctx || w == 0u) return;
    sl_fill_rect(ui->ctx, x, y, w, 1u, 72u, 72u, 72u);
    sl_fill_rect(ui->ctx, x, y + 1u, w, 1u, 28u, 28u, 28u);
}

uint8_t sl_ui_list_item(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const char *title, const char *subtitle, uint8_t enabled) {
    if (!title) title = "";
    if (!subtitle) subtitle = "";
    if (!ui || !ui->ctx || w < 16u || h < 16u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) changed = 1u;
    }

    uint8_t bg = hovered ? 56u : 46u;
    if (active && ui->mouse_down) bg = 40u;
    if (!enabled) bg = 36u;
    sl_fill_rect(ui->ctx, x, y, w, h, bg, bg, bg);
    sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 28u, 28u, 28u);

    sl_draw_text(ui->ctx, x + 8u, y + 4u, title,
                 enabled ? 242u : 160u, enabled ? 242u : 160u, enabled ? 242u : 160u);
    if (subtitle[0]) {
        uint32_t sub_y = y + ((h > 22u) ? 14u : 12u);
        sl_draw_text(ui->ctx, x + 8u, sub_y, subtitle,
                     enabled ? 184u : 128u, enabled ? 184u : 128u, enabled ? 184u : 128u);
    }
    sl_draw_text(ui->ctx, x + w - 10u, y + ((h > 12u) ? ((h - 8u) / 2u) : 0u), ">",
                 enabled ? 180u : 120u, enabled ? 180u : 120u, enabled ? 180u : 120u);

    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

void sl_ui_stack_begin(sl_ui_stack_t *stack,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       sl_ui_stack_axis_t axis, uint32_t spacing) {
    if (!stack) return;
    stack->x = x;
    stack->y = y;
    stack->w = w;
    stack->h = h;
    stack->spacing = spacing;
    stack->cursor = 0u;
    stack->axis = axis;
}

uint8_t sl_ui_stack_next(sl_ui_stack_t *stack, uint32_t primary_size,
                         uint32_t *out_x, uint32_t *out_y,
                         uint32_t *out_w, uint32_t *out_h) {
    if (!stack || !out_x || !out_y || !out_w || !out_h || primary_size == 0u) return 0u;

    if (stack->axis == SL_UI_STACK_VERTICAL) {
        if (stack->cursor > stack->h) return 0u;
        if (primary_size > (stack->h - stack->cursor)) return 0u;

        *out_x = stack->x;
        *out_y = stack->y + stack->cursor;
        *out_w = stack->w;
        *out_h = primary_size;
    } else {
        if (stack->cursor > stack->w) return 0u;
        if (primary_size > (stack->w - stack->cursor)) return 0u;

        *out_x = stack->x + stack->cursor;
        *out_y = stack->y;
        *out_w = primary_size;
        *out_h = stack->h;
    }

    uint32_t next_cursor = stack->cursor + primary_size;
    if (next_cursor < stack->cursor) return 0u;
    if (stack->spacing > 0u) {
        if (next_cursor > 0xFFFFFFFFu - stack->spacing) return 0u;
        next_cursor += stack->spacing;
    }
    stack->cursor = next_cursor;
    return 1u;
}

void sl_ui_grid_begin(sl_ui_grid_t *grid,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t cols, uint32_t rows,
                      uint32_t gap_x, uint32_t gap_y) {
    if (!grid) return;
    grid->x = x;
    grid->y = y;
    grid->w = w;
    grid->h = h;
    grid->cols = cols;
    grid->rows = rows;
    grid->gap_x = gap_x;
    grid->gap_y = gap_y;
    grid->cursor = 0u;
}

static uint8_t sl_ui_grid_metrics(const sl_ui_grid_t *grid,
                                  uint32_t *cell_w, uint32_t *cell_h) {
    if (!grid || !cell_w || !cell_h || grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t gaps_x = (uint64_t)(grid->cols - 1u) * (uint64_t)grid->gap_x;
    uint64_t gaps_y = (uint64_t)(grid->rows - 1u) * (uint64_t)grid->gap_y;
    if (gaps_x >= (uint64_t)grid->w || gaps_y >= (uint64_t)grid->h) return 0u;

    *cell_w = (uint32_t)(((uint64_t)grid->w - gaps_x) / (uint64_t)grid->cols);
    *cell_h = (uint32_t)(((uint64_t)grid->h - gaps_y) / (uint64_t)grid->rows);
    return (*cell_w > 0u && *cell_h > 0u) ? 1u : 0u;
}

uint8_t sl_ui_grid_cell(const sl_ui_grid_t *grid, uint32_t col, uint32_t row,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (col >= grid->cols || row >= grid->rows) return 0u;

    uint32_t cell_w = 0u;
    uint32_t cell_h = 0u;
    if (!sl_ui_grid_metrics(grid, &cell_w, &cell_h)) return 0u;

    uint64_t step_x = (uint64_t)cell_w + (uint64_t)grid->gap_x;
    uint64_t step_y = (uint64_t)cell_h + (uint64_t)grid->gap_y;
    uint64_t px = (uint64_t)grid->x + ((uint64_t)col * step_x);
    uint64_t py = (uint64_t)grid->y + ((uint64_t)row * step_y);
    if (px > 0xFFFFFFFFu || py > 0xFFFFFFFFu) return 0u;

    *out_x = (uint32_t)px;
    *out_y = (uint32_t)py;
    *out_w = cell_w;
    *out_h = cell_h;
    return 1u;
}

uint8_t sl_ui_grid_next(sl_ui_grid_t *grid,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t total = (uint64_t)grid->cols * (uint64_t)grid->rows;
    if ((uint64_t)grid->cursor >= total) return 0u;

    uint32_t col = grid->cursor % grid->cols;
    uint32_t row = grid->cursor / grid->cols;
    if (!sl_ui_grid_cell(grid, col, row, out_x, out_y, out_w, out_h)) return 0u;

    grid->cursor++;
    return 1u;
}
