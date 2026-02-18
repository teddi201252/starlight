#include "peos/peos.h"

typedef struct {
    peos_fb_info_t fb;
    uint8_t *frame;
    size_t frame_bytes;
    size_t map_bytes;
    uint8_t bytespp;
    int input_raw;
} sl_ctx_t;

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

    return 1;
}

static void sl_shutdown(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->input_raw) (void)peos_input_mode(PEOS_INPUT_MODE_COOKED);
    if (ctx->frame) (void)peos_munmap((uint64_t)(uintptr_t)ctx->frame, ctx->map_bytes);
}

static int sl_handle_input(void) {
    peos_input_event_t evs[16];
    uint64_t n = peos_input_poll(evs, 16);
    if ((int64_t)n < 0) return 1;

    for (uint64_t i = 0; i < n; i++) {
        if (evs[i].pressed && evs[i].code == PEOS_KEY_ESC) return 1;
    }
    return 0;
}

static void sl_render_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !ctx->frame) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    size_t off = ((size_t)y * (size_t)ctx->fb.width + (size_t)x) * (size_t)ctx->bytespp;
    ctx->frame[off + 0] = 0xFF; // B
    ctx->frame[off + 1] = 0xFF; // G
    ctx->frame[off + 2] = 0xFF; // R
    if (ctx->bytespp == 4) ctx->frame[off + 3] = 0xFF;
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

    // Placeholder cursor in the middle of the screen.
    sl_render_cursor(ctx, ctx->fb.width / 2, ctx->fb.height / 2);
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
        if (sl_handle_input()) break;

        // TODO: update desktop state here.
        sl_render(&ctx);
        if (!sl_present(&ctx)) break;
        peos_sleep(16);
    }

    sl_shutdown(&ctx);
    peos_exit(0);
}
