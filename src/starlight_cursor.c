#include "starlight_internal.h"
#include "render/image_decode.h"
#include "render/pixels.h"
#include "render/blit.h"
#include "render/skin.h"

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

// ------------------------------------------------------------- the set

static const char *const k_cursor_names[SL_CUR_COUNT] = {
    "cursor-arrow", "cursor-hand", "cursor-text", "cursor-resize-h", "cursor-resize-v",
    "cursor-resize-nwse", "cursor-resize-nesw", "cursor-move", "cursor-wait", "cursor-no",
};

// The cursor showing: premultiplied BGRA at the configured scale, its hot
// spot, and what the software cursor saved under it.
static struct {
    uint8_t ready;                 // the set is in the skin and in use
    int kind;                      // showing, -1 none yet
    uint8_t px[SL_HW_CURSOR_MAX_SIDE * SL_HW_CURSOR_MAX_SIDE * 4u];
    uint32_t w, h, hx, hy;
    uint32_t crop_x, crop_y;       // columns and rows cut off the hardware image (screen edge)
    uint8_t saved[SL_HW_CURSOR_MAX_SIDE * SL_HW_CURSOR_MAX_SIDE * 4u];
} g_cur = { .kind = -1 };

// The hot spot is Starlight's to place, not the host's: the hardware
// cursor gets its image with the hot spot at 0,0 and is moved to the mouse
// minus the hot spot (QEMU's displays do not all honour a hot spot, and the
// tip of the arrow sat 10,7 px off). Near the left or top edge, where that
// would be below zero, the image goes with its first columns or rows cut.
static void sl_cursor_hw_place(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y, uint32_t *cx, uint32_t *cy) {
    *cx = ctx->cursor_x < g_cur.hx ? g_cur.hx - ctx->cursor_x : 0u;
    *cy = ctx->cursor_y < g_cur.hy ? g_cur.hy - ctx->cursor_y : 0u;
    *x = ctx->cursor_x + *cx - g_cur.hx;
    *y = ctx->cursor_y + *cy - g_cur.hy;
}

// Hand the current image (less crop_x, crop_y) to the hardware cursor, in
// straight RGBA, its hot spot at 0,0.
static int sl_cursor_apply_hw(sl_ctx_t *ctx) {
    uint8_t rgba[SL_HW_CURSOR_MAX_SIDE * SL_HW_CURSOR_MAX_SIDE * 4u];
    peos_memset(rgba, 0, (size_t)g_cur.w * g_cur.h * 4u);
    for (uint32_t y = g_cur.crop_y; y < g_cur.h; y++) {
        for (uint32_t x = g_cur.crop_x; x < g_cur.w; x++) {
            const uint8_t *s = &g_cur.px[(y * g_cur.w + x) * 4u];
            uint8_t *d = &rgba[((y - g_cur.crop_y) * g_cur.w + (x - g_cur.crop_x)) * 4u];
            uint32_t a = s[3];
            d[0] = a ? (uint8_t)((s[2] * 255u + a / 2u) / a) : 0u;
            d[1] = a ? (uint8_t)((s[1] * 255u + a / 2u) / a) : 0u;
            d[2] = a ? (uint8_t)((s[0] * 255u + a / 2u) / a) : 0u;
            d[3] = (uint8_t)a;
        }
    }
    if ((int64_t)peos_cursor_set_image_rgba(rgba, g_cur.w, g_cur.h, 0u, 0u) < 0) return 0;
    uint32_t x, y, cx, cy;
    sl_cursor_hw_place(ctx, &x, &y, &cx, &cy);
    (void)peos_cursor_move_abs(x, y);
    if ((int64_t)peos_cursor_set_visible(1) < 0) return 0;
    ctx->hw_cursor = 1u;
    return 1;
}

void sl_cursor_set(sl_ctx_t *ctx, sl_cursor_kind_t kind) {
    if (!ctx || !g_cur.ready || (int)kind == g_cur.kind || kind >= SL_CUR_COUNT) return;
    const slsk_entry_t *e = sl_skin_find(&ctx->skin, k_cursor_names[kind]);
    if (!e) e = sl_skin_find(&ctx->skin, k_cursor_names[SL_CUR_ARROW]);
    if (!e) return;
    uint32_t scale = sl_cursor_effective_scale_pct(e->width, e->height, ctx->hw_cursor_scale_pct);
    uint32_t w = 0u, h = 0u;
    if (scale == 0u || !sl_cursor_scale_rgba_nn(sl_skin_data(&ctx->skin, e), e->width, e->height, e->width * 4u, scale,
                                                g_cur.px, &w, &h)) {
        return;
    }
    g_cur.kind = (int)kind;
    g_cur.w = w;
    g_cur.h = h;
    // The hot spot is kept in pad_l/pad_t (slskin's "cursor").
    g_cur.hx = (e->pad_l * scale + 50u) / 100u;
    g_cur.hy = (e->pad_t * scale + 50u) / 100u;
    if (g_cur.hx >= w) g_cur.hx = w - 1u;
    if (g_cur.hy >= h) g_cur.hy = h - 1u;
    if (ctx->hw_cursor) {
        uint32_t x, y;
        sl_cursor_hw_place(ctx, &x, &y, &g_cur.crop_x, &g_cur.crop_y);
        (void)sl_cursor_apply_hw(ctx);
    }
}

void sl_cursor_init(sl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->hw_cursor = 0u;
    g_cur.ready = 0u;
    g_cur.kind = -1;
    // The Soft UI set: the hardware cursor where there is one (virtio-gpu,
    // Intel), else drawn by Starlight with the same images.
    if (ctx->cursor_theme && ctx->skin_loaded && sl_skin_find(&ctx->skin, k_cursor_names[SL_CUR_ARROW])) {
        g_cur.ready = 1u;
        ctx->hw_cursor = 1u;   // try it first
        sl_cursor_set(ctx, SL_CUR_ARROW);
        if (g_cur.kind < 0 || !sl_cursor_apply_hw(ctx)) ctx->hw_cursor = 0u;
        return;
    }
    // One image of the user's (cursor= in settings.cfg).
    if (!sl_hw_cursor_load_resource(ctx, ctx->cursor_path)) {
        (void)sl_hw_cursor_set_default(ctx);
    }
    if (!ctx->hw_cursor && ctx->skin_loaded && sl_skin_find(&ctx->skin, k_cursor_names[SL_CUR_ARROW])) {
        g_cur.ready = 1u;   // no hardware cursor: draw the arrow at least
        sl_cursor_set(ctx, SL_CUR_ARROW);
    }
}

void sl_hw_cursor_sync_pos(sl_ctx_t *ctx) {
    if (!ctx || !ctx->hw_cursor) return;
    if (!g_cur.ready || g_cur.kind < 0) {   // a single image: its hot spot is 0,0
        (void)peos_cursor_move_abs(ctx->cursor_x, ctx->cursor_y);
        return;
    }
    uint32_t x, y, cx, cy;
    sl_cursor_hw_place(ctx, &x, &y, &cx, &cy);
    if (cx != g_cur.crop_x || cy != g_cur.crop_y) {   // at an edge: the image cut differently
        g_cur.crop_x = cx;
        g_cur.crop_y = cy;
        (void)sl_cursor_apply_hw(ctx);
        return;
    }
    (void)peos_cursor_move_abs(x, y);
}

// ------------------------------------------------------ the software cursor

// Where the software cursor is drawn now, clipped to the screen.
static int sl_sw_cursor_rect(const sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t *rx, uint32_t *ry,
                             uint32_t *rw, uint32_t *rh) {
    if (!ctx || g_cur.kind < 0 || x >= ctx->fb.width || y >= ctx->fb.height) return 0;
    int64_t x0 = (int64_t)x - g_cur.hx, y0 = (int64_t)y - g_cur.hy;
    int64_t x1 = x0 + g_cur.w, y1 = y0 + g_cur.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)ctx->fb.width) x1 = ctx->fb.width;
    if (y1 > (int64_t)ctx->fb.height) y1 = ctx->fb.height;
    if (x1 <= x0 || y1 <= y0) return 0;
    *rx = (uint32_t)x0;
    *ry = (uint32_t)y0;
    *rw = (uint32_t)(x1 - x0);
    *rh = (uint32_t)(y1 - y0);
    return 1;
}

void sl_render_cursor(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || g_cur.kind < 0) return;
    slsk_entry_t e;
    peos_memset(&e, 0, sizeof(e));
    e.kind = SLSK_KIND_PREMUL;
    e.width = (uint16_t)g_cur.w;
    e.height = (uint16_t)g_cur.h;
    uint8_t clip = ctx->clip_enabled;
    ctx->clip_enabled = 0u;
    sl_target_t t = sl_target_from_ctx(ctx);
    ctx->clip_enabled = clip;
    sl_blit_sprite(&t, &e, g_cur.px, (int32_t)ctx->cursor_x - (int32_t)g_cur.hx,
                   (int32_t)ctx->cursor_y - (int32_t)g_cur.hy, g_cur.w, g_cur.h);
}

void sl_sw_cursor_dirty(sl_ctx_t *ctx) {
    uint32_t x, y, w, h;
    if (sl_sw_cursor_rect(ctx, ctx->cursor_x, ctx->cursor_y, &x, &y, &w, &h)) sl_dirty_add(ctx, x, y, w, h);
}

void sl_sw_cursor_capture(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;
    uint32_t rx, ry, w, h;
    if (!sl_sw_cursor_rect(ctx, x, y, &rx, &ry, &w, &h)) {
        ctx->cursor_saved_valid = 0u;
        return;
    }
    const size_t save_stride = (size_t)SL_HW_CURSOR_MAX_SIDE * 4u;
    const size_t row_bytes = (size_t)w * (size_t)ctx->bytespp;
    for (uint32_t j = 0; j < h; j++) {
        const uint8_t *src = ctx->frame + ((size_t)(ry + j) * ctx->row_stride) + ((size_t)rx * (size_t)ctx->bytespp);
        peos_memcpy(&g_cur.saved[(size_t)j * save_stride], src, row_bytes);
    }
    ctx->cursor_saved_x = rx;
    ctx->cursor_saved_y = ry;
    ctx->cursor_saved_w = w;
    ctx->cursor_saved_h = h;
    ctx->cursor_saved_valid = 1u;
}

void sl_sw_cursor_restore(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->cursor_saved_valid) return;
    uint32_t x = ctx->cursor_saved_x, y = ctx->cursor_saved_y, w = ctx->cursor_saved_w, h = ctx->cursor_saved_h;
    const size_t save_stride = (size_t)SL_HW_CURSOR_MAX_SIDE * 4u;
    const size_t row_bytes = (size_t)w * (size_t)ctx->bytespp;
    for (uint32_t j = 0; j < h; j++) {
        uint8_t *dst = ctx->frame + ((size_t)(y + j) * ctx->row_stride) + ((size_t)x * (size_t)ctx->bytespp);
        peos_memcpy(dst, &g_cur.saved[(size_t)j * save_stride], row_bytes);
    }
    ctx->cursor_saved_valid = 0u;
}
