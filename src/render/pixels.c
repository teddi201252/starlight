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
    if (w == 0u || h == 0u) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    uint32_t x_end = x + w;
    uint32_t y_end = y + h;
    if (x_end > ctx->fb.width) x_end = ctx->fb.width;
    if (y_end > ctx->fb.height) y_end = ctx->fb.height;

    uint32_t draw_w = x_end - x;
    uint32_t draw_h = y_end - y;
    if (draw_w == 0u || draw_h == 0u) return;

    uint8_t *row = ctx->frame + ((size_t)y * ctx->row_stride) + ((size_t)x * (size_t)ctx->bytespp);

    if (ctx->bytespp == 4u) {
        uint32_t px = ((uint32_t)0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        for (uint32_t j = 0; j < draw_h; j++) {
            uint32_t *dst = (uint32_t *)(void *)row;
            for (uint32_t i = 0; i < draw_w; i++) dst[i] = px;
            row += ctx->row_stride;
        }
        return;
    }

    if (ctx->bytespp == 3u) {
        for (uint32_t j = 0; j < draw_h; j++) {
            uint8_t *dst = row;
            for (uint32_t i = 0; i < draw_w; i++) {
                dst[0] = b;
                dst[1] = g;
                dst[2] = r;
                dst += 3;
            }
            row += ctx->row_stride;
        }
        return;
    }

    for (uint32_t j = 0; j < draw_h; j++) {
        for (uint32_t i = 0; i < draw_w; i++) {
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

void sl_blit_rgba_stretch(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y, uint32_t dst_w, uint32_t dst_h,
                          const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ctx || !ctx->frame || !src_rgba) return;
    if (src_w == 0 || src_h == 0) return;
    if (dst_w == 0 || dst_h == 0) return;
    if (ctx->bytespp != 3 && ctx->bytespp != 4) return;
    if (src_stride < src_w * 4u) return;
    if (dst_x >= ctx->fb.width || dst_y >= ctx->fb.height) return;

    uint32_t copy_w = dst_w;
    uint32_t copy_h = dst_h;
    uint32_t rem_w = ctx->fb.width - dst_x;
    uint32_t rem_h = ctx->fb.height - dst_y;
    if (copy_w > rem_w) copy_w = rem_w;
    if (copy_h > rem_h) copy_h = rem_h;
    if (copy_w == 0 || copy_h == 0) return;

    float src_x_step = (float)src_w / (float)dst_w;
    float src_y_step = (float)src_h / (float)dst_h;

    for (uint32_t y = 0; y < copy_h; y++) {
        int src_y = (int)(y * src_y_step);
        const uint8_t *srow = src_rgba + ((size_t)src_y * (size_t)src_stride);
        uint8_t *drow = ctx->frame + ((size_t)(dst_y + y) * ctx->row_stride) +
                        ((size_t)dst_x * (size_t)ctx->bytespp);

        for (uint32_t x = 0; x < copy_w; x++) {
            int src_x = (int)(x * src_x_step);
            const uint8_t *s = srow + ((size_t)src_x * 4u);
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
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;
    if (ctx->bytespp != 3u && ctx->bytespp != 4u) return;

    const uint8_t *glyph = peos_font8x8_glyph(c);
    for (uint32_t j = 0; j < 8; j++) {
        uint32_t yy = y + j;
        if (yy >= ctx->fb.height) break;
        uint8_t bits = glyph[j];
        if (!bits) continue;

        uint8_t *dst = ctx->frame + ((size_t)yy * ctx->row_stride) + ((size_t)x * (size_t)ctx->bytespp);
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t xx = x + i;
            if (xx >= ctx->fb.width) break;
            if ((bits & (uint8_t)(1u << i)) != 0u) {
                dst[0] = b;
                dst[1] = g;
                dst[2] = r;
                if (ctx->bytespp == 4u) dst[3] = 0xFFu;
            }
            dst += ctx->bytespp;
        }
    }
}

void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    for (uint32_t i = 0; text[i]; i++) {
        sl_draw_char(ctx, x + i * 8, y, text[i], r, g, b);
    }
}
