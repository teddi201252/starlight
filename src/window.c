#include "window.h"

#include "starlight_internal.h"
#include "app_host.h"
#include "ui_sdk.h"
#include "perf_stats.h"

#include <stdlib.h>
#include <string.h>

#define SL_FPS_BOOST_FOCUSED 60u
#define SL_FPS_BOOST_UNFOCUSED 24u
#define SL_ACTIVITY_BOOST_NS 300000000ull
#define SL_ACTIVITY_IDLE_NS 2500000000ull

// Colours of the frame: the runtime theme (day or night).
#define SL_WIN_RGB(tok) sl_ui_runtime_color(tok)->r, sl_ui_runtime_color(tok)->g, sl_ui_runtime_color(tok)->b
#define SL_WIN_INK SL_WIN_RGB(SL_UI_COLOR_TEXT_PRIMARY)
#define SL_WIN_MUTED SL_WIN_RGB(SL_UI_COLOR_TEXT_SECONDARY)
#define SL_WIN_DOT_OFF SL_WIN_RGB(SL_UI_COLOR_BORDER)

// Skin entries the frame uses, looked up once.
static struct {
    uint8_t ready;
    const slsk_entry_t *body_active, *body_inactive;
    const slsk_entry_t *btn, *btn_down;
    uint32_t theme_version;          // the sprites below are this theme's
    const slsk_entry_t *glyph[3], *dot;
    const slsk_font_t *title_active, *title_inactive;
} g_frame;

static int sl_window_frame_ready(sl_ctx_t *ctx) {
    if (g_frame.ready && g_frame.theme_version == sl_ui_runtime_version()) return g_frame.ready == 1u;
    g_frame.ready = 2u;
    g_frame.theme_version = sl_ui_runtime_version();
    if (!ctx->skin_loaded) return 0;
    const sl_skin_t *s = &ctx->skin;
    uint8_t night = sl_ui_runtime_night();
    const slsk_entry_t *ta = sl_skin_find(s, "ui-16-mid");
    const slsk_entry_t *ti = sl_skin_find(s, "ui-16");
    g_frame.body_active = sl_skin_find(s, night ? "window-n-a" : "window-a");
    g_frame.body_inactive = sl_skin_find(s, night ? "window-flat-n-a" : "window-flat-a");
    g_frame.btn = sl_skin_find(s, night ? "win-btn-n-a" : "win-btn-a");
    g_frame.btn_down = sl_skin_find(s, night ? "win-btn-down-n-a" : "win-btn-down-a");
    g_frame.glyph[SL_WIN_BTN_CLOSE] = sl_skin_find(s, "win-close");
    g_frame.glyph[SL_WIN_BTN_MIN] = sl_skin_find(s, "win-min");
    g_frame.glyph[SL_WIN_BTN_MAX] = sl_skin_find(s, "win-max");
    g_frame.dot = sl_skin_find(s, "dot-12");
    if (!ta || !ti || !g_frame.body_active || !g_frame.body_inactive || !g_frame.btn || !g_frame.btn_down ||
        !g_frame.glyph[0] || !g_frame.glyph[1] || !g_frame.glyph[2] || !g_frame.dot) {
        peos_puts("starlight: skin lacks the window frame, plain frames\n");
        return 0;
    }
    g_frame.title_active = sl_skin_font(s, ta);
    g_frame.title_inactive = sl_skin_font(s, ti);
    g_frame.ready = 1u;
    return 1;
}

// ------------------------------------------------------------------ geometry

void sl_window_body_rect(const sl_window_t *win, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint32_t bx = 0u, by = 0u, bw = 0u, bh = 0u;
    if (win && win->width > 2u * SL_WIN_PAD_X && win->height > SL_WIN_BODY_TOP + SL_WIN_PAD_BOTTOM) {
        bx = win->x + SL_WIN_PAD_X;
        by = win->y + SL_WIN_BODY_TOP;
        bw = win->width - 2u * SL_WIN_PAD_X;
        bh = win->height - SL_WIN_BODY_TOP - SL_WIN_PAD_BOTTOM;
    }
    if (x) *x = bx;
    if (y) *y = by;
    if (w) *w = bw;
    if (h) *h = bh;
}

// The opaque inside, cached between redraws.
static sl_win_rect_t sl_window_inner_rect(const sl_window_t *win) {
    sl_win_rect_t r = { (int32_t)(win->x + SL_WIN_OPAQUE_INSET), (int32_t)(win->y + SL_WIN_OPAQUE_INSET), 0u, 0u };
    if (win->width > 2u * SL_WIN_OPAQUE_INSET) r.w = win->width - 2u * SL_WIN_OPAQUE_INSET;
    if (win->height > 2u * SL_WIN_OPAQUE_INSET) r.h = win->height - 2u * SL_WIN_OPAQUE_INSET;
    return r;
}

static const slsk_entry_t *sl_window_body_sprite(const sl_window_t *win) {
    if (g_frame.ready != 1u) return NULL;
    return win->focused ? g_frame.body_active : g_frame.body_inactive;
}

sl_win_rect_t sl_window_outer_rect(const sl_window_t *win) {
    sl_win_rect_t r = { 0, 0, 0u, 0u };
    if (!win || win->width == 0u || win->height == 0u) return r;
    // The larger of the two shadows, so that a focus change is covered too.
    uint32_t pl = 0u, pt = 0u, pr = 0u, pb = 0u;
    for (int i = 0; i < 2 && g_frame.ready == 1u; i++) {
        const slsk_entry_t *e = i ? g_frame.body_inactive : g_frame.body_active;
        if (e->pad_l > pl) pl = e->pad_l;
        if (e->pad_t > pt) pt = e->pad_t;
        if (e->pad_r > pr) pr = e->pad_r;
        if (e->pad_b > pb) pb = e->pad_b;
    }
    r.x = (int32_t)win->x - (int32_t)pl;
    r.y = (int32_t)win->y - (int32_t)pt;
    r.w = win->width + pl + pr;
    r.h = win->height + pt + pb;
    return r;
}

static void sl_dirty_add_signed(const sl_ctx_t *ctx, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0) return;
    sl_dirty_add(ctx, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

void sl_window_dirty_all(const sl_ctx_t *ctx, const sl_window_t *win) {
    if (!ctx || !win) return;
    sl_win_rect_t r = sl_window_outer_rect(win);
    sl_dirty_add_signed(ctx, r.x, r.y, r.w, r.h);
}

void sl_window_dirty_body(const sl_ctx_t *ctx, const sl_window_t *win) {
    uint32_t x, y, w, h;
    sl_window_body_rect(win, &x, &y, &w, &h);
    if (w && h) sl_dirty_add(ctx, x, y, w, h);
}

void sl_window_button_rect(const sl_window_t *win, int button, uint32_t *x, uint32_t *y) {
    *x = win->x + SL_WIN_PAD_X + (uint32_t)button * (SL_WIN_BTN + SL_WIN_BTN_GAP);
    *y = win->y + SL_WIN_PAD_TOP + (SL_WIN_BAR_H - SL_WIN_BTN) / 2u;
}

int sl_window_button_at(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win || !win->focused) return SL_WIN_BTN_NONE;   // inactive windows show dots, not buttons
    for (int b = SL_WIN_BTN_CLOSE; b <= SL_WIN_BTN_MAX; b++) {
        uint32_t bx, by;
        sl_window_button_rect(win, b, &bx, &by);
        // A circle: the corners of its box are not the button.
        int64_t dx = (int64_t)x * 2 + 1 - ((int64_t)bx * 2 + SL_WIN_BTN);
        int64_t dy = (int64_t)y * 2 + 1 - ((int64_t)by * 2 + SL_WIN_BTN);
        if (dx * dx + dy * dy <= (int64_t)SL_WIN_BTN * SL_WIN_BTN) return b;
    }
    return SL_WIN_BTN_NONE;
}

// -------------------------------------------------------------------- cache

static int sl_window_ensure_surface(sl_window_t *win, uint32_t w, uint32_t h) {
    if (!win || !win->ctx || w == 0u || h == 0u) return 0;
    uint32_t stride = w * win->ctx->bytespp;
    if (win->client_surface && win->client_width == w && win->client_height == h && win->client_stride == stride) {
        return 1;
    }
    size_t need = (size_t)stride * h;
    if (!win->client_surface || need > win->client_cap) {   // while resizing, grown once and reused
        // Not realloc: the old pixels would be copied, and the cache of a
        // window that changed size is drawn again anyway.
        free(win->client_surface);
        win->client_cap = 0u;
        uint8_t *buf = (uint8_t *)malloc(need + need / 4u);
        win->client_surface = buf;
        if (!buf) return 0;
        win->client_surface = buf;
        win->client_cap = need + need / 4u;
    }
    win->client_width = w;
    win->client_height = h;
    win->client_stride = stride;
    return 1;
}

// Copy between the frame and the cache over (rect ∩ inner ∩ screen).
// to_cache: frame -> cache, else cache -> frame.
static int sl_window_cache_copy(sl_window_t *win, int64_t rx, int64_t ry, int64_t rw, int64_t rh, int to_cache) {
    sl_ctx_t *ctx = win->ctx;
    if (!ctx || !ctx->frame) return 0;
    sl_win_rect_t in = sl_window_inner_rect(win);
    if (to_cache && !sl_window_ensure_surface(win, in.w, in.h)) return 0;
    if (!win->client_surface || win->client_width != in.w || win->client_height != in.h) return 0;

    int64_t x0 = rx > in.x ? rx : in.x;
    int64_t y0 = ry > in.y ? ry : in.y;
    int64_t x1 = rx + rw < (int64_t)in.x + in.w ? rx + rw : (int64_t)in.x + in.w;
    int64_t y1 = ry + rh < (int64_t)in.y + in.h ? ry + rh : (int64_t)in.y + in.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)ctx->fb.width) x1 = ctx->fb.width;
    if (y1 > (int64_t)ctx->fb.height) y1 = ctx->fb.height;
    if (x1 <= x0 || y1 <= y0) return 1;

    size_t bytes = (size_t)(x1 - x0) * ctx->bytespp;
    for (int64_t yy = y0; yy < y1; yy++) {
        uint8_t *f = ctx->frame + (size_t)yy * ctx->row_stride + (size_t)x0 * ctx->bytespp;
        uint8_t *c = win->client_surface + (size_t)(yy - in.y) * win->client_stride +
                     (size_t)(x0 - in.x) * ctx->bytespp;
        if (to_cache) memcpy(c, f, bytes);
        else memcpy(f, c, bytes);
    }
    return 1;
}

static void sl_window_blit_cache_rect(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, void *userdata) {
    (void)sl_window_cache_copy((sl_window_t *)userdata, dx, dy, dw, dh, 0);
}

// ---------------------------------------------------------------- the frame

// Shadow and corners: the sprite outside the opaque inside, only where the
// frame is dirty (it blends over what the background restore put there).
static void sl_window_ring_rect(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, void *userdata) {
    sl_window_t *win = (sl_window_t *)userdata;
    sl_ctx_t *ctx = win->ctx;
    const slsk_entry_t *e = sl_window_body_sprite(win);
    if (!e) return;
    sl_win_rect_t in = sl_window_inner_rect(win);
    int64_t x0 = dx, y0 = dy, x1 = (int64_t)dx + dw, y1 = (int64_t)dy + dh;
    int64_t ix0 = in.x, iy0 = in.y, ix1 = (int64_t)in.x + in.w, iy1 = (int64_t)in.y + in.h;
    // Up to four strips of the dirty rect around the inside.
    int64_t strips[4][4] = {
        { x0, y0, x1, y1 < iy0 ? y1 : iy0 },                              // above
        { x0, y0 > iy1 ? y0 : iy1, x1, y1 },                              // below
        { x0, y0 > iy0 ? y0 : iy0, x1 < ix0 ? x1 : ix0, y1 < iy1 ? y1 : iy1 },   // left
        { x0 > ix1 ? x0 : ix1, y0 > iy0 ? y0 : iy0, x1, y1 < iy1 ? y1 : iy1 },   // right
    };
    sl_target_t t = sl_target_from_ctx(ctx);
    sl_target_t base = t;
    for (int k = 0; k < 4; k++) {
        t = base;
        if (strips[k][0] > t.clip_x0) t.clip_x0 = (int32_t)strips[k][0];
        if (strips[k][1] > t.clip_y0) t.clip_y0 = (int32_t)strips[k][1];
        if (strips[k][2] < t.clip_x1) t.clip_x1 = (int32_t)strips[k][2];
        if (strips[k][3] < t.clip_y1) t.clip_y1 = (int32_t)strips[k][3];
        if (t.clip_x1 <= t.clip_x0 || t.clip_y1 <= t.clip_y0) continue;
        sl_blit_sprite(&t, e, sl_skin_data(&ctx->skin, e), (int32_t)win->x - e->pad_l, (int32_t)win->y - e->pad_t,
                       win->width + e->pad_l + e->pad_r, win->height + e->pad_t + e->pad_b);
    }
}

// The inside: pearl, title bar, then the app.
static void sl_window_draw_inside(sl_window_t *win) {
    sl_ctx_t *ctx = win->ctx;
    sl_win_rect_t in = sl_window_inner_rect(win);
    sl_fill_rect(ctx, (uint32_t)in.x, (uint32_t)in.y, in.w, in.h, SL_WIN_RGB(SL_UI_COLOR_BG_CANVAS));
    const char *title = win->title[0] ? win->title : "App";

    if (g_frame.ready == 1u) {
        uint8_t saved_clip = ctx->clip_enabled;
        uint32_t scx = ctx->clip_x, scy = ctx->clip_y, scw = ctx->clip_w, sch = ctx->clip_h;
        sl_clip_set(ctx, (uint32_t)in.x, (uint32_t)in.y, in.w, in.h);
        sl_target_t t = sl_target_from_ctx(ctx);
        for (int b = SL_WIN_BTN_CLOSE; b <= SL_WIN_BTN_MAX; b++) {
            uint32_t bx, by;
            sl_window_button_rect(win, b, &bx, &by);
            const slsk_entry_t *g = g_frame.glyph[b];
            int32_t gx = (int32_t)(bx + (SL_WIN_BTN - g->width) / 2u);
            int32_t gy = (int32_t)(by + (SL_WIN_BTN - g->height) / 2u);
            if (win->focused) {
                const slsk_entry_t *bs = (win->pressed_btn == b) ? g_frame.btn_down : g_frame.btn;
                sl_draw_skin(ctx, bs, (int32_t)bx, (int32_t)by, SL_WIN_BTN, SL_WIN_BTN);
                sl_blit_mask(&t, sl_skin_data(&ctx->skin, g), g->width, g->height, g->width, gx, gy, SL_WIN_INK, 255u);
            } else {
                const slsk_entry_t *d = g_frame.dot;
                sl_blit_mask(&t, sl_skin_data(&ctx->skin, d), d->width, d->height, d->width,
                             (int32_t)(bx + (SL_WIN_BTN - d->width) / 2u), (int32_t)(by + (SL_WIN_BTN - d->height) / 2u),
                             SL_WIN_DOT_OFF, 255u);
            }
        }
        // Title centred on the bar, between the buttons and the dot.
        const slsk_font_t *f = win->focused ? g_frame.title_active : g_frame.title_inactive;
        uint32_t tw = sl_text_width(f, title);
        // Centred on the window if it fits there, else centred between the
        // buttons and the dot; cut if even that is too narrow.
        uint32_t left = SL_WIN_PAD_X + 3u * SL_WIN_BTN + 2u * SL_WIN_BTN_GAP + 12u;
        uint32_t right = SL_WIN_PAD_X + 10u + 12u;
        if (win->width > left + right) {
            uint32_t room = win->width - left - right;
            uint32_t centred = (win->width > tw) ? (win->width - tw) / 2u : 0u;
            uint32_t off = centred;
            if (off < left || off + tw > win->width - right) off = left + (room > tw ? (room - tw) / 2u : 0u);
            int32_t base = (int32_t)(win->y + SL_WIN_PAD_TOP + SL_WIN_BAR_H / 2u) + (f->ascent - f->descent) / 2;
            sl_target_t tt = t;
            if ((int64_t)win->x + left > tt.clip_x0) tt.clip_x0 = (int32_t)(win->x + left);
            if ((int64_t)win->x + win->width - right < tt.clip_x1) tt.clip_x1 = (int32_t)(win->x + win->width - right);
            if (win->focused) sl_text_draw(&tt, f, (int32_t)(win->x + off), base, title, SL_WIN_INK);
            else sl_text_draw(&tt, f, (int32_t)(win->x + off), base, title, SL_WIN_MUTED);
        }
        if (win->focused) {   // the accent dot, from the theme
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, win->x + win->width - SL_WIN_PAD_X - 10u,
                              win->y + SL_WIN_PAD_TOP + (SL_WIN_BAR_H - 10u) / 2u, 10u, 10u, 5u);
        }
        if (saved_clip) sl_clip_set(ctx, scx, scy, scw, sch);
        else sl_clip_clear(ctx);
    } else {
        sl_draw_text(ctx, win->x + SL_WIN_PAD_X, win->y + SL_WIN_PAD_TOP + 14u, title, SL_WIN_INK);
    }

    if (win->task && win->task->on_render) {
        uint64_t t0 = peos_time_ns();
        win->task->on_render(ctx, win, win->task->userdata);
        sl_perf_add_app(peos_time_ns() - t0);
    }
}

void sl_window_set_fps_profile(sl_window_t *win, uint16_t focused_fps, uint16_t unfocused_fps) {
    if (!win) return;
    win->fps_focused = focused_fps;
    win->fps_unfocused = unfocused_fps;
    win->force_redraw = 1u;
    win->next_render_ns = 0ull;
}

void sl_window_mark_activity(sl_window_t *win, uint64_t now_ns) {
    if (!win) return;
    if (now_ns == 0ull) now_ns = peos_time_ns();
    win->last_activity_ns = now_ns;
    win->next_render_ns = 0ull;
}

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, app_instance_t* task, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!win) return 0;
    peos_memset(win, 0, sizeof(*win));
    win->ctx = ctx;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->z_index = 0u;
    win->focused = 0u;
    win->pressed_btn = SL_WIN_BTN_NONE;
    win->resizable = 1u;
    win->top_bar_height = SL_WIN_PAD_TOP + SL_WIN_BAR_H;
    win->cache_valid = 0u;
    win->force_redraw = 1u;
    win->anim_opacity = 255u;
    win->fps_focused = 60u;
    win->fps_unfocused = 24u;
    win->next_render_ns = 0ull;
    win->last_activity_ns = peos_time_ns();
    win->opened_ns = win->last_activity_ns;
    if (ctx) (void)sl_window_frame_ready(ctx);
    if (task) {
        win->task = task;
    }
    return 1;
}

void sl_window_shutdown(sl_window_t *win) {
    if (!win) return;
    if (win->client_surface) {
        free(win->client_surface);
        win->client_surface = NULL;
    }
    if (win->anim_underlay) {
        free(win->anim_underlay);
        win->anim_underlay = NULL;
    }
    win->client_cap = 0u;
    win->client_width = 0u;
    win->client_height = 0u;
    win->client_stride = 0u;
    win->anim_underlay_stride = 0u;
    win->anim_underlay_height = 0u;
    win->cache_valid = 0u;
}

// While a window fades in or out, what was under it (screen-clipped outer
// rect) is kept to blend the finished window with.
static int sl_window_capture_underlay(sl_window_t *win, int64_t *ux0, int64_t *uy0, uint32_t *uw, uint32_t *uh) {
    sl_ctx_t *ctx = win->ctx;
    sl_win_rect_t o = sl_window_outer_rect(win);
    int64_t x0 = o.x < 0 ? 0 : o.x, y0 = o.y < 0 ? 0 : o.y;
    int64_t x1 = (int64_t)o.x + o.w, y1 = (int64_t)o.y + o.h;
    if (x1 > (int64_t)ctx->fb.width) x1 = ctx->fb.width;
    if (y1 > (int64_t)ctx->fb.height) y1 = ctx->fb.height;
    if (x1 <= x0 || y1 <= y0) return 0;
    uint32_t w = (uint32_t)(x1 - x0), h = (uint32_t)(y1 - y0);
    uint32_t stride = w * ctx->bytespp;
    if (!win->anim_underlay || win->anim_underlay_stride != stride || win->anim_underlay_height != h) {
        uint8_t *buf = (uint8_t *)realloc(win->anim_underlay, (size_t)stride * h);
        if (!buf) return 0;
        win->anim_underlay = buf;
        win->anim_underlay_stride = stride;
        win->anim_underlay_height = h;
    }
    for (uint32_t row = 0; row < h; row++) {
        memcpy(win->anim_underlay + (size_t)row * stride,
                    ctx->frame + (size_t)(y0 + row) * ctx->row_stride + (size_t)x0 * ctx->bytespp, stride);
    }
    *ux0 = x0;
    *uy0 = y0;
    *uw = w;
    *uh = h;
    return 1;
}

static void sl_window_blend_underlay(sl_window_t *win, int64_t x0, int64_t y0, uint32_t w, uint32_t h) {
    sl_ctx_t *ctx = win->ctx;
    uint32_t a = win->anim_opacity, inv = 255u - a;
    for (uint32_t row = 0; row < h; row++) {
        uint8_t *d = ctx->frame + (size_t)(y0 + row) * ctx->row_stride + (size_t)x0 * ctx->bytespp;
        const uint8_t *b = win->anim_underlay + (size_t)row * win->anim_underlay_stride;
        for (uint32_t col = 0u; col < w; col++, d += ctx->bytespp, b += ctx->bytespp) {
            d[0] = (uint8_t)((d[0] * a + b[0] * inv + 127u) / 255u);
            d[1] = (uint8_t)((d[1] * a + b[1] * inv + 127u) / 255u);
            d[2] = (uint8_t)((d[2] * a + b[2] * inv + 127u) / 255u);
            if (ctx->bytespp == 4u) d[3] = 0xFFu;
        }
    }
}

int sl_window_render(sl_window_t *win) {
    if (!win || !win->ctx || win->minimized) return 0;
    sl_ctx_t *ctx = win->ctx;
    (void)sl_window_frame_ready(ctx);

    uint64_t now = peos_time_ns();
    uint16_t base_fps = win->focused ? win->fps_focused : win->fps_unfocused;
    uint16_t target_fps = base_fps;
    uint64_t idle_ns = (now > win->last_activity_ns) ? (now - win->last_activity_ns) : 0ull;
    if (idle_ns <= SL_ACTIVITY_BOOST_NS) {
        uint16_t boost = win->focused ? SL_FPS_BOOST_FOCUSED : SL_FPS_BOOST_UNFOCUSED;
        if (boost > target_fps) target_fps = boost;
    } else if (idle_ns >= SL_ACTIVITY_IDLE_NS) {
        uint16_t min_idle = win->focused ? 18u : 6u;
        uint16_t decayed = (base_fps > 1u) ? (uint16_t)(base_fps / 2u) : base_fps;
        if (decayed < min_idle) decayed = min_idle;
        target_fps = decayed;
    }
    uint8_t due = !(target_fps > 0u && win->cache_valid && !win->force_redraw && win->next_render_ns > now);
    {
        sl_win_rect_t in = sl_window_inner_rect(win);
        if (win->client_width != in.w || win->client_height != in.h) due = 1u;   // resized: the cache is stale
        // Being dragged: the content has not changed, only its place. The
        // periodic redraw waits for the drop, unless something asked for it.
        else if (win->dragging && win->cache_valid && !win->force_redraw) due = 0u;
    }

    uint8_t fading = win->anim_opacity < 255u;
    int64_t ux0 = 0, uy0 = 0;
    uint32_t uw = 0u, uh = 0u;
    if (fading && !sl_window_capture_underlay(win, &ux0, &uy0, &uw, &uh)) fading = 0u;

    sl_win_rect_t o = sl_window_outer_rect(win);
    int64_t ox0 = o.x < 0 ? 0 : o.x, oy0 = o.y < 0 ? 0 : o.y;
    int64_t ow = (int64_t)o.x + o.w - ox0, oh = (int64_t)o.y + o.h - oy0;
    if (ow > 0 && oh > 0) {
        sl_dirty_for_each_intersection((uint32_t)ox0, (uint32_t)oy0, (uint32_t)ow, (uint32_t)oh,
                                       sl_window_ring_rect, win);
    }

    if (!due) {
        sl_win_rect_t in = sl_window_inner_rect(win);
        sl_dirty_for_each_intersection((uint32_t)in.x, (uint32_t)in.y, in.w, in.h, sl_window_blit_cache_rect, win);
    } else {
        uint8_t cache_was_valid = win->cache_valid;
        sl_window_draw_inside(win);
        uint64_t c0 = peos_time_ns();
        sl_win_rect_t in = sl_window_inner_rect(win);
        uint32_t rx = 0u, ry = 0u, rw = 0u, rh = 0u;
        if (cache_was_valid && !win->force_redraw && !fading && win->task && win->task->userdata &&
            sl_app_host_cache_update_rect(win, win->task->userdata, &rx, &ry, &rw, &rh)) {
            win->cache_valid = (uint8_t)sl_window_cache_copy(win, rx, ry, rw, rh, 1);
        } else {
            win->cache_valid = (uint8_t)sl_window_cache_copy(win, in.x, in.y, in.w, in.h, 1);
        }
        sl_perf_add_cache(peos_time_ns() - c0);
        win->force_redraw = 0u;
        uint64_t interval = target_fps > 0u ? 1000000000ull / (uint64_t)target_fps : 0ull;
        win->next_render_ns = now + (interval ? interval : 1ull);
    }

    if (fading) sl_window_blend_underlay(win, ux0, uy0, uw, uh);
    return 1;
}
