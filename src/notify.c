// Desktop notifications (Soft UI plan, phases 7 and 9). Starlight registers
// the "notify" service (peos/notify.h); each message becomes a night card at
// the top right (tavola Finestre, "notification toast") that slides in from
// the right edge, stays a few seconds and slides out, and a count on the
// dock tile of the app whose title is its source, until that app comes to
// the front. The last ones stay in the notification centre, opened from the
// time in the top bar.

#include "notify.h"

#include "desktop.h"
#include "i18n.h"
#include "starlight_internal.h"
#include "render/blit.h"
#include "render/pixels.h"
#include "render/surface.h"
#include "ui_sdk.h"
#include "window.h"
#include "peos/notify.h"

#include <stdio.h>
#include <string.h>

#define SL_NOTIFY_PENDING 8u
#define SL_NOTIFY_QUEUE 8u
#define SL_NOTIFY_BADGES 16u
#define SL_NOTIFY_HISTORY 16u
#define SL_NOTIFY_SHOW_NS 5000000000ull
#define SL_NOTIFY_IN_NS 280000000ull        // sliding in
#define SL_NOTIFY_OUT_NS 220000000ull       // and out
#define SL_NOTIFY_ACCEPT_NS 2000000000ull   // a client that connects and never sends
#define SL_NOTIFY_TOP 56u
#define SL_NOTIFY_RIGHT 40u
// The card (tavola Finestre): 92 high, radius 46, 28 of padding, an icon in a
// 46 px circle, 18 between them.
#define SL_NOTIFY_H 92u
#define SL_NOTIFY_PAD 28u
#define SL_NOTIFY_ICON 46u
#define SL_NOTIFY_GAP 18u
#define SL_NOTIFY_MIN_W 480u
#define SL_NOTIFY_SHADOW 24u
// The centre: a night panel under the bar, the newest first.
#define SL_CENTER_W 480u
#define SL_CENTER_HEAD 70u
#define SL_CENTER_ROW 76u
#define SL_CENTER_ROWS 6u
#define SL_CENTER_FOOT 20u

typedef struct {
    char source[PEOS_NOTIFY_SOURCE_MAX];
    char text[PEOS_NOTIFY_TEXT_MAX];
    char when[8];                       // local hh:mm it came
} sl_note_t;

static struct {
    int64_t listen;
    struct { int64_t fd; uint64_t since; } pending[SL_NOTIFY_PENDING];
    sl_note_t queue[SL_NOTIFY_QUEUE];
    uint32_t head, count;
    sl_note_t shown;
    uint8_t showing;
    uint64_t start_ns, until_ns;        // shown since, and until (then it slides out)
    uint32_t card_x, card_w;            // where the card rests
    int32_t drawn_off;                  // its slide offset as last drawn
    struct { char source[PEOS_NOTIFY_SOURCE_MAX]; uint32_t count; } badges[SL_NOTIFY_BADGES];
    sl_note_t history[SL_NOTIFY_HISTORY];   // newest at history_head - 1
    uint32_t history_head, history_count;
    uint8_t center_open;
    const void *focus_task;
    const slsk_font_t *source_f, *text_f, *small_f, *title_f;
    const slsk_entry_t *bell, *globe;
} g_note = { .listen = -1 };

static int sl_note_same(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
        if (!x) return 1;
    }
}

void sl_notify_init(sl_ctx_t *ctx) {
    (void)ctx;
    g_note.listen = peos_svc_register(PEOS_NOTIFY_SERVICE);
    if (g_note.listen < 0) peos_puts("starlight: cannot register the notify service (no \"service\" capability?)\n");
}

uint32_t sl_notify_badge(const char *title) {
    for (uint32_t i = 0; title && i < SL_NOTIFY_BADGES; i++) {
        if (g_note.badges[i].count && sl_note_same(g_note.badges[i].source, title)) return g_note.badges[i].count;
    }
    return 0u;
}

static void sl_note_count(sl_ctx_t *ctx, const char *source) {
    // The app in front sees it already: no badge to clear later.
    sl_window_t *focused = sl_window_get_focused();
    if (focused && !focused->minimized && sl_note_same(focused->title, source)) return;
    uint32_t slot = SL_NOTIFY_BADGES;
    for (uint32_t i = 0; i < SL_NOTIFY_BADGES; i++) {
        if (g_note.badges[i].count && sl_note_same(g_note.badges[i].source, source)) {
            slot = i;
            break;
        }
        if (!g_note.badges[i].count && slot == SL_NOTIFY_BADGES) slot = i;
    }
    if (slot == SL_NOTIFY_BADGES) return;
    if (!g_note.badges[slot].count) snprintf(g_note.badges[slot].source, sizeof(g_note.badges[slot].source), "%s", source);
    if (g_note.badges[slot].count < 99u) g_note.badges[slot].count++;
    sl_launcher_mark_dirty(ctx);
}

// ------------------------------------------------------------------- centre

static uint32_t sl_center_h(void) {
    uint32_t rows = g_note.history_count < SL_CENTER_ROWS ? g_note.history_count : SL_CENTER_ROWS;
    if (rows == 0u) rows = 1u;   // "Nessuna notifica"
    return SL_CENTER_HEAD + rows * SL_CENTER_ROW + SL_CENTER_FOOT;
}

static uint32_t sl_center_x(const sl_ctx_t *ctx) {
    return ctx->fb.width > SL_CENTER_W + SL_NOTIFY_RIGHT ? ctx->fb.width - SL_CENTER_W - SL_NOTIFY_RIGHT : 0u;
}

static void sl_center_dirty(sl_ctx_t *ctx) {
    uint32_t x = sl_center_x(ctx);
    x = x > SL_NOTIFY_SHADOW ? x - SL_NOTIFY_SHADOW : 0u;
    sl_dirty_add(ctx, x, SL_NOTIFY_TOP - 20u, SL_CENTER_W + 2u * SL_NOTIFY_SHADOW,
                 SL_CENTER_HEAD + SL_CENTER_ROWS * SL_CENTER_ROW + SL_CENTER_FOOT + 2u * SL_NOTIFY_SHADOW);
}

static void sl_center_set(sl_ctx_t *ctx, uint8_t open) {
    if (g_note.center_open == open) return;
    sl_center_dirty(ctx);
    g_note.center_open = open;
}

// "Cancella", at the right of the centre's title.
static int sl_center_clear_hit(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    uint32_t cx = sl_center_x(ctx);
    return x >= cx + SL_CENTER_W - 140u && x < cx + SL_CENTER_W && y >= SL_NOTIFY_TOP && y < SL_NOTIFY_TOP + SL_CENTER_HEAD;
}

int sl_notify_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    // The time at the right of the top bar opens and closes the centre.
    if (y < SL_DESK_BAR_HEIGHT && x + 140u >= ctx->fb.width) {
        sl_center_set(ctx, (uint8_t)!g_note.center_open);
        return 1;
    }
    if (!g_note.center_open) return 0;
    uint32_t cx = sl_center_x(ctx);
    if (x >= cx && x < cx + SL_CENTER_W && y >= SL_NOTIFY_TOP && y < SL_NOTIFY_TOP + sl_center_h()) {
        if (sl_center_clear_hit(ctx, x, y) && g_note.history_count) {
            sl_center_dirty(ctx);
            g_note.history_count = 0u;
        }
        return 1;
    }
    sl_center_set(ctx, 0u);   // a click elsewhere closes it, and does nothing else
    return 1;
}

int sl_notify_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return -1;
    if (y < SL_DESK_BAR_HEIGHT && x + 140u >= ctx->fb.width) return SL_CUR_HAND;   // the time opens the centre
    if (!g_note.center_open) return -1;
    uint32_t cx = sl_center_x(ctx);
    if (x < cx || x >= cx + SL_CENTER_W || y < SL_NOTIFY_TOP || y >= SL_NOTIFY_TOP + sl_center_h()) return -1;
    return (g_note.history_count && sl_center_clear_hit(ctx, x, y)) ? SL_CUR_HAND : SL_CUR_ARROW;
}

int sl_notify_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev) {
    if (!ctx || !ev || !g_note.center_open) return 0;
    if (ev->code == PEOS_KEY_ESC && ev->pressed) sl_center_set(ctx, 0u);
    return ev->code == PEOS_KEY_ESC;
}

// --------------------------------------------------------------- receiving

static void sl_note_receive(sl_ctx_t *ctx, const peos_notify_msg_t *msg) {
    sl_note_t n;
    memcpy(n.source, msg->source, sizeof(n.source));
    memcpy(n.text, msg->text, sizeof(n.text));
    n.source[sizeof(n.source) - 1u] = '\0';
    n.text[sizeof(n.text) - 1u] = '\0';
    sl_desk_local_hhmm(ctx, n.when, sizeof(n.when));
    if (g_note.count == SL_NOTIFY_QUEUE) {   // full: the oldest goes
        g_note.head = (g_note.head + 1u) % SL_NOTIFY_QUEUE;
        g_note.count--;
    }
    g_note.queue[(g_note.head + g_note.count) % SL_NOTIFY_QUEUE] = n;
    g_note.count++;
    g_note.history[g_note.history_head] = n;
    g_note.history_head = (g_note.history_head + 1u) % SL_NOTIFY_HISTORY;
    if (g_note.history_count < SL_NOTIFY_HISTORY) g_note.history_count++;
    if (g_note.center_open) sl_center_dirty(ctx);
    sl_note_count(ctx, n.source);
}

void sl_notify_post(sl_ctx_t *ctx, const char *source, const char *text) {
    if (!ctx || !source || !text) return;
    peos_notify_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.magic = PEOS_NOTIFY_MAGIC;
    snprintf(msg.source, sizeof(msg.source), "%s", source);
    snprintf(msg.text, sizeof(msg.text), "%s", text);
    sl_note_receive(ctx, &msg);
}

void sl_notify_toggle_center(sl_ctx_t *ctx) {
    if (ctx) sl_center_set(ctx, (uint8_t)!g_note.center_open);
}

static int sl_note_assets(sl_ctx_t *ctx) {
    if (g_note.source_f) return 1;
    if (!ctx->skin_loaded) return 0;
    const slsk_entry_t *a = sl_skin_find(&ctx->skin, "ui-15");
    const slsk_entry_t *b = sl_skin_find(&ctx->skin, "ui-17");
    const slsk_entry_t *c = sl_skin_find(&ctx->skin, "ui-14");
    const slsk_entry_t *d = sl_skin_find(&ctx->skin, "ui-22-mid");
    g_note.bell = sl_skin_find(&ctx->skin, "bell-22");
    g_note.globe = sl_skin_find(&ctx->skin, "globe-22");
    if (!a || !b || !c || !d || !g_note.bell || !g_note.globe) return 0;
    g_note.source_f = sl_skin_font(&ctx->skin, a);
    g_note.text_f = sl_skin_font(&ctx->skin, b);
    g_note.small_f = sl_skin_font(&ctx->skin, c);
    g_note.title_f = sl_skin_font(&ctx->skin, d);
    return 1;
}

// The card's slide offset now: from off screen to 0, then back.
static int32_t sl_note_offset(const sl_ctx_t *ctx, uint64_t now) {
    int32_t travel = (int32_t)(ctx->fb.width - g_note.card_x) + (int32_t)SL_NOTIFY_SHADOW;
    if (now < g_note.start_ns + SL_NOTIFY_IN_NS) {
        uint32_t t = (uint32_t)((now - g_note.start_ns) * 1000u / SL_NOTIFY_IN_NS);
        uint32_t u = 1000u - t;   // ease out: 1 - (1 - t)^3
        uint64_t left = (uint64_t)u * u * u / 1000000u;
        return (int32_t)((int64_t)travel * (int64_t)left / 1000);
    }
    if (now >= g_note.until_ns) {
        uint64_t t = (now - g_note.until_ns) * 1000u / SL_NOTIFY_OUT_NS;
        if (t > 1000u) t = 1000u;
        uint64_t gone = t * t * t / 1000000u;   // ease in
        return (int32_t)((int64_t)travel * (int64_t)gone / 1000);
    }
    return 0;
}

// The card's whole reach while it slides: from where it rests to the edge.
static void sl_note_dirty_card(sl_ctx_t *ctx) {
    if (!g_note.card_w) return;
    uint32_t x = g_note.card_x > SL_NOTIFY_SHADOW ? g_note.card_x - SL_NOTIFY_SHADOW : 0u;
    sl_dirty_add(ctx, x, SL_NOTIFY_TOP - 20u, ctx->fb.width - x, SL_NOTIFY_H + 2u * SL_NOTIFY_SHADOW);
}

void sl_notify_tick(sl_ctx_t *ctx) {
    if (!ctx || g_note.listen < 0) return;
    uint64_t now = peos_time_ns();

    // New clients: each connection carries one message.
    for (;;) {
        uint8_t buf[PEOS_CHANNEL_MSG_MAX];
        peos_channel_msg_t m;
        memset(&m, 0, sizeof(m));
        m.data = buf;
        m.len = sizeof(buf);
        if (peos_channel_recv(g_note.listen, &m, PEOS_CHANNEL_NONBLOCK) < 0) break;
        for (uint32_t k = 0; k < m.nfds; k++) {
            uint32_t i = 0;
            while (i < SL_NOTIFY_PENDING && g_note.pending[i].fd > 0) i++;
            if (i == SL_NOTIFY_PENDING) {
                (void)peos_fclose((uint64_t)m.fds[k].fd);
                continue;
            }
            g_note.pending[i].fd = m.fds[k].fd;
            g_note.pending[i].since = now;
        }
    }
    for (uint32_t i = 0; i < SL_NOTIFY_PENDING; i++) {
        if (g_note.pending[i].fd <= 0) continue;
        peos_notify_msg_t msg;
        peos_channel_msg_t m;
        memset(&m, 0, sizeof(m));
        m.data = &msg;
        m.len = sizeof(msg);
        int64_t rc = peos_channel_recv(g_note.pending[i].fd, &m, PEOS_CHANNEL_NONBLOCK);
        if (rc == PEOS_CHANNEL_AGAIN && now - g_note.pending[i].since < SL_NOTIFY_ACCEPT_NS) continue;
        if (rc >= 0 && m.len == sizeof(msg) && msg.magic == PEOS_NOTIFY_MAGIC) sl_note_receive(ctx, &msg);
        for (uint32_t k = 0; rc >= 0 && k < m.nfds; k++) (void)peos_fclose((uint64_t)m.fds[k].fd);
        (void)peos_fclose((uint64_t)g_note.pending[i].fd);
        g_note.pending[i].fd = 0;
    }

    // The app in front has seen its notifications.
    sl_window_t *focused = sl_window_get_focused();
    const void *task = (focused && !focused->minimized) ? (const void *)focused->task : NULL;
    if (task != g_note.focus_task) {
        g_note.focus_task = task;
        for (uint32_t i = 0; task && i < SL_NOTIFY_BADGES; i++) {
            if (g_note.badges[i].count && sl_note_same(g_note.badges[i].source, focused->title)) {
                g_note.badges[i].count = 0u;
                sl_launcher_mark_dirty(ctx);
            }
        }
    }

    // One card at a time: in, a few seconds, out. While the centre is open
    // the cards wait (it shows them already).
    if (g_note.showing) {
        if (now >= g_note.until_ns + SL_NOTIFY_OUT_NS) {
            sl_note_dirty_card(ctx);
            g_note.showing = 0u;
            g_note.card_w = 0u;
        } else {
            int32_t off = sl_note_offset(ctx, now);
            if (off != g_note.drawn_off) sl_note_dirty_card(ctx);
        }
    }
    if (!g_note.showing && g_note.count && !g_note.center_open && sl_note_assets(ctx)) {
        g_note.shown = g_note.queue[g_note.head];
        g_note.head = (g_note.head + 1u) % SL_NOTIFY_QUEUE;
        g_note.count--;
        g_note.showing = 1u;
        g_note.start_ns = now;
        g_note.until_ns = now + SL_NOTIFY_IN_NS + SL_NOTIFY_SHOW_NS;
        uint32_t tw = sl_text_width(g_note.text_f, g_note.shown.text);
        uint32_t sw = sl_text_width(g_note.source_f, g_note.shown.source);
        uint32_t w = 2u * SL_NOTIFY_PAD + SL_NOTIFY_ICON + 2u * SL_NOTIFY_GAP + (tw > sw ? tw : sw) +
                     sl_text_width(g_note.small_f, sl_tr("notify.now"));
        uint32_t max_w = ctx->fb.width > 2u * SL_NOTIFY_RIGHT + SL_NOTIFY_MIN_W ? ctx->fb.width / 2u : ctx->fb.width;
        if (w < SL_NOTIFY_MIN_W) w = SL_NOTIFY_MIN_W;
        g_note.card_w = w > max_w ? max_w : w;
        g_note.card_x = ctx->fb.width > g_note.card_w + SL_NOTIFY_RIGHT ? ctx->fb.width - g_note.card_w - SL_NOTIFY_RIGHT : 0u;
        g_note.drawn_off = 0x7FFFFFFF;
        sl_note_dirty_card(ctx);
    }
}

// ------------------------------------------------------------------ drawing

static void sl_note_surface(sl_target_t *t, const sl_surf_style_t *st, int32_t x, int32_t y, uint32_t w, uint32_t h,
                            uint32_t r) {
    const sl_surf_t *s = sl_surf_get(st, w, h, r);
    if (!s) return;
    slsk_entry_t e;
    memset(&e, 0, sizeof(e));
    e.kind = SLSK_KIND_PREMUL;
    e.width = (uint16_t)s->w;
    e.height = (uint16_t)s->h;
    sl_blit_sprite(t, &e, s->px, x - (int32_t)s->pad_l, y - (int32_t)s->pad_t, s->w, s->h);
}

// The icon in its circle: a globe for the network, a bell for the rest.
static void sl_note_icon(sl_ctx_t *ctx, sl_target_t *t, int32_t x, int32_t y, uint32_t size, const char *source) {
    static const sl_surf_style_t circle = { 0x1C, 0x24, 0x40, 0, { { 0 } } };
    sl_note_surface(t, &circle, x, y, size, size, size / 2u);
    const slsk_entry_t *m = sl_note_same(source, "netd") ? g_note.globe : g_note.bell;
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    sl_blit_mask(t, sl_skin_data(&ctx->skin, m), m->width, m->height, m->width, x + (int32_t)(size - m->width) / 2,
                 y + (int32_t)(size - m->height) / 2, n->r, n->g, n->b, 255u);
}

// Source over text, the time at the right, between x and right.
static void sl_note_lines(sl_target_t *t, const sl_note_t *n, int32_t x, int32_t right, int32_t cy, const char *when) {
    static const uint8_t head[3] = { 0x9F, 0xB4, 0xD8 }, text[3] = { 0xE8, 0xF4, 0xFF };
    int32_t ww = (int32_t)sl_text_width(g_note.small_f, when);
    sl_text_draw(t, g_note.small_f, right - ww, cy + 5, when, head[0], head[1], head[2]);
    sl_target_t tt = *t;
    if (right - ww - 12 < tt.clip_x1) tt.clip_x1 = right - ww - 12;
    sl_text_draw(&tt, g_note.source_f, x, cy - 6, n->source, head[0], head[1], head[2]);
    sl_text_draw(&tt, g_note.text_f, x, cy + 18, n->text, text[0], text[1], text[2]);
}

static const sl_surf_style_t k_night_card = { 0x11, 0x16, 0x26, 1, { { 6, 6, 16, 0, 0x00, 0x00, 0x00, 110 } } };

static void sl_note_draw_card(sl_ctx_t *ctx, sl_target_t *t, int32_t off) {
    int32_t x = (int32_t)g_note.card_x + off, y = (int32_t)SL_NOTIFY_TOP;
    sl_note_surface(t, &k_night_card, x, y, g_note.card_w, SL_NOTIFY_H, SL_NOTIFY_H / 2u);
    int32_t ix = x + (int32_t)SL_NOTIFY_PAD, iy = y + (int32_t)(SL_NOTIFY_H - SL_NOTIFY_ICON) / 2;
    sl_note_icon(ctx, t, ix, iy, SL_NOTIFY_ICON, g_note.shown.source);
    sl_note_lines(t, &g_note.shown, ix + (int32_t)(SL_NOTIFY_ICON + SL_NOTIFY_GAP),
                  x + (int32_t)(g_note.card_w - SL_NOTIFY_PAD), y + (int32_t)SL_NOTIFY_H / 2, sl_tr("notify.now"));
}

static void sl_note_draw_center(sl_ctx_t *ctx, sl_target_t *t) {
    int32_t x = (int32_t)sl_center_x(ctx), y = (int32_t)SL_NOTIFY_TOP;
    uint32_t h = sl_center_h();
    sl_note_surface(t, &k_night_card, x, y, SL_CENTER_W, h, 40u);
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    int32_t right = x + (int32_t)SL_CENTER_W - 30;
    sl_text_draw(t, g_note.title_f, x + 30, y + 44, sl_tr("notify.title"), 0xE8, 0xF4, 0xFF);
    if (g_note.history_count) {
        int32_t cw = (int32_t)sl_text_width(g_note.source_f, sl_tr("notify.clear"));
        sl_text_draw(t, g_note.source_f, right - cw, y + 42, sl_tr("notify.clear"), n->r, n->g, n->b);
    }
    int32_t ry = y + (int32_t)SL_CENTER_HEAD;
    if (!g_note.history_count) {
        sl_text_draw(t, g_note.text_f, x + 30, ry + 42, sl_tr("notify.none"), 0x9F, 0xB4, 0xD8);
        return;
    }
    for (uint32_t i = 0; i < g_note.history_count && i < SL_CENTER_ROWS; i++) {
        const sl_note_t *note = &g_note.history[(g_note.history_head + SL_NOTIFY_HISTORY - 1u - i) % SL_NOTIFY_HISTORY];
        if (i) sl_fill_rect(ctx, (uint32_t)x + 30u, (uint32_t)ry, SL_CENTER_W - 60u, 1u, 0x23, 0x2B, 0x45);
        sl_note_icon(ctx, t, x + 30, ry + 16, 44u, note->source);
        sl_note_lines(t, note, x + 30 + 44 + 16, right, ry + (int32_t)SL_CENTER_ROW / 2, note->when);
        ry += (int32_t)SL_CENTER_ROW;
    }
}

static void sl_note_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_target_t t = sl_target_from_ctx(ctx);
    if (g_note.center_open) sl_note_draw_center(ctx, &t);
    else if (g_note.showing && g_note.card_w) sl_note_draw_card(ctx, &t, g_note.drawn_off);
    sl_clip_clear(ctx);
}

void sl_notify_render(sl_ctx_t *ctx) {
    if (!ctx || !sl_note_assets(ctx)) return;
    if (g_note.center_open) {
        uint32_t x = sl_center_x(ctx);
        x = x > SL_NOTIFY_SHADOW ? x - SL_NOTIFY_SHADOW : 0u;
        sl_dirty_for_each_intersection(x, SL_NOTIFY_TOP - 20u, SL_CENTER_W + 2u * SL_NOTIFY_SHADOW,
                                       sl_center_h() + 2u * SL_NOTIFY_SHADOW, sl_note_draw_rect, ctx);
        return;
    }
    if (!g_note.showing || !g_note.card_w) return;
    g_note.drawn_off = sl_note_offset(ctx, peos_time_ns());
    uint32_t x = g_note.card_x > SL_NOTIFY_SHADOW ? g_note.card_x - SL_NOTIFY_SHADOW : 0u;
    sl_dirty_for_each_intersection(x, SL_NOTIFY_TOP - 20u, ctx->fb.width - x, SL_NOTIFY_H + 2u * SL_NOTIFY_SHADOW,
                                   sl_note_draw_rect, ctx);
}
