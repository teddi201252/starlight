#include "desktop.h"

#include "starlight_internal.h"
#include "render/blit.h"
#include "render/pixels.h"
#include "render/surface.h"
#include "peos/net.h"
#include "ui_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SL_DESK_BAR_H 44u
#define SL_DESK_LEFT 172u            // widgets start right of the dock and its fin
#define SL_DESK_CLOCK_W 300u
#define SL_DESK_CLOCK_TOP 72u
#define SL_DESK_CAL_W 330u
#define SL_DESK_CAL_GAP 48u
#define SL_DESK_POLL_MS 2000u

typedef struct { int32_t x, y; uint32_t w, h; } sl_desk_rect_t;

// What the widgets show, as last drawn.
static struct {
    uint8_t ready;
    const slsk_font_t *bar, *bar_bold, *digits, *day, *date, *cal_title, *cal, *cal_bold;
    const slsk_entry_t *star, *wifi, *volume;
    sl_desk_rect_t bar_r, clock_r, cal_r;
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
} g_feed;
static uint8_t g_poller_stack[16384] __attribute__((aligned(16)));
static uint8_t g_poller_started;

static const char *const k_days[7] = { "Domenica", "Lunedì", "Martedì", "Mercoledì", "Giovedì", "Venerdì", "Sabato" };
static const char *const k_months[12] = { "Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio",
                                          "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre" };
static const char *const k_week[7] = { "Lu", "Ma", "Me", "Gi", "Ve", "Sa", "Do" };

// ------------------------------------------------------------------ poller

static int32_t sl_desk_read_volume(void) {
    static const char path[] = "/system/config/audio.cfg";
    uint64_t fd = peos_fopen(path, sizeof(path) - 1u, 0);
    if ((int64_t)fd < 0) return -1;
    char buf[256];
    uint64_t n = peos_fread(fd, buf, sizeof(buf) - 1u);
    (void)peos_fclose(fd);
    if ((int64_t)n <= 0) return -1;
    buf[n] = '\0';
    const char *p = strstr(buf, "volume=");
    if (!p) return -1;
    int32_t v = 0;
    for (p += 7; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
    return v > 100 ? 100 : v;
}

static void sl_desk_poller(void *arg) {
    (void)arg;
    for (;;) {
        peos_netcfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        uint8_t has_ip = (net_config_get(&cfg) == NET_OK && (cfg.ip[0] | cfg.ip[1] | cfg.ip[2] | cfg.ip[3])) ? 1u : 0u;
        int32_t vol = sl_desk_read_volume();
        if (has_ip != g_feed.has_ip || vol != g_feed.volume || memcmp(cfg.ip, g_feed.ip, 4) != 0) {
            __atomic_add_fetch(&g_feed.version, 1u, __ATOMIC_RELEASE);
            memcpy(g_feed.ip, cfg.ip, 4);
            g_feed.has_ip = has_ip;
            g_feed.volume = vol;
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
    return 1;
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
    if (!g_desk.bar || !g_desk.bar_bold || !g_desk.digits || !g_desk.day || !g_desk.date || !g_desk.cal_title ||
        !g_desk.cal || !g_desk.cal_bold || !g_desk.star || !g_desk.wifi || !g_desk.volume) {
        peos_puts("starlight: skin lacks the desktop widgets\n");
        return 0;
    }
    g_feed.volume = -1;
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
    sl_text_draw(&t, g_desk.day, right - (int32_t)sl_text_width(g_desk.day, day), base, day, ink[0], ink[1], ink[2]);
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

static int sl_desk_days_in(int month, int year) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[month];
}

// The month on a night card, weeks from Monday, today in neon.
static void sl_desk_draw_calendar(sl_ctx_t *ctx, const struct tm *now) {
    if (g_desk.cal_r.w == 0u) return;
    sl_desk_clear(ctx, &g_desk.cal_r);
    sl_target_t t = sl_desk_target(ctx, &g_desk.cal_r);
    int year = now->tm_year + 1900, month = now->tm_mon;
    int first_wday = (now->tm_wday - (now->tm_mday - 1) % 7 + 7) % 7;   // weekday of the 1st, 0 = Sunday
    int lead = (first_wday + 6) % 7;                                       // blanks before it, weeks from Monday
    int days = sl_desk_days_in(month, year);
    int weeks = (lead + days + 6) / 7;

    int32_t x0 = g_desk.cal_r.x + 20, y0 = g_desk.cal_r.y + 20;
    uint32_t row = 32u, h = 24u + 30u + 14u + (uint32_t)(weeks + 1) * row - 12u + 26u;
    sl_surf_style_t card = { 0x11, 0x16, 0x26, 1, { { 6, 6, 18, 0, 0x00, 0x00, 0x00, 110 } } };
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
    sl_desk_draw_calendar(ctx, &now);
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
    if (version != g_desk.data_version) {
        g_desk.data_version = version;
        bar = 1u;
    }
    if (minute == g_desk.minute && !bar) return;   // nothing changed: no work

    struct tm now;
    sl_desk_now(ctx, &now);
    sl_desk_draw_bar(ctx, &now);
    if (minute != g_desk.minute) {
        g_desk.minute = minute;
        sl_desk_draw_clock(ctx, &now);
        if (minute / 1440 != g_desk.day_no) {
            g_desk.day_no = minute / 1440;
            sl_desk_draw_calendar(ctx, &now);
        }
    }
}

void sl_desk_free(sl_ctx_t *ctx) {
    if (!ctx) return;
    free(ctx->desk_frame);
    ctx->desk_frame = NULL;
}
