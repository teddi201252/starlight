#include "starlight_internal.h"
#include "orbit_window.h"
#include "window.h"

#include <stdlib.h>

static sl_window_t *g_windows = NULL;
static size_t g_window_count = 0;
static size_t g_focused_window = (size_t)-1;

static int sl_window_contains_point(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win) return 0;
    if (x < win->x || y < win->y) return 0;
    if (x >= (win->x + win->width)) return 0;
    if (y >= (win->y + win->height)) return 0;
    return 1;
}

static void sl_window_refresh_zorder(void) {
    for (size_t i = 0; i < g_window_count; i++) {
        g_windows[i].z_index = (uint32_t)i;
        g_windows[i].focused = (i == g_focused_window) ? 1u : 0u;
    }
}

static void sl_focus_window(size_t index) {
    if (!g_windows || g_window_count == 0u || index >= g_window_count) return;
    if (index == g_window_count - 1u) {
        g_focused_window = index;
        sl_window_refresh_zorder();
        return;
    }

    sl_window_t win = g_windows[index];
    for (size_t i = index; i + 1u < g_window_count; i++) {
        g_windows[i] = g_windows[i + 1u];
    }
    g_windows[g_window_count - 1u] = win;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();
}

void sl_windows_focus_at_cursor(uint32_t x, uint32_t y) {
    if (!g_windows || g_window_count == 0u) return;
    for (size_t i = g_window_count; i > 0u; i--) {
        size_t idx = i - 1u;
        if (sl_window_contains_point(&g_windows[idx], x, y)) {
            sl_focus_window(idx);
            return;
        }
    }
    return;
}

void sl_windows_create_test(sl_ctx_t *ctx, size_t count) {
    if (!ctx || count == 0u) return;

    for (size_t i = 0; i < count; i++) {
        sl_window_t w;
        sl_window_init(&w, ctx, NULL, 50u + (i * 30u), 50u + (i * 30u), 200u, 150u);

        sl_window_t *new_windows =
            (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + 1u));
        if (!new_windows) continue;

        g_windows = new_windows;
        g_windows[g_window_count] = w;
        g_window_count++;
        g_focused_window = g_window_count - 1u;
        sl_window_refresh_zorder();
    }
}

void sl_windows_create_default(sl_ctx_t *ctx) {
    if (!ctx) return;

    sl_orbit_app_reset();

    uint32_t w = 860u;
    uint32_t h = 520u;
    if (ctx->fb.width > 40u && w > ctx->fb.width - 40u) w = ctx->fb.width - 40u;
    if (ctx->fb.height > 40u && h > ctx->fb.height - 40u) h = ctx->fb.height - 40u;
    if (w < 320u) w = (ctx->fb.width > 20u) ? (ctx->fb.width - 20u) : ctx->fb.width;
    if (h < 220u) h = (ctx->fb.height > 20u) ? (ctx->fb.height - 20u) : ctx->fb.height;

    uint32_t x = (ctx->fb.width > w) ? ((ctx->fb.width - w) / 2u) : 0u;
    uint32_t y = (ctx->fb.height > h) ? ((ctx->fb.height - h) / 2u) : 0u;

    sl_window_t win;
    sl_window_init(&win, ctx, sl_orbit_app_instance(), x, y, w, h);

    sl_window_t *new_windows =
        (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + 1u));
    if (!new_windows) return;

    g_windows = new_windows;
    g_windows[g_window_count] = win;
    g_window_count++;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();
}

void sl_windows_render_all(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_window_count == 0u) return;
    for (size_t i = 0; i < g_window_count; i++) {
        if (!sl_dirty_intersects_rect(g_windows[i].x, g_windows[i].y,
                                      g_windows[i].width, g_windows[i].height)) {
            continue;
        }
        sl_window_render(&g_windows[i]);
    }
}

void sl_windows_destroy_all(void) {
    if (!g_windows) return;
    for (size_t i = 0; i < g_window_count; i++) {
        sl_window_shutdown(&g_windows[i]);
    }
    free(g_windows);
    g_windows = NULL;
    g_window_count = 0u;
    g_focused_window = (size_t)-1;
}

void sl_window_move(sl_window_t *win, uint32_t new_x, uint32_t new_y) {
    if (!win) return;
    win->x = new_x;
    win->y = new_y;
}

sl_window_t* sl_window_get_focused(void) {
    if (!g_windows || g_window_count == 0u || g_focused_window >= g_window_count) return NULL;
    return &g_windows[g_focused_window];
}