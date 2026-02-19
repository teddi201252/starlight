#include "pixels.h"
#include "font8x8.h"

static inline uint8_t sl_blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    uint32_t inv = 255u - (uint32_t)a;
    uint32_t out = ((uint32_t)src * (uint32_t)a) + ((uint32_t)dst * inv) + 127u;
    return (uint8_t)(out / 255u);
}

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

// Blend an RGBA image into the framebuffer at (dst_x, dst_y).
// Source format is 8-bit RGBA; destination framebuffer is BGR/BGRA.
void sl_blit_rgba(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y,
                  const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ctx || !ctx->frame || !src_rgba) return;
    if (src_w == 0 || src_h == 0) return;
    if (ctx->bytespp != 3 && ctx->bytespp != 4) return;
    if (src_stride < src_w * 4u) return;
    if (dst_x >= ctx->fb.width || dst_y >= ctx->fb.height) return;

    uint32_t copy_w = src_w;
    uint32_t copy_h = src_h;
    if (dst_x + copy_w > ctx->fb.width) copy_w = ctx->fb.width - dst_x;
    if (dst_y + copy_h > ctx->fb.height) copy_h = ctx->fb.height - dst_y;

    for (uint32_t y = 0; y < copy_h; y++) {
        const uint8_t *srow = src_rgba + ((size_t)y * (size_t)src_stride);
        uint8_t *drow = ctx->frame + ((size_t)(dst_y + y) * ctx->row_stride) +
                        ((size_t)dst_x * (size_t)ctx->bytespp);

        for (uint32_t x = 0; x < copy_w; x++) {
            const uint8_t *s = srow + ((size_t)x * 4u);
            uint8_t sr = s[0];
            uint8_t sg = s[1];
            uint8_t sb = s[2];
            uint8_t sa = s[3];

            if (sa == 255u) {
                drow[0] = sb;
                drow[1] = sg;
                drow[2] = sr;
            } else if (sa != 0u) {
                drow[0] = sl_blend_chan(sb, drow[0], sa);
                drow[1] = sl_blend_chan(sg, drow[1], sa);
                drow[2] = sl_blend_chan(sr, drow[2], sa);
            }

            if (ctx->bytespp == 4) drow[3] = 0xFF;
            drow += ctx->bytespp;
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
