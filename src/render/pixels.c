#include "pixels.h"
#include "font.h"
#include "render_ops.h"

#include <string.h>

static inline uint8_t sl_blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    uint32_t inv = 255u - (uint32_t)a;
    uint32_t out = ((uint32_t)src * (uint32_t)a) + ((uint32_t)dst * inv) + 127u;
    return (uint8_t)(out / 255u);
}

static inline uint8_t sl_u8_from_float(float v) {
    if (v <= 0.0f) return 0u;
    if (v >= 255.0f) return 255u;
    return (uint8_t)(v + 0.5f);
}

static void sl_sample_rgba_bilinear(const uint8_t *src_rgba, uint32_t src_w, uint32_t src_h,
                                    uint32_t src_stride, float src_fx, float src_fy,
                                    uint8_t *out_r, uint8_t *out_g, uint8_t *out_b, uint8_t *out_a) {
    uint32_t x0 = 0u;
    uint32_t y0 = 0u;
    uint32_t x1 = 0u;
    uint32_t y1 = 0u;
    float tx = 0.0f;
    float ty = 0.0f;
    float w00 = 0.0f;
    float w10 = 0.0f;
    float w01 = 0.0f;
    float w11 = 0.0f;
    float acc_r = 0.0f;
    float acc_g = 0.0f;
    float acc_b = 0.0f;
    float acc_a = 0.0f;
    const uint8_t *p00 = NULL;
    const uint8_t *p10 = NULL;
    const uint8_t *p01 = NULL;
    const uint8_t *p11 = NULL;

    if (!src_rgba || !out_r || !out_g || !out_b || !out_a || src_w == 0u || src_h == 0u) return;

    if (src_fx <= 0.0f) {
        x0 = 0u;
        tx = 0.0f;
    } else if (src_fx >= (float)(src_w - 1u)) {
        x0 = src_w - 1u;
        tx = 0.0f;
    } else {
        x0 = (uint32_t)src_fx;
        tx = src_fx - (float)x0;
    }

    if (src_fy <= 0.0f) {
        y0 = 0u;
        ty = 0.0f;
    } else if (src_fy >= (float)(src_h - 1u)) {
        y0 = src_h - 1u;
        ty = 0.0f;
    } else {
        y0 = (uint32_t)src_fy;
        ty = src_fy - (float)y0;
    }

    x1 = (x0 + 1u < src_w) ? (x0 + 1u) : x0;
    y1 = (y0 + 1u < src_h) ? (y0 + 1u) : y0;

    w00 = (1.0f - tx) * (1.0f - ty);
    w10 = tx * (1.0f - ty);
    w01 = (1.0f - tx) * ty;
    w11 = tx * ty;

    p00 = src_rgba + ((size_t)y0 * (size_t)src_stride) + ((size_t)x0 * 4u);
    p10 = src_rgba + ((size_t)y0 * (size_t)src_stride) + ((size_t)x1 * 4u);
    p01 = src_rgba + ((size_t)y1 * (size_t)src_stride) + ((size_t)x0 * 4u);
    p11 = src_rgba + ((size_t)y1 * (size_t)src_stride) + ((size_t)x1 * 4u);

    {
        float a = ((float)p00[3] / 255.0f) * w00;
        acc_r += (float)p00[0] * a;
        acc_g += (float)p00[1] * a;
        acc_b += (float)p00[2] * a;
        acc_a += a;
    }
    {
        float a = ((float)p10[3] / 255.0f) * w10;
        acc_r += (float)p10[0] * a;
        acc_g += (float)p10[1] * a;
        acc_b += (float)p10[2] * a;
        acc_a += a;
    }
    {
        float a = ((float)p01[3] / 255.0f) * w01;
        acc_r += (float)p01[0] * a;
        acc_g += (float)p01[1] * a;
        acc_b += (float)p01[2] * a;
        acc_a += a;
    }
    {
        float a = ((float)p11[3] / 255.0f) * w11;
        acc_r += (float)p11[0] * a;
        acc_g += (float)p11[1] * a;
        acc_b += (float)p11[2] * a;
        acc_a += a;
    }

    if (acc_a <= 0.0f) {
        *out_r = 0u;
        *out_g = 0u;
        *out_b = 0u;
        *out_a = 0u;
        return;
    }

    *out_a = sl_u8_from_float(acc_a * 255.0f);
    *out_r = sl_u8_from_float(acc_r / acc_a);
    *out_g = sl_u8_from_float(acc_g / acc_a);
    *out_b = sl_u8_from_float(acc_b / acc_a);
}

static uint32_t sl_image_level_width(const sl_image_t *image, uint32_t level) {
    if (!image) return 0u;
    return (level == 0u) ? image->width : image->mip_width[level - 1u];
}

static uint32_t sl_image_level_height(const sl_image_t *image, uint32_t level) {
    if (!image) return 0u;
    return (level == 0u) ? image->height : image->mip_height[level - 1u];
}

static uint32_t sl_image_level_stride(const sl_image_t *image, uint32_t level) {
    if (!image) return 0u;
    return (level == 0u) ? image->stride : image->mip_stride[level - 1u];
}

static const uint8_t *sl_image_level_data(const sl_image_t *image, uint32_t level) {
    if (!image) return NULL;
    return (level == 0u) ? image->data : image->mip_data[level - 1u];
}

static uint32_t sl_image_pick_mip_level(const sl_image_t *image, uint32_t dst_w, uint32_t dst_h) {
    uint32_t level = 0u;
    if (!image || dst_w == 0u || dst_h == 0u) return 0u;
    while (level < image->mip_count) {
        uint32_t next_level = level + 1u;
        uint32_t next_w = sl_image_level_width(image, next_level);
        uint32_t next_h = sl_image_level_height(image, next_level);
        if (!next_w || !next_h) break;
        if (next_w < dst_w || next_h < dst_h) break;
        level = next_level;
    }
    return level;
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
            sl_render_fill_bgra32((uint32_t *)(void *)row, draw_w, px);
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

void sl_fill_rect_a(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ctx || !ctx->frame) return;
    if (a == 0u) return;
    if (a == 255u) {
        sl_fill_rect(ctx, x, y, w, h, r, g, b);
        return;
    }

    uint32_t draw_x = x;
    uint32_t draw_y = y;
    uint32_t draw_w = w;
    uint32_t draw_h = h;
    if (!sl_clip_rect_to_ctx(ctx, &draw_x, &draw_y, &draw_w, &draw_h)) return;
    if (draw_w == 0u || draw_h == 0u) return;

    if (ctx->bytespp != 3u && ctx->bytespp != 4u) return;

    if (ctx->bytespp == 4u) {
        for (uint32_t j = 0u; j < draw_h; j++) {
            uint32_t *dst = (uint32_t *)(void *)(ctx->frame +
                            ((size_t)(draw_y + j) * ctx->row_stride) +
                            ((size_t)draw_x * 4u));
            sl_render_blend_fill_bgra32(dst, draw_w, r, g, b, a);
        }
        return;
    }

    for (uint32_t j = 0u; j < draw_h; j++) {
        uint8_t *dst = ctx->frame + ((size_t)(draw_y + j) * ctx->row_stride) +
                       ((size_t)draw_x * (size_t)ctx->bytespp);
        for (uint32_t i = 0u; i < draw_w; i++) {
            dst[0] = sl_blend_chan(b, dst[0], a);
            dst[1] = sl_blend_chan(g, dst[1], a);
            dst[2] = sl_blend_chan(r, dst[2], a);
            if (ctx->bytespp == 4u) dst[3] = 0xFFu;
            dst += ctx->bytespp;
        }
    }
}

void sl_blend_coverage_row(sl_ctx_t *ctx, int32_t dst_x, int32_t dst_y,
                           const uint8_t *coverage, uint32_t count,
                           uint8_t r, uint8_t g, uint8_t b) {
    if (!ctx || !ctx->frame || !coverage || count == 0u || dst_y < 0 ||
        (uint32_t)dst_y >= ctx->fb.height ||
        (ctx->bytespp != 3u && ctx->bytespp != 4u)) return;

    int64_t start = 0;
    int64_t end = count;
    int64_t x0 = dst_x;
    if (x0 < 0) start = -x0;
    if (x0 + end > (int64_t)ctx->fb.width) end = (int64_t)ctx->fb.width - x0;
    if (ctx->clip_enabled) {
        if ((uint32_t)dst_y < ctx->clip_y ||
            (uint64_t)(uint32_t)dst_y >= (uint64_t)ctx->clip_y + ctx->clip_h) return;
        if (x0 + start < (int64_t)ctx->clip_x) start = (int64_t)ctx->clip_x - x0;
        uint64_t clip_end = (uint64_t)ctx->clip_x + ctx->clip_w;
        if ((uint64_t)(x0 + end) > clip_end) end = (int64_t)clip_end - x0;
    }
    if (start < 0) start = 0;
    if (end > (int64_t)count) end = count;
    if (end <= start) return;

    uint8_t *dst = ctx->frame + (size_t)(uint32_t)dst_y * ctx->row_stride +
                   (size_t)(x0 + start) * ctx->bytespp;
    for (int64_t i = start; i < end; i++) {
        uint8_t a = coverage[i];
        if (a == 255u) {
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
        } else if (a != 0u) {
            dst[0] = sl_blend_chan(b, dst[0], a);
            dst[1] = sl_blend_chan(g, dst[1], a);
            dst[2] = sl_blend_chan(r, dst[2], a);
        }
        if (ctx->bytespp == 4u) dst[3] = 0xFFu;
        dst += ctx->bytespp;
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

        uint32_t x = 0u;
        if (ctx->bytespp == 4u) {
            uint32_t opaque_run = 0u;
            while (opaque_run < draw_w &&
                   srow[((size_t)(src_x0 + opaque_run) * 4u) + 3u] == 255u) {
                opaque_run++;
            }
            if (opaque_run == draw_w) {
                sl_render_copy_opaque_rgba_to_bgra32(
                    (uint32_t *)(void *)drow,
                    (const uint32_t *)(const void *)(srow + (size_t)src_x0 * 4u),
                    draw_w);
                continue;
            }
        }
        for (; x < draw_w; x++) {
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
    int use_bilinear = (dst_w < src_w || dst_h < src_h) ? 1 : 0;

    for (uint32_t y = 0; y < draw_h; y++) {
        uint8_t *drow = ctx->frame + ((size_t)(draw_y + y) * ctx->row_stride) +
                        ((size_t)draw_x * (size_t)ctx->bytespp);

        for (uint32_t x = 0; x < draw_w; x++) {
            uint8_t sr = 0u;
            uint8_t sg = 0u;
            uint8_t sb = 0u;
            uint8_t sa = 0u;

            if (use_bilinear) {
                float src_fx = (((float)(off_x + x) + 0.5f) * src_x_step) - 0.5f;
                float src_fy = (((float)(off_y + y) + 0.5f) * src_y_step) - 0.5f;
                sl_sample_rgba_bilinear(src_rgba, src_w, src_h, src_stride,
                                        src_fx, src_fy, &sr, &sg, &sb, &sa);
            } else {
                int src_y = (int)((off_y + y) * src_y_step);
                int src_x = (int)((off_x + x) * src_x_step);
                const uint8_t *srow = NULL;
                const uint8_t *s = NULL;

                if (src_y < 0) src_y = 0;
                if ((uint32_t)src_y >= src_h) src_y = (int)(src_h - 1u);
                if (src_x < 0) src_x = 0;
                if ((uint32_t)src_x >= src_w) src_x = (int)(src_w - 1u);
                srow = src_rgba + ((size_t)src_y * (size_t)src_stride);
                s = srow + ((size_t)src_x * 4u);
                sr = s[0];
                sg = s[1];
                sb = s[2];
                sa = s[3];
            }

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

void sl_blit_image_stretch(sl_ctx_t *ctx, uint32_t dst_x, uint32_t dst_y, uint32_t dst_w, uint32_t dst_h,
                           const sl_image_t *image) {
    uint32_t level = 0u;
    const uint8_t *src = NULL;
    uint32_t src_w = 0u;
    uint32_t src_h = 0u;
    uint32_t src_stride = 0u;

    if (!image || !image->data) return;
    level = sl_image_pick_mip_level(image, dst_w, dst_h);
    src = sl_image_level_data(image, level);
    src_w = sl_image_level_width(image, level);
    src_h = sl_image_level_height(image, level);
    src_stride = sl_image_level_stride(image, level);
    if (!src || src_w == 0u || src_h == 0u || src_stride < src_w * 4u) return;

    sl_blit_rgba_stretch(ctx, dst_x, dst_y, dst_w, dst_h, src, src_w, src_h, src_stride);
}

void sl_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c, uint8_t r, uint8_t g, uint8_t b) {
    sl_font_draw_char(ctx, x, y, c, r, g, b);
}

void sl_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    sl_font_draw_text(ctx, x, y, text, r, g, b);
}

void sl_draw_text_px(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                     uint8_t r, uint8_t g, uint8_t b, uint32_t font_px) {
    sl_font_draw_text_px(ctx, x, y, text, r, g, b, font_px);
}

uint32_t sl_text_cell_w(void) {
    return sl_font_advance_x();
}

uint32_t sl_text_cell_h(void) {
    return sl_font_line_height();
}

uint32_t sl_text_measure_width(const char *text) {
    return sl_font_measure_text(text);
}

uint32_t sl_text_cell_w_px(uint32_t font_px) {
    return sl_font_advance_x_px(font_px);
}

uint32_t sl_text_cell_h_px(uint32_t font_px) {
    return sl_font_line_height_px(font_px);
}

uint32_t sl_text_measure_width_px(const char *text, uint32_t font_px) {
    return sl_font_measure_text_px(text, font_px);
}

sl_target_t sl_target_from_ctx(const sl_ctx_t *ctx) {
    sl_target_t t;
    memset(&t, 0, sizeof(t));
    if (!ctx || !ctx->frame || (ctx->bytespp != 3u && ctx->bytespp != 4u)) return t;
    t.px = ctx->frame;
    t.stride = ctx->row_stride;
    t.bpp = ctx->bytespp;
    t.clip_x1 = (int32_t)ctx->fb.width;
    t.clip_y1 = (int32_t)ctx->fb.height;
    if (ctx->clip_enabled) {
        int64_t cx1 = (int64_t)ctx->clip_x + ctx->clip_w;
        int64_t cy1 = (int64_t)ctx->clip_y + ctx->clip_h;
        if ((int64_t)ctx->clip_x > t.clip_x0) t.clip_x0 = (int32_t)(ctx->clip_x < ctx->fb.width ? ctx->clip_x : ctx->fb.width);
        if ((int64_t)ctx->clip_y > t.clip_y0) t.clip_y0 = (int32_t)(ctx->clip_y < ctx->fb.height ? ctx->clip_y : ctx->fb.height);
        if (cx1 < t.clip_x1) t.clip_x1 = (int32_t)cx1;
        if (cy1 < t.clip_y1) t.clip_y1 = (int32_t)cy1;
    }
    return t;
}

void sl_draw_skin(sl_ctx_t *ctx, const slsk_entry_t *e, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!ctx || !e || !ctx->skin_loaded) return;
    sl_target_t t = sl_target_from_ctx(ctx);
    sl_blit_sprite(&t, e, sl_skin_data(&ctx->skin, e), x - (int32_t)e->pad_l, y - (int32_t)e->pad_t,
                   w + e->pad_l + e->pad_r, h + e->pad_t + e->pad_b);
}

int32_t sl_draw_text_face(sl_ctx_t *ctx, const slsk_font_t *font, int32_t x, int32_t baseline,
                          const char *utf8, uint8_t r, uint8_t g, uint8_t b) {
    sl_target_t t = sl_target_from_ctx(ctx);
    return sl_text_draw(&t, font, x, baseline, utf8, r, g, b);
}
