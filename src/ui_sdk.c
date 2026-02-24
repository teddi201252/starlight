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
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released) {
    if (!ui) return;
    ui->ctx = ctx;
    ui->win = win;
    ui->cursor_x = cursor_x;
    ui->cursor_y = cursor_y;
    ui->mouse_down = mouse_down;
    ui->mouse_pressed = mouse_pressed;
    ui->mouse_released = mouse_released;
}

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || !text) return;
    sl_draw_text(ui->ctx, x, y, text, r, g, b);
}

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text) {
    if (!ui || !ui->ctx || !text || w < 8u || h < 8u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (hovered && ui->mouse_pressed) {
        ui->active_id = id;
    }

    int active = (ui->active_id == id);
    uint8_t br = 100u, bg = 100u, bb = 100u;
    if (hovered) { br = 120u; bg = 120u; bb = 120u; }
    if (active && ui->mouse_down) { br = 80u; bg = 80u; bb = 80u; }

    sl_fill_rect(ui->ctx, x, y, w, h, br, bg, bb);
    sl_fill_rect(ui->ctx, x, y, w, 1u, 220u, 220u, 220u);
    sl_fill_rect(ui->ctx, x, y + h - 1u, w, 1u, 40u, 40u, 40u);
    sl_fill_rect(ui->ctx, x, y, 1u, h, 220u, 220u, 220u);
    sl_fill_rect(ui->ctx, x + w - 1u, y, 1u, h, 40u, 40u, 40u);
    sl_draw_text(ui->ctx, x + 6u, y + ((h > 12u) ? ((h - 8u) / 2u) : 0u), text, 255u, 255u, 255u);

    if (ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }

    return 0u;
}

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value) {
    if (!ui || !ui->ctx || !value || w < 24u || min_v > max_v) return 0u;

    uint32_t h = 16u;
    uint32_t knob_w = 10u;
    uint32_t track_x = x + 2u;
    uint32_t track_w = w - 4u;

    if (sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h) && ui->mouse_pressed) {
        ui->active_id = id;
    }

    uint8_t changed = 0u;
    if (ui->active_id == id && ui->mouse_down) {
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

    sl_fill_rect(ui->ctx, x, y + 6u, w, 4u, 70u, 70u, 70u);

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

    sl_fill_rect(ui->ctx, knob_x, y + 2u, knob_w, 12u, 190u, 190u, 80u);
    return changed;
}

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ui || !ui->ctx || !rgba || w == 0u || h == 0u || src_w == 0u || src_h == 0u) return;
    sl_blit_rgba_stretch(ui->ctx, x, y, w, h, rgba, src_w, src_h, src_stride);
}
