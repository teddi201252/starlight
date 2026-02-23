#include "starlight_internal.h"
#include "render/pixels.h"

// Entry point of the Starlight desktop loop.
int main(void) {
    sl_ctx_t ctx;
    if (!sl_init(&ctx)) {
        peos_puts("starlight: init failed\n");
        peos_exit(1);
    }
    sl_audio_init();
    peos_puts("starlight: minimal skeleton running (ESC to exit)\n");
    if (ctx.fb_direct) {
        peos_puts("starlight: renderer mode = fbmap zero-copy\n");
    } else {
        peos_puts("starlight: renderer mode = fallback copy\n");
    }
    if (ctx.hw_cursor) peos_puts("starlight: cursor mode = hardware\n");
    else peos_puts("starlight: cursor mode = software\n");

    sl_render_full(&ctx);
    sl_dirty_mark_full();
    if (!ctx.hw_cursor) {
        sl_sw_cursor_capture(&ctx, ctx.cursor_x, ctx.cursor_y);
        sl_render_cursor(&ctx);
        sl_dirty_add(&ctx, ctx.cursor_x, ctx.cursor_y, SL_CURSOR_W, SL_CURSOR_H);
    }
    else sl_hw_cursor_sync_pos(&ctx);
    if (!sl_present(&ctx)) {
        sl_audio_shutdown();
        sl_shutdown(&ctx);
        peos_exit(1);
    }

    sl_windows_create_default(&ctx);

    uint64_t next_frame_ns = 0;
    uint8_t orbit_prompt_blink_prev = 0u;

    for (;;) {
        if (!ctx.hw_cursor) {
            if (ctx.cursor_saved_valid) {
                sl_dirty_add(&ctx, ctx.cursor_saved_x, ctx.cursor_saved_y,
                             ctx.cursor_saved_w, ctx.cursor_saved_h);
            }
            sl_sw_cursor_restore(&ctx);
        }

        uint32_t old_x = ctx.cursor_x;
        uint32_t old_y = ctx.cursor_y;
        if (sl_handle_input(&ctx)) break;
        if (ctx.cursor_x != old_x || ctx.cursor_y != old_y) {
            if (ctx.hw_cursor) {
                sl_hw_cursor_sync_pos(&ctx);
            }
        }

        {
            uint8_t orbit_prompt_blink_now = (uint8_t)((peos_time_ns() / 500000000ull) & 1ull);
            if (orbit_prompt_blink_now != orbit_prompt_blink_prev) {
                orbit_prompt_blink_prev = orbit_prompt_blink_now;
                sl_window_t *focused = sl_window_get_focused();
                if (focused) {
                    sl_dirty_add(&ctx, focused->x, focused->y, focused->width, focused->height);
                }
            }
        }

        sl_render_dirty_background(&ctx);

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
        sl_dirty_add(&ctx, 0u, 0u, 128u, 16u);
        sl_fill_rect(&ctx, 0, 0, 128, 16, 0x00, 0x00, 0x00);
        sl_draw_text(&ctx, 0, 0, "fps: ", 0xFF, 0xFF, 0xFF);
        sl_draw_text(&ctx, 32, 0, fps_str, 0xFF, 0xFF, 0xFF);

        sl_windows_render_all(&ctx);

        if (!ctx.hw_cursor) {
            sl_sw_cursor_capture(&ctx, ctx.cursor_x, ctx.cursor_y);
            sl_render_cursor(&ctx);
            sl_dirty_add(&ctx, ctx.cursor_x, ctx.cursor_y, SL_CURSOR_W, SL_CURSOR_H);
        }
        if (!sl_present(&ctx)) break;

        sl_frame_pace(&next_frame_ns);

    }

    if (ctx.bg_loaded) {
        sl_image_free(&ctx.bg_image);
    }
    sl_windows_destroy_all();
    sl_audio_shutdown();
    sl_shutdown(&ctx);
    peos_exit(0);
}
