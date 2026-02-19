#include "starlight.h"
#include "resources.h"
#include "render/pixels.h"
#include "render/image_decode.h"
#include <stdbool.h>

#define SL_CURSOR_W 3u
#define SL_CURSOR_H 3u
#define SL_HW_CURSOR_W 20u
#define SL_HW_CURSOR_H 20u
#ifndef SL_ENABLE_BG_REDRAW
#define SL_ENABLE_BG_REDRAW 1
#endif
#ifndef SL_TARGET_FPS
#define SL_TARGET_FPS 144u
#endif

#if SL_TARGET_FPS == 0
#error "SL_TARGET_FPS must be > 0"
#endif

#define SL_FRAME_NS (1000000000ull / (uint64_t)(SL_TARGET_FPS))
#define SL_TICK_NS 1000000ull
#define SL_PACER_SPIN_NS 300000ull

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

static int sl_hw_cursor_set_default(sl_ctx_t *ctx) {
    uint8_t cursor_rgba[SL_HW_CURSOR_W * SL_HW_CURSOR_H * 4u];
    sl_build_default_cursor_rgba(cursor_rgba, SL_HW_CURSOR_W, SL_HW_CURSOR_H);
    return sl_hw_cursor_apply(ctx, cursor_rgba, SL_HW_CURSOR_W, SL_HW_CURSOR_H, 0u, 0u);
}

static int sl_hw_cursor_load_resource(sl_ctx_t *ctx, const char *path) {
    if (!ctx || !path || !path[0]) return 0;

    sl_image_t img;
    if (!sl_decode_image_file(path, &img)) return 0;

    int ok = 0;
    if (img.data &&
        img.channels == 4u &&
        img.width > 0u && img.height > 0u &&
        img.width <= 64u && img.height <= 64u &&
        img.stride >= (img.width * 4u)) {
        ok = sl_hw_cursor_apply(ctx, img.data, img.width, img.height, 0u, 0u);
    }

    sl_image_free(&img);
    return ok;
}

static void sl_hw_cursor_sync_pos(sl_ctx_t *ctx) {
    if (!ctx || !ctx->hw_cursor) return;
    (void)peos_cursor_move_abs(ctx->cursor_x, ctx->cursor_y);
}


// Initialize renderer state, map framebuffer memory, and enable raw input mode.
static int sl_init(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    peos_memset(ctx, 0, sizeof(*ctx));

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

    if ((int64_t)peos_input_mode(PEOS_INPUT_MODE_RAW) == 0) {
        ctx->input_raw = 1;
    }

    ctx->cursor_x = ctx->fb.width / 2u;
    ctx->cursor_y = ctx->fb.height / 2u;
    ctx->hw_cursor = 0u;

    // Try hardware cursor first, fallback to software cursor if unavailable.
    if (sl_hw_cursor_set_default(ctx)) {
        (void)sl_hw_cursor_load_resource(ctx, SL_RESOURCE_CURSOR);
    }

    ctx->fps_last_ns = peos_time_ns();
    ctx->fps_frames = 0;
    ctx->fps_value = 0;

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
        if (evs[i].pressed && evs[i].code == PEOS_KEY_C) {
            (void)sl_hw_cursor_load_resource(ctx, SL_RESOURCE_CURSOR);
        }
        if (evs[i].pressed && evs[i].code == PEOS_KEY_V) {
            (void)sl_hw_cursor_set_default(ctx);
        }
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

// Render the full desktop background into the active framebuffer.
static void sl_render_full(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return;

    // Render a simple 2D gradient background.
    uint32_t w = ctx->fb.width;
    uint32_t h = ctx->fb.height;
    uint32_t wden = (w > 1u) ? (w - 1u) : 1u;
    uint32_t hden = (h > 1u) ? (h - 1u) : 1u;

    for (uint32_t y = 0; y < ctx->fb.height; y++) {
        uint32_t ty = (y * 255u) / hden;
        for (uint32_t x = 0; x < ctx->fb.width; x++) {
            uint32_t tx = (x * 255u) / wden;
            uint8_t r = (uint8_t)(12u + ((tx * 36u + ty * 26u) >> 8));
            uint8_t g = (uint8_t)(16u + ((tx * 52u + ty * 54u) >> 8));
            uint8_t b = (uint8_t)(32u + ((tx * 48u + ty * 88u) >> 8));
            size_t off = ((size_t)y * ctx->row_stride) + ((size_t)x * (size_t)ctx->bytespp);
            ctx->frame[off + 0] = b;
            ctx->frame[off + 1] = g;
            ctx->frame[off + 2] = r;
            if (ctx->bytespp == 4) ctx->frame[off + 3] = 0xFF;
        }
    }
}

// Re-render a rectangular portion of the gradient background.
static void sl_render_bg_rect(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
#if !SL_ENABLE_BG_REDRAW
    (void)ctx; (void)x; (void)y; (void)w; (void)h;
    return;
#else
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;
    if (x + w > ctx->fb.width) w = ctx->fb.width - x;
    if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    if (w == 0 || h == 0) return;

    uint32_t fw = ctx->fb.width;
    uint32_t fh = ctx->fb.height;
    uint32_t wden = (fw > 1u) ? (fw - 1u) : 1u;
    uint32_t hden = (fh > 1u) ? (fh - 1u) : 1u;

    for (uint32_t j = 0; j < h; j++) {
        uint32_t py = y + j;
        uint32_t ty = (py * 255u) / hden;
        for (uint32_t i = 0; i < w; i++) {
            uint32_t px = x + i;
            uint32_t tx = (px * 255u) / wden;
            uint8_t r = (uint8_t)(12u + ((tx * 36u + ty * 26u) >> 8));
            uint8_t g = (uint8_t)(16u + ((tx * 52u + ty * 54u) >> 8));
            uint8_t b = (uint8_t)(32u + ((tx * 48u + ty * 88u) >> 8));
            size_t off = ((size_t)py * ctx->row_stride) + ((size_t)px * (size_t)ctx->bytespp);
            ctx->frame[off + 0] = b;
            ctx->frame[off + 1] = g;
            ctx->frame[off + 2] = r;
            if (ctx->bytespp == 4) ctx->frame[off + 3] = 0xFF;
        }
    }
#endif
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
    if (!ctx.hw_cursor) sl_render_cursor(&ctx);
    else sl_hw_cursor_sync_pos(&ctx);
    if (!sl_present(&ctx)) {
        sl_shutdown(&ctx);
        peos_exit(1);
    }

    uint64_t next_frame_ns = peos_time_ns() + SL_FRAME_NS;

    for (;;) {
        uint32_t old_x = ctx.cursor_x;
        uint32_t old_y = ctx.cursor_y;
        if (sl_handle_input(&ctx)) break;
        if (ctx.cursor_x != old_x || ctx.cursor_y != old_y) {
            if (ctx.hw_cursor) {
                sl_hw_cursor_sync_pos(&ctx);
            } else {
#if SL_ENABLE_BG_REDRAW
                sl_render_bg_rect(&ctx, old_x, old_y, SL_CURSOR_W, SL_CURSOR_H);
#endif
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
        if (!ctx.hw_cursor) sl_render_cursor(&ctx);
        if (!sl_present(&ctx)) break;

        sl_frame_pace(&next_frame_ns);

    }

    sl_shutdown(&ctx);
    peos_exit(0);
}
