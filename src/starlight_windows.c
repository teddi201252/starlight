#include "starlight_internal.h"
#include "orbit_window.h"
#include "window.h"

#include <stdlib.h>

static sl_window_t *g_windows = NULL;
static size_t g_window_count = 0;
static size_t g_focused_window = (size_t)-1;

static void sl_window_refresh_zorder(void);

static int sl_window_contains_point(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win) return 0;
    if (x < win->x || y < win->y) return 0;
    if (x >= (win->x + win->width)) return 0;
    if (y >= (win->y + win->height)) return 0;
    return 1;
}

static int sl_window_close_btn_hit(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win) return 0;
    if (!sl_window_contains_point(win, x, y)) return 0;
    if (y >= win->y + win->top_bar_height) return 0;
    if (win->width <= (SL_WIN_CLOSE_BTN_W + SL_WIN_CLOSE_BTN_MARGIN + 2u)) return 0;
    if (win->top_bar_height <= (SL_WIN_CLOSE_BTN_H + 2u)) return 0;

    uint32_t cb_x = win->x + win->width - SL_WIN_CLOSE_BTN_MARGIN - SL_WIN_CLOSE_BTN_W - 1u;
    uint32_t cb_y = win->y + 1u + ((win->top_bar_height - 2u - SL_WIN_CLOSE_BTN_H) / 2u);
    if (x < cb_x || y < cb_y) return 0;
    if (x >= cb_x + SL_WIN_CLOSE_BTN_W) return 0;
    if (y >= cb_y + SL_WIN_CLOSE_BTN_H) return 0;
    return 1;
}

static void sl_window_close_index(size_t index) {
    if (!g_windows || index >= g_window_count) return;

    if (g_windows[index].task && g_windows[index].task->on_close) {
        g_windows[index].task->on_close(g_windows[index].task->userdata);
    }
    sl_window_shutdown(&g_windows[index]);

    for (size_t i = index + 1u; i < g_window_count; i++) {
        g_windows[i - 1u] = g_windows[i];
    }

    g_window_count--;
    if (g_window_count == 0u) {
        free(g_windows);
        g_windows = NULL;
        g_focused_window = (size_t)-1;
        return;
    }

    if (g_focused_window >= g_window_count) {
        g_focused_window = g_window_count - 1u;
    }
    sl_window_refresh_zorder();
}

static void sl_window_refresh_zorder(void) {
    uint64_t now = peos_time_ns();
    for (size_t i = 0; i < g_window_count; i++) {
        uint8_t was_focused = g_windows[i].focused;
        g_windows[i].z_index = (uint32_t)i;
        g_windows[i].focused = (i == g_focused_window) ? 1u : 0u;
        if (g_windows[i].focused != was_focused) {
            g_windows[i].force_redraw = 1u;
            g_windows[i].next_render_ns = 0ull;
            if (g_windows[i].focused) {
                sl_window_mark_activity(&g_windows[i], now);
            }
        }
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

int sl_windows_close_at_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_windows || g_window_count == 0u) return 0;
    for (size_t i = g_window_count; i > 0u; i--) {
        size_t idx = i - 1u;
        if (!sl_window_close_btn_hit(&g_windows[idx], x, y)) continue;
        sl_window_close_index(idx);
        sl_dirty_mark_full();
        return 1;
    }
    return 0;
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

    const size_t orbit_count = 4u;
    const uint32_t margin = 20u;
    const uint32_t gap_x = 24u;
    const uint32_t gap_y = 24u;

    uint32_t avail_w = (ctx->fb.width > (margin * 2u + gap_x)) ? (ctx->fb.width - (margin * 2u + gap_x)) : ctx->fb.width;
    uint32_t avail_h = (ctx->fb.height > (margin * 2u + gap_y)) ? (ctx->fb.height - (margin * 2u + gap_y)) : ctx->fb.height;
    uint32_t cell_w = avail_w / 2u;
    uint32_t cell_h = avail_h / 2u;
    if (cell_w < 280u) cell_w = (ctx->fb.width > 40u) ? ((ctx->fb.width - 40u) / 2u) : ctx->fb.width;
    if (cell_h < 180u) cell_h = (ctx->fb.height > 40u) ? ((ctx->fb.height - 40u) / 2u) : ctx->fb.height;

    sl_window_t wins[orbit_count];
    for (size_t i = 0u; i < orbit_count; i++) {
        uint32_t col = (uint32_t)(i & 1u);
        uint32_t row = (uint32_t)(i >> 1u);
        uint32_t x = margin + col * (cell_w + gap_x);
        uint32_t y = margin + row * (cell_h + gap_y);

        if (x + cell_w > ctx->fb.width) x = (ctx->fb.width > cell_w) ? (ctx->fb.width - cell_w) : 0u;
        if (y + cell_h > ctx->fb.height) y = (ctx->fb.height > cell_h) ? (ctx->fb.height - cell_h) : 0u;

        sl_window_init(&wins[i], ctx, sl_orbit_app_instance_at(i), x, y, cell_w, cell_h);
        sl_window_set_fps_profile(&wins[i], 30u, 8u);
    }

    sl_window_t *new_windows =
        (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + orbit_count));
    if (!new_windows) return;

    g_windows = new_windows;
    for (size_t i = 0u; i < orbit_count; i++) {
        g_windows[g_window_count + i] = wins[i];
    }
    g_window_count += orbit_count;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();
}

void sl_windows_mark_app_dirty(const sl_ctx_t *ctx, const void *userdata) {
    if (!ctx || !userdata || !g_windows || g_window_count == 0u) return;
    uint64_t now = peos_time_ns();
    for (size_t i = 0; i < g_window_count; i++) {
        if (!g_windows[i].task) continue;
        if (g_windows[i].task->userdata != userdata) continue;
        sl_window_mark_activity(&g_windows[i], now);
        sl_dirty_add(ctx, g_windows[i].x, g_windows[i].y, g_windows[i].width, g_windows[i].height);
    }
}

void sl_windows_mark_focused_activity(void) {
    sl_window_t *focused = sl_window_get_focused();
    if (!focused) return;
    sl_window_mark_activity(focused, peos_time_ns());
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