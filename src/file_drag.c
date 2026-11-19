// Files dragged between apps: see file_drag.h.
#include "file_drag.h"

#include "app_host.h"
#include "i18n.h"
#include "render/blit.h"
#include "render/pixels.h"
#include "ui_sdk.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

#define SL_DRAG_PATH_MAX 256u
#define SL_DRAG_PILL_H 34u
#define SL_DRAG_PILL_MAX_W 320u
#define SL_DRAG_OFFSET_X 18
#define SL_DRAG_OFFSET_Y 14

static struct {
    uint8_t active;
    const void *source;
    uint32_t count;
    char paths[SL_FILE_DRAG_MAX][SL_DRAG_PATH_MAX];
    char label[96];
    uint32_t label_w;
    uint8_t over_target;            // over a window that takes them
    uint8_t shown;                  // the pill is on the screen at x,y w x h
    uint32_t x, y, w, h;
} g_drag;

static const slsk_font_t *g_drag_font;
static sl_ctx_t *g_drag_ctx;   // the desktop's, from the input of every frame

static uint8_t sl_drag_buttons_left(void);

uint8_t sl_file_drag_active(void) { return g_drag.active; }

static const slsk_font_t *sl_drag_font(sl_ctx_t *ctx) {
    if (!g_drag_font && ctx->skin_loaded) {
        const slsk_entry_t *e = sl_skin_find(&ctx->skin, "ui-14");
        if (e) g_drag_font = sl_skin_font(&ctx->skin, e);
    }
    return g_drag_font;
}

static void sl_drag_pill_dirty(sl_ctx_t *ctx) {
    if (!g_drag.shown) return;
    uint32_t m = 12u, l = g_drag.x > m ? m : g_drag.x, t = g_drag.y > m ? m : g_drag.y;
    sl_dirty_add(ctx, g_drag.x - l, g_drag.y - t, g_drag.w + l + m, g_drag.h + t + m);
}

// The name of the one file, or "3 file", cut to fit.
static void sl_drag_make_label(sl_ctx_t *ctx) {
    if (g_drag.count == 1u) {
        const char *base = strrchr(g_drag.paths[0], '/');
        base = (base && base[1]) ? base + 1 : g_drag.paths[0];
        snprintf(g_drag.label, sizeof(g_drag.label), "%s", base);
    } else {
        snprintf(g_drag.label, sizeof(g_drag.label), sl_tr("drag.files"), (unsigned)g_drag.count);
    }
    const slsk_font_t *f = sl_drag_font(ctx);
    g_drag.label_w = f ? sl_text_width(f, g_drag.label) : (uint32_t)strlen(g_drag.label) * 8u;
    // Too long: cut with an ellipsis (UTF-8 continuation bytes are not cut in half).
    size_t n = strlen(g_drag.label);
    while (f && g_drag.label_w > SL_DRAG_PILL_MAX_W - 48u && n > 4u) {
        n--;
        while (n > 1u && ((unsigned char)g_drag.label[n] & 0xC0u) == 0x80u) n--;
        memcpy(g_drag.label + n, "\xe2\x80\xa6", 4u);
        g_drag.label_w = sl_text_width(f, g_drag.label);
    }
}

static void sl_drag_place(sl_ctx_t *ctx) {
    sl_drag_pill_dirty(ctx);
    g_drag.w = g_drag.label_w + 44u;
    g_drag.h = SL_DRAG_PILL_H;
    int64_t x = (int64_t)ctx->cursor_x + SL_DRAG_OFFSET_X, y = (int64_t)ctx->cursor_y + SL_DRAG_OFFSET_Y;
    if (x + g_drag.w > ctx->fb.width) x = (int64_t)ctx->fb.width - g_drag.w;
    if (y + g_drag.h > ctx->fb.height) y = (int64_t)ctx->cursor_y - SL_DRAG_OFFSET_Y - (int64_t)g_drag.h;
    g_drag.x = x < 0 ? 0u : (uint32_t)x;
    g_drag.y = y < 0 ? 0u : (uint32_t)y;
    g_drag.shown = 1u;
    sl_drag_pill_dirty(ctx);
}

// The window under the pointer, when it is another app that can take files.
static sl_window_t *sl_drag_target(sl_ctx_t *ctx) {
    sl_window_t *win = sl_windows_top_at(ctx->cursor_x, ctx->cursor_y);
    if (!win || !win->task || !win->task->userdata || win->task->userdata == g_drag.source) return NULL;
    if (!sl_app_host_takes_files(win->task->userdata)) return NULL;
    return win;
}

static void sl_drag_end(sl_ctx_t *ctx) {
    sl_drag_pill_dirty(ctx);
    memset(&g_drag, 0, sizeof(g_drag));
}

void sl_file_drag_add(const void *source, const char *path) {
    sl_ctx_t *ctx = g_drag_ctx;
    if (!ctx || !source || !path || path[0] != '/') return;
    if (!sl_drag_buttons_left()) return;   // let go already: no drag
    if (!g_drag.active || g_drag.source != source) {
        if (g_drag.active) sl_drag_end(ctx);
        g_drag.active = 1u;
        g_drag.source = source;
        g_drag.count = 0u;
    }
    if (g_drag.count >= SL_FILE_DRAG_MAX) return;
    for (uint32_t i = 0u; i < g_drag.count; i++) {   // the app said it again (its drag goes on): once
        if (strcmp(g_drag.paths[i], path) == 0) return;
    }
    snprintf(g_drag.paths[g_drag.count], SL_DRAG_PATH_MAX, "%s", path);
    g_drag.count++;
    printf("starlight: drag: %s (%u)\n", path, (unsigned)g_drag.count);
    sl_drag_make_label(ctx);
    sl_drag_place(ctx);
}

static uint8_t g_left_held;

static uint8_t sl_drag_buttons_left(void) { return g_left_held; }

int sl_file_drag_input(sl_ctx_t *ctx, sl_input_frame_t *input) {
    if (!ctx || !input) return 0;
    g_drag_ctx = ctx;
    g_left_held = (input->mouse_buttons & 0x01u) ? 1u : 0u;
    if (!g_drag.active) return 0;
    for (uint8_t i = 0u; i < input->key_event_count; i++) {   // Esc: nothing is dropped
        if (input->key_events[i].pressed && input->key_events[i].code == PEOS_KEY_ESC) {
            printf("starlight: drag: cancelled\n");
            sl_drag_end(ctx);
            input->key_events[i] = input->key_events[--input->key_event_count];
            return 1;
        }
    }
    if (input->mouse_left_released || !g_left_held) {
        sl_window_t *win = sl_drag_target(ctx);
        if (!win) {
            printf("starlight: drag: let go over nothing that takes files\n");
        } else {
            uint32_t bx = 0u, by = 0u, bw = 0u, bh = 0u;
            sl_window_body_rect(win, &bx, &by, &bw, &bh);
            int64_t rx = (int64_t)ctx->cursor_x - bx, ry = (int64_t)ctx->cursor_y - by;
            uint32_t x = rx < 0 ? 0u : (uint32_t)rx, y = ry < 0 ? 0u : (uint32_t)ry;
            for (uint32_t i = 0u; i < g_drag.count; i++) {
                sl_app_host_drop_file(win->task->userdata, i, g_drag.count, x, y, g_drag.paths[i]);
            }
            printf("starlight: drag: %u file(s) dropped on %s at %u,%u\n", (unsigned)g_drag.count, win->title,
                   (unsigned)x, (unsigned)y);
        }
        sl_drag_end(ctx);
        return 0;
    }
    if (input->mouse_moved) {
        uint8_t over = sl_drag_target(ctx) ? 1u : 0u;
        if (over != g_drag.over_target) g_drag.over_target = over;
        sl_drag_place(ctx);
    }
    return 0;
}

static void sl_drag_draw_rect(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    const slsk_font_t *f = sl_drag_font(ctx);
    sl_clip_set(ctx, dx, dy, dw, dh);
    uint32_t x = g_drag.x, y = g_drag.y, w = g_drag.w, h = g_drag.h;
    // Over a window that takes them: the neon edge says "here".
    if (g_drag.over_target) sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, x - 1u, y - 1u, w + 2u, h + 2u, (h + 2u) / 2u);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, x, y, w, h, h / 2u);
    // A small page: the file.
    sl_ui_ctx_surface(ctx, g_drag.over_target ? SL_UI_SURF_NEON_DOT : SL_UI_SURF_RAISED, x + 12u, y + 9u, 12u, 16u, 2u);
    if (f) {
        sl_target_t t = sl_target_from_ctx(ctx);
        sl_text_draw(&t, f, (int32_t)(x + 32u), (int32_t)(y + 22u), g_drag.label, 0xE8u, 0xF4u, 0xFFu);
    }
    sl_clip_clear(ctx);
}

void sl_file_drag_render(sl_ctx_t *ctx) {
    if (!ctx || !g_drag.active || !g_drag.shown) return;
    uint32_t x = g_drag.x ? g_drag.x - 1u : 0u, y = g_drag.y ? g_drag.y - 1u : 0u;   // with the neon edge
    sl_dirty_for_each_intersection(x, y, g_drag.w + 2u, g_drag.h + 2u, sl_drag_draw_rect, ctx);
}
