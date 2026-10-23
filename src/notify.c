// Desktop notifications (Soft UI plan, phase 7). Starlight registers the
// "notify" service (peos/notify.h); each message becomes a night pill at the
// top right for a few seconds, and a count on the dock tile of the app whose
// title is its source, until that app comes to the front.

#include "notify.h"

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
#define SL_NOTIFY_SHOW_NS 5000000000ull
#define SL_NOTIFY_ACCEPT_NS 2000000000ull     // a client that connects and never sends
#define SL_NOTIFY_TOP 56u
#define SL_NOTIFY_RIGHT 40u
#define SL_NOTIFY_H 48u
#define SL_NOTIFY_PAD 22u

typedef struct {
    char source[PEOS_NOTIFY_SOURCE_MAX];
    char text[PEOS_NOTIFY_TEXT_MAX];
} sl_note_t;

static struct {
    int64_t listen;
    struct { int64_t fd; uint64_t since; } pending[SL_NOTIFY_PENDING];
    sl_note_t queue[SL_NOTIFY_QUEUE];
    uint32_t head, count;
    sl_note_t shown;
    uint8_t showing;
    uint64_t until_ns;
    uint32_t pill_x, pill_w;           // where the shown pill is
    struct { char source[PEOS_NOTIFY_SOURCE_MAX]; uint32_t count; } badges[SL_NOTIFY_BADGES];
    const void *focus_task;
    const slsk_font_t *bold, *body;
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

static void sl_note_receive(sl_ctx_t *ctx, const peos_notify_msg_t *msg) {
    sl_note_t n;
    memcpy(n.source, msg->source, sizeof(n.source));
    memcpy(n.text, msg->text, sizeof(n.text));
    n.source[sizeof(n.source) - 1u] = '\0';
    n.text[sizeof(n.text) - 1u] = '\0';
    if (g_note.count == SL_NOTIFY_QUEUE) {   // full: the oldest goes
        g_note.head = (g_note.head + 1u) % SL_NOTIFY_QUEUE;
        g_note.count--;
    }
    g_note.queue[(g_note.head + g_note.count) % SL_NOTIFY_QUEUE] = n;
    g_note.count++;
    sl_note_count(ctx, n.source);
}

static void sl_note_dirty_pill(sl_ctx_t *ctx) {
    if (!g_note.pill_w) return;
    // The pill and its shadow.
    uint32_t x = g_note.pill_x > 24u ? g_note.pill_x - 24u : 0u;
    sl_dirty_add(ctx, x, SL_NOTIFY_TOP - 20u, g_note.pill_w + 48u, SL_NOTIFY_H + 48u);
}

static int sl_note_fonts(sl_ctx_t *ctx) {
    if (g_note.bold) return 1;
    if (!ctx->skin_loaded) return 0;
    const slsk_entry_t *b = sl_skin_find(&ctx->skin, "ui-15-mid");
    const slsk_entry_t *t = sl_skin_find(&ctx->skin, "ui-14");
    if (!b || !t) return 0;
    g_note.bold = sl_skin_font(&ctx->skin, b);
    g_note.body = sl_skin_font(&ctx->skin, t);
    return 1;
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

    // One pill at a time, each for a few seconds.
    if (g_note.showing && now >= g_note.until_ns) {
        sl_note_dirty_pill(ctx);
        g_note.showing = 0u;
        g_note.pill_w = 0u;
    }
    if (!g_note.showing && g_note.count && sl_note_fonts(ctx)) {
        g_note.shown = g_note.queue[g_note.head];
        g_note.head = (g_note.head + 1u) % SL_NOTIFY_QUEUE;
        g_note.count--;
        g_note.showing = 1u;
        g_note.until_ns = now + SL_NOTIFY_SHOW_NS;
        uint32_t w = SL_NOTIFY_PAD + 10u + 12u + sl_text_width(g_note.bold, g_note.shown.source) +
                     sl_text_width(g_note.body, "  ·  ") + sl_text_width(g_note.body, g_note.shown.text) + SL_NOTIFY_PAD;
        uint32_t max_w = ctx->fb.width > 2u * SL_NOTIFY_RIGHT + 200u ? ctx->fb.width / 2u : ctx->fb.width;
        g_note.pill_w = w > max_w ? max_w : w;
        g_note.pill_x = ctx->fb.width > g_note.pill_w + SL_NOTIFY_RIGHT ? ctx->fb.width - g_note.pill_w - SL_NOTIFY_RIGHT : 0u;
        sl_note_dirty_pill(ctx);
    }
}

static void sl_note_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_target_t t = sl_target_from_ctx(ctx);
    // Always night, as in the design; a soft dark shadow reads on any wallpaper.
    sl_surf_style_t st = { 0x11, 0x16, 0x26, 1, { { 6, 6, 16, 0, 0x00, 0x00, 0x00, 110 } } };
    const sl_surf_t *s = sl_surf_get(&st, g_note.pill_w, SL_NOTIFY_H, SL_NOTIFY_H / 2u);
    if (s) {
        slsk_entry_t e;
        memset(&e, 0, sizeof(e));
        e.kind = SLSK_KIND_PREMUL;
        e.width = (uint16_t)s->w;
        e.height = (uint16_t)s->h;
        sl_blit_sprite(&t, &e, s->px, (int32_t)g_note.pill_x - (int32_t)s->pad_l, (int32_t)SL_NOTIFY_TOP - (int32_t)s->pad_t,
                       s->w, s->h);
    }
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    uint32_t cy = SL_NOTIFY_TOP + SL_NOTIFY_H / 2u;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, g_note.pill_x + SL_NOTIFY_PAD, cy - 5u, 10u, 10u, 5u);
    sl_target_t tt = t;
    int32_t right = (int32_t)(g_note.pill_x + g_note.pill_w - SL_NOTIFY_PAD);
    if (right < tt.clip_x1) tt.clip_x1 = right;
    int32_t base = (int32_t)cy + 5;
    int32_t px = (int32_t)(g_note.pill_x + SL_NOTIFY_PAD + 10u + 12u);
    px = sl_text_draw(&tt, g_note.bold, px, base, g_note.shown.source, n->r, n->g, n->b);
    px = sl_text_draw(&tt, g_note.body, px, base, "  ·  ", 0x9F, 0xB4, 0xD8);
    (void)sl_text_draw(&tt, g_note.body, px, base, g_note.shown.text, 0xE8, 0xF4, 0xFF);
    sl_clip_clear(ctx);
}

void sl_notify_render(sl_ctx_t *ctx) {
    if (!ctx || !g_note.showing || !g_note.pill_w) return;
    uint32_t x = g_note.pill_x > 24u ? g_note.pill_x - 24u : 0u;
    sl_dirty_for_each_intersection(x, SL_NOTIFY_TOP - 20u, g_note.pill_w + 48u, SL_NOTIFY_H + 48u, sl_note_draw_rect, ctx);
}
