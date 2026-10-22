#pragma once

#include "../starlight.h"
#include "blit.h"

void sl_clip_set(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sl_clip_clear(sl_ctx_t *ctx);

void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
void sl_fill_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint8_t r, uint8_t g, uint8_t b);
void sl_fill_rect_a(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sl_blend_coverage_row(sl_ctx_t *ctx, int32_t dst_x, int32_t dst_y,
                           const uint8_t *coverage, uint32_t count,
                           uint8_t r, uint8_t g, uint8_t b);
void sl_blit_rgba(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y,
                  const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
void sl_blit_rgba_stretch(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y, uint32_t dst_w, uint32_t dst_h,
                          const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
void sl_blit_image_stretch(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y, uint32_t dst_w, uint32_t dst_h,
                           const sl_image_t *image);
void sl_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c, uint8_t r, uint8_t g, uint8_t b);
void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b);
void sl_draw_text_px(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                     uint8_t r, uint8_t g, uint8_t b, uint32_t font_px);
uint32_t sl_text_cell_w(void);
uint32_t sl_text_cell_h(void);
uint32_t sl_text_measure_width(const char *text);
uint32_t sl_text_cell_w_px(uint32_t font_px);
uint32_t sl_text_cell_h_px(uint32_t font_px);
uint32_t sl_text_measure_width_px(const char *text, uint32_t font_px);

// The baked skin (render/blit.h) drawn into the frame, inside the clip.
sl_target_t sl_target_from_ctx(const sl_ctx_t *ctx);
// Sprite for a shape at (x, y) w x h: the shadow margins go around it.
void sl_draw_skin(sl_ctx_t *ctx, const slsk_entry_t *e, int32_t x, int32_t y, uint32_t w, uint32_t h);
// Text with a baked font; returns the pen position after it.
int32_t sl_draw_text_face(sl_ctx_t *ctx, const slsk_font_t *font, int32_t x, int32_t baseline,
                          const char *utf8, uint8_t r, uint8_t g, uint8_t b);
