#include "pixels.h"
#include "font8x8.h"

// Write a single pixel in RGB order with bounds checking.
void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    size_t off = ((size_t)y * ctx->row_stride) + ((size_t)x * (size_t)ctx->bytespp);
    ctx->frame[off + 0] = b;
    ctx->frame[off + 1] = g;
    ctx->frame[off + 2] = r;
    if (ctx->bytespp == 4) ctx->frame[off + 3] = 0xFF;
}

// Fill a rectangular area by repeatedly writing individual pixels.
void sl_fill_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            sl_put_pixel(ctx, x + i, y + j, r, g, b);
        }
    }
}

void sl_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;

    const uint8_t *glyph = peos_font8x8_glyph(c);
    for (uint32_t j = 0; j < 8; j++) {
        for (uint32_t i = 0; i < 8; i++) {
            if (glyph[j] & (1 << i)) {
                sl_put_pixel(ctx, x + i, y + j, r, g, b);
            }
        }
    }
}

void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    for (uint32_t i = 0; text[i]; i++) {
        sl_draw_char(ctx, x + i * 8, y, text[i], r, g, b);
    }
}
