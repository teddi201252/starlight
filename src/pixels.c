#include "pixels.h"

void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    size_t off = ((size_t)y * (size_t)ctx->fb.width + (size_t)x) * (size_t)ctx->bytespp;
    ctx->frame[off + 0] = b;
    ctx->frame[off + 1] = g;
    ctx->frame[off + 2] = r;
    if (ctx->bytespp == 4) ctx->frame[off + 3] = 0xFF;
}

void sl_fill_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            sl_put_pixel(ctx, x + i, y + j, r, g, b);
        }
    }
}
