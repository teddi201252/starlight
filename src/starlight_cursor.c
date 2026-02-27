#include "starlight_internal.h"
#include "render/image_decode.h"
#include "render/pixels.h"

#include <stdbool.h>
#include <stdlib.h>

#define SL_HW_CURSOR_MAX_SIDE 64u

static uint8_t sl_cursor_scale_fits_hw(uint32_t src_w, uint32_t src_h, uint32_t scale_pct) {
    if (src_w == 0u || src_h == 0u || scale_pct == 0u) return 0u;
    uint64_t dst_w64 = (((uint64_t)src_w * (uint64_t)scale_pct) + 50u) / 100u;
    uint64_t dst_h64 = (((uint64_t)src_h * (uint64_t)scale_pct) + 50u) / 100u;
    if (dst_w64 == 0u || dst_h64 == 0u) return 0u;
    if (dst_w64 > SL_HW_CURSOR_MAX_SIDE || dst_h64 > SL_HW_CURSOR_MAX_SIDE) return 0u;
    return 1u;
}

static uint32_t sl_cursor_effective_scale_pct(uint32_t src_w, uint32_t src_h, uint32_t requested_pct) {
    if (src_w == 0u || src_h == 0u) return 0u;
    if (requested_pct == 0u) requested_pct = 1u;

    if (sl_cursor_scale_fits_hw(src_w, src_h, requested_pct)) return requested_pct;

    uint64_t by_w = ((uint64_t)SL_HW_CURSOR_MAX_SIDE * 100u) / (uint64_t)src_w;
    uint64_t by_h = ((uint64_t)SL_HW_CURSOR_MAX_SIDE * 100u) / (uint64_t)src_h;
    uint64_t fit64 = by_w < by_h ? by_w : by_h;
    if (fit64 == 0u) fit64 = 1u;
    if (fit64 > requested_pct) fit64 = requested_pct;

    uint32_t fit = (uint32_t)fit64;
    while (fit > 1u && !sl_cursor_scale_fits_hw(src_w, src_h, fit)) {
        fit--;
    }
    if (!sl_cursor_scale_fits_hw(src_w, src_h, fit)) return 0u;
    return fit;
}

static void sl_build_default_cursor_rgba(uint8_t *dst, uint32_t w, uint32_t h) {
    if (!dst || w == 0 || h == 0) return;
    peos_memset(dst, 0, (size_t)w * (size_t)h * 4u);

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            bool body = (x <= (y / 2u)) && (x < (w / 2u)) && (y < (h - 6u));
            bool stem = (x < 4u) && (y < h);
            if (!(body || stem)) continue;

            bool edge = (x == 0u) ||
                        (y == 0u) ||
                        (body && x == (y / 2u)) ||
                        (stem && x == 3u);

            size_t off = (((size_t)y * (size_t)w) + (size_t)x) * 4u;
            if (edge) {
                dst[off + 0] = 0x00;
                dst[off + 1] = 0x00;
                dst[off + 2] = 0x00;
                dst[off + 3] = 0xFF;
            } else {
                dst[off + 0] = 0xFF;
                dst[off + 1] = 0xFF;
                dst[off + 2] = 0xFF;
                dst[off + 3] = 0xFF;
            }
        }
    }
}

static int sl_hw_cursor_apply(sl_ctx_t *ctx, const uint8_t *rgba,
                              uint32_t w, uint32_t h, uint32_t hot_x, uint32_t hot_y) {
    if (!ctx || !rgba || w == 0 || h == 0) return 0;
    if (hot_x >= w || hot_y >= h) return 0;
    if ((int64_t)peos_cursor_set_image_rgba(rgba, w, h, hot_x, hot_y) < 0) return 0;
    (void)peos_cursor_move_abs(ctx->cursor_x, ctx->cursor_y);
    if ((int64_t)peos_cursor_set_visible(1) < 0) return 0;
    ctx->hw_cursor = 1u;
    return 1;
}

static int sl_cursor_scale_rgba_nn(const uint8_t *src_rgba,
                                   uint32_t src_w, uint32_t src_h, uint32_t src_stride,
                                   uint32_t scale_pct,
                                   uint8_t *dst_rgba,
                                   uint32_t *out_w, uint32_t *out_h) {
    if (!src_rgba || !dst_rgba || !out_w || !out_h) return 0;
    if (src_w == 0 || src_h == 0) return 0;
    if (scale_pct == 0) return 0;
    if (src_stride < src_w * 4u) return 0;

    uint64_t dst_w64 = (((uint64_t)src_w * (uint64_t)scale_pct) + 50u) / 100u;
    uint64_t dst_h64 = (((uint64_t)src_h * (uint64_t)scale_pct) + 50u) / 100u;
    if (dst_w64 == 0 || dst_h64 == 0) return 0;
    if (dst_w64 > SL_HW_CURSOR_MAX_SIDE || dst_h64 > SL_HW_CURSOR_MAX_SIDE) return 0;

    uint32_t dst_w = (uint32_t)dst_w64;
    uint32_t dst_h = (uint32_t)dst_h64;

    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)dst_h);
        if (sy >= src_h) sy = src_h - 1u;
        const uint8_t *srow = src_rgba + ((size_t)sy * (size_t)src_stride);
        uint8_t *drow = dst_rgba + ((size_t)y * (size_t)dst_w * 4u);

        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)src_w) / (uint64_t)dst_w);
            if (sx >= src_w) sx = src_w - 1u;
            const uint8_t *s = srow + ((size_t)sx * 4u);
            uint8_t *d = drow + ((size_t)x * 4u);
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }

    *out_w = dst_w;
    *out_h = dst_h;
    return 1;
}

static int sl_hw_cursor_set_default(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    enum {
        SL_DEFAULT_CURSOR_W = 20u,
        SL_DEFAULT_CURSOR_H = 20u,
    };

    uint8_t base_rgba[SL_DEFAULT_CURSOR_W * SL_DEFAULT_CURSOR_H * 4u];
    uint8_t *scaled_rgba = (uint8_t *)malloc(SL_HW_CURSOR_MAX_SIDE * SL_HW_CURSOR_MAX_SIDE * 4u);
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    if (!scaled_rgba) return 0;

    sl_build_default_cursor_rgba(base_rgba, SL_DEFAULT_CURSOR_W, SL_DEFAULT_CURSOR_H);
    uint32_t effective_scale = sl_cursor_effective_scale_pct(SL_DEFAULT_CURSOR_W,
                                                             SL_DEFAULT_CURSOR_H,
                                                             ctx->hw_cursor_scale_pct);
    if (effective_scale == 0u) {
        free(scaled_rgba);
        return 0;
    }
    if (!sl_cursor_scale_rgba_nn(base_rgba,
                                 SL_DEFAULT_CURSOR_W, SL_DEFAULT_CURSOR_H, SL_DEFAULT_CURSOR_W * 4u,
                                 effective_scale,
                                 scaled_rgba,
                                 &out_w, &out_h)) {
        free(scaled_rgba);
        return 0;
    }
    int ok = sl_hw_cursor_apply(ctx, scaled_rgba, out_w, out_h, 0u, 0u);
    free(scaled_rgba);
    return ok;
}

static int sl_hw_cursor_load_resource(sl_ctx_t *ctx, const char *path) {
    if (!ctx || !path || !path[0]) return 0;

    sl_image_t img;
    if (!sl_decode_image_file(path, &img)) return 0;

    uint8_t *scaled_rgba = (uint8_t *)malloc(SL_HW_CURSOR_MAX_SIDE * SL_HW_CURSOR_MAX_SIDE * 4u);
    if (!scaled_rgba) {
        sl_image_free(&img);
        return 0;
    }
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    int ok = 0;
    if (img.data &&
        img.channels == 4u &&
        img.width > 0u && img.height > 0u &&
        img.stride >= (img.width * 4u)) {
        uint32_t effective_scale = sl_cursor_effective_scale_pct(img.width, img.height,
                                                                 ctx->hw_cursor_scale_pct);
        if (effective_scale == 0u) {
            free(scaled_rgba);
            sl_image_free(&img);
            return 0;
        }
        if (sl_cursor_scale_rgba_nn(img.data,
                                    img.width, img.height, img.stride,
                                    effective_scale,
                                    scaled_rgba,
                                    &out_w, &out_h)) {
            ok = sl_hw_cursor_apply(ctx, scaled_rgba, out_w, out_h, 0u, 0u);
        }
    }

    free(scaled_rgba);
    sl_image_free(&img);
    return ok;
}

void sl_cursor_init(sl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->hw_cursor = 0u;
    if (!sl_hw_cursor_load_resource(ctx, ctx->cursor_path)) {
        (void)sl_hw_cursor_set_default(ctx);
    }
}

void sl_hw_cursor_sync_pos(sl_ctx_t *ctx) {
    if (!ctx || !ctx->hw_cursor) return;
    (void)peos_cursor_move_abs(ctx->cursor_x, ctx->cursor_y);
}

void sl_render_cursor_at(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;
    sl_fill_rect(ctx, x, y, SL_CURSOR_W, SL_CURSOR_H, 0xFF, 0xFF, 0xFF);
}

void sl_render_cursor(sl_ctx_t *ctx) {
    sl_render_cursor_at(ctx, ctx->cursor_x, ctx->cursor_y);
}

void sl_sw_cursor_capture(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) {
        ctx->cursor_saved_valid = 0u;
        return;
    }

    uint32_t w = SL_CURSOR_W;
    uint32_t h = SL_CURSOR_H;
    if (x + w > ctx->fb.width) w = ctx->fb.width - x;
    if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    if (w == 0 || h == 0) {
        ctx->cursor_saved_valid = 0u;
        return;
    }

    const size_t save_stride = (size_t)SL_CURSOR_W * 4u;
    const size_t row_bytes = (size_t)w * (size_t)ctx->bytespp;
    for (uint32_t j = 0; j < h; j++) {
        const uint8_t *src = ctx->frame + ((size_t)(y + j) * ctx->row_stride) +
                             ((size_t)x * (size_t)ctx->bytespp);
        peos_memcpy(&ctx->cursor_saved[(size_t)j * save_stride], src, row_bytes);
    }

    ctx->cursor_saved_x = x;
    ctx->cursor_saved_y = y;
    ctx->cursor_saved_w = w;
    ctx->cursor_saved_h = h;
    ctx->cursor_saved_valid = 1u;
}

void sl_sw_cursor_restore(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->cursor_saved_valid) return;

    uint32_t x = ctx->cursor_saved_x;
    uint32_t y = ctx->cursor_saved_y;
    uint32_t w = ctx->cursor_saved_w;
    uint32_t h = ctx->cursor_saved_h;
    if (x >= ctx->fb.width || y >= ctx->fb.height) {
        ctx->cursor_saved_valid = 0u;
        return;
    }
    if (x + w > ctx->fb.width) w = ctx->fb.width - x;
    if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    if (w == 0 || h == 0) {
        ctx->cursor_saved_valid = 0u;
        return;
    }

    const size_t save_stride = (size_t)SL_CURSOR_W * 4u;
    const size_t row_bytes = (size_t)w * (size_t)ctx->bytespp;
    for (uint32_t j = 0; j < h; j++) {
        uint8_t *dst = ctx->frame + ((size_t)(y + j) * ctx->row_stride) +
                       ((size_t)x * (size_t)ctx->bytespp);
        peos_memcpy(dst, &ctx->cursor_saved[(size_t)j * save_stride], row_bytes);
    }

    ctx->cursor_saved_valid = 0u;
}
