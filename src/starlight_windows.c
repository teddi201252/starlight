#include "starlight_internal.h"
#include "app_host.h"
#include "sdk_demo_app.h"
#include "window.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SL_APP_DIR "/system/apps"
#define SL_APP_MANIFEST "app.ini"

typedef struct {
    char title[64];
    char exec[160];
    char args[160];
    uint32_t width;
    uint32_t height;
} sl_app_bundle_t;

static sl_window_t *g_windows = NULL;
static size_t g_window_count = 0;
static size_t g_focused_window = (size_t)-1;

static void sl_window_refresh_zorder(void);

static int sl_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *sl_trim(char *s) {
    if (!s) return s;
    while (*s && sl_is_space(*s)) s++;
    size_t n = strlen(s);
    while (n > 0u && sl_is_space(s[n - 1u])) {
        s[n - 1u] = '\0';
        n--;
    }
    return s;
}

static void sl_copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    size_t i = 0u;
    while (i + 1u < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int sl_parse_u32(const char *s, uint32_t *out) {
    if (!s || !out) return 0;
    uint64_t v = 0u;
    size_t i = 0u;
    if (s[0] == '\0') return 0;
    while (s[i]) {
        char c = s[i++];
        if (c < '0' || c > '9') return 0;
        v = v * 10u + (uint64_t)(c - '0');
        if (v > 0xFFFFFFFFu) return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int sl_has_suffix(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    if (n < m) return 0;
    return (memcmp(s + (n - m), suffix, m) == 0) ? 1 : 0;
}

static size_t sl_trim_trailing_slash(char *s) {
    if (!s) return 0u;
    size_t n = strlen(s);
    while (n > 0u && s[n - 1u] == '/') {
        s[n - 1u] = '\0';
        n--;
    }
    return n;
}

static int sl_join_path(char *out, size_t out_cap, const char *base, const char *name) {
    if (!out || out_cap == 0u || !base || !name) return 0;
    size_t w = 0u;
    while (base[w] && w + 1u < out_cap) {
        out[w] = base[w];
        w++;
    }
    if (base[w] != '\0') return 0;
    if (w == 0u || out[w - 1u] != '/') {
        if (w + 1u >= out_cap) return 0;
        out[w++] = '/';
    }
    size_t i = 0u;
    while (name[i] && w + 1u < out_cap) {
        out[w++] = name[i++];
    }
    if (name[i] != '\0') return 0;
    out[w] = '\0';
    return 1;
}

static int sl_resolve_exec_path(const char *bundle_dir, const char *exec_val,
                                char *out, size_t out_cap) {
    if (!bundle_dir || !exec_val || !out || out_cap == 0u) return 0;
    const char *val = exec_val;
    while (*val == ' ') val++;
    if (*val == '\0') return 0;

    if (val[0] == '/') {
        sl_copy_str(out, out_cap, val);
        return out[0] != '\0';
    }

    if (val[0] == '.' && val[1] == '/') val += 2;
    return sl_join_path(out, out_cap, bundle_dir, val);
}

static int sl_load_app_bundle(const char *manifest_path, const char *bundle_dir, sl_app_bundle_t *out) {
    if (!manifest_path || !out) return 0;
    FILE *f = fopen(manifest_path, "r");
    if (!f) return 0;

    peos_memset(out, 0, sizeof(*out));
    sl_copy_str(out->title, sizeof(out->title), "App");

    char raw_exec[160];
    raw_exec[0] = '\0';

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = sl_trim(line);
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = sl_trim(p);
        char *val = sl_trim(eq + 1);

        if (strcmp(key, "title") == 0) {
            sl_copy_str(out->title, sizeof(out->title), val);
        } else if (strcmp(key, "exec") == 0) {
            sl_copy_str(raw_exec, sizeof(raw_exec), val);
        } else if (strcmp(key, "args") == 0) {
            sl_copy_str(out->args, sizeof(out->args), val);
        } else if (strcmp(key, "width") == 0) {
            (void)sl_parse_u32(val, &out->width);
        } else if (strcmp(key, "height") == 0) {
            (void)sl_parse_u32(val, &out->height);
        }
    }

    fclose(f);
    if (raw_exec[0] == '\0') return 0;
    if (bundle_dir && bundle_dir[0]) {
        if (!sl_resolve_exec_path(bundle_dir, raw_exec, out->exec, sizeof(out->exec))) {
            return 0;
        }
    } else {
        sl_copy_str(out->exec, sizeof(out->exec), raw_exec);
    }
    return (out->exec[0] != '\0') ? 1 : 0;
}

static size_t sl_load_apps_from_system(void) {
    sl_app_host_slots_clear();

    char list[4096];
    uint64_t n = peos_readdir(SL_APP_DIR, sizeof(SL_APP_DIR) - 1u, list, sizeof(list) - 1u);
    if ((int64_t)n <= 0) return 0u;
    list[n] = '\0';

    size_t slot = 0u;
    size_t i = 0u;
    while (i < (size_t)n) {
        size_t start = i;
        while (i < (size_t)n && list[i] != '\n') i++;
        size_t len = i - start;
        if (i < (size_t)n && list[i] == '\n') i++;
        if (len == 0u) continue;

        char name[128];
        if (len >= sizeof(name)) continue;
        peos_memcpy(name, &list[start], len);
        name[len] = '\0';
        size_t name_len = sl_trim_trailing_slash(name);
        if (name_len == 0u) continue;

        if (!sl_has_suffix(name, ".app")) continue;
        if (slot >= 8u) break;

        char bundle_path[256];
        if (!sl_join_path(bundle_path, sizeof(bundle_path), SL_APP_DIR, name)) continue;

        char manifest_path[320];
        if (!sl_join_path(manifest_path, sizeof(manifest_path), bundle_path, SL_APP_MANIFEST)) {
            continue;
        }

        sl_app_bundle_t app;
        if (!sl_load_app_bundle(manifest_path, bundle_path, &app)) {
            if (!sl_load_app_bundle(bundle_path, NULL, &app)) continue;
        }
        if (!sl_app_host_slot_configure(slot, app.title, app.exec, app.args)) continue;
        slot++;
    }

    return slot;
}

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

    size_t loaded = sl_load_apps_from_system();
    if (loaded == 0u) {
        sl_app_host_slots_use_default();
        loaded = sl_app_host_slot_count_enabled();
    }

    sl_app_host_reset();

    size_t app_count = loaded;
    if (app_count == 0u) app_count = 1u;

    uint32_t cols = 1u;
    if (app_count >= 2u) cols = 2u;
    if (app_count >= 5u) cols = 3u;
    uint32_t rows = (uint32_t)((app_count + (size_t)cols - 1u) / (size_t)cols);

    uint32_t margin = 20u;
    uint32_t gap = 20u;
    uint32_t avail_w = (ctx->fb.width > (margin * 2u + gap * (cols - 1u)))
                       ? (ctx->fb.width - (margin * 2u + gap * (cols - 1u)))
                       : ctx->fb.width;
    uint32_t avail_h = (ctx->fb.height > (margin * 2u + gap * (rows - 1u)))
                       ? (ctx->fb.height - (margin * 2u + gap * (rows - 1u)))
                       : ctx->fb.height;

    uint32_t cell_w = (cols > 0u) ? (avail_w / cols) : avail_w;
    uint32_t cell_h = (rows > 0u) ? (avail_h / rows) : avail_h;
    if (cell_w < 320u) cell_w = (ctx->fb.width > 20u) ? (ctx->fb.width - 20u) : ctx->fb.width;
    if (cell_h < 220u) cell_h = (ctx->fb.height > 20u) ? (ctx->fb.height - 20u) : ctx->fb.height;

    sl_window_t wins[app_count];
    size_t win_count = 0u;
    for (size_t i = 0u; i < app_count; i++) {
        uint32_t col = (uint32_t)(i % cols);
        uint32_t row = (uint32_t)(i / cols);
        uint32_t x = margin + col * (cell_w + gap);
        uint32_t y = margin + row * (cell_h + gap);

        if (x + cell_w > ctx->fb.width) x = (ctx->fb.width > cell_w) ? (ctx->fb.width - cell_w) : 0u;
        if (y + cell_h > ctx->fb.height) y = (ctx->fb.height > cell_h) ? (ctx->fb.height - cell_h) : 0u;

        app_instance_t *app = sl_app_host_instance_at(i);
        if (!app) continue;
        sl_window_init(&wins[win_count], ctx, app, x, y, cell_w, cell_h);
        sl_window_set_fps_profile(&wins[win_count], 30u, 8u);
        win_count++;
    }

    if (win_count == 0u) return;

    sl_window_t *new_windows =
        (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + win_count));
    if (!new_windows) return;

    g_windows = new_windows;
    for (size_t i = 0u; i < win_count; i++) {
        g_windows[g_window_count + i] = wins[i];
    }
    g_window_count += win_count;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();

#ifndef SL_ENABLE_SDK_DEMO_WINDOW
#define SL_ENABLE_SDK_DEMO_WINDOW 1
#endif
#if SL_ENABLE_SDK_DEMO_WINDOW
    {
        sl_window_t demo;
        uint32_t dw = (ctx->fb.width > 460u) ? 440u : ctx->fb.width;
        uint32_t dh = (ctx->fb.height > 320u) ? 300u : ctx->fb.height;
        uint32_t dx = (ctx->fb.width > dw) ? ((ctx->fb.width - dw) / 2u) : 0u;
        uint32_t dy = (ctx->fb.height > dh) ? ((ctx->fb.height - dh) / 2u) : 0u;

        sl_window_init(&demo, ctx, sl_sdk_demo_app_instance(), dx, dy, dw, dh);
        sl_window_set_fps_profile(&demo, 30u, 12u);

        sl_window_t *nw =
            (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + 1u));
        if (nw) {
            g_windows = nw;
            g_windows[g_window_count] = demo;
            g_window_count++;
            g_focused_window = g_window_count - 1u;
            sl_window_refresh_zorder();
        }
    }
#endif
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