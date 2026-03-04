#pragma once

#include "../starlight.h"

#include <stdint.h>

void sl_font_reset_default(void);
void sl_font_shutdown(void);
int sl_font_load_manifest_default(const char *manifest_path);

uint32_t sl_font_advance_x(void);
uint32_t sl_font_line_height(void);
uint32_t sl_font_measure_text(const char *text);
uint32_t sl_font_advance_x_px(uint32_t font_px);
uint32_t sl_font_line_height_px(uint32_t font_px);
uint32_t sl_font_measure_text_px(const char *text, uint32_t font_px);

void sl_font_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c,
                       uint8_t r, uint8_t g, uint8_t b);
void sl_font_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                       uint8_t r, uint8_t g, uint8_t b);
void sl_font_draw_text_px(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                          uint8_t r, uint8_t g, uint8_t b, uint32_t font_px);
