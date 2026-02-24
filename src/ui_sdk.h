#pragma once

#include "starlight_internal.h"
#include "render/pixels.h"

typedef struct {
    sl_ctx_t *ctx;
    const sl_window_t *win;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t mouse_down;
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    uint32_t active_id;
} sl_ui_ctx_t;

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released);

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 uint8_t r, uint8_t g, uint8_t b);

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text);

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value);

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
