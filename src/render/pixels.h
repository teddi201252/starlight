#pragma once

#include "../starlight.h"

void sl_clip_set(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sl_clip_clear(sl_ctx_t *ctx);

void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
void sl_fill_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint8_t r, uint8_t g, uint8_t b);
void sl_fill_rect_a(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sl_blit_rgba(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y,
                  const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
void sl_blit_rgba_stretch(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y, uint32_t dst_w, uint32_t dst_h,
                          const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
void sl_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c, uint8_t r, uint8_t g, uint8_t b);
void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b);
uint32_t sl_text_cell_w(void);
uint32_t sl_text_cell_h(void);
uint32_t sl_text_measure_width(const char *text);
