#include "starlight_internal.h"
#include "perf_stats.h"
#include "desktop.h"
#include "notify.h"
#include "sysmenu.h"
#include "render/pixels.h"
#include "render/blit.h"
#include "ui_sdk.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

// The performance card (settings: show_performance=1), at the bottom right:
// the frames of the last second and the work they took, average and peak.
#define SL_PERF_W 250u
#define SL_PERF_H 72u
#define SL_PERF_MARGIN 40u

static void sl_perf_card_rect(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y) {
    *x = ctx->fb.width > SL_PERF_W + SL_PERF_MARGIN ? ctx->fb.width - SL_PERF_W - SL_PERF_MARGIN : 0u;
    *y = ctx->fb.height > SL_PERF_H + SL_PERF_MARGIN ? ctx->fb.height - SL_PERF_H - SL_PERF_MARGIN : 0u;
}

static void sl_perf_card_dirty(sl_ctx_t *ctx) {
    uint32_t x, y;
    sl_perf_card_rect(ctx, &x, &y);
    sl_dirty_add(ctx, x > 24u ? x - 24u : 0u, y > 24u ? y - 24u : 0u, SL_PERF_W + 48u, SL_PERF_H + 48u);
}

static void sl_perf_card_draw_rect(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    static const slsk_font_t *big, *small;
    if (!big) {
        const slsk_entry_t *b = sl_skin_find(&ctx->skin, "mono-24"), *m = sl_skin_find(&ctx->skin, "ui-14");
        if (!b || !m) return;
        big = sl_skin_font(&ctx->skin, b);
        small = sl_skin_font(&ctx->skin, m);
    }
    uint32_t x, y;
    sl_perf_card_rect(ctx, &x, &y);
    sl_clip_set(ctx, dx, dy, dw, dh);
    sl_target_t t = sl_target_from_ctx(ctx);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT, x, y, SL_PERF_W, SL_PERF_H, SL_PERF_H / 2u);
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    char fps[16], work[64];
    snprintf(fps, sizeof(fps), "%u", (unsigned)ctx->fps_value);
    snprintf(work, sizeof(work), sl_tr("perf.work"),
             (unsigned)(ctx->perf_work_avg_us / 1000u), (unsigned)(ctx->perf_work_avg_us / 100u % 10u),
             (unsigned)(ctx->perf_work_peak_us / 1000u), (unsigned)(ctx->perf_work_peak_us / 100u % 10u));
    int32_t fx = (int32_t)x + 30, base = (int32_t)y + 34;
    int32_t end = sl_text_draw(&t, big, fx, base, fps, n->r, n->g, n->b);
    sl_text_draw(&t, small, end + 8, base, "fps", 0x9F, 0xB4, 0xD8);
    sl_text_draw(&t, small, fx, (int32_t)y + 56, work, 0x9F, 0xB4, 0xD8);
    sl_clip_clear(ctx);
}

// The frame's work, counted once it is done (no drawing, no damage here).
static void sl_fps_account(sl_ctx_t *ctx, uint64_t frame_start_ns) {
    if (!ctx) return;
    ctx->fps_frames++;
    uint64_t work = peos_time_ns() - frame_start_ns;
    ctx->perf_work_ns += work;
    if (work > ctx->perf_work_max_ns) ctx->perf_work_max_ns = work;
}

// Before the frame is composed: new numbers once a second, or the card
// turned on or off, put its place in the damage.
static void sl_fps_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    uint64_t dt = now - ctx->fps_last_ns;
    if (dt >= 1000000000ull) {
        ctx->fps_value = (uint32_t)(((uint64_t)ctx->fps_frames * 1000000000ull) / dt);
        ctx->perf_work_avg_us = (uint32_t)(ctx->perf_work_ns / (ctx->fps_frames ? ctx->fps_frames : 1u) / 1000u);
        ctx->perf_work_peak_us = (uint32_t)(ctx->perf_work_max_ns / 1000u);
        ctx->fps_frames = 0u;
        ctx->perf_work_ns = 0u;
        ctx->perf_work_max_ns = 0u;
        ctx->fps_last_ns = now;
        if (ctx->show_perf) sl_perf_card_dirty(ctx);   // new numbers, once a second
    }
    if (ctx->show_perf != ctx->perf_shown) {   // turned on or off (Impostazioni)
        ctx->perf_shown = ctx->show_perf;
        sl_perf_card_dirty(ctx);
    }
}

// Drawn wherever its place was repainted this frame.
static void sl_perf_card_render(sl_ctx_t *ctx) {
    if (!ctx->show_perf || !ctx->skin_loaded) return;
    uint32_t x, y;
    sl_perf_card_rect(ctx, &x, &y);
    sl_dirty_for_each_intersection(x > 24u ? x - 24u : 0u, y > 24u ? y - 24u : 0u, SL_PERF_W + 48u, SL_PERF_H + 48u,
                                   sl_perf_card_draw_rect, ctx);
}

// Every layer, from the bottom up, each drawing only in the damage.
static void sl_compose(sl_ctx_t *ctx) {
    sl_render_dirty_background(ctx);
    sl_windows_render_all(ctx);
    sl_launcher_render(ctx);
    sl_notify_render(ctx);
    sl_sysmenu_render(ctx);
    sl_clipboard_render_overlay(ctx);
    sl_perf_card_render(ctx);
}

// Entry point of the Starlight desktop loop.
int main(int argc, char **argv) {
    sl_ctx_t ctx;
    if (!sl_init(&ctx)) {
        peos_puts("starlight: init failed\n");
        peos_exit(1);
    }
    // `starlight --perf` turns the [GFXPERF] log on without touching settings.cfg.
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--perf") == 0) ctx.graphics_perf_enabled = 1u;
        if (argv[i] && strcmp(argv[i], "--check-damage") == 0) ctx.check_damage = 1u;
    }
    sl_perf_init(ctx.graphics_perf_enabled);
    {
        uint64_t t0 = peos_time_ns();
        ctx.skin_loaded = (uint8_t)sl_skin_load(&ctx.skin, SL_RESOURCE_SKIN);
        if (ctx.skin_loaded) {
            printf("starlight: skin %u entries, %u KiB, loaded in %u us\n",
                   (unsigned)ctx.skin.count, (unsigned)(ctx.skin.len / 1024u),
                   (unsigned)((peos_time_ns() - t0) / 1000u));
        } else {
            peos_puts("starlight: skin " SL_RESOURCE_SKIN " missing or invalid\n");
        }
    }
    sl_cursor_init(&ctx);   // again, now that the skin (and its cursors) is there
    sl_audio_init();
    peos_puts("starlight: minimal skeleton running\n");
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
        sl_sw_cursor_dirty(&ctx);
    }
    else sl_hw_cursor_sync_pos(&ctx);
    if (!sl_present(&ctx)) {
        sl_audio_shutdown();
        sl_shutdown(&ctx);
        peos_exit(1);
    }

    sl_windows_create_default(&ctx);
    sl_notify_init(&ctx);
    sl_tween_init();

    uint64_t next_frame_ns = 0;
    uint64_t tween_last_ns = peos_time_ns();
    uint8_t app_prompt_blink_prev = 0u;

    for (;;) {
        uint64_t frame_start_ns = peos_time_ns();
        sl_perf_frame_begin(frame_start_ns);
        if (!ctx.hw_cursor) {
            if (ctx.cursor_saved_valid) {
                sl_dirty_add(&ctx, ctx.cursor_saved_x, ctx.cursor_saved_y,
                             ctx.cursor_saved_w, ctx.cursor_saved_h);
            }
            sl_sw_cursor_restore(&ctx);
        }
        // Damage from here to the cursor redraw is real activity; the software
        // cursor's own restore/redraw happens every frame and does not count.
        uint32_t dirty_mark = sl_dirty_request_count();

        uint32_t old_x = ctx.cursor_x;
        uint32_t old_y = ctx.cursor_y;
        if (sl_handle_input(&ctx)) break;
        sl_cursor_update(&ctx);
        if (ctx.cursor_x != old_x || ctx.cursor_y != old_y) {
            if (ctx.hw_cursor) {
                sl_hw_cursor_sync_pos(&ctx);
            }
        }

        {
            uint64_t now_ns = peos_time_ns();
            uint64_t dt_ns = now_ns - tween_last_ns;
            uint32_t dt_ms = (uint32_t)(dt_ns / 1000000ull);
            if (dt_ms > 0u) {
                sl_tween_update(dt_ms);
                tween_last_ns = now_ns;
            }
        }
        sl_windows_tick_animations(&ctx);

        sl_app_host_tick(&ctx);
        sl_clipboard_tick(&ctx);

        {
            uint8_t app_prompt_blink_now = (uint8_t)((peos_time_ns() / 500000000ull) & 1ull);
            if (app_prompt_blink_now != app_prompt_blink_prev) {
                app_prompt_blink_prev = app_prompt_blink_now;
                sl_window_t *focused = sl_window_get_focused();
                if (focused) sl_window_dirty_body(&ctx, focused);   // the prompt blinks in the body
            }
        }

        {
            uint64_t d0 = peos_time_ns();
            sl_desk_tick(&ctx);
            sl_perf_add_desk(peos_time_ns() - d0);
        }
        sl_notify_tick(&ctx);
        sl_fps_tick(&ctx);

        // The damage is complete here: from now on, nothing adds to it until
        // the cursor (which restores what it covers next frame). Every layer
        // draws only in it, so the frame stays whole outside it.
        int windows_waiting = sl_windows_prepare_render(&ctx);

        uint64_t composite_start_ns = peos_time_ns();
        sl_render_dirty_background(&ctx);
        sl_perf_add_composite(peos_time_ns() - composite_start_ns);

        uint64_t render_start_ns = peos_time_ns();
        {
            uint64_t w0 = peos_time_ns();
            sl_windows_render_all(&ctx);
            uint64_t w1 = peos_time_ns();
            sl_launcher_render(&ctx);
            sl_perf_add_windows(w1 - w0);
            sl_perf_add_dock(peos_time_ns() - w1);
        }
        sl_notify_render(&ctx);
        sl_sysmenu_render(&ctx);
        sl_clipboard_render_overlay(&ctx);
        sl_perf_card_render(&ctx);
        if (ctx.check_damage) (void)sl_dirty_check(&ctx, sl_compose);
        sl_fps_account(&ctx, frame_start_ns);

        int frame_active = (sl_dirty_request_count() != dirty_mark) || windows_waiting ||
                           ctx.cursor_x != old_x || ctx.cursor_y != old_y;
        if (!ctx.hw_cursor) {
            sl_sw_cursor_capture(&ctx, ctx.cursor_x, ctx.cursor_y);
            sl_render_cursor(&ctx);
            sl_sw_cursor_dirty(&ctx);
        }
        sl_perf_add_render(peos_time_ns() - render_start_ns);
        if (!sl_present(&ctx)) break;
        sl_perf_frame_work_done(peos_time_ns());

        sl_frame_pace(&next_frame_ns, ctx.input_fd, frame_active);
        sl_perf_frame_end(peos_time_ns());

    }

    sl_tween_reset();
    sl_clipboard_shutdown();
    if (ctx.skin_loaded) sl_skin_free(&ctx.skin);
    sl_desk_free(&ctx);
    free(ctx.bg_frame);
    if (ctx.bg_loaded) {
        sl_image_free(&ctx.bg_image);
    }
    sl_windows_destroy_all();
    sl_audio_shutdown();
    sl_shutdown(&ctx);
    peos_exit(0);
}
