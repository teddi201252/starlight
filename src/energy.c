// The screen's light and the battery: see energy.h.
#include "energy.h"

#include "app_host.h"
#include "desktop.h"
#include "i18n.h"
#include "music.h"
#include "notify.h"
#include "render/pixels.h"
#include "window.h"

#include <stdio.h>

#define SL_ENERGY_DIM_PCT 60u

static struct {
    uint64_t last_input_ns;
    uint8_t off;              // the screen is dark
    int64_t off_light;        // the light before it went dark (-1: no light here)
    uint8_t dimmed;           // on the battery, the light lowered
    int64_t dim_light;        // the light before that
    uint8_t warned10, warned5;
    uint64_t deadline;        // when the screen goes dark (0: never)
} g_en = { .off_light = -1, .dim_light = -1 };

uint8_t sl_energy_screen_off(void) { return g_en.off; }

static void sl_energy_screen_on(sl_ctx_t *ctx) {
    g_en.off = 0u;
    if (g_en.off_light >= 0) (void)peos_backlight_set((uint32_t)g_en.off_light);
    if (ctx->hw_cursor) (void)peos_cursor_set_visible(1);
    sl_dirty_mark_full();
    printf("starlight: screen on\n");
}

int sl_energy_input(sl_ctx_t *ctx, const sl_input_frame_t *in) {
    if (!ctx || !in) return 0;
    uint8_t any = in->key_event_count || in->mouse_moved || in->mouse_left_pressed || in->mouse_left_released ||
                  in->mouse_right_pressed || in->mouse_right_released || in->mouse_wheel_x || in->mouse_wheel_y ||
                  in->clipboard_cmds || in->window_cmds;
    if (!any) return 0;
    g_en.last_input_ns = peos_time_ns();
    if (!g_en.off) return 0;
    sl_energy_screen_on(ctx);
    return 1;   // it only lit the screen: the app under it does not get it
}

// The window in front shows its own full surface (a game): the screen stays on.
static uint8_t sl_energy_fullscreen_app(void) {
    sl_window_t *w = sl_window_get_focused();
    return (w && !w->minimized && w->task && w->task->userdata && sl_app_host_fullscreen(w->task->userdata)) ? 1u : 0u;
}

static void sl_energy_battery(sl_ctx_t *ctx, int32_t pct, uint32_t flags) {
    uint8_t present = pct >= 0, on_battery = present && !(flags & PEOS_BATTERY_AC);
    // the light: down on the battery, back on the charger
    if (on_battery && ctx->battery_dim && !g_en.dimmed && !g_en.off) {
        int64_t now = peos_backlight_get();
        if (now >= 0) {
            g_en.dim_light = now;
            (void)peos_backlight_set((uint32_t)(now * SL_ENERGY_DIM_PCT / 100));
            printf("starlight: on the battery: light %d -> %d permille\n", (int)now,
                   (int)(now * SL_ENERGY_DIM_PCT / 100));
        }
        g_en.dimmed = 1u;
    } else if ((!on_battery || !ctx->battery_dim) && g_en.dimmed) {
        if (g_en.dim_light >= 0 && !g_en.off) (void)peos_backlight_set((uint32_t)g_en.dim_light);
        else if (g_en.dim_light >= 0) g_en.off_light = g_en.dim_light;   // given back when the screen lights
        printf("starlight: the light as it was (%d permille)\n", (int)g_en.dim_light);
        g_en.dimmed = 0u;
        g_en.dim_light = -1;
    }
    // low: once at 10%, once at 5%, a discharge
    if (!on_battery) {
        g_en.warned10 = g_en.warned5 = 0u;
        return;
    }
    if (!ctx->battery_warn) return;
    uint8_t *flag = pct <= 5 ? &g_en.warned5 : pct <= 10 ? &g_en.warned10 : NULL;
    if (!flag || *flag) return;
    *flag = 1u;
    if (pct <= 5) g_en.warned10 = 1u;
    char text[160];
    snprintf(text, sizeof(text), sl_tr("energy.battery_low"), (unsigned)pct);
    printf("starlight: battery low, %d%%\n", (int)pct);
    sl_notify_post(ctx, sl_tr("notify.system"), text);
}

void sl_energy_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    if (!g_en.last_input_ns) g_en.last_input_ns = now;   // from the start
    int32_t pct = -1;
    uint32_t flags = 0u;
    sl_desk_battery(&pct, &flags);
    sl_energy_battery(ctx, pct, flags);

    uint8_t on_battery = pct >= 0 && !(flags & PEOS_BATTERY_AC);
    uint32_t minutes = on_battery ? ctx->screen_off_battery_min : ctx->screen_off_ac_min;
    // Music playing, a game in front: the screen stays on (counted from now).
    if (sl_music_playing() || sl_energy_fullscreen_app()) {
        g_en.last_input_ns = now;
        g_en.deadline = 0u;
        return;
    }
    g_en.deadline = minutes ? g_en.last_input_ns + (uint64_t)minutes * 60000000000ull : 0u;
    if (g_en.off || !g_en.deadline || now < g_en.deadline) return;
    g_en.off = 1u;
    g_en.off_light = peos_backlight_get();
    if (g_en.off_light >= 0) (void)peos_backlight_set(0u);
    if (ctx->hw_cursor) (void)peos_cursor_set_visible(0);
    sl_dirty_mark_full();
    printf("starlight: screen off (%u min without input)\n", (unsigned)minutes);
}

void sl_energy_wake(void) {
    // Music or a game: looked at again now and then, the screen's time
    // counting from there.
    if (sl_music_playing() || sl_energy_fullscreen_app()) {
        sl_wake_at(sl_frame_time_ns() + 30000000000ull);
        return;
    }
    if (!g_en.off && g_en.deadline) sl_wake_at(g_en.deadline);
}

static void sl_energy_black(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_fill_rect((sl_ctx_t *)userdata, x, y, w, h, 0u, 0u, 0u);
}

void sl_energy_render(sl_ctx_t *ctx) {
    if (!ctx || !g_en.off) return;
    sl_dirty_for_each_intersection(0u, 0u, ctx->fb.width, ctx->fb.height, sl_energy_black, ctx);
}
