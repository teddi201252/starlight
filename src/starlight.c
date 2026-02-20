#include "starlight.h"
#include "render/pixels.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SL_CURSOR_W 3u
#define SL_CURSOR_H 3u
#ifndef SL_TARGET_FPS
#define SL_TARGET_FPS 144u
#endif

#if SL_TARGET_FPS == 0
#error "SL_TARGET_FPS must be > 0"
#endif

#define SL_FRAME_NS (1000000000ull / (uint64_t)(SL_TARGET_FPS))
#define SL_TICK_NS 1000000ull
#define SL_PACER_SPIN_NS 300000ull
#define SL_HW_CURSOR_MAX_SIDE 64u
#define SL_HW_CURSOR_SCALE_DEFAULT 100u
#define SL_CFG_FILE_MAX 4096u
#define SL_CFG_LINE_MAX 256u

static void sl_copy_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = 0;
    while (n + 1 < cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int sl_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *sl_trim(char *s) {
    if (!s) return s;
    while (*s && sl_is_space(*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && sl_is_space(s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
    return s;
}

static int sl_parse_u32(const char *s, uint32_t *out) {
    if (!s || !out) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || (uint64_t)v > 0xFFFFFFFFull) return 0;
    *out = (uint32_t)v;
    return 1;
}

static void sl_ensure_config_dirs(void) {
    (void)mkdir("/system", 0755);
    (void)mkdir(SL_OS_CONFIG_DIR, 0755);
    (void)mkdir(SL_RESOURCE_ROOT, 0755);
    (void)mkdir(SL_RESOURCE_CONFIG_DIR, 0755);
}

static char *sl_load_text_file(const char *path, size_t max_bytes) {
    if (!path || max_bytes == 0) return NULL;
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static void sl_write_default_display_config(void) {
    FILE *f = fopen(SL_OS_DISPLAY_CONFIG_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# PeOS global display config\n");
    (void)fprintf(f, "# 0 0 means host/default mode\n");
    (void)fprintf(f, "width=0\n");
    (void)fprintf(f, "height=0\n");
    (void)fclose(f);
}

static void sl_write_default_starlight_config(void) {
    FILE *f = fopen(SL_RESOURCE_CONFIG_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# Starlight desktop config\n");
    (void)fprintf(f, "background=%s\n", SL_RESOURCE_BACKGROUND_ALT);
    (void)fprintf(f, "cursor=%s\n", SL_RESOURCE_CURSOR);
    (void)fprintf(f, "cursor_scale=100\n");
    (void)fclose(f);
}

static void sl_load_display_config(sl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->display_width = 0u;
    ctx->display_height = 0u;

    char *cfg = sl_load_text_file(SL_OS_DISPLAY_CONFIG_FILE, SL_CFG_FILE_MAX);
    if (!cfg) {
        sl_write_default_display_config();
        return;
    }

    char line[SL_CFG_LINE_MAX];
    size_t li = 0;
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0;

        char *p = sl_trim(line);
        if (p[0] != '\0' && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = sl_trim(p);
                char *val = sl_trim(eq + 1);
                uint32_t n = 0;
                if (strcmp(key, "width") == 0 && sl_parse_u32(val, &n)) {
                    ctx->display_width = n;
                } else if (strcmp(key, "height") == 0 && sl_parse_u32(val, &n)) {
                    ctx->display_height = n;
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);
}

static void sl_load_starlight_config(sl_ctx_t *ctx) {
    if (!ctx) return;
    sl_copy_text(ctx->background_path, sizeof(ctx->background_path), SL_RESOURCE_BACKGROUND_ALT);
    sl_copy_text(ctx->cursor_path, sizeof(ctx->cursor_path), SL_RESOURCE_CURSOR);
    ctx->hw_cursor_scale_pct = SL_HW_CURSOR_SCALE_DEFAULT;

    char *cfg = sl_load_text_file(SL_RESOURCE_CONFIG_FILE, SL_CFG_FILE_MAX);
    if (!cfg) {
        sl_write_default_starlight_config();
        return;
    }

    char line[SL_CFG_LINE_MAX];
    size_t li = 0;
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0;

        char *p = sl_trim(line);
        if (p[0] != '\0' && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = sl_trim(p);
                char *val = sl_trim(eq + 1);
                if (strcmp(key, "background") == 0 && val[0]) {
                    sl_copy_text(ctx->background_path, sizeof(ctx->background_path), val);
                } else if (strcmp(key, "cursor") == 0 && val[0]) {
                    sl_copy_text(ctx->cursor_path, sizeof(ctx->cursor_path), val);
                } else if (strcmp(key, "cursor_scale") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n) && n >= 25u && n <= 400u) {
                        ctx->hw_cursor_scale_pct = n;
                    }
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);
}

// Converts a uint32_t to decimal text.
// Returns `out` on success, NULL on error.
static char *sl_u32_to_str(uint32_t value, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return NULL;

    if (value == 0) {
        if (out_cap < 2) return NULL;
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    char tmp[10]; // max decimal digits for uint32_t
    size_t n = 0;
    while (value != 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    if (n + 1 > out_cap) return NULL;

    for (size_t i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    return out;
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
                dst[off + 0] = 0x00; // R
                dst[off + 1] = 0x00; // G
                dst[off + 2] = 0x00; // B
                dst[off + 3] = 0xFF; // A
            } else {
                dst[off + 0] = 0xFF; // R
                dst[off + 1] = 0xFF; // G
                dst[off + 2] = 0xFF; // B
                dst[off + 3] = 0xFF; // A
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

    // Round to nearest pixel: dst = src * scale_pct / 100.
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
    if (!sl_cursor_scale_rgba_nn(base_rgba,
                                 SL_DEFAULT_CURSOR_W, SL_DEFAULT_CURSOR_H, SL_DEFAULT_CURSOR_W * 4u,
                                 ctx->hw_cursor_scale_pct,
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
        if (sl_cursor_scale_rgba_nn(img.data,
                                    img.width, img.height, img.stride,
                                    ctx->hw_cursor_scale_pct,
                                    scaled_rgba,
                                    &out_w, &out_h)) {
            ok = sl_hw_cursor_apply(ctx, scaled_rgba, out_w, out_h, 0u, 0u);
        }
    }

    free(scaled_rgba);
    sl_image_free(&img);
    return ok;
}

static void sl_hw_cursor_sync_pos(sl_ctx_t *ctx) {
    if (!ctx || !ctx->hw_cursor) return;
    (void)peos_cursor_move_abs(ctx->cursor_x, ctx->cursor_y);
}

static int sl_update_fb_info(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    if ((int64_t)peos_fbinfo(&ctx->fb) < 0) return 0;
    if (ctx->fb.bpp != 24 && ctx->fb.bpp != 32) return 0;

    ctx->bytespp = (uint8_t)(ctx->fb.bpp / 8u);
    ctx->row_stride = (size_t)ctx->fb.width * (size_t)ctx->bytespp;
    ctx->frame_bytes = (size_t)ctx->fb.width * (size_t)ctx->fb.height * (size_t)ctx->bytespp;
    ctx->map_bytes = (size_t)align_up((uint64_t)ctx->frame_bytes, 4096u);

    peos_fb_map_t fbm;
    size_t fb_row_stride = ctx->fb.pitch ? (size_t)ctx->fb.pitch : ctx->row_stride;
    size_t fb_needed = fb_row_stride * (size_t)ctx->fb.height;
    if (peos_fbmap(&fbm, PEOS_FBMAP_EXCLUSIVE) == 0 &&
        fbm.addr != 0 &&
        fbm.len >= fb_needed) {
        ctx->frame = (uint8_t *)(uintptr_t)fbm.addr;
        ctx->map_bytes = (size_t)fbm.len;
        ctx->row_stride = fb_row_stride;
        ctx->fb_direct = 1;
    } else {
        uint64_t map = peos_mmap(0, ctx->map_bytes,
                                 PEOS_PROT_READ | PEOS_PROT_WRITE,
                                 PEOS_MAP_ANON | PEOS_MAP_PRIVATE);
        if ((int64_t)map < 0) return 0;
        ctx->frame = (uint8_t *)(uintptr_t)map;
    }
    return 1;
}

static void sl_load_background(sl_ctx_t *ctx, const char *path) {
    int ok = 0;
    if (path && path[0]) {
        ok = sl_decode_image_file(path, &ctx->bg_image);
    }
    if (!ok) {
        ok = sl_decode_image_file(SL_RESOURCE_BACKGROUND_ALT, &ctx->bg_image);
    }
    if (!ok) {
        ok = sl_decode_image_file(SL_RESOURCE_BACKGROUND, &ctx->bg_image);
    }
    ctx->bg_loaded = ok;
}

// Initialize renderer state, map framebuffer memory, and enable raw input mode.
static int sl_init(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    peos_memset(ctx, 0, sizeof(*ctx));

    sl_ensure_config_dirs();
    sl_load_display_config(ctx);
    sl_load_starlight_config(ctx);

    if ((ctx->display_width == 0u && ctx->display_height == 0u) ||
        (ctx->display_width > 0u && ctx->display_height > 0u)) {
        (void)peos_fb_set_mode(ctx->display_width, ctx->display_height);
    }

    if (!sl_update_fb_info(ctx)) return 0;

    if ((int64_t)peos_input_mode(PEOS_INPUT_MODE_RAW) == 0) {
        ctx->input_raw = 1;
    }

    ctx->cursor_x = ctx->fb.width / 2u;
    ctx->cursor_y = ctx->fb.height / 2u;
    ctx->hw_cursor = 0u;

    // Prefer resource cursor (size from image), fallback to default shape.
    if (!sl_hw_cursor_load_resource(ctx, ctx->cursor_path)) {
        (void)sl_hw_cursor_set_default(ctx);
    }

    ctx->fps_last_ns = peos_time_ns();
    ctx->fps_frames = 0;
    ctx->fps_value = 0;

    sl_load_background(ctx, ctx->background_path);

    return 1;
}

// Restore input mode and release user-mapped memory when needed.
static void sl_shutdown(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->hw_cursor) (void)peos_cursor_set_visible(0);
    if (ctx->input_raw) (void)peos_input_mode(PEOS_INPUT_MODE_COOKED);
    if (ctx->frame && !ctx->fb_direct) (void)peos_munmap((uint64_t)(uintptr_t)ctx->frame, ctx->map_bytes);
}

// Poll keyboard/mouse events and update the cursor position.
static int sl_handle_input(sl_ctx_t *ctx) {
    peos_input_event_t evs[16];
    peos_mouse_event_t mev;
    uint64_t n = peos_input_poll(evs, 16);
    int64_t mr = peos_input_mouse(&mev);
    if ((int64_t)n < 0 || mr < 0) return 1;

    for (uint64_t i = 0; i < n; i++) {
        if (evs[i].pressed && evs[i].code == PEOS_KEY_ESC) return 1;
    }

    if (mr >= 0) {
        int64_t nx = (int64_t)ctx->cursor_x + (int64_t)mev.dx;
        int64_t ny = (int64_t)ctx->cursor_y + (int64_t)mev.dy;

        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (ctx->fb.width > 0 && nx >= (int64_t)ctx->fb.width) nx = (int64_t)ctx->fb.width - 1;
        if (ctx->fb.height > 0 && ny >= (int64_t)ctx->fb.height) ny = (int64_t)ctx->fb.height - 1;

        ctx->cursor_x = (uint32_t)nx;
        ctx->cursor_y = (uint32_t)ny;
    }
    return 0;
}

// Draw the cursor at an explicit pixel coordinate.
static void sl_render_cursor_at(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;
    sl_fill_rect(ctx, x, y, SL_CURSOR_W, SL_CURSOR_H, 0xFF, 0xFF, 0xFF);
}

// Draw the cursor at the current cursor position.
static void sl_render_cursor(sl_ctx_t *ctx) {
    sl_render_cursor_at(ctx, ctx->cursor_x, ctx->cursor_y);
}

// Save framebuffer pixels under the software cursor so we can restore them on next frame.
static void sl_sw_cursor_capture(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
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

    const size_t save_stride = (size_t)SL_CURSOR_W * 4u; // max bytes per saved row
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

// Restore previously saved 3x3 area that was covered by the software cursor.
static void sl_sw_cursor_restore(sl_ctx_t *ctx) {
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

// Render the full desktop background into the active framebuffer.
static void sl_render_full(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->bg_loaded) return;
    if (ctx->bg_image.data && ctx->bg_image.channels >= 3u) {
        sl_blit_rgba_stretch(ctx, 0, 0, ctx->fb.width, ctx->fb.height, ctx->bg_image.data, ctx->bg_image.width, ctx->bg_image.height, ctx->bg_image.stride);
    }
}

// Present the frame when using copy mode; no-op in direct fbmap mode.
static int sl_present(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return 0;
    if (ctx->fb_direct) return 1;
    uint64_t wr = peos_fbpresent(ctx->frame, ctx->frame_bytes, PEOS_FBPRESENT_EXCLUSIVE);
    return (int64_t)wr >= 0 && wr == (uint64_t)ctx->frame_bytes;
}

// Pace the main loop to ~60 FPS using coarse sleep plus a short spin wait.
static void sl_frame_pace(uint64_t *next_deadline_ns) {
    if (!next_deadline_ns) return;

    uint64_t now = peos_time_ns();
    if (*next_deadline_ns == 0) {
        *next_deadline_ns = now + SL_FRAME_NS;
        return;
    }

    if (now < *next_deadline_ns) {
        uint64_t remaining = *next_deadline_ns - now;
        if (remaining > (SL_PACER_SPIN_NS + SL_TICK_NS)) {
            uint64_t sleep_ns = remaining - SL_PACER_SPIN_NS;
            uint64_t sleep_ticks = sleep_ns / SL_TICK_NS;
            if (sleep_ticks) peos_sleep(sleep_ticks);
        }
        while ((now = peos_time_ns()) < *next_deadline_ns) {
            __asm__ volatile("pause");
        }
    } else if ((now - *next_deadline_ns) > (SL_FRAME_NS * 4ull)) {
        // If the app lags badly, resync pacing to avoid long-term drift.
        *next_deadline_ns = now;
    }

    *next_deadline_ns += SL_FRAME_NS;
}

// Entry point of the Starlight desktop loop.
int main(void) {
    sl_ctx_t ctx;
    if (!sl_init(&ctx)) {
        peos_puts("starlight: init failed\n");
        peos_exit(1);
    }

    peos_puts("starlight: minimal skeleton running (ESC to exit)\n");
    if (ctx.fb_direct) {
        peos_puts("starlight: renderer mode = fbmap zero-copy\n");
    } else {
        peos_puts("starlight: renderer mode = fallback copy\n");
    }
    if (ctx.hw_cursor) peos_puts("starlight: cursor mode = hardware\n");
    else peos_puts("starlight: cursor mode = software\n");

    sl_render_full(&ctx);
    if (!ctx.hw_cursor) {
        sl_sw_cursor_capture(&ctx, ctx.cursor_x, ctx.cursor_y);
        sl_render_cursor(&ctx);
    }
    else sl_hw_cursor_sync_pos(&ctx);
    if (!sl_present(&ctx)) {
        sl_shutdown(&ctx);
        peos_exit(1);
    }

    uint64_t next_frame_ns = peos_time_ns() + SL_FRAME_NS;

    for (;;) {
        if (!ctx.hw_cursor) sl_sw_cursor_restore(&ctx);

        uint32_t old_x = ctx.cursor_x;
        uint32_t old_y = ctx.cursor_y;
        if (sl_handle_input(&ctx)) break;
        if (ctx.cursor_x != old_x || ctx.cursor_y != old_y) {
            if (ctx.hw_cursor) {
                sl_hw_cursor_sync_pos(&ctx);
            }
        }

        // FPS Counter
        ctx.fps_frames++;
        uint64_t now = peos_time_ns();
        uint64_t dt = now - ctx.fps_last_ns;
        if (dt >= 1000000000ull) {
            ctx.fps_value = (uint32_t)((ctx.fps_frames * 1000000000ull) / dt);
            ctx.fps_frames = 0;
            ctx.fps_last_ns = now;
        }

        char fps_str[16];
        if (!sl_u32_to_str(ctx.fps_value, fps_str, sizeof(fps_str))) {
            fps_str[0] = '?';
            fps_str[1] = '\0';
        }
        sl_fill_rect(&ctx, 0, 0, 128, 16, 0x00, 0x00, 0x00);
        sl_draw_text(&ctx, 0, 0, "fps: ", 0xFF, 0xFF, 0xFF);
        sl_draw_text(&ctx, 32, 0, fps_str, 0xFF, 0xFF, 0xFF);
        if (!ctx.hw_cursor) {
            sl_sw_cursor_capture(&ctx, ctx.cursor_x, ctx.cursor_y);
            sl_render_cursor(&ctx);
        }
        if (!sl_present(&ctx)) break;

        sl_frame_pace(&next_frame_ns);

    }

    if (ctx.bg_loaded) {
        sl_image_free(&ctx.bg_image);
    }
    sl_shutdown(&ctx);
    peos_exit(0);
}
