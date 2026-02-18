#include "starlight.h"
#include "pixels.h"

#define SL_CURSOR_W 3u
#define SL_CURSOR_H 3u

// Initialize renderer state, map framebuffer memory, and enable raw input mode.
static int sl_init(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    peos_memset(ctx, 0, sizeof(*ctx));

    if ((int64_t)peos_fbinfo(&ctx->fb) < 0) return 0;
    if (ctx->fb.bpp != 24 && ctx->fb.bpp != 32) return 0;

    ctx->bytespp = (uint8_t)(ctx->fb.bpp / 8u);
    ctx->frame_bytes = (size_t)ctx->fb.width * (size_t)ctx->fb.height * (size_t)ctx->bytespp;
    ctx->map_bytes = (size_t)align_up((uint64_t)ctx->frame_bytes, 4096u);

    peos_fb_map_t fbm;
    if (peos_fbmap(&fbm, PEOS_FBMAP_EXCLUSIVE) == 0 &&
        fbm.addr != 0 &&
        fbm.len >= ctx->frame_bytes) {
        ctx->frame = (uint8_t *)(uintptr_t)fbm.addr;
        ctx->map_bytes = (size_t)fbm.len;
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

    return 1;
}

// Restore input mode and release user-mapped memory when needed.
static void sl_shutdown(sl_ctx_t *ctx) {
    if (!ctx) return;
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

// Clamp cursor dimensions to the visible framebuffer bounds.
static void sl_cursor_clip(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t *out_w, uint32_t *out_h) {
    uint32_t w = 0;
    uint32_t h = 0;
    if (ctx && ctx->frame && x < ctx->fb.width && y < ctx->fb.height) {
        w = SL_CURSOR_W;
        h = SL_CURSOR_H;
        if (x + w > ctx->fb.width) w = ctx->fb.width - x;
        if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    }
    *out_w = w;
    *out_h = h;
}

// Save the pixels currently under the cursor so they can be restored later.
static void sl_capture_under_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;

    uint32_t w = 0;
    uint32_t h = 0;
    sl_cursor_clip(ctx, x, y, &w, &h);
    if (w == 0 || h == 0) {
        ctx->cursor_saved_valid = 0;
        return;
    }

    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            size_t src_off = ((size_t)(y + j) * (size_t)ctx->fb.width + (size_t)(x + i)) * (size_t)ctx->bytespp;
            size_t dst_off = ((size_t)j * (size_t)SL_CURSOR_W + (size_t)i) * 4u;
            for (uint32_t b = 0; b < (uint32_t)ctx->bytespp; b++) {
                ctx->cursor_saved[dst_off + b] = ctx->frame[src_off + b];
            }
        }
    }

    ctx->cursor_saved_x = x;
    ctx->cursor_saved_y = y;
    ctx->cursor_saved_w = w;
    ctx->cursor_saved_h = h;
    ctx->cursor_saved_valid = 1;
}

// Restore the previously saved pixels from the last cursor position.
static void sl_restore_under_cursor(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->cursor_saved_valid) return;

    uint32_t x = ctx->cursor_saved_x;
    uint32_t y = ctx->cursor_saved_y;
    uint32_t w = ctx->cursor_saved_w;
    uint32_t h = ctx->cursor_saved_h;

    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            size_t dst_off = ((size_t)(y + j) * (size_t)ctx->fb.width + (size_t)(x + i)) * (size_t)ctx->bytespp;
            size_t src_off = ((size_t)j * (size_t)SL_CURSOR_W + (size_t)i) * 4u;
            for (uint32_t b = 0; b < (uint32_t)ctx->bytespp; b++) {
                ctx->frame[dst_off + b] = ctx->cursor_saved[src_off + b];
            }
        }
    }

    ctx->cursor_saved_valid = 0;
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
            size_t off = ((size_t)y * (size_t)ctx->fb.width + (size_t)x) * (size_t)ctx->bytespp;
            ctx->frame[off + 0] = b;
            ctx->frame[off + 1] = g;
            ctx->frame[off + 2] = r;
            if (ctx->bytespp == 4) ctx->frame[off + 3] = 0x00;
        }
    }
}

// Present the frame when using copy mode; no-op in direct fbmap mode.
static int sl_present(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return 0;
    if (ctx->fb_direct) return 1;
    uint64_t wr = peos_fbpresent(ctx->frame, ctx->frame_bytes, PEOS_FBPRESENT_EXCLUSIVE);
    return (int64_t)wr >= 0 && wr == (uint64_t)ctx->frame_bytes;
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

    sl_render_full(&ctx);
    sl_capture_under_cursor(&ctx, ctx.cursor_x, ctx.cursor_y);
    sl_render_cursor(&ctx);
    if (!sl_present(&ctx)) {
        sl_shutdown(&ctx);
        peos_exit(1);
    }

    for (;;) {
        uint32_t old_x = ctx.cursor_x;
        uint32_t old_y = ctx.cursor_y;
        if (sl_handle_input(&ctx)) break;
        if (ctx.cursor_x != old_x || ctx.cursor_y != old_y) {
            sl_restore_under_cursor(&ctx);
            sl_capture_under_cursor(&ctx, ctx.cursor_x, ctx.cursor_y);
            sl_render_cursor(&ctx);
            if (!sl_present(&ctx)) break;
        }
        peos_sleep(16);
    }

    sl_shutdown(&ctx);
    peos_exit(0);
}
