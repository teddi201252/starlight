#include "starlight.h"
#include "pixels.h"

static int sl_init(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    peos_memset(ctx, 0, sizeof(*ctx));

    if ((int64_t)peos_fbinfo(&ctx->fb) < 0) return 0;
    if (ctx->fb.bpp != 24 && ctx->fb.bpp != 32) return 0;

    ctx->bytespp = (uint8_t)(ctx->fb.bpp / 8u);
    ctx->frame_bytes = (size_t)ctx->fb.width * (size_t)ctx->fb.height * (size_t)ctx->bytespp;
    ctx->map_bytes = (size_t)align_up((uint64_t)ctx->frame_bytes, 4096u);

    uint64_t map = peos_mmap(0, ctx->map_bytes,
                             PEOS_PROT_READ | PEOS_PROT_WRITE,
                             PEOS_MAP_ANON | PEOS_MAP_PRIVATE);
    if ((int64_t)map < 0) return 0;
    ctx->frame = (uint8_t *)(uintptr_t)map;

    if ((int64_t)peos_input_mode(PEOS_INPUT_MODE_RAW) == 0) {
        ctx->input_raw = 1;
    }

    ctx->cursor_x = ctx->fb.width / 2u;
    ctx->cursor_y = ctx->fb.height / 2u;

    return 1;
}

static void sl_shutdown(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->input_raw) (void)peos_input_mode(PEOS_INPUT_MODE_COOKED);
    if (ctx->frame) (void)peos_munmap((uint64_t)(uintptr_t)ctx->frame, ctx->map_bytes);
}

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

static void sl_render_cursor(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return;
    if (ctx->cursor_x >= ctx->fb.width || ctx->cursor_y >= ctx->fb.height) return;
    sl_fill_rect(ctx, ctx->cursor_x, ctx->cursor_y, 3, 3, 0xFF, 0xFF, 0xFF);
}

static void sl_render(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return;

    // Minimal placeholder background.
    for (uint32_t y = 0; y < ctx->fb.height; y++) {
        for (uint32_t x = 0; x < ctx->fb.width; x++) {
            size_t off = ((size_t)y * (size_t)ctx->fb.width + (size_t)x) * (size_t)ctx->bytespp;
            ctx->frame[off + 0] = 0x14; // B
            ctx->frame[off + 1] = 0x10; // G
            ctx->frame[off + 2] = 0x0E; // R
            if (ctx->bytespp == 4) ctx->frame[off + 3] = 0x00;
        }
    }

    sl_render_cursor(ctx);
}

static int sl_present(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return 0;
    uint64_t wr = peos_fbpresent(ctx->frame, ctx->frame_bytes, PEOS_FBPRESENT_EXCLUSIVE);
    return (int64_t)wr >= 0 && wr == (uint64_t)ctx->frame_bytes;
}

int main(void) {
    sl_ctx_t ctx;
    if (!sl_init(&ctx)) {
        peos_puts("starlight: init failed\n");
        peos_exit(1);
    }

    peos_puts("starlight: minimal skeleton running (ESC to exit)\n");

    for (;;) {
        if (sl_handle_input(&ctx)) break;

        // TODO: update desktop state here.
        sl_render(&ctx);
        if (!sl_present(&ctx)) break;
        peos_sleep(16);
    }

    sl_shutdown(&ctx);
    peos_exit(0);
}
