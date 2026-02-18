#pragma once

#include "starlight.h"

void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
void sl_fill_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint8_t r, uint8_t g, uint8_t b);
void sl_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c, uint8_t r, uint8_t g, uint8_t b);
void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b);
