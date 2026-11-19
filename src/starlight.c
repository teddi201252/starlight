#include "starlight_internal.h"
#include "app_host.h"
#include "perf_stats.h"
#include "desktop.h"
#include "notify.h"
#include "stick.h"
#include "music.h"
#include "sysmenu.h"
#include "render/pixels.h"
#include "render/blit.h"
#include "ui_sdk.h"
#include "file_drag.h"
#include "energy.h"
#include "starlight_input.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
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

// --idle-check (make test-starlight-idle): every 10 s, how much of a CPU
// Starlight took (its threads too, the kernel's count) and how many frames
// it made. Read once the desktop is still: what it costs doing nothing.
#define SL_IDLE_CHECK_NS 10000000000ull
static uint64_t g_idle_work_ns, g_idle_work_max_ns;   // the frames' own work, the waits left out
static void sl_idle_check_tick(uint64_t now) {
    static uint64_t since, frames, busy0, idle0;
    static struct { uint32_t pid; uint64_t cycles; } prev[PEOS_PROC_MAX];
    static uint32_t nprev;
    frames++;
    if (since && now - since < SL_IDLE_CHECK_NS) return;
    peos_proc_info_t procs[PEOS_PROC_MAX];
    int64_t n = peos_proc_list(procs, PEOS_PROC_MAX);
    if (n < 0) n = 0;
    uint64_t me = peos_getpid();
    peos_sysinfo_t si;
    memset(&si, 0, sizeof(si));
    int have_si = peos_sysinfo(&si) >= 0 && si.tsc_hz;
    if (since && have_si) {
        uint64_t ns = now - since;
        uint64_t mhz = si.tsc_hz / 1000000u ? si.tsc_hz / 1000000u : 1u;
        uint64_t us = ns / 1000u ? ns / 1000u : 1u;
        // Each process's share of one CPU in the window; Starlight's, and the
        // two that took most besides it (to see where the rest goes).
        uint32_t mine = 0u, threads = 0u, top_pm[2] = { 0u, 0u };
        const char *top_name[2] = { "-", "-" };
        for (int64_t i = 0; i < n; i++) {
            uint64_t before = procs[i].cycles;
            for (uint32_t k = 0u; k < nprev; k++) {
                if (prev[k].pid == procs[i].pid) before = prev[k].cycles;
            }
            uint32_t pm = (uint32_t)((procs[i].cycles - before) / mhz * 1000u / us);
            if (procs[i].pid == me) {
                mine = pm;
                threads = procs[i].threads;
            } else if (pm > top_pm[0]) {
                top_pm[1] = top_pm[0], top_name[1] = top_name[0];
                top_pm[0] = pm, top_name[0] = procs[i].name;
            } else if (pm > top_pm[1]) {
                top_pm[1] = pm, top_name[1] = procs[i].name;
            }
        }
        uint64_t db = si.cpu_busy_cycles - busy0, di = si.cpu_idle_cycles - idle0;
        uint32_t sys = (db + di) ? (uint32_t)(db * 1000u / (db + di)) : 0u;
        char why[256];
        sl_wake_report(why, sizeof(why));
        printf("starlight: idle check: %u.%u%% of a CPU, %u frames in %u s (woken by: %s; %u threads; all CPUs "
               "%u.%u%% busy; then %s %u.%u%%, %s %u.%u%%; frames' work %u ms, longest %u ms)\n",
               (unsigned)(mine / 10u), (unsigned)(mine % 10u), (unsigned)frames, (unsigned)(ns / 1000000000ull), why,
               (unsigned)threads, (unsigned)(sys / 10u), (unsigned)(sys % 10u), top_name[0],
               (unsigned)(top_pm[0] / 10u), (unsigned)(top_pm[0] % 10u), top_name[1], (unsigned)(top_pm[1] / 10u),
               (unsigned)(top_pm[1] % 10u), (unsigned)(g_idle_work_ns / 1000000u),
               (unsigned)(g_idle_work_max_ns / 1000000u));
    }
    if (have_si) {
        busy0 = si.cpu_busy_cycles;
        idle0 = si.cpu_idle_cycles;
    }
    nprev = 0u;
    for (int64_t i = 0; i < n && nprev < PEOS_PROC_MAX; i++) {
        prev[nprev].pid = procs[i].pid;
        prev[nprev].cycles = procs[i].cycles;
        nprev++;
    }
    since = now;
    frames = 0u;
    g_idle_work_ns = g_idle_work_max_ns = 0u;
}

// The layers, from the bottom up, each drawing only in the damage; the
// cursor goes on top of them all (release 0.12.0 plan, phase 2). Windows
// never reach under the top bar, so it stays over them from the bottom.
typedef struct {
    void (*render)(sl_ctx_t *ctx);
    void (*account)(uint64_t elapsed_ns);   // where its time goes, or NULL
} sl_layer_t;

static const sl_layer_t k_layers[] = {
    { sl_render_dirty_background, sl_perf_add_composite },   // wallpaper, widgets, top bar
    { sl_windows_render_all, sl_perf_add_windows },
    { sl_launcher_render, sl_perf_add_dock },                // dock and app manager
    { sl_notify_render, NULL },
    { sl_music_render, NULL },                               // the mini player, the toast
    { sl_desk_pill_render, NULL },                           // the volume or light pill
    { sl_dock_label_render, sl_perf_add_dock },              // the hovered tile's name
    { sl_sysmenu_render, NULL },                             // sun menu, veil and dialogs
    { sl_clipboard_render_overlay, NULL },
    { sl_file_drag_render, NULL },                           // files dragged between apps
    { sl_perf_card_render, NULL },
    { sl_energy_render, NULL },                              // the screen dark: black over all
};

// Composes every layer; timed, the time of each is counted. Returns when the
// background was done (the rest is "render" in the numbers).
static uint64_t sl_compose_layers(sl_ctx_t *ctx, int timed) {
    uint64_t t = timed ? peos_time_ns() : 0ull, after_bg = t;
    for (size_t i = 0u; i < sizeof(k_layers) / sizeof(k_layers[0]); i++) {
        k_layers[i].render(ctx);
        if (!timed) continue;
        uint64_t now = peos_time_ns();
        if (k_layers[i].account) k_layers[i].account(now - t);
        if (i == 0u) after_bg = now;
        t = now;
    }
    return after_bg;
}

static void sl_compose(sl_ctx_t *ctx) {
    (void)sl_compose_layers(ctx, 0);
}

// Entry point of the Starlight desktop loop.
int main(int argc, char **argv) {
    sl_ctx_t ctx;
    uint8_t idle_check = 0u;
    if (!sl_init(&ctx)) {
        peos_puts("starlight: init failed\n");
        peos_exit(1);
    }
    // `starlight --perf` turns the [GFXPERF] log on without touching settings.cfg.
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--perf") == 0) ctx.graphics_perf_enabled = 1u;
        if (argv[i] && strcmp(argv[i], "--check-damage") == 0) ctx.check_damage = 1u;
        // --idle-check=50: the same, the idle wait at most 50 ms (the fixed
        // pace Starlight had before, to compare with).
        if (argv[i] && strncmp(argv[i], "--idle-check", 12) == 0) {
            idle_check = 1u;
            if (argv[i][12] == '=') sl_wake_set_idle_max_ms((uint32_t)atoi(argv[i] + 13));
        }
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
    sl_skin_colors_apply(&ctx);   // the theme's colours over it (docs/skin-colors-plan.md)
    sl_cursor_init(&ctx);   // again, now that the skin (and its cursors) is there
    sl_audio_init(ctx.start_sound);
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
    {   // the first start of an installed PeOS: Welcome, once (it deletes the file)
        FILE *first = fopen(SL_FIRSTBOOT_FILE, "r");
        peos_stat_t st;
        if (first) {
            fclose(first);
            if ((int64_t)peos_stat(SL_WELCOME_APP, sizeof(SL_WELCOME_APP) - 1u, &st, sizeof(st)) >= 0 &&
                sl_windows_open_app_bundle_path(&ctx, SL_WELCOME_APP)) {
                printf("starlight: first start: %s\n", SL_WELCOME_APP);
            }
        }
    }
    sl_notify_init(&ctx);
    sl_music_init(&ctx);
    sl_tween_init();

    uint64_t next_frame_ns = 0;
    uint64_t tween_last_ns = peos_time_ns();
    uint8_t app_prompt_blink_prev = 0u;

    for (;;) {
        uint64_t frame_start_ns = peos_time_ns();
        sl_frame_time_set(frame_start_ns);
        sl_wake_frame_begin();
        if (idle_check) sl_idle_check_tick(frame_start_ns);
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
                // The prompt, or a field's caret, blinks in the body.
                if (focused && focused->task && sl_app_host_mark_field(focused->task->userdata, NULL)) {
                    sl_windows_mark_app_pixels(&ctx, focused->task->userdata);   // a field's caret: that field
                } else if (focused && (!focused->task || !focused->task->userdata ||
                                       sl_app_host_has_caret(focused->task->userdata))) {
                    sl_window_dirty_body(&ctx, focused);
                }
            }
        }

        {
            uint64_t d0 = peos_time_ns();
            sl_desk_tick(&ctx);
            sl_perf_add_desk(peos_time_ns() - d0);
        }
        sl_skin_colors_tick(&ctx);   // a skin baked on its thread, when it is ready
        sl_notify_tick(&ctx);
        sl_stick_tick(&ctx);   // a stick mounted: Curiosity on it, if asked
        sl_music_tick(&ctx);
        sl_sysmenu_tick(&ctx);   // power off at the end of an installation
        sl_energy_tick(&ctx);    // the screen dark after a while, the battery
        sl_fps_tick(&ctx);

        // What the frames to come wait for, when nothing moves.
        if (sl_tween_any_active()) sl_wake_at(frame_start_ns + SL_WAKE_FRAME_NS);
        {
            sl_window_t *focused = sl_window_get_focused();
            if (focused && !focused->minimized &&
                (!focused->task || !focused->task->userdata || sl_app_host_blinks(focused->task->userdata))) {
                sl_wake_at((frame_start_ns / 500000000ull + 1ull) * 500000000ull);   // the caret's next blink
            }
        }
        if (ctx.show_perf) sl_wake_at(ctx.fps_last_ns + 1000000000ull);   // its numbers, once a second
        sl_input_wake();
        sl_app_host_wake();
        sl_clipboard_wake();
        sl_desk_wake(&ctx);
        sl_notify_wake();
        sl_stick_wake();
        sl_music_wake();
        sl_windows_wake();
        sl_sysmenu_wake();
        sl_energy_wake();

        // The damage is complete here: from now on, nothing adds to it until
        // the cursor (which restores what it covers next frame). Every layer
        // draws only in it, so the frame stays whole outside it.
        int windows_waiting = sl_windows_prepare_render(&ctx);

        uint64_t render_start_ns = sl_compose_layers(&ctx, 1);
        if (ctx.check_damage) (void)sl_dirty_check(&ctx, sl_compose);
        sl_fps_account(&ctx, frame_start_ns);
        if (idle_check) {
            uint64_t work = peos_time_ns() - frame_start_ns;
            g_idle_work_ns += work;
            if (work > g_idle_work_max_ns) g_idle_work_max_ns = work;
        }

        int frame_active = (sl_dirty_request_count() != dirty_mark) || windows_waiting ||
                           ctx.cursor_x != old_x || ctx.cursor_y != old_y;
        if (sl_energy_screen_off()) {
            ctx.cursor_saved_valid = 0u;   // dark: no cursor either
        } else if (!ctx.hw_cursor) {
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
