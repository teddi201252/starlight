#include "desktop.h"

#include "starlight_internal.h"
#include "render/blit.h"
#include "render/pixels.h"
#include "render/surface.h"
#include "peos/net.h"
#include "ui_sdk.h"
#include "notify.h"
#include "peos/audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define SL_DESK_BAR_H SL_DESK_BAR_HEIGHT
#define SL_DESK_LEFT 172u            // widgets start right of the dock and its fin
#define SL_DESK_CLOCK_W 300u
#define SL_DESK_CLOCK_TOP 72u
#define SL_DESK_CAL_W 330u
#define SL_DESK_CAL_GAP 48u
#define SL_DESK_POLL_MS 2000u
// The widgets (tavola Desktop): the volume capsule and the CPU ring in a
// column right of the clock, the network card near the calendar.
#define SL_DESK_VOL_W 112u
#define SL_DESK_VOL_H 270u
#define SL_DESK_CPU_S 112u
#define SL_DESK_NET_W 340u
#define SL_DESK_NET_H 170u
#define SL_DESK_SHADOW 30               // what a widget's shadow reaches past it
#define SL_DESK_NET_BARS 7u

typedef struct { int32_t x, y; uint32_t w, h; } sl_desk_rect_t;

// The widgets (Soft UI plan, phase 10): each one also answers the mouse.
enum { SL_DW_CLOCK = 0, SL_DW_CAL, SL_DW_VOL, SL_DW_CPU, SL_DW_NET, SL_DW_COUNT };

// What the widgets show, as last drawn.
static struct {
    uint8_t ready;
    const slsk_font_t *bar, *bar_bold, *digits, *day, *date, *cal_title, *cal, *cal_bold;
    const slsk_entry_t *star, *wifi, *volume, *globe;
    const slsk_font_t *w_label, *w_value, *w_small, *mono, *mono_small;
    sl_desk_rect_t bar_r, clock_r, cal_r;
    sl_desk_rect_t vol_r, cpu_r, net_r;   // the widgets themselves (0 wide: no room)
    sl_desk_rect_t cal_card;              // the calendar's card, inside cal_r
    int8_t hot, pressed;                  // widget under the mouse / held down (SL_DW_*, -1 none)
    uint8_t dragging;                     // the pressed widget follows the mouse (the volume)
    int32_t local_volume;                 // set from the desktop, until the poller reads it back
    uint64_t local_volume_until;
    int32_t shown_volume, shown_cpu;      // last drawn
    uint32_t shown_net;                   // feed version of the network card
    uint8_t dark_bar, dark_clock;    // the wallpaper under them is dark: light text
    int64_t minute, day_no;          // last drawn
    const void *focus_task;
    char focus_title[64];
    uint32_t data_version;
} g_desk;

// Filled by the poller thread: netd and the audio config can be slow or
// absent, and the frame loop never waits for them.
static struct {
    volatile uint32_t version;       // odd while being written
    uint8_t ip[4];
    uint8_t has_ip;
    int32_t volume;                  // -1 unknown
    int32_t cpu;                     // load 0..100 over the last poll, -1 unknown
    uint64_t rx_rate, tx_rate;       // bytes a second over the last poll
    uint64_t hist[SL_DESK_NET_BARS]; // rx + tx a second, oldest first
    uint32_t net_version;            // bumped when the rates move
} g_feed;
static uint8_t g_poller_stack[16384] __attribute__((aligned(16)));
static uint8_t g_poller_started;

static const char *const k_days[7] = { "Domenica", "Lunedì", "Martedì", "Mercoledì", "Giovedì", "Venerdì", "Sabato" };
static const char *const k_months[12] = { "Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio",
                                          "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre" };
static const char *const k_week[7] = { "Lu", "Ma", "Me", "Gi", "Ve", "Sa", "Do" };

// ------------------------------------------------------------------ poller

// The poller's view of the last sample, to turn counters into rates.
static struct {
    uint8_t have;
    uint64_t busy, idle, rx, tx, ns;
} g_prev;

static void sl_desk_poller(void *arg) {
    (void)arg;
    for (;;) {
        peos_netcfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        uint8_t has_ip = (net_config_get(&cfg) == NET_OK && (cfg.ip[0] | cfg.ip[1] | cfg.ip[2] | cfg.ip[3])) ? 1u : 0u;
        // The kernel's volume and CPU time (no capability needed), netd's bytes.
        peos_sysinfo_t si;
        memset(&si, 0, sizeof(si));
        uint8_t have_si = peos_sysinfo(&si) >= 0;
        net_stats_t ns;
        memset(&ns, 0, sizeof(ns));
        uint8_t have_ns = net_stats(&ns) == NET_OK;
        uint64_t now = peos_time_ns();

        int32_t vol = have_si ? (int32_t)si.volume : -1;
        int32_t cpu = g_feed.cpu;
        uint64_t rx_rate = g_feed.rx_rate, tx_rate = g_feed.tx_rate;
        uint8_t net_moved = 0u;
        if (g_prev.have && have_si) {
            uint64_t db = si.cpu_busy_cycles - g_prev.busy, di = si.cpu_idle_cycles - g_prev.idle;
            cpu = (db + di) ? (int32_t)((db * 100u + (db + di) / 2u) / (db + di)) : 0;
        }
        if (g_prev.have && have_ns && now > g_prev.ns) {
            uint64_t dt = now - g_prev.ns;
            rx_rate = (ns.bytes_rx - g_prev.rx) * 1000000000ull / dt;
            tx_rate = (ns.bytes_tx - g_prev.tx) * 1000000000ull / dt;
            net_moved = 1u;
        }
        if (have_si) {
            g_prev.busy = si.cpu_busy_cycles;
            g_prev.idle = si.cpu_idle_cycles;
        }
        if (have_ns) {
            g_prev.rx = ns.bytes_rx;
            g_prev.tx = ns.bytes_tx;
            g_prev.ns = now;
        }
        g_prev.have = have_si || have_ns;

        if (has_ip != g_feed.has_ip || vol != g_feed.volume || memcmp(cfg.ip, g_feed.ip, 4) != 0 ||
            cpu != g_feed.cpu || net_moved) {
            __atomic_add_fetch(&g_feed.version, 1u, __ATOMIC_RELEASE);
            memcpy(g_feed.ip, cfg.ip, 4);
            g_feed.has_ip = has_ip;
            g_feed.volume = vol;
            g_feed.cpu = cpu;
            if (net_moved) {
                g_feed.rx_rate = rx_rate;
                g_feed.tx_rate = tx_rate;
                memmove(g_feed.hist, g_feed.hist + 1, sizeof(g_feed.hist) - sizeof(g_feed.hist[0]));
                g_feed.hist[SL_DESK_NET_BARS - 1u] = rx_rate + tx_rate;
                g_feed.net_version++;
            }
            __atomic_add_fetch(&g_feed.version, 1u, __ATOMIC_RELEASE);
        }
        peos_sleep(SL_DESK_POLL_MS);
    }
}

// A consistent copy of the feed; 0 if the poller is writing right now.
static int sl_desk_feed(uint8_t ip[4], uint8_t *has_ip, int32_t *volume, uint32_t *version) {
    uint32_t v0 = __atomic_load_n(&g_feed.version, __ATOMIC_ACQUIRE);
    if (v0 & 1u) return 0;
    memcpy(ip, g_feed.ip, 4);
    *has_ip = g_feed.has_ip;
    *volume = g_feed.volume;
    if (__atomic_load_n(&g_feed.version, __ATOMIC_ACQUIRE) != v0) return 0;
    *version = v0;
    if (g_desk.local_volume >= 0 && peos_time_ns() < g_desk.local_volume_until) *volume = g_desk.local_volume;
    return 1;
}

// The widgets' share of the feed, consistent like the above.
typedef struct {
    int32_t volume, cpu;
    uint64_t rx_rate, tx_rate, hist[SL_DESK_NET_BARS];
    uint32_t net_version;
    uint8_t ip[4], has_ip;
} sl_desk_widgets_feed_t;

static int sl_desk_widgets_feed(sl_desk_widgets_feed_t *out) {
    uint32_t v0 = __atomic_load_n(&g_feed.version, __ATOMIC_ACQUIRE);
    if (v0 & 1u) return 0;
    out->volume = g_feed.volume;
    out->cpu = g_feed.cpu;
    out->rx_rate = g_feed.rx_rate;
    out->tx_rate = g_feed.tx_rate;
    memcpy(out->hist, g_feed.hist, sizeof(out->hist));
    out->net_version = g_feed.net_version;
    memcpy(out->ip, g_feed.ip, 4);
    out->has_ip = g_feed.has_ip;
    if (g_desk.local_volume >= 0 && peos_time_ns() < g_desk.local_volume_until) out->volume = g_desk.local_volume;
    return __atomic_load_n(&g_feed.version, __ATOMIC_ACQUIRE) == v0;
}

// ------------------------------------------------------------------ setup

static const slsk_font_t *sl_desk_font(const sl_ctx_t *ctx, const char *name) {
    const slsk_entry_t *e = sl_skin_find(&ctx->skin, name);
    return e ? sl_skin_font(&ctx->skin, e) : NULL;
}

static int sl_desk_ready(sl_ctx_t *ctx) {
    if (g_desk.ready) return g_desk.ready == 1u;
    g_desk.ready = 2u;
    if (!ctx->skin_loaded) return 0;
    g_desk.bar = sl_desk_font(ctx, "ui-14");
    g_desk.bar_bold = sl_desk_font(ctx, "ui-15-mid");
    g_desk.digits = sl_desk_font(ctx, "clock-136");
    g_desk.day = sl_desk_font(ctx, "mono-32");
    g_desk.date = sl_desk_font(ctx, "mono-30-light");
    g_desk.cal_title = sl_desk_font(ctx, "mono-24");
    g_desk.cal = sl_desk_font(ctx, "mono-16");
    g_desk.cal_bold = sl_desk_font(ctx, "mono-16-bold");
    g_desk.star = sl_skin_find(&ctx->skin, "bar-star");
    g_desk.wifi = sl_skin_find(&ctx->skin, "bar-wifi");
    g_desk.volume = sl_skin_find(&ctx->skin, "bar-volume");
    g_desk.globe = sl_skin_find(&ctx->skin, "globe-40");
    g_desk.w_label = sl_desk_font(ctx, "ui-15");
    g_desk.w_value = sl_desk_font(ctx, "ui-22-mid");
    g_desk.w_small = sl_desk_font(ctx, "ui-14");
    g_desk.mono = sl_desk_font(ctx, "mono-16");
    g_desk.mono_small = sl_desk_font(ctx, "mono-13");
    if (!g_desk.bar || !g_desk.bar_bold || !g_desk.digits || !g_desk.day || !g_desk.date || !g_desk.cal_title ||
        !g_desk.cal || !g_desk.cal_bold || !g_desk.star || !g_desk.wifi || !g_desk.volume || !g_desk.globe ||
        !g_desk.w_label || !g_desk.w_value || !g_desk.w_small || !g_desk.mono || !g_desk.mono_small) {
        peos_puts("starlight: skin lacks the desktop widgets\n");
        return 0;
    }
    g_feed.volume = -1;
    g_feed.cpu = -1;
    g_desk.hot = g_desk.pressed = -1;
    g_desk.local_volume = -1;
    g_desk.ready = 1u;
    return 1;
}

static sl_target_t sl_desk_target(const sl_ctx_t *ctx, const sl_desk_rect_t *clip) {
    sl_target_t t;
    memset(&t, 0, sizeof(t));
    t.px = ctx->desk_frame;
    t.stride = ctx->bg_frame_stride;
    t.bpp = ctx->bytespp;
    t.clip_x0 = clip->x < 0 ? 0 : clip->x;
    t.clip_y0 = clip->y < 0 ? 0 : clip->y;
    t.clip_x1 = clip->x + (int32_t)clip->w;
    t.clip_y1 = clip->y + (int32_t)clip->h;
    if (t.clip_x1 > (int32_t)ctx->fb.width) t.clip_x1 = (int32_t)ctx->fb.width;
    if (t.clip_y1 > (int32_t)ctx->fb.height) t.clip_y1 = (int32_t)ctx->fb.height;
    return t;
}

// Average luminance of the background under r, 0..255.
static uint32_t sl_desk_luma(const sl_ctx_t *ctx, const sl_desk_rect_t *r) {
    uint64_t sum = 0u, n = 0u;
    for (uint32_t y = (uint32_t)r->y; y < (uint32_t)r->y + r->h && y < ctx->fb.height; y += 4u) {
        for (uint32_t x = (uint32_t)r->x; x < (uint32_t)r->x + r->w && x < ctx->fb.width; x += 4u) {
            const uint8_t *p = ctx->bg_frame + (size_t)y * ctx->bg_frame_stride + (size_t)x * ctx->bytespp;
            sum += (uint64_t)p[2] * 54u + (uint64_t)p[1] * 183u + (uint64_t)p[0] * 19u;
            n += 256u;
        }
    }
    return n ? (uint32_t)(sum / n) : 255u;
}

// Back to the plain background, then mark the region dirty on screen.
static void sl_desk_clear(sl_ctx_t *ctx, const sl_desk_rect_t *r) {
    int64_t x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int64_t x1 = (int64_t)r->x + r->w, y1 = (int64_t)r->y + r->h;
    if (x1 > (int64_t)ctx->fb.width) x1 = ctx->fb.width;
    if (y1 > (int64_t)ctx->fb.height) y1 = ctx->fb.height;
    if (x1 <= x0 || y1 <= y0) return;
    size_t off = (size_t)x0 * ctx->bytespp, bytes = (size_t)(x1 - x0) * ctx->bytespp;
    for (int64_t y = y0; y < y1; y++) {
        memcpy(ctx->desk_frame + (size_t)y * ctx->bg_frame_stride + off,
               ctx->bg_frame + (size_t)y * ctx->bg_frame_stride + off, bytes);
    }
    sl_dirty_add(ctx, (uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
}

static void sl_desk_layout(sl_ctx_t *ctx) {
    uint32_t W = ctx->fb.width, H = ctx->fb.height;
    g_desk.bar_r = (sl_desk_rect_t){ 0, 0, W, SL_DESK_BAR_H };
    g_desk.clock_r = (sl_desk_rect_t){ (int32_t)SL_DESK_LEFT, (int32_t)SL_DESK_CLOCK_TOP, SL_DESK_CLOCK_W, 380u };
    // The calendar under the clock as in the design, or beside it if the
    // screen is not tall enough (6 weeks at most: 330 x 320 with its shadow).
    uint32_t cal_h = 340u;
    if (H >= 484u + cal_h) g_desk.cal_r = (sl_desk_rect_t){ (int32_t)SL_DESK_LEFT - 20, 464, SL_DESK_CAL_W + 40u, cal_h };
    else g_desk.cal_r = (sl_desk_rect_t){ (int32_t)(SL_DESK_LEFT + SL_DESK_CLOCK_W + SL_DESK_CAL_GAP) - 20, 52,
                                          SL_DESK_CAL_W + 40u, cal_h };
    if (W < (uint32_t)g_desk.cal_r.x + g_desk.cal_r.w) g_desk.cal_r.w = 0u;   // no room: no calendar
    // The widgets as in the design (a column 60 px right of the clock, the
    // network card right of the calendar), or, with the calendar beside the
    // clock, the column right of the calendar and the card under it.
    uint8_t tall = H >= 484u + cal_h;
    int32_t col = tall ? (int32_t)(SL_DESK_LEFT + SL_DESK_CLOCK_W + 60u)
                       : g_desk.cal_r.x + (int32_t)g_desk.cal_r.w + 10;
    g_desk.vol_r = (sl_desk_rect_t){ col, 84, SL_DESK_VOL_W, SL_DESK_VOL_H };
    g_desk.cpu_r = (sl_desk_rect_t){ col, 394, SL_DESK_CPU_S, SL_DESK_CPU_S };
    g_desk.net_r = tall ? (sl_desk_rect_t){ (int32_t)(SL_DESK_LEFT + SL_DESK_CAL_W + 198u), 600, SL_DESK_NET_W, SL_DESK_NET_H }
                        : (sl_desk_rect_t){ g_desk.cal_r.x + 20, 430, SL_DESK_NET_W, SL_DESK_NET_H };
    sl_desk_rect_t *wr[3] = { &g_desk.vol_r, &g_desk.cpu_r, &g_desk.net_r };
    for (int i = 0; i < 3; i++) {   // a widget that does not fit (with its shadow) is left out
        if ((uint32_t)(wr[i]->x + (int32_t)wr[i]->w + SL_DESK_SHADOW) > W ||
            (uint32_t)(wr[i]->y + (int32_t)wr[i]->h + SL_DESK_SHADOW) > H) wr[i]->w = 0u;
    }
    if (!tall && g_desk.cal_r.w == 0u) g_desk.net_r.w = 0u;
    g_desk.shown_volume = g_desk.shown_cpu = -2;
    g_desk.shown_net = 0xFFFFFFFFu;
    g_desk.dark_bar = sl_desk_luma(ctx, &g_desk.bar_r) < 128u;
    g_desk.dark_clock = sl_desk_luma(ctx, &g_desk.clock_r) < 128u;
}

// ------------------------------------------------------------------ regions

static void sl_desk_colors(uint8_t dark, uint8_t ink[3], uint8_t muted[3]) {
    static const uint8_t light_ink[3] = { 0xEA, 0xF4, 0xFF }, light_muted[3] = { 0xB8, 0xC6, 0xE0 };
    static const uint8_t day_ink[3] = { 0x1A, 0x1D, 0x26 }, day_muted[3] = { 0x5A, 0x60, 0x6E };
    memcpy(ink, dark ? light_ink : day_ink, 3);
    memcpy(muted, dark ? light_muted : day_muted, 3);
}

static int32_t sl_desk_mask(const sl_ctx_t *ctx, sl_target_t *t, const slsk_entry_t *m, int32_t x, int32_t cy,
                            const uint8_t c[3]) {
    sl_blit_mask(t, sl_skin_data(&ctx->skin, m), m->width, m->height, m->width, x, cy - (int32_t)m->height / 2,
                 c[0], c[1], c[2], 255u);
    return x + (int32_t)m->width;
}

static void sl_desk_draw_bar(sl_ctx_t *ctx, const struct tm *now) {
    sl_desk_clear(ctx, &g_desk.bar_r);
    sl_target_t t = sl_desk_target(ctx, &g_desk.bar_r);
    uint8_t ink[3], muted[3];
    sl_desk_colors(g_desk.dark_bar, ink, muted);
    int32_t cy = (int32_t)SL_DESK_BAR_H / 2;
    int32_t text_base = cy + 5;

    // Left: the star, STARLIGHT, the app in front. Right of the dock when
    // the dock reaches up into the bar.
    int32_t x = ctx->fb.height < 800u ? (int32_t)SL_DESK_LEFT : 40;
    x = sl_desk_mask(ctx, &t, g_desk.star, x, cy, ink) + 14;
    x = sl_text_draw(&t, g_desk.bar_bold, x, text_base, "STARLIGHT", ink[0], ink[1], ink[2]) + 14;
    if (g_desk.focus_title[0]) sl_text_draw(&t, g_desk.bar, x, text_base, g_desk.focus_title, muted[0], muted[1], muted[2]);

    // Right, from the edge leftwards: the time, the volume, the address.
    char clock[8], vol[8], ip[20];
    snprintf(clock, sizeof(clock), "%02d:%02d", now->tm_hour, now->tm_min);
    int32_t r = (int32_t)ctx->fb.width - 40;
    r -= (int32_t)sl_text_width(g_desk.bar_bold, clock);
    sl_text_draw(&t, g_desk.bar_bold, r, text_base, clock, ink[0], ink[1], ink[2]);
    uint8_t addr[4] = { 0, 0, 0, 0 }, has_ip = 0u;
    int32_t volume = -1;
    uint32_t version = 0u;
    (void)sl_desk_feed(addr, &has_ip, &volume, &version);
    if (volume >= 0) {
        snprintf(vol, sizeof(vol), "%d", (int)volume);
        r -= 22 + (int32_t)sl_text_width(g_desk.bar, vol);
        sl_text_draw(&t, g_desk.bar, r, text_base, vol, ink[0], ink[1], ink[2]);
        r -= 8 + (int32_t)g_desk.volume->width;
        (void)sl_desk_mask(ctx, &t, g_desk.volume, r, cy, ink);
    }
    if (has_ip) snprintf(ip, sizeof(ip), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
    else snprintf(ip, sizeof(ip), "offline");
    r -= 22 + (int32_t)sl_text_width(g_desk.bar, ip);
    sl_text_draw(&t, g_desk.bar, r, text_base, ip, has_ip ? ink[0] : muted[0], has_ip ? ink[1] : muted[1],
                 has_ip ? ink[2] : muted[2]);
    r -= 8 + (int32_t)g_desk.wifi->width;
    (void)sl_desk_mask(ctx, &t, g_desk.wifi, r, cy, has_ip ? ink : muted);
}

// Digits stacked: hours over minutes, right-aligned; then day and date.
static void sl_desk_draw_clock(sl_ctx_t *ctx, const struct tm *now) {
    sl_desk_clear(ctx, &g_desk.clock_r);
    sl_target_t t = sl_desk_target(ctx, &g_desk.clock_r);
    uint8_t ink[3], muted[3];
    sl_desk_colors(g_desk.dark_clock, ink, muted);
    int32_t right = g_desk.clock_r.x + (int32_t)SL_DESK_CLOCK_W;
    char hh[4], mm[4], date[8];
    snprintf(hh, sizeof(hh), "%02d", now->tm_hour);
    snprintf(mm, sizeof(mm), "%02d", now->tm_min);
    snprintf(date, sizeof(date), "%02d/%02d", now->tm_mday, now->tm_mon + 1);
    int32_t base = g_desk.clock_r.y + 110;
    sl_text_draw(&t, g_desk.digits, right - (int32_t)sl_text_width(g_desk.digits, hh), base, hh, ink[0], ink[1], ink[2]);
    base += 122;
    sl_text_draw(&t, g_desk.digits, right - (int32_t)sl_text_width(g_desk.digits, mm), base, mm, ink[0], ink[1], ink[2]);
    const char *day = k_days[now->tm_wday % 7];
    base += 68;
    if (g_desk.hot == SL_DW_CLOCK) {   // under the mouse: the day in the accent
        const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
        sl_text_draw(&t, g_desk.day, right - (int32_t)sl_text_width(g_desk.day, day), base, day, n->r, n->g, n->b);
    } else {
        sl_text_draw(&t, g_desk.day, right - (int32_t)sl_text_width(g_desk.day, day), base, day, ink[0], ink[1], ink[2]);
    }
    base += 46;
    sl_text_draw(&t, g_desk.date, right - (int32_t)sl_text_width(g_desk.date, date), base, date, muted[0], muted[1],
                 muted[2]);
}

static void sl_desk_surface(sl_ctx_t *ctx, sl_target_t *t, const sl_surf_style_t *st, int32_t x, int32_t y, uint32_t w,
                            uint32_t h, uint32_t r) {
    const sl_surf_t *s = sl_surf_get(st, w, h, r);
    if (!s) return;
    slsk_entry_t e;
    memset(&e, 0, sizeof(e));
    e.kind = SLSK_KIND_PREMUL;
    e.width = (uint16_t)s->w;
    e.height = (uint16_t)s->h;
    (void)ctx;
    sl_blit_sprite(t, &e, s->px, x - (int32_t)s->pad_l, y - (int32_t)s->pad_t, s->w, s->h);
}

// A widget's look by its state: under the mouse it rises (raised ones) or
// lights up in the accent (night ones); held down it sinks.
static sl_surf_style_t sl_desk_night_card_style(int id) {
    if (g_desk.pressed == id) {
        sl_surf_style_t st = { 0x0D, 0x11, 0x1E, 1, { { 2, 2, 6, 0, 0x00, 0x00, 0x00, 90 } } };
        return st;
    }
    if (g_desk.hot == id) {
        const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
        sl_surf_style_t st = { 0x11, 0x16, 0x26, 2,
                               { { 0, 0, 14, 0, n->r, n->g, n->b, 120 }, { 8, 8, 20, 0, 0x00, 0x00, 0x00, 120 } } };
        return st;
    }
    sl_surf_style_t st = { 0x11, 0x16, 0x26, 1, { { 6, 6, 18, 0, 0x00, 0x00, 0x00, 110 } } };
    return st;
}

static int sl_desk_days_in(int month, int year) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[month];
}

// The month on a night card, weeks from Monday, today in neon.
static void sl_desk_draw_calendar(sl_ctx_t *ctx, const struct tm *now, const sl_desk_rect_t *clip) {
    if (g_desk.cal_r.w == 0u) return;
    sl_target_t t = sl_desk_target(ctx, clip);
    int year = now->tm_year + 1900, month = now->tm_mon;
    int first_wday = (now->tm_wday - (now->tm_mday - 1) % 7 + 7) % 7;   // weekday of the 1st, 0 = Sunday
    int lead = (first_wday + 6) % 7;                                       // blanks before it, weeks from Monday
    int days = sl_desk_days_in(month, year);
    int weeks = (lead + days + 6) / 7;

    int32_t x0 = g_desk.cal_r.x + 20, y0 = g_desk.cal_r.y + 20;
    uint32_t row = 32u, h = 24u + 30u + 14u + (uint32_t)(weeks + 1) * row - 12u + 26u;
    g_desk.cal_card = (sl_desk_rect_t){ x0, y0, SL_DESK_CAL_W, h };
    sl_surf_style_t card = sl_desk_night_card_style(SL_DW_CAL);
    sl_desk_surface(ctx, &t, &card, x0, y0, SL_DESK_CAL_W, h, 34u);

    static const uint8_t text[3] = { 0xE8, 0xF4, 0xFF }, head[3] = { 0x9F, 0xB4, 0xD8 };
    const char *title = k_months[month % 12];
    int32_t base = y0 + 24 + 22;
    sl_text_draw(&t, g_desk.cal_title, x0 + ((int32_t)SL_DESK_CAL_W - (int32_t)sl_text_width(g_desk.cal_title, title)) / 2,
                 base, title, text[0], text[1], text[2]);
    int32_t gx = x0 + 22, cw = ((int32_t)SL_DESK_CAL_W - 44) / 7;
    base += 14 + 26;
    for (int c = 0; c < 7; c++) {
        int32_t cx = gx + c * cw + cw / 2;
        sl_text_draw(&t, g_desk.cal, cx - (int32_t)sl_text_width(g_desk.cal, k_week[c]) / 2, base, k_week[c], head[0],
                     head[1], head[2]);
    }
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    sl_surf_style_t today = { n->r, n->g, n->b, 1, { { 0, 0, 10, 0, n->r, n->g, n->b, 160 } } };
    for (int d = 1; d <= days; d++) {
        int cell = lead + d - 1;
        int32_t cx = gx + (cell % 7) * cw + cw / 2;
        int32_t b = base + (int32_t)row * (1 + cell / 7);
        char num[4];
        snprintf(num, sizeof(num), "%d", d);
        int32_t tw = (int32_t)sl_text_width(d == now->tm_mday ? g_desk.cal_bold : g_desk.cal, num);
        if (d == now->tm_mday) {
            sl_desk_surface(ctx, &t, &today, cx - 15, b - 21, 30u, 30u, 15u);
            sl_text_draw(&t, g_desk.cal_bold, cx - tw / 2, b, num, 0x11, 0x16, 0x26);
        } else {
            sl_text_draw(&t, g_desk.cal, cx - tw / 2, b, num, text[0], text[1], text[2]);
        }
    }
}

// ------------------------------------------------------------------ widgets

// A widget's area with its shadow: what is cleared and redrawn.
static sl_desk_rect_t sl_desk_widget_area(const sl_desk_rect_t *r) {
    return (sl_desk_rect_t){ r->x - SL_DESK_SHADOW, r->y - SL_DESK_SHADOW, r->w + 2u * SL_DESK_SHADOW,
                             r->h + 2u * SL_DESK_SHADOW };
}

// The theme's raised (day pearl, night navy) or night surface.
static sl_surf_style_t sl_desk_raised_style(int id) {
    const sl_ui_color_t *bg = sl_ui_runtime_color(SL_UI_COLOR_BG_CANVAS);
    const sl_ui_color_t *dk = sl_ui_runtime_color(SL_UI_COLOR_SHADOW_DARK);
    const sl_ui_color_t *lt = sl_ui_runtime_color(SL_UI_COLOR_SHADOW_LIGHT);
    int8_t d = 9, blur = 20;
    if (g_desk.pressed == id) d = 3, blur = 8;          // sunk, nearly flat
    else if (g_desk.hot == id) d = 12, blur = 26;       // a little higher
    sl_surf_style_t st = { bg->r, bg->g, bg->b, 2,
                           { { d, d, (uint8_t)blur, 0, dk->r, dk->g, dk->b, 255 },
                             { (int8_t)-d, (int8_t)-d, (uint8_t)blur, 0, lt->r, lt->g, lt->b, 255 } } };
    return st;
}

static sl_surf_style_t sl_desk_night_style(int id) {
    if (g_desk.hot == id || g_desk.pressed == id) return sl_desk_night_card_style(id);
    const sl_ui_color_t *dk = sl_ui_runtime_color(SL_UI_COLOR_SHADOW_DARK);
    const sl_ui_color_t *lt = sl_ui_runtime_color(SL_UI_COLOR_SHADOW_LIGHT);
    sl_surf_style_t st = { 0x11, 0x16, 0x26, 2,
                           { { 9, 9, 20, 0, dk->r, dk->g, dk->b, 255 }, { -6, -6, 16, 0, lt->r, lt->g, lt->b, 255 } } };
    return st;
}

// What the widgets need of libm, which Starlight does not have: precise to
// far less than a pixel at their sizes.
static inline float sl_desk_sqrtf(float v) {
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(v));
    return r;
}

static float sl_desk_sinf(float x) {
    const float PI = 3.14159265f;
    while (x > PI) x -= 2.0f * PI;
    while (x < -PI) x += 2.0f * PI;
    if (x > PI / 2.0f) x = PI - x;           // sin(x) = sin(pi - x)
    else if (x < -PI / 2.0f) x = -PI - x;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f)));
}

static float sl_desk_atan2f(float y, float x) {
    const float PI = 3.14159265f;
    float ax = fabsf(x), ay = fabsf(y);
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    float z = ax > ay ? ay / ax : ax / ay;
    float a = z * (PI / 4.0f) + 0.273f * z * (1.0f - z);   // atan(z) on [0, 1]
    if (ay > ax) a = PI / 2.0f - a;
    if (x < 0.0f) a = PI - a;
    return y < 0.0f ? -a : a;
}

static inline float sl_desk_clampf(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// The region being repainted: the widgets' own pixels stay inside it.
static sl_desk_rect_t g_desk_clip;

// Blend colour c with coverage a (0..1) over the desk layer at x,y.
static inline void sl_desk_put(sl_ctx_t *ctx, int32_t x, int32_t y, const uint8_t c[3], float a) {
    if (a <= 0.0f || x < 0 || y < 0 || (uint32_t)x >= ctx->fb.width || (uint32_t)y >= ctx->fb.height) return;
    if (x < g_desk_clip.x || y < g_desk_clip.y || x >= g_desk_clip.x + (int32_t)g_desk_clip.w ||
        y >= g_desk_clip.y + (int32_t)g_desk_clip.h) return;
    uint8_t *p = ctx->desk_frame + (size_t)y * ctx->bg_frame_stride + (size_t)x * ctx->bytespp;
    uint32_t k = (uint32_t)(a * 256.0f + 0.5f);
    if (k > 256u) k = 256u;
    p[0] = (uint8_t)((p[0] * (256u - k) + c[2] * k) >> 8);
    p[1] = (uint8_t)((p[1] * (256u - k) + c[1] * k) >> 8);
    p[2] = (uint8_t)((p[2] * (256u - k) + c[0] * k) >> 8);
}

static void sl_desk_text_centered(sl_target_t *t, const slsk_font_t *f, int32_t cx, int32_t base, const char *s,
                                  const uint8_t c[3]) {
    sl_text_draw(t, f, cx - (int32_t)sl_text_width(f, s) / 2, base, s, c[0], c[1], c[2]);
}

// Blend colour c with 8-bit coverage k over the desk layer at x,y.
static inline void sl_desk_put8(sl_ctx_t *ctx, int32_t x, int32_t y, const uint8_t c[3], uint32_t k) {
    if (k == 0u) return;
    if (x < g_desk_clip.x || y < g_desk_clip.y || x >= g_desk_clip.x + (int32_t)g_desk_clip.w ||
        y >= g_desk_clip.y + (int32_t)g_desk_clip.h) return;
    if (x < 0 || y < 0 || (uint32_t)x >= ctx->fb.width || (uint32_t)y >= ctx->fb.height) return;
    uint8_t *p = ctx->desk_frame + (size_t)y * ctx->bg_frame_stride + (size_t)x * ctx->bytespp;
    k += k >> 7;   // 0..256
    p[0] = (uint8_t)((p[0] * (256u - k) + c[2] * k) >> 8);
    p[1] = (uint8_t)((p[1] * (256u - k) + c[1] * k) >> 8);
    p[2] = (uint8_t)((p[2] * (256u - k) + c[0] * k) >> 8);
}

// The widgets' shapes, worked out once: the inner capsule's coverage, and
// the CPU ring's coverage and the angle of each of its pixels.
#define SL_DESK_CAP_W 84
#define SL_DESK_CAP_H 240
#define SL_DESK_RING 88
static uint8_t g_cap_mask[SL_DESK_CAP_H][SL_DESK_CAP_W];
static uint8_t g_ring_mask[SL_DESK_RING][SL_DESK_RING];
static uint16_t g_ring_angle[SL_DESK_RING][SL_DESK_RING];   // 0..65535 for 0..2pi, clockwise from the top
static uint8_t g_shapes_ready;

static void sl_desk_shapes(void) {
    if (g_shapes_ready) return;
    g_shapes_ready = 1u;
    const float R = (float)SL_DESK_CAP_W / 2.0f;
    for (int32_t y = 0; y < SL_DESK_CAP_H; y++) {
        for (int32_t x = 0; x < SL_DESK_CAP_W; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float sy = py < R ? R : (py > (float)SL_DESK_CAP_H - R ? (float)SL_DESK_CAP_H - R : py);
            float d = sl_desk_sqrtf((px - R) * (px - R) + (py - sy) * (py - sy)) - R;
            g_cap_mask[y][x] = (uint8_t)(sl_desk_clampf(0.5f - d) * 255.0f + 0.5f);
        }
    }
    const float C = (float)SL_DESK_RING / 2.0f, RR = 36.0f, half = 3.5f, PI2 = 6.2831853f;
    for (int32_t y = 0; y < SL_DESK_RING; y++) {
        for (int32_t x = 0; x < SL_DESK_RING; x++) {
            float dx = (float)x + 0.5f - C, dy = (float)y + 0.5f - C;
            float dist = sl_desk_sqrtf(dx * dx + dy * dy);
            g_ring_mask[y][x] = (uint8_t)(sl_desk_clampf(half - fabsf(dist - RR) + 0.5f) * 255.0f + 0.5f);
            float ang = sl_desk_atan2f(dx, -dy);
            if (ang < 0.0f) ang += PI2;
            g_ring_angle[y][x] = (uint16_t)(ang / PI2 * 65535.0f);
        }
    }
}

// The volume: a raised capsule holding a night one, filled from the bottom
// by a wave up to the level, the value in neon (tavola Desktop).
static void sl_desk_draw_volume(sl_ctx_t *ctx, int32_t volume, const sl_desk_rect_t *clip) {
    const sl_desk_rect_t *r = &g_desk.vol_r;
    if (r->w == 0u) return;
    sl_desk_shapes();
    sl_target_t t = sl_desk_target(ctx, clip);
    sl_surf_style_t raised = sl_desk_raised_style(SL_DW_VOL);
    sl_desk_surface(ctx, &t, &raised, r->x, r->y, r->w, r->h, r->w / 2u);

    // The night capsule, then the wave over it below its edge (smooth on
    // the edge row), both through the capsule's coverage.
    const int32_t ix = r->x + 14, iy = r->y + 15;
    static const uint8_t night[3] = { 0x11, 0x16, 0x26 }, wave[3] = { 0x1C, 0x24, 0x40 };
    float level = volume < 0 ? 0.0f : (float)volume / 100.0f;
    // The wave's mean height: from just under the bottom to past the top, so
    // that a full volume fills the capsule, crests included.
    float top = (float)SL_DESK_CAP_H - 8.0f - level * (float)(SL_DESK_CAP_H + 8);
    for (int32_t x = 0; x < SL_DESK_CAP_W; x++) {
        float wy = top + 8.0f * sl_desk_sinf(((float)x + 0.5f) / (float)SL_DESK_CAP_W * 6.2831853f * 1.1f + 0.6f);
        int32_t edge = volume > 0 ? (int32_t)wy : SL_DESK_CAP_H;
        uint32_t frac = (uint32_t)((wy - (float)(int32_t)wy) * 255.0f);   // the edge row's share of night
        for (int32_t y = 0; y < SL_DESK_CAP_H; y++) {
            uint32_t k = g_cap_mask[y][x];
            if (!k) continue;
            sl_desk_put8(ctx, ix + x, iy + y, night, k);
            if (y > edge) sl_desk_put8(ctx, ix + x, iy + y, wave, k);
            else if (y == edge) sl_desk_put8(ctx, ix + x, iy + y, wave, k * (255u - frac) / 255u);
        }
    }
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    uint8_t neon[3] = { n->r, n->g, n->b };
    char val[8];
    if (volume >= 0) snprintf(val, sizeof(val), "%d%%", (int)volume);
    else snprintf(val, sizeof(val), "--");
    int32_t cx = r->x + (int32_t)r->w / 2, base = iy + SL_DESK_CAP_H - 36;
    sl_desk_text_centered(&t, g_desk.w_value, cx, base, val, neon);
    sl_desk_text_centered(&t, g_desk.w_label, cx, base - 28, "Volume", neon);
}

// A round end of the arc: an anti-aliased disc of radius 3.5 at cx,cy.
static void sl_desk_cap(sl_ctx_t *ctx, int32_t ox, int32_t oy, float cx, float cy, const uint8_t c[3]) {
    for (int32_t y = (int32_t)(cy - 5.0f); y <= (int32_t)(cy + 5.0f); y++) {
        for (int32_t x = (int32_t)(cx - 5.0f); x <= (int32_t)(cx + 5.0f); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float cov = sl_desk_clampf(3.5f - sl_desk_sqrtf(dx * dx + dy * dy) + 0.5f);
            if (cov > 0.0f) sl_desk_put(ctx, ox + x, oy + y, c, cov);
        }
    }
}

// The CPU: a ring on a raised square, the load as a neon arc from the top.
static void sl_desk_draw_cpu(sl_ctx_t *ctx, int32_t cpu, const sl_desk_rect_t *clip) {
    const sl_desk_rect_t *r = &g_desk.cpu_r;
    if (r->w == 0u) return;
    sl_desk_shapes();
    sl_target_t t = sl_desk_target(ctx, clip);
    sl_surf_style_t raised = sl_desk_raised_style(SL_DW_CPU);
    sl_desk_surface(ctx, &t, &raised, r->x, r->y, r->w, r->h, 30u);

    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    const sl_ui_color_t *lt = sl_ui_runtime_color(SL_UI_COLOR_SHADOW_LIGHT);
    uint8_t neon[3] = { n->r, n->g, n->b };
    uint8_t track[3] = { (uint8_t)((lt->r * 3u + 0xE3u) / 4u), (uint8_t)((lt->g * 3u + 0xE6u) / 4u),
                         (uint8_t)((lt->b * 3u + 0xECu) / 4u) };   // #F4F5F8 by day
    uint32_t end = cpu <= 0 ? 0u : (uint32_t)cpu * 65535u / 100u;
    int32_t ox = r->x + 12, oy = r->y + 12;
    for (int32_t y = 0; y < SL_DESK_RING; y++) {
        for (int32_t x = 0; x < SL_DESK_RING; x++) {
            uint32_t k = g_ring_mask[y][x];
            if (!k) continue;
            sl_desk_put8(ctx, ox + x, oy + y, track, k);
            if (g_ring_angle[y][x] < end) sl_desk_put8(ctx, ox + x, oy + y, neon, k);
        }
    }
    if (end) {   // round ends, as stroke-linecap round
        const float C = (float)SL_DESK_RING / 2.0f, a = (float)end / 65535.0f * 6.2831853f;
        sl_desk_cap(ctx, ox, oy, C, C - 36.0f, neon);
        sl_desk_cap(ctx, ox, oy, C + 36.0f * sl_desk_sinf(a), C - 36.0f * sl_desk_sinf(a + 1.5707963f), neon);
    }
    const sl_ui_color_t *ink = sl_ui_runtime_color(SL_UI_COLOR_TEXT_PRIMARY);
    const sl_ui_color_t *mu = sl_ui_runtime_color(SL_UI_COLOR_TEXT_SECONDARY);
    uint8_t ic[3] = { ink->r, ink->g, ink->b }, mc[3] = { mu->r, mu->g, mu->b };
    char val[8];
    if (cpu >= 0) snprintf(val, sizeof(val), "%d%%", (int)cpu);
    else snprintf(val, sizeof(val), "--");
    sl_desk_text_centered(&t, g_desk.mono, ox + 44, oy + 47, val, ic);
    sl_desk_text_centered(&t, g_desk.mono_small, ox + 44, oy + 62, "CPU", mc);
}

// "6,9 MB/s": one decimal under ten, comma as the decimal point.
static void sl_desk_rate(char *out, size_t cap, const char *arrow, uint64_t bps) {
    static const char *const unit[] = { "B/s", "KB/s", "MB/s", "GB/s" };
    uint64_t v10 = bps * 10u;
    int u = 0;
    while (v10 >= 10240u && u < 3) {
        v10 /= 1024u;
        u++;
    }
    if (v10 < 100u && u > 0) snprintf(out, cap, "%s %u,%u %s", arrow, (unsigned)(v10 / 10u), (unsigned)(v10 % 10u), unit[u]);
    else snprintf(out, cap, "%s %u %s", arrow, (unsigned)(v10 / 10u), unit[u]);
}

// The network: a night card with netd's address, the traffic of the last
// polls as bars, and the rates now.
static void sl_desk_draw_net(sl_ctx_t *ctx, const sl_desk_widgets_feed_t *f, const sl_desk_rect_t *clip) {
    const sl_desk_rect_t *r = &g_desk.net_r;
    if (r->w == 0u) return;
    sl_target_t t = sl_desk_target(ctx, clip);
    sl_surf_style_t night = sl_desk_night_style(SL_DW_NET);
    sl_desk_surface(ctx, &t, &night, r->x, r->y, r->w, r->h, 40u);

    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    static const uint8_t head[3] = { 0x9F, 0xB4, 0xD8 }, text[3] = { 0xE8, 0xF4, 0xFF };
    int32_t x0 = r->x + 30, y0 = r->y + 24;
    const slsk_entry_t *g = g_desk.globe;
    sl_blit_mask(&t, sl_skin_data(&ctx->skin, g), g->width, g->height, g->width, x0, y0, n->r, n->g, n->b, 255u);
    char ip[20];
    if (f->has_ip) snprintf(ip, sizeof(ip), "%u.%u.%u.%u", f->ip[0], f->ip[1], f->ip[2], f->ip[3]);
    else snprintf(ip, sizeof(ip), "offline");
    sl_text_draw(&t, g_desk.w_small, x0 + 56, y0 + 15, "Rete \xC2\xB7 netd", head[0], head[1], head[2]);
    sl_text_draw(&t, g_desk.w_value, x0 + 56, y0 + 40, ip, text[0], text[1], text[2]);

    // Bars: the busiest poll is full height; older ones dimmer.
    uint64_t peak = 1u;
    for (uint32_t i = 0; i < SL_DESK_NET_BARS; i++) if (f->hist[i] > peak) peak = f->hist[i];
    int32_t bottom = r->y + (int32_t)r->h - 24;
    for (uint32_t i = 0; i < SL_DESK_NET_BARS; i++) {
        uint32_t h = 6u + (uint32_t)((f->hist[i] * 34u) / peak);
        uint32_t k = i < 3u ? 110u + i * 40u : 255u;   // dimmer to the left
        sl_surf_style_t bar = { (uint8_t)((n->r * k + 0x11u * (255u - k)) / 255u),
                                (uint8_t)((n->g * k + 0x16u * (255u - k)) / 255u),
                                (uint8_t)((n->b * k + 0x26u * (255u - k)) / 255u), 0, { { 0 } } };
        sl_desk_surface(ctx, &t, &bar, x0 + (int32_t)(i * 11u), bottom - (int32_t)h, 6u, h, 3u);
    }
    char down[32], up[32];
    sl_desk_rate(down, sizeof(down), "\xE2\x86\x93", f->rx_rate);
    sl_desk_rate(up, sizeof(up), "\xE2\x86\x91", f->tx_rate);
    int32_t right = r->x + (int32_t)r->w - 30;
    sl_text_draw(&t, g_desk.w_label, right - (int32_t)sl_text_width(g_desk.w_label, down), bottom - 22, down,
                 text[0], text[1], text[2]);
    sl_text_draw(&t, g_desk.w_label, right - (int32_t)sl_text_width(g_desk.w_label, up), bottom, up,
                 text[0], text[1], text[2]);
}

static int sl_desk_overlap(const sl_desk_rect_t *a, const sl_desk_rect_t *b) {
    return a->w && b->w && a->x < b->x + (int32_t)b->w && b->x < a->x + (int32_t)a->w &&
           a->y < b->y + (int32_t)b->h && b->y < a->y + (int32_t)a->h;
}

// Repaint a region of the layer: back to the wallpaper, then every card
// that reaches into it, bottom to top, clipped to it. Cards' shadows overlap
// (the design puts them 30-40 px apart): repainting one must not cut the
// shadow of its neighbour.
static void sl_desk_paint(sl_ctx_t *ctx, const sl_desk_rect_t *area, const struct tm *now,
                          const sl_desk_widgets_feed_t *f) {
    if (area->w == 0u) return;
    sl_desk_clear(ctx, area);
    g_desk_clip = *area;
    sl_desk_rect_t cal = g_desk.cal_r, vol = sl_desk_widget_area(&g_desk.vol_r);
    sl_desk_rect_t cpu = sl_desk_widget_area(&g_desk.cpu_r), net = sl_desk_widget_area(&g_desk.net_r);
    if (g_desk.vol_r.w == 0u) vol.w = 0u;
    if (g_desk.cpu_r.w == 0u) cpu.w = 0u;
    if (g_desk.net_r.w == 0u) net.w = 0u;
    if (sl_desk_overlap(area, &cal)) sl_desk_draw_calendar(ctx, now, area);
    if (sl_desk_overlap(area, &vol)) sl_desk_draw_volume(ctx, f->volume, area);
    if (sl_desk_overlap(area, &cpu)) sl_desk_draw_cpu(ctx, f->cpu, area);
    if (sl_desk_overlap(area, &net)) sl_desk_draw_net(ctx, f, area);
}

// Repaint the widgets whose value moved since they were drawn (force: all).
static void sl_desk_draw_widgets(sl_ctx_t *ctx, const struct tm *now, uint8_t force) {
    sl_desk_widgets_feed_t f;
    if (!sl_desk_widgets_feed(&f)) {
        if (!force) return;
        memset(&f, 0, sizeof(f));
        f.volume = g_feed.volume;
        f.cpu = g_feed.cpu;
    }
    uint8_t vol = force || f.volume != g_desk.shown_volume;
    uint8_t cpu = force || f.cpu != g_desk.shown_cpu;
    uint8_t net = force || f.net_version != g_desk.shown_net;
    g_desk.shown_volume = f.volume;
    g_desk.shown_cpu = f.cpu;
    g_desk.shown_net = f.net_version;
    sl_desk_rect_t a;
    if (vol && g_desk.vol_r.w) { a = sl_desk_widget_area(&g_desk.vol_r); sl_desk_paint(ctx, &a, now, &f); }
    if (cpu && g_desk.cpu_r.w) { a = sl_desk_widget_area(&g_desk.cpu_r); sl_desk_paint(ctx, &a, now, &f); }
    if (net && g_desk.net_r.w) { a = sl_desk_widget_area(&g_desk.net_r); sl_desk_paint(ctx, &a, now, &f); }
}

// The calendar's region, with whatever widgets reach into it.
static void sl_desk_draw_calendar_region(sl_ctx_t *ctx, const struct tm *now) {
    sl_desk_widgets_feed_t f;
    if (!sl_desk_widgets_feed(&f)) {
        memset(&f, 0, sizeof(f));
        f.volume = g_desk.shown_volume;
        f.cpu = g_desk.shown_cpu;
    }
    sl_desk_paint(ctx, &g_desk.cal_r, now, &f);
}

// ------------------------------------------------------------------ public

// Seconds of the last Sunday of a month (March or October) at 01:00 UTC.
static int64_t sl_desk_last_sunday_0100(int year, int month) {
    // Days from 1970-01-01 to the 1st of the next month (civil from days).
    int y = year, m = month + 2;   // month is 0-based; m is the next one, 1-based
    if (m > 12) {
        m = 1;
        y++;
    }
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468 - 1;      // the last day of the month
    int64_t wday = (days + 4) % 7;                        // 0 = Sunday
    if (wday < 0) wday += 7;
    return (days - wday) * 86400 + 3600;
}

// Local time: UTC, or Central European with summer time (EU rule).
static int64_t sl_desk_offset(const sl_ctx_t *ctx, int64_t utc) {
    if (!ctx->timezone_eu_central) return 0;
    time_t t = (time_t)utc;
    int year = localtime(&t)->tm_year + 1900;
    int64_t start = sl_desk_last_sunday_0100(year, 2), end = sl_desk_last_sunday_0100(year, 9);
    return (utc >= start && utc < end) ? 7200 : 3600;
}

static int64_t sl_desk_local_seconds(const sl_ctx_t *ctx) {
    int64_t utc = (int64_t)time(NULL);
    return utc + sl_desk_offset(ctx, utc);
}

static void sl_desk_now(const sl_ctx_t *ctx, struct tm *out) {
    time_t local = (time_t)sl_desk_local_seconds(ctx);
    *out = *localtime(&local);
}

void sl_desk_local_hhmm(const sl_ctx_t *ctx, char *out, size_t cap) {
    struct tm now;
    sl_desk_now(ctx, &now);
    snprintf(out, cap, "%02d:%02d", now.tm_hour, now.tm_min);
}

void sl_desk_build(sl_ctx_t *ctx) {
    if (!ctx || !ctx->bg_frame || !sl_desk_ready(ctx)) return;
    size_t bytes = ctx->bg_frame_stride * ctx->fb.height;
    if (!ctx->desk_frame) {
        ctx->desk_frame = (uint8_t *)malloc(bytes);
        if (!ctx->desk_frame) return;
    }
    memcpy(ctx->desk_frame, ctx->bg_frame, bytes);
    if (!g_poller_started) {
        g_poller_started = 1u;
        (void)peos_thread_spawn(sl_desk_poller, NULL, g_poller_stack + sizeof(g_poller_stack));
    }
    sl_desk_layout(ctx);
    struct tm now;
    sl_desk_now(ctx, &now);
    g_desk.minute = sl_desk_local_seconds(ctx) / 60;
    g_desk.day_no = g_desk.minute / 1440;
    sl_desk_draw_bar(ctx, &now);
    sl_desk_draw_clock(ctx, &now);
    sl_desk_draw_calendar_region(ctx, &now);
    sl_desk_draw_widgets(ctx, &now, 1u);
}

void sl_desk_tick(sl_ctx_t *ctx) {
    if (!ctx || !ctx->desk_frame || g_desk.ready != 1u) return;
    int64_t minute = sl_desk_local_seconds(ctx) / 60;
    uint8_t ip[4], has_ip;
    int32_t volume;
    uint32_t version = g_desk.data_version;
    (void)sl_desk_feed(ip, &has_ip, &volume, &version);
    sl_window_t *focused = sl_window_get_focused();
    const void *task = (focused && !focused->minimized) ? (const void *)focused->task : NULL;

    uint8_t bar = 0u;
    if (task != g_desk.focus_task) {
        g_desk.focus_task = task;
        snprintf(g_desk.focus_title, sizeof(g_desk.focus_title), "%s", task ? focused->title : "");
        bar = 1u;
    }
    uint8_t widgets = 0u;
    if (version != g_desk.data_version) {
        g_desk.data_version = version;
        bar = 1u;
        widgets = 1u;
    }
    if (minute == g_desk.minute && !bar) return;   // nothing changed: no work

    struct tm now;
    sl_desk_now(ctx, &now);
    if (widgets) sl_desk_draw_widgets(ctx, &now, 0u);
    sl_desk_draw_bar(ctx, &now);
    if (minute != g_desk.minute) {
        g_desk.minute = minute;
        sl_desk_draw_clock(ctx, &now);
        if (minute / 1440 != g_desk.day_no) {
            g_desk.day_no = minute / 1440;
            sl_desk_draw_calendar_region(ctx, &now);
        }
    }
}

void sl_desk_free(sl_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->desk_frame);
    ctx->desk_frame = NULL;
}

// ------------------------------------------------------------- the mouse

// The widgets' own reactions; anything else a click does is its shortcut.
static void sl_desk_volume_at(sl_ctx_t *ctx, int32_t y);
static void sl_desk_volume_save(void);
static void sl_desk_volume_step(sl_ctx_t *ctx, int32_t delta);
static void sl_desk_clock_click(sl_ctx_t *ctx);

typedef struct {
    const char *name;
    // Pressed: may start a drag (1). Dragged: follows the mouse. Released.
    uint8_t (*press)(sl_ctx_t *ctx, int32_t x, int32_t y);
    void (*drag)(sl_ctx_t *ctx, int32_t x, int32_t y);
    void (*release)(sl_ctx_t *ctx);
    void (*wheel)(sl_ctx_t *ctx, int32_t delta);
    // A click: this function, or else open the app with this title (a
    // notification says so when it is not installed yet).
    void (*click)(sl_ctx_t *ctx);
    const char *app;
} sl_desk_widget_t;

static uint8_t sl_desk_volume_press(sl_ctx_t *ctx, int32_t x, int32_t y) {
    (void)x;
    sl_desk_volume_at(ctx, y);
    return 1u;
}

static void sl_desk_volume_drag(sl_ctx_t *ctx, int32_t x, int32_t y) {
    (void)x;
    sl_desk_volume_at(ctx, y);
}

static void sl_desk_volume_release(sl_ctx_t *ctx) {
    (void)ctx;
    sl_desk_volume_save();
}

// The registry: a new widget is a row here (and its rectangle and drawing).
static const sl_desk_widget_t k_widgets[SL_DW_COUNT] = {
    [SL_DW_CLOCK] = { "orologio", NULL, NULL, NULL, NULL, sl_desk_clock_click, NULL },
    [SL_DW_CAL] = { "calendario", NULL, NULL, NULL, NULL, NULL, "Calendario" },
    [SL_DW_VOL] = { "volume", sl_desk_volume_press, sl_desk_volume_drag, sl_desk_volume_release, sl_desk_volume_step,
                    NULL, NULL },
    [SL_DW_CPU] = { "cpu", NULL, NULL, NULL, NULL, NULL, "Monitor" },
    [SL_DW_NET] = { "rete", NULL, NULL, NULL, NULL, NULL, "Impostazioni" },
};

static const sl_desk_rect_t *sl_desk_widget_rect(int id) {
    switch (id) {
    case SL_DW_CLOCK: return &g_desk.clock_r;
    case SL_DW_CAL: return g_desk.cal_r.w ? &g_desk.cal_card : &g_desk.cal_r;
    case SL_DW_VOL: return &g_desk.vol_r;
    case SL_DW_CPU: return &g_desk.cpu_r;
    case SL_DW_NET: return &g_desk.net_r;
    default: return NULL;
    }
}

static int sl_desk_widget_at(int32_t x, int32_t y) {
    for (int id = SL_DW_COUNT - 1; id >= 0; id--) {
        const sl_desk_rect_t *r = sl_desk_widget_rect(id);
        if (!r || r->w == 0u || r->h == 0u) continue;
        if (x >= r->x && y >= r->y && x < r->x + (int32_t)r->w && y < r->y + (int32_t)r->h) return id;
    }
    return -1;
}

// Draw a widget again in its region (its state or its value changed).
static void sl_desk_repaint(sl_ctx_t *ctx, int id) {
    if (id < 0 || !ctx->desk_frame || g_desk.ready != 1u) return;
    struct tm now;
    sl_desk_now(ctx, &now);
    if (id == SL_DW_CLOCK) {
        sl_desk_draw_clock(ctx, &now);
        return;
    }
    sl_desk_widgets_feed_t f;
    if (!sl_desk_widgets_feed(&f)) {
        memset(&f, 0, sizeof(f));
        f.volume = g_desk.shown_volume;
        f.cpu = g_desk.shown_cpu;
    }
    if (id == SL_DW_VOL) g_desk.shown_volume = f.volume;
    sl_desk_rect_t area = id == SL_DW_CAL ? g_desk.cal_r : sl_desk_widget_area(sl_desk_widget_rect(id));
    sl_desk_paint(ctx, &area, &now, &f);
}

static void sl_desk_set_state(sl_ctx_t *ctx, int hot, int pressed) {
    int old_hot = g_desk.hot, old_pressed = g_desk.pressed;
    if (old_hot == hot && old_pressed == pressed) return;
    g_desk.hot = (int8_t)hot;
    g_desk.pressed = (int8_t)pressed;
    int ids[4] = { old_hot, old_pressed, hot, pressed };
    for (int i = 0; i < 4; i++) {
        int dup = 0;
        for (int k = 0; k < i; k++) dup |= ids[k] == ids[i];
        if (!dup) sl_desk_repaint(ctx, ids[i]);
    }
}

// The volume from the mouse's height on the inner capsule (bottom 0, top
// 100); set in the kernel at once, shown at once.
static void sl_desk_set_volume(sl_ctx_t *ctx, int32_t v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (audio_set_volume((uint32_t)v) != 0) {
        sl_notify_post(ctx, "Starlight", "Volume: Starlight non ha il permesso audio");
        return;
    }
    if (v == g_desk.local_volume && peos_time_ns() < g_desk.local_volume_until) return;
    g_desk.local_volume = v;
    g_desk.local_volume_until = peos_time_ns() + 3u * SL_DESK_POLL_MS * 1000000ull;   // the poller catches up
    sl_desk_repaint(ctx, SL_DW_VOL);
    struct tm now;
    sl_desk_now(ctx, &now);
    sl_desk_draw_bar(ctx, &now);   // the number in the bar too
}

static void sl_desk_volume_at(sl_ctx_t *ctx, int32_t y) {
    int32_t bottom = g_desk.vol_r.y + 15 + SL_DESK_CAP_H;
    sl_desk_set_volume(ctx, (bottom - y) * 100 / SL_DESK_CAP_H);
}

static int32_t sl_desk_volume_now(void) {
    if (g_desk.local_volume >= 0 && peos_time_ns() < g_desk.local_volume_until) return g_desk.local_volume;
    return g_feed.volume < 0 ? 0 : g_feed.volume;
}

static void sl_desk_volume_step(sl_ctx_t *ctx, int32_t delta) {
    sl_desk_set_volume(ctx, sl_desk_volume_now() + delta * 5);
    sl_desk_volume_save();
}

// As setvolume does: the level survives a restart (where the disk can be
// written: not on the live ISO).
static void sl_desk_volume_save(void) {
    static const char dir1[] = "/system", dir2[] = "/system/config", path[] = "/system/config/audio.cfg";
    (void)peos_mkdir(dir1, sizeof(dir1) - 1u);
    (void)peos_mkdir(dir2, sizeof(dir2) - 1u);
    uint64_t fd = peos_fopen(path, sizeof(path) - 1u, PEOS_O_CREAT | PEOS_O_TRUNC | PEOS_O_WRITE);
    if ((int64_t)fd < 0) return;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "# PeOS global audio config\nvolume=%d\n", (int)sl_desk_volume_now());
    if (n > 0) (void)peos_fwrite(fd, buf, (size_t)n);
    (void)peos_fclose(fd);
}

static void sl_desk_clock_click(sl_ctx_t *ctx) {
    sl_notify_toggle_center(ctx);
}

static void sl_desk_activate(sl_ctx_t *ctx, int id) {
    const sl_desk_widget_t *w = &k_widgets[id];
    if (w->click) {
        w->click(ctx);
        return;
    }
    if (!w->app) return;
    if (!sl_windows_open_app_title(ctx, w->app)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s non e' ancora installata", w->app);
        sl_notify_post(ctx, "Starlight", msg);
    }
}

void sl_desk_hover(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint8_t covered) {
    if (!ctx || g_desk.ready != 1u || g_desk.pressed >= 0) return;
    sl_desk_set_state(ctx, covered ? -1 : sl_desk_widget_at((int32_t)x, (int32_t)y), -1);
}

int sl_desk_press(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || g_desk.ready != 1u) return 0;
    int id = sl_desk_widget_at((int32_t)x, (int32_t)y);
    if (id < 0) return 0;
    sl_desk_set_state(ctx, id, id);
    g_desk.dragging = k_widgets[id].press ? k_widgets[id].press(ctx, (int32_t)x, (int32_t)y) : 0u;
    return 1;
}

int sl_desk_cursor(uint32_t x, uint32_t y) {
    if (g_desk.ready != 1u) return -1;
    return sl_desk_widget_at((int32_t)x, (int32_t)y) >= 0 ? SL_CUR_HAND : -1;
}

uint8_t sl_desk_held(void) {
    return g_desk.pressed >= 0;
}

void sl_desk_drag(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || g_desk.pressed < 0) return;
    int id = g_desk.pressed;
    if (g_desk.dragging && k_widgets[id].drag) {
        k_widgets[id].drag(ctx, (int32_t)x, (int32_t)y);
        return;
    }
    // A plain click: sunk while the mouse is on it, up when it leaves.
    int over = sl_desk_widget_at((int32_t)x, (int32_t)y) == id;
    if (over != (g_desk.hot == id)) sl_desk_set_state(ctx, over ? id : -1, id);
}

void sl_desk_release(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || g_desk.pressed < 0) return;
    int id = g_desk.pressed;
    uint8_t dragged = g_desk.dragging;
    g_desk.dragging = 0u;
    int over = sl_desk_widget_at((int32_t)x, (int32_t)y);
    sl_desk_set_state(ctx, over, -1);
    if (dragged) {
        if (k_widgets[id].release) k_widgets[id].release(ctx);
        return;
    }
    if (over == id) sl_desk_activate(ctx, id);
}

int sl_desk_wheel(sl_ctx_t *ctx, uint32_t x, uint32_t y, int32_t delta) {
    if (!ctx || g_desk.ready != 1u || delta == 0) return 0;
    int id = sl_desk_widget_at((int32_t)x, (int32_t)y);
    if (id < 0 || !k_widgets[id].wheel) return 0;
    k_widgets[id].wheel(ctx, delta);
    return 1;
}
