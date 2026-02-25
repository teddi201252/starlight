#include "pixels.h"
#include "font8x8.h"

static inline uint8_t sl_blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    uint32_t inv = 255u - (uint32_t)a;
    uint32_t out = ((uint32_t)src * (uint32_t)a) + ((uint32_t)dst * inv) + 127u;
    return (uint8_t)(out / 255u);
}

static int sl_point_in_clip(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->clip_enabled) return 1;
    if (ctx->clip_w == 0u || ctx->clip_h == 0u) return 0;
    uint32_t cx1 = ctx->clip_x + ctx->clip_w;
    uint32_t cy1 = ctx->clip_y + ctx->clip_h;
    if (cx1 < ctx->clip_x) cx1 = 0xFFFFFFFFu;
    if (cy1 < ctx->clip_y) cy1 = 0xFFFFFFFFu;
    if (x < ctx->clip_x || y < ctx->clip_y) return 0;
    if (x >= cx1 || y >= cy1) return 0;
    return 1;
}

static int sl_clip_rect_to_ctx(const sl_ctx_t *ctx,
                               uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!ctx || !x || !y || !w || !h) return 0;
    if (*w == 0u || *h == 0u) return 0;
    if (*x >= ctx->fb.width || *y >= ctx->fb.height) return 0;

    uint32_t x0 = *x;
    uint32_t y0 = *y;
    uint32_t x1 = x0 + *w;
    uint32_t y1 = y0 + *h;
    if (x1 < x0) x1 = 0xFFFFFFFFu;
    if (y1 < y0) y1 = 0xFFFFFFFFu;

    uint32_t fb_x1 = ctx->fb.width;
    uint32_t fb_y1 = ctx->fb.height;
    if (x1 > fb_x1) x1 = fb_x1;
    if (y1 > fb_y1) y1 = fb_y1;
    if (x1 <= x0 || y1 <= y0) return 0;

    if (ctx->clip_enabled) {
        if (ctx->clip_w == 0u || ctx->clip_h == 0u) return 0;
        uint32_t cx0 = ctx->clip_x;
        uint32_t cy0 = ctx->clip_y;
        uint32_t cx1 = cx0 + ctx->clip_w;
        uint32_t cy1 = cy0 + ctx->clip_h;
        if (cx1 < cx0) cx1 = 0xFFFFFFFFu;
        if (cy1 < cy0) cy1 = 0xFFFFFFFFu;

        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x1 <= x0 || y1 <= y0) return 0;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

void sl_clip_set(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!ctx) return;
    ctx->clip_enabled = 1u;
    ctx->clip_x = x;
    ctx->clip_y = y;
    ctx->clip_w = w;
    ctx->clip_h = h;
}

void sl_clip_clear(sl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->clip_enabled = 0u;
    ctx->clip_x = 0u;
    ctx->clip_y = 0u;
    ctx->clip_w = 0u;
    ctx->clip_h = 0u;
}

// Write a single pixel in RGB order with bounds checking.
void sl_put_pixel(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;
    if (!sl_point_in_clip(ctx, x, y)) return;

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
    uint32_t draw_x = x;
    uint32_t draw_y = y;
    uint32_t draw_w = w;
    uint32_t draw_h = h;
    if (!sl_clip_rect_to_ctx(ctx, &draw_x, &draw_y, &draw_w, &draw_h)) return;
    if (draw_w == 0u || draw_h == 0u) return;

    uint8_t *row = ctx->frame + ((size_t)draw_y * ctx->row_stride) +
                   ((size_t)draw_x * (size_t)ctx->bytespp);

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
    uint32_t draw_x = dst_x;
    uint32_t draw_y = dst_y;
    uint32_t draw_w = src_w;
    uint32_t draw_h = src_h;
    if (!sl_clip_rect_to_ctx(ctx, &draw_x, &draw_y, &draw_w, &draw_h)) return;

    uint32_t src_x0 = draw_x - dst_x;
    uint32_t src_y0 = draw_y - dst_y;

    for (uint32_t y = 0; y < draw_h; y++) {
        const uint8_t *srow = src_rgba + ((size_t)(src_y0 + y) * (size_t)src_stride);
        uint8_t *drow = ctx->frame + ((size_t)(draw_y + y) * ctx->row_stride) +
                        ((size_t)draw_x * (size_t)ctx->bytespp);

        for (uint32_t x = 0; x < draw_w; x++) {
            const uint8_t *s = srow + ((size_t)(src_x0 + x) * 4u);
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
    uint32_t draw_x = dst_x;
    uint32_t draw_y = dst_y;
    uint32_t draw_w = dst_w;
    uint32_t draw_h = dst_h;
    if (!sl_clip_rect_to_ctx(ctx, &draw_x, &draw_y, &draw_w, &draw_h)) return;
    if (draw_w == 0u || draw_h == 0u) return;

    float src_x_step = (float)src_w / (float)dst_w;
    float src_y_step = (float)src_h / (float)dst_h;
    uint32_t off_x = draw_x - dst_x;
    uint32_t off_y = draw_y - dst_y;

    for (uint32_t y = 0; y < draw_h; y++) {
        int src_y = (int)((off_y + y) * src_y_step);
        if (src_y < 0) src_y = 0;
        if ((uint32_t)src_y >= src_h) src_y = (int)(src_h - 1u);
        const uint8_t *srow = src_rgba + ((size_t)src_y * (size_t)src_stride);
        uint8_t *drow = ctx->frame + ((size_t)(draw_y + y) * ctx->row_stride) +
                        ((size_t)draw_x * (size_t)ctx->bytespp);

        for (uint32_t x = 0; x < draw_w; x++) {
            int src_x = (int)((off_x + x) * src_x_step);
            if (src_x < 0) src_x = 0;
            if ((uint32_t)src_x >= src_w) src_x = (int)(src_w - 1u);
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
            if ((bits & (uint8_t)(1u << i)) != 0u && sl_point_in_clip(ctx, xx, yy)) {
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
