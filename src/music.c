// The music in Starlight: see music.h. musicd's statuses come on a watching
// connection (peos/musicd.h), read once a frame without waiting; the
// commands are connections of their own.

#include "music.h"

#include "desktop.h"
#include "i18n.h"
#include "starlight_internal.h"
#include "render/blit.h"
#include "render/image_decode.h"
#include "render/pixels.h"
#include "render/surface.h"
#include "ui_sdk.h"
#include "peos/music.h"
#include "peos/musicd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SL_MUS_ORFEUS "/system/apps/orfeus.app"
#define SL_MUS_RETRY_NS 2000000000ull
#define SL_MUS_TOAST_NS 2500000000ull
#define SL_MUS_TOAST_WAIT_NS 800000000ull   // after a key: the new track's name, or the old one after this

// The mini player (design/orfeus): 360 wide, under the bar.
#define SL_MUS_W 360u
#define SL_MUS_H 272u
#define SL_MUS_TOP 58u
#define SL_MUS_COVER 76u
#define SL_MUS_SHADOW 30u
// The toast, at the bottom in the middle.
#define SL_MUS_TOAST_H 52u
#define SL_MUS_TOAST_BOTTOM 40u

static struct {
    int64_t watch;
    uint64_t retry_ns;
    music_status_t st;
    uint8_t have;
    uint32_t version;
    // the pill, in screen coordinates (the bar is at the top)
    int32_t pill_x, pill_w;
    uint8_t pill_shown;
    // the mini player
    uint8_t open;
    uint32_t px, py;
    uint64_t second;               // of the clock, as last drawn
    // the cover, premultiplied BGRA, SL_MUS_COVER square
    char cover_for[MUSIC_PATH + MUSIC_TEXT * 2];
    uint8_t *cover;
    // the toast
    uint8_t toast;                 // 0 none, 1 waiting for the new track, 2 shown
    uint8_t toast_key;             // 1 next, 2 previous, 3 play/pause
    uint64_t toast_since, toast_until;
    uint32_t toast_x, toast_w;
    uint32_t toast_index;
    uint8_t super_down;
    const slsk_font_t *f13, *f14, *f18, *mono11;
    const slsk_entry_t *play20, *pause20, *prev18, *next18, *play16, *pause16, *prev16, *next16, *vol18;
} g_mus = { .watch = -1 };

static int sl_mus_assets(sl_ctx_t *ctx) {
    if (g_mus.f13) return 1;
    if (!ctx->skin_loaded) return 0;
    const slsk_entry_t *a = sl_skin_find(&ctx->skin, "ui-13"), *b = sl_skin_find(&ctx->skin, "ui-14");
    const slsk_entry_t *c = sl_skin_find(&ctx->skin, "ui-18-mid"), *d = sl_skin_find(&ctx->skin, "mono-11");
    g_mus.play20 = sl_skin_find(&ctx->skin, "mus-play-20");
    g_mus.pause20 = sl_skin_find(&ctx->skin, "mus-pause-20");
    g_mus.prev18 = sl_skin_find(&ctx->skin, "mus-prev-18");
    g_mus.next18 = sl_skin_find(&ctx->skin, "mus-next-18");
    g_mus.play16 = sl_skin_find(&ctx->skin, "mus-play-16");
    g_mus.pause16 = sl_skin_find(&ctx->skin, "mus-pause-16");
    g_mus.prev16 = sl_skin_find(&ctx->skin, "mus-prev-16");
    g_mus.next16 = sl_skin_find(&ctx->skin, "mus-next-16");
    g_mus.vol18 = sl_skin_find(&ctx->skin, "bar-volume");
    if (!a || !b || !c || !d || !g_mus.play20 || !g_mus.pause20 || !g_mus.prev18 || !g_mus.next18 || !g_mus.play16 ||
        !g_mus.pause16 || !g_mus.prev16 || !g_mus.next16 || !g_mus.vol18)
        return 0;
    g_mus.f13 = sl_skin_font(&ctx->skin, a);
    g_mus.f14 = sl_skin_font(&ctx->skin, b);
    g_mus.f18 = sl_skin_font(&ctx->skin, c);
    g_mus.mono11 = sl_skin_font(&ctx->skin, d);
    return 1;
}

void sl_music_init(sl_ctx_t *ctx) {
    (void)ctx;
    g_mus.watch = -1;
    g_mus.retry_ns = 0;
}

uint32_t sl_music_version(void) { return g_mus.version; }

int sl_music_active(void) { return g_mus.have && g_mus.st.state != MUSIC_STOPPED && g_mus.st.count; }

static uint64_t sl_mus_pos(void) { return g_mus.have ? music_position_ms(&g_mus.st, peos_time_ns()) : 0; }

// ------------------------------------------------------------------ geometry

static void sl_mus_place(const sl_ctx_t *ctx) {
    int32_t right = g_mus.pill_shown ? g_mus.pill_x + g_mus.pill_w + 70 : (int32_t)ctx->fb.width - 120;
    int32_t x = right - (int32_t)SL_MUS_W;
    if (x + (int32_t)SL_MUS_W > (int32_t)ctx->fb.width - 20) x = (int32_t)ctx->fb.width - 20 - (int32_t)SL_MUS_W;
    if (x < 20) x = 20;
    g_mus.px = (uint32_t)x;
    g_mus.py = SL_MUS_TOP;
}

static void sl_mus_dirty_card(sl_ctx_t *ctx) {
    uint32_t x = g_mus.px > SL_MUS_SHADOW ? g_mus.px - SL_MUS_SHADOW : 0u;
    sl_dirty_add(ctx, x, g_mus.py > SL_MUS_SHADOW ? g_mus.py - SL_MUS_SHADOW : 0u, SL_MUS_W + 2u * SL_MUS_SHADOW,
                 SL_MUS_H + 2u * SL_MUS_SHADOW);
}

static void sl_mus_dirty_toast(sl_ctx_t *ctx) {
    if (!g_mus.toast_w) return;
    uint32_t y = ctx->fb.height > SL_MUS_TOAST_BOTTOM + SL_MUS_TOAST_H + SL_MUS_SHADOW
                     ? ctx->fb.height - SL_MUS_TOAST_BOTTOM - SL_MUS_TOAST_H - SL_MUS_SHADOW : 0u;
    uint32_t x = g_mus.toast_x > SL_MUS_SHADOW ? g_mus.toast_x - SL_MUS_SHADOW : 0u;
    sl_dirty_add(ctx, x, y, g_mus.toast_w + 2u * SL_MUS_SHADOW, SL_MUS_TOAST_H + 2u * SL_MUS_SHADOW);
}

static void sl_mus_set_open(sl_ctx_t *ctx, uint8_t open) {
    if (g_mus.open == open) return;
    if (open) sl_mus_place(ctx);
    sl_mus_dirty_card(ctx);
    g_mus.open = open;
}

// Where the parts of the mini player are, relative to its corner.
#define POP_PAD 18
#define POP_PROG_Y 110
#define POP_CTL_Y 150
#define POP_VOL_Y 222

static int sl_mus_in(uint32_t x, uint32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    int32_t lx = (int32_t)x - (int32_t)g_mus.px, ly = (int32_t)y - (int32_t)g_mus.py;
    return lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh;
}

static void sl_mus_open_rect(int32_t *x, int32_t *w) {
    uint32_t tw = g_mus.f13 ? sl_text_width(g_mus.f13, sl_tr("music.open")) : 80u;
    *w = (int32_t)tw + 28;
    *x = (int32_t)SL_MUS_W - POP_PAD - *w;
}

// ------------------------------------------------------------------ the cover

static uint32_t sl_mus_mix(uint32_t a, uint32_t b, uint32_t k256) {
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8) {
        int x = (int)((a >> s) & 255u), y = (int)((b >> s) & 255u);
        out |= (uint32_t)(x + (((y - x) * (int)k256) >> 8)) << s;
    }
    return out;
}

static float sl_mus_sqrt(float v) {
    if (v <= 0) return 0;
    float r = v > 1 ? v : 1;
    for (int i = 0; i < 12; i++) r = 0.5f * (r + v / r);
    return r;
}

// Coverage (0..256) of a disc of radius r at cx,cy for the pixel at x,y.
static uint32_t sl_mus_disc(float x, float y, float cx, float cy, float r) {
    float dx = x - cx, dy = y - cy;
    float d = sl_mus_sqrt(dx * dx + dy * dy) - r;
    if (d <= -0.5f) return 256u;
    if (d >= 0.5f) return 0u;
    return (uint32_t)((0.5f - d) * 256.0f);
}

// The cover, made again when the track's album changes: its picture file,
// or the one drawn for the album (as Orfeus draws it).
static void sl_mus_cover(void) {
    char key[sizeof(g_mus.cover_for)];
    snprintf(key, sizeof(key), "%s\x1f%s\x1f%s", g_mus.st.cover, g_mus.st.album, g_mus.st.artist);
    if (g_mus.cover && !strcmp(key, g_mus.cover_for)) return;
    snprintf(g_mus.cover_for, sizeof(g_mus.cover_for), "%s", key);
    const uint32_t S = SL_MUS_COVER;
    if (!g_mus.cover) g_mus.cover = malloc(S * S * 4u);
    if (!g_mus.cover) return;
    uint32_t *rgb = malloc(S * S * 4u);
    if (!rgb) return;
    sl_image_t img;
    memset(&img, 0, sizeof(img));
    int have = g_mus.st.cover[0] && sl_decode_image_file(g_mus.st.cover, &img) && img.data && img.width && img.height;
    if (have) {   // the middle square, each pixel the average of what falls in it
        uint32_t side = img.width < img.height ? img.width : img.height;
        uint32_t ox = (img.width - side) / 2u, oy = (img.height - side) / 2u, ch = img.channels ? img.channels : 4u;
        for (uint32_t y = 0; y < S; y++) {
            uint32_t y0 = oy + y * side / S, y1 = oy + (y + 1u) * side / S;
            if (y1 <= y0) y1 = y0 + 1u;
            for (uint32_t x = 0; x < S; x++) {
                uint32_t x0 = ox + x * side / S, x1 = ox + (x + 1u) * side / S;
                if (x1 <= x0) x1 = x0 + 1u;
                uint32_t r = 0, gg = 0, b = 0, n = 0;
                for (uint32_t yy = y0; yy < y1 && yy < img.height; yy++)
                    for (uint32_t xx = x0; xx < x1 && xx < img.width; xx++) {
                        const uint8_t *p = img.data + (size_t)yy * img.stride + (size_t)xx * ch;
                        r += p[0];
                        gg += p[1];
                        b += p[2];
                        n++;
                    }
                if (!n) n = 1;
                rgb[y * S + x] = (r / n) << 16 | (gg / n) << 8 | (b / n);
            }
        }
        sl_image_free(&img);
    } else {
        mus_art_t a;
        mus_art_for(g_mus.st.album, g_mus.st.artist, &a);
        float s = (float)a.s * (float)S / 1000.0f;
        float cx = (float)a.x * (float)S / 1000.0f + s / 2, cy = (float)a.y * (float)S / 1000.0f + s / 2;
        float lx0 = (float)S * 0.12f, lx1 = (float)S * 0.88f, ly1 = (float)S * 0.84f, ly0 = ly1 - 3.0f;
        uint32_t line = sl_mus_mix(a.bg, a.fg, 178u);
        for (uint32_t y = 0; y < S; y++)
            for (uint32_t x = 0; x < S; x++) {
                float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                uint32_t c = a.bg;
                if (fx >= lx0 && fx <= lx1 && fy >= ly0 && fy <= ly1) c = line;
                c = sl_mus_mix(c, a.fg, sl_mus_disc(fx, fy, cx, cy, s / 2));
                rgb[y * S + x] = c;
            }
    }
    // round corners (16), premultiplied BGRA
    const float R = 16.0f;
    for (uint32_t y = 0; y < S; y++)
        for (uint32_t x = 0; x < S; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float qx = fx < R ? R : fx > (float)S - R ? (float)S - R : fx;
            float qy = fy < R ? R : fy > (float)S - R ? (float)S - R : fy;
            uint32_t a = (fx < R || fx > (float)S - R) && (fy < R || fy > (float)S - R) ? sl_mus_disc(fx, fy, qx, qy, R) : 256u;
            if (a > 255u) a = 255u;
            uint32_t c = rgb[y * S + x];
            uint8_t *o = g_mus.cover + (y * S + x) * 4u;
            o[0] = (uint8_t)(((c & 255u) * a) / 255u);
            o[1] = (uint8_t)((((c >> 8) & 255u) * a) / 255u);
            o[2] = (uint8_t)((((c >> 16) & 255u) * a) / 255u);
            o[3] = (uint8_t)a;
        }
    free(rgb);
}

// ------------------------------------------------------------------ commands

static void sl_mus_call(uint32_t op, uint32_t value) {
    music_status_t s;
    if (music_call(op, value, 0, 0, &s) == MUSIC_OK && s.magic == MUSIC_STATUS_MAGIC) g_mus.st = s, g_mus.have = 1;
}

// A media key (or Super + an arrow): the command, and the toast.
static void sl_mus_key(sl_ctx_t *ctx, uint8_t which) {
    if (!g_mus.have || !g_mus.st.count) return;
    g_mus.toast_index = g_mus.st.index;
    uint32_t op = which == 1 ? MUSIC_OP_NEXT : which == 2 ? MUSIC_OP_PREV : MUSIC_OP_TOGGLE;
    (void)music_call(op, 0, 0, 0, NULL);
    if (g_mus.open) return;   // the mini player shows it already
    sl_mus_dirty_toast(ctx);
    g_mus.toast = which == 3 ? 2u : 1u;   // play/pause: at once; a new track: when it is there
    g_mus.toast_key = which;
    g_mus.toast_since = peos_time_ns();
    g_mus.toast_until = g_mus.toast_since + SL_MUS_TOAST_NS + (which == 3 ? 0 : SL_MUS_TOAST_WAIT_NS);
    g_mus.toast_w = 0;
}

int sl_music_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev) {
    if (!ctx || !ev) return 0;
    if (ev->code == PEOS_KEY_LSUPER || ev->code == PEOS_KEY_RSUPER) {
        g_mus.super_down = ev->pressed;
        return 0;
    }
    uint8_t which = 0;
    switch (ev->code) {
    case 0xE019u: which = 1; break;   // next track
    case 0xE010u: which = 2; break;   // previous track
    case 0xE022u: which = 3; break;   // play/pause
    case PEOS_KEY_RIGHT: if (g_mus.super_down) which = 1; break;
    case PEOS_KEY_LEFT: if (g_mus.super_down) which = 2; break;
    case PEOS_KEY_ESC:
        if (g_mus.open && ev->pressed) {
            sl_mus_set_open(ctx, 0);
            return 1;
        }
        return 0;
    default: return 0;
    }
    if (!which) return 0;
    if (ev->code != 0xE019u && ev->code != 0xE010u && ev->code != 0xE022u && !sl_music_active()) return 0;
    if (ev->pressed) sl_mus_key(ctx, which);
    return 1;
}

int sl_music_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    if (g_mus.pill_shown && y < SL_DESK_BAR_HEIGHT && (int32_t)x >= g_mus.pill_x && (int32_t)x < g_mus.pill_x + g_mus.pill_w) {
        sl_mus_set_open(ctx, (uint8_t)!g_mus.open);
        return 1;
    }
    if (!g_mus.open) return 0;
    if (!sl_mus_in(x, y, 0, 0, (int32_t)SL_MUS_W, (int32_t)SL_MUS_H)) {
        sl_mus_set_open(ctx, 0);   // a click elsewhere closes it, and does nothing else
        return 1;
    }
    int32_t mid = (int32_t)SL_MUS_W / 2, ox, ow;
    sl_mus_open_rect(&ox, &ow);
    if (sl_mus_in(x, y, mid - 28 - 14 - 40, POP_CTL_Y + 8, 40, 40)) sl_mus_call(MUSIC_OP_PREV, 0);
    else if (sl_mus_in(x, y, mid - 28, POP_CTL_Y, 56, 56)) sl_mus_call(MUSIC_OP_TOGGLE, 0);
    else if (sl_mus_in(x, y, mid + 28 + 14, POP_CTL_Y + 8, 40, 40)) sl_mus_call(MUSIC_OP_NEXT, 0);
    else if (sl_mus_in(x, y, ox, POP_VOL_Y, ow, 32)) {
        sl_mus_set_open(ctx, 0);
        (void)sl_windows_open_app_bundle_path(ctx, SL_MUS_ORFEUS);
        return 1;
    } else if (sl_mus_in(x, y, POP_PAD + 28, POP_VOL_Y, ox - 8 - POP_PAD - 28, 32)) {
        int32_t bx = POP_PAD + 28, bw = ox - 12 - bx;
        int32_t d = (int32_t)x - (int32_t)g_mus.px - bx;
        if (d < 0) d = 0;
        if (d > bw) d = bw;
        sl_mus_call(MUSIC_OP_VOLUME, bw > 0 ? (uint32_t)(d * 100 / bw) : 0u);
    } else if (sl_mus_in(x, y, POP_PAD, POP_PROG_Y - 8, (int32_t)SL_MUS_W - 2 * POP_PAD, 16) && g_mus.st.dur_ms) {
        int32_t d = (int32_t)x - (int32_t)g_mus.px - POP_PAD;
        uint64_t ms = g_mus.st.dur_ms * (uint64_t)(d < 0 ? 0 : d) / (SL_MUS_W - 2u * POP_PAD);
        music_status_t s;
        if (music_call(MUSIC_OP_SEEK, 0, 0, ms, &s) == MUSIC_OK) g_mus.st = s;
    }
    sl_mus_dirty_card(ctx);
    return 1;
}

int sl_music_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    (void)ctx;
    if (g_mus.pill_shown && y < SL_DESK_BAR_HEIGHT && (int32_t)x >= g_mus.pill_x && (int32_t)x < g_mus.pill_x + g_mus.pill_w)
        return SL_CUR_HAND;
    if (!g_mus.open || !sl_mus_in(x, y, 0, 0, (int32_t)SL_MUS_W, (int32_t)SL_MUS_H)) return -1;
    return sl_mus_in(x, y, 0, POP_PROG_Y - 8, (int32_t)SL_MUS_W, (int32_t)SL_MUS_H) ? SL_CUR_HAND : SL_CUR_ARROW;
}

// ------------------------------------------------------------------ the tick

static void sl_mus_got(sl_ctx_t *ctx, const music_status_t *s) {
    int pill = !g_mus.have || s->state != g_mus.st.state || strcmp(s->title, g_mus.st.title) || s->count != g_mus.st.count;
    g_mus.st = *s;
    g_mus.have = 1;
    if (pill) g_mus.version++;
    if (g_mus.open) sl_mus_dirty_card(ctx);
    if (g_mus.toast == 1 && s->index != g_mus.toast_index) {   // the new track is there: show it
        g_mus.toast = 2;
        g_mus.toast_w = 0;
    }
}

void sl_music_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    if (g_mus.watch < 0) {
        if (now >= g_mus.retry_ns) {
            g_mus.retry_ns = now + SL_MUS_RETRY_NS;
            g_mus.watch = music_watch();
        }
    }
    if (g_mus.watch >= 0) {
        music_status_t s;
        int r = music_watch_read(g_mus.watch, &s);
        if (r < 0) {   // musicd went away: again in a while
            (void)peos_fclose((uint64_t)g_mus.watch);
            g_mus.watch = -1;
            if (g_mus.have) g_mus.version++;
            g_mus.have = 0;
            sl_mus_set_open(ctx, 0);
        } else if (r == 1) {
            sl_mus_got(ctx, &s);
        }
    }
    // the mini player's clock
    if (g_mus.open && sl_music_active()) {
        uint64_t sec = sl_mus_pos() / 1000u;
        if (sec != g_mus.second) {
            g_mus.second = sec;
            sl_mus_dirty_card(ctx);
        }
    }
    if (g_mus.open && !sl_music_active()) sl_mus_set_open(ctx, 0);
    // the toast
    if (g_mus.toast == 1 && now >= g_mus.toast_since + SL_MUS_TOAST_WAIT_NS) {
        g_mus.toast = 2;
        g_mus.toast_w = 0;
    }
    if (g_mus.toast && now >= g_mus.toast_until) {
        sl_mus_dirty_toast(ctx);
        g_mus.toast = 0;
        g_mus.toast_w = 0;
    }
    if (g_mus.toast == 2 && !g_mus.toast_w && sl_mus_assets(ctx)) {
        char text[2 * MUSIC_TEXT + 8];
        snprintf(text, sizeof(text), "%s", g_mus.st.title);
        uint32_t w = 10u + 34u + 12u + sl_text_width(g_mus.f14, text);
        if (g_mus.st.artist[0]) w += sl_text_width(g_mus.f14, " \xc2\xb7 ") + sl_text_width(g_mus.f14, g_mus.st.artist);
        if (g_mus.toast_key != 3) w += 6u + 30u;
        w += 20u;
        if (w > ctx->fb.width - 80u) w = ctx->fb.width - 80u;
        g_mus.toast_w = w;
        g_mus.toast_x = (ctx->fb.width - w) / 2u;
        sl_mus_dirty_toast(ctx);
    }
}

// ------------------------------------------------------------------ drawing

static void sl_mus_surface(sl_target_t *t, const sl_surf_style_t *st, int32_t x, int32_t y, uint32_t w, uint32_t h,
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

static void sl_mus_mask(sl_ctx_t *ctx, sl_target_t *t, const slsk_entry_t *m, int32_t cx, int32_t cy, uint32_t col) {
    sl_blit_mask(t, sl_skin_data(&ctx->skin, m), m->width, m->height, m->width, cx - (int32_t)m->width / 2,
                 cy - (int32_t)m->height / 2, (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col, 255u);
}

static void sl_mus_text(sl_target_t *t, const slsk_font_t *f, int32_t x, int32_t base, const char *s, int32_t maxw,
                        uint32_t col) {
    char cut[MUSIC_PATH];
    snprintf(cut, sizeof(cut), "%s", s);
    if (maxw > 0 && (int32_t)sl_text_width(f, cut) > maxw) {
        size_t n = strlen(cut);
        while (n > 0) {
            n--;
            while (n > 0 && ((unsigned char)cut[n] & 0xC0u) == 0x80u) n--;
            cut[n] = '\0';
            char tryit[MUSIC_PATH + 4];
            snprintf(tryit, sizeof(tryit), "%s\xe2\x80\xa6", cut);
            if ((int32_t)sl_text_width(f, tryit) <= maxw) {
                snprintf(cut, sizeof(cut), "%s", tryit);
                break;
            }
        }
    }
    sl_text_draw(t, f, x, base, cut, (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col);
}

static void sl_mus_time(char *out, size_t cap, uint64_t ms) { mus_format_time(out, cap, ms); }

static const sl_surf_style_t k_mus_card = { 0x11, 0x16, 0x26, 1, { { 6, 8, 22, 0, 0x00, 0x00, 0x00, 120 } } };
static const sl_surf_style_t k_mus_track = { 0x26, 0x30, 0x4C, 0, { { 0 } } };
static const sl_surf_style_t k_mus_neon = { 0x59, 0xD0, 0xFF, 0, { { 0 } } };
static const sl_surf_style_t k_mus_glow = { 0x59, 0xD0, 0xFF, 1, { { 0, 0, 20, 0, 0x59, 0xD0, 0xFF, 128 } } };
static const sl_surf_style_t k_mus_knob = { 0xE8, 0xF4, 0xFF, 1, { { 0, 0, 8, 0, 0x59, 0xD0, 0xFF, 200 } } };
static const sl_surf_style_t k_mus_vol = { 0xC9, 0xE8, 0xFF, 0, { { 0 } } };
static const sl_surf_style_t k_mus_button = { 0x1C, 0x24, 0x38, 0, { { 0 } } };

static void sl_mus_draw_card(sl_ctx_t *ctx, sl_target_t *t) {
    int32_t x = (int32_t)g_mus.px, y = (int32_t)g_mus.py;
    sl_mus_surface(t, &k_mus_card, x, y, SL_MUS_W, SL_MUS_H, 28u);
    // the cover and the names
    sl_mus_cover();
    if (g_mus.cover) {
        static const sl_surf_style_t halo = { 0x0A, 0x0E, 0x1A, 1, { { 0, 0, 24, 0, 0x59, 0xD0, 0xFF, 46 } } };
        sl_mus_surface(t, &halo, x + POP_PAD, y + POP_PAD, SL_MUS_COVER, SL_MUS_COVER, 16u);
        slsk_entry_t e;
        memset(&e, 0, sizeof(e));
        e.kind = SLSK_KIND_PREMUL;
        e.width = (uint16_t)SL_MUS_COVER;
        e.height = (uint16_t)SL_MUS_COVER;
        sl_blit_sprite(t, &e, g_mus.cover, x + POP_PAD, y + POP_PAD, SL_MUS_COVER, SL_MUS_COVER);
    }
    int32_t tx = x + POP_PAD + (int32_t)SL_MUS_COVER + 14, tw = (int32_t)SL_MUS_W - POP_PAD * 2 - (int32_t)SL_MUS_COVER - 14;
    sl_mus_text(t, g_mus.f18, tx, y + POP_PAD + 28, g_mus.st.title, tw, 0xE8F4FFu);
    sl_mus_text(t, g_mus.f14, tx, y + POP_PAD + 50, g_mus.st.artist, tw, 0x8FA3C0u);
    sl_mus_text(t, g_mus.f13, tx, y + POP_PAD + 69, g_mus.st.album, tw, 0x8FA3C0u);
    // the progress
    uint32_t pw = SL_MUS_W - 2u * POP_PAD;
    uint64_t pos = sl_mus_pos(), dur = g_mus.st.dur_ms;
    uint32_t filled = dur ? (uint32_t)((uint64_t)pw * (pos > dur ? dur : pos) / dur) : 0u;
    sl_mus_surface(t, &k_mus_track, x + POP_PAD, y + POP_PROG_Y, pw, 4u, 2u);
    if (filled >= 4u) sl_mus_surface(t, &k_mus_neon, x + POP_PAD, y + POP_PROG_Y, filled, 4u, 2u);
    sl_mus_surface(t, &k_mus_knob, x + POP_PAD + (int32_t)filled - 6, y + POP_PROG_Y - 4, 12u, 12u, 6u);
    char a[16], b[16];
    sl_mus_time(a, sizeof(a), pos);
    sl_mus_time(b, sizeof(b), dur);
    sl_mus_text(t, g_mus.mono11, x + POP_PAD, y + POP_PROG_Y + 22, a, 0, 0x8FA3C0u);
    int32_t bw = (int32_t)sl_text_width(g_mus.mono11, b);
    sl_mus_text(t, g_mus.mono11, x + (int32_t)SL_MUS_W - POP_PAD - bw, y + POP_PROG_Y + 22, b, 0, 0x8FA3C0u);
    // previous, play, next
    int32_t mid = x + (int32_t)SL_MUS_W / 2, cy = y + POP_CTL_Y + 28;
    sl_mus_mask(ctx, t, g_mus.prev18, mid - 28 - 14 - 20, cy, 0xC9E8FFu);
    sl_mus_surface(t, &k_mus_glow, mid - 28, y + POP_CTL_Y, 56u, 56u, 28u);
    sl_mus_mask(ctx, t, g_mus.st.state == MUSIC_PLAYING ? g_mus.pause20 : g_mus.play20, mid, cy, 0x0A0E1Au);
    sl_mus_mask(ctx, t, g_mus.next18, mid + 28 + 14 + 20, cy, 0xC9E8FFu);
    // the volume and the way to Orfeus
    int32_t ox, ow;
    sl_mus_open_rect(&ox, &ow);
    int32_t vy = y + POP_VOL_Y + 16;
    sl_mus_mask(ctx, t, g_mus.vol18, x + POP_PAD + 9, vy, 0xC9E8FFu);
    int32_t vx = x + POP_PAD + 28, vw = x + ox - 12 - vx;
    if (vw > 8) {
        sl_mus_surface(t, &k_mus_track, vx, vy - 2, (uint32_t)vw, 4u, 2u);
        uint32_t vf = (uint32_t)vw * g_mus.st.volume / 100u;
        if (vf >= 4u) sl_mus_surface(t, &k_mus_vol, vx, vy - 2, vf, 4u, 2u);
    }
    sl_mus_surface(t, &k_mus_button, x + ox, y + POP_VOL_Y, (uint32_t)ow, 32u, 16u);
    sl_mus_text(t, g_mus.f13, x + ox + 14, y + POP_VOL_Y + 21, sl_tr("music.open"), 0, 0xC9E8FFu);
}

static void sl_mus_draw_toast(sl_ctx_t *ctx, sl_target_t *t) {
    static const sl_surf_style_t pill = { 0x11, 0x16, 0x26, 1, { { 6, 8, 20, 0, 0x00, 0x00, 0x00, 110 } } };
    int32_t x = (int32_t)g_mus.toast_x, y = (int32_t)(ctx->fb.height - SL_MUS_TOAST_BOTTOM - SL_MUS_TOAST_H);
    sl_mus_surface(t, &pill, x, y, g_mus.toast_w, SL_MUS_TOAST_H, SL_MUS_TOAST_H / 2u);
    int32_t cy = y + (int32_t)SL_MUS_TOAST_H / 2;
    sl_mus_surface(t, &k_mus_button, x + 10, cy - 17, 34u, 34u, 17u);
    const slsk_entry_t *icon = g_mus.toast_key == 1 ? g_mus.next16 : g_mus.toast_key == 2 ? g_mus.prev16
                             : g_mus.st.state == MUSIC_PLAYING ? g_mus.play16 : g_mus.pause16;
    sl_mus_mask(ctx, t, icon, x + 10 + 17, cy, 0x59D0FFu);
    int32_t right = x + (int32_t)g_mus.toast_w - 20 - (g_mus.toast_key != 3 ? 36 : 0);
    int32_t tx = x + 10 + 34 + 12;
    sl_target_t tt = *t;
    if (right < tt.clip_x1) tt.clip_x1 = right;
    tx = sl_text_draw(&tt, g_mus.f14, tx, cy + 5, g_mus.st.title, 0xE8, 0xF4, 0xFF);
    if (g_mus.st.artist[0]) {
        tx = sl_text_draw(&tt, g_mus.f14, tx, cy + 5, " \xc2\xb7 ", 0x8F, 0xA3, 0xC0);
        sl_text_draw(&tt, g_mus.f14, tx, cy + 5, g_mus.st.artist, 0x8F, 0xA3, 0xC0);
    }
    if (g_mus.toast_key != 3) {   // the key, in a chip: an arrow
        int32_t kx = x + (int32_t)g_mus.toast_w - 20 - 30;
        sl_mus_surface(t, &k_mus_button, kx, cy - 11, 30u, 22u, 8u);
        const char *arrow = g_mus.toast_key == 1 ? "\xe2\x86\x92" : "\xe2\x86\x90";
        int32_t aw = (int32_t)sl_text_width(g_mus.mono11, arrow);
        sl_text_draw(t, g_mus.mono11, kx + 15 - aw / 2, cy + 4, arrow, 0x8F, 0xA3, 0xC0);
    }
}

static void sl_mus_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_target_t t = sl_target_from_ctx(ctx);
    if (g_mus.open) sl_mus_draw_card(ctx, &t);
    else if (g_mus.toast == 2 && g_mus.toast_w) sl_mus_draw_toast(ctx, &t);
    sl_clip_clear(ctx);
}

void sl_music_render(sl_ctx_t *ctx) {
    if (!ctx || !sl_mus_assets(ctx)) return;
    if (g_mus.open && g_mus.have) {
        uint32_t x = g_mus.px > SL_MUS_SHADOW ? g_mus.px - SL_MUS_SHADOW : 0u;
        uint32_t y = g_mus.py > SL_MUS_SHADOW ? g_mus.py - SL_MUS_SHADOW : 0u;
        sl_dirty_for_each_intersection(x, y, SL_MUS_W + 2u * SL_MUS_SHADOW, SL_MUS_H + 2u * SL_MUS_SHADOW, sl_mus_draw_rect, ctx);
        return;
    }
    if (g_mus.toast == 2 && g_mus.toast_w && g_mus.have) {
        uint32_t y = ctx->fb.height - SL_MUS_TOAST_BOTTOM - SL_MUS_TOAST_H - SL_MUS_SHADOW;
        uint32_t x = g_mus.toast_x > SL_MUS_SHADOW ? g_mus.toast_x - SL_MUS_SHADOW : 0u;
        sl_dirty_for_each_intersection(x, y, g_mus.toast_w + 2u * SL_MUS_SHADOW, SL_MUS_TOAST_H + 2u * SL_MUS_SHADOW,
                                       sl_mus_draw_rect, ctx);
    }
}

// ------------------------------------------------------------------ the pill

int32_t sl_music_draw_pill(sl_ctx_t *ctx, sl_target_t *t, int32_t right, int32_t cy, uint8_t dark_bar) {
    g_mus.pill_shown = 0;
    if (!sl_music_active() || !sl_mus_assets(ctx)) return right;
    static const sl_surf_style_t night = { 0x1C, 0x24, 0x38, 1, { { 3, 3, 10, 0, 0x00, 0x00, 0x00, 90 } } };
    static const sl_surf_style_t day = { 0xE3, 0xE6, 0xEC, 2,
                                         { { 5, 5, 12, 0, 0xBC, 0xC1, 0xCB, 255 }, { -5, -5, 12, 0, 0xFF, 0xFF, 0xFF, 255 } } };
    const char *title = g_mus.st.title;
    int32_t tw = (int32_t)sl_text_width(g_mus.f13, title);
    if (tw > 180) tw = 180;
    int32_t w = 12 + 14 + 8 + tw + 12;
    int32_t x = right - w;
    sl_mus_surface(t, dark_bar ? &night : &day, x, cy - 15, (uint32_t)w, 30u, 15u);
    // four bars: lit while playing
    static const int32_t hs[4] = { 7, 12, 5, 10 };
    uint32_t col = g_mus.st.state == MUSIC_PLAYING ? (dark_bar ? 0x59D0FFu : 0x0B6E99u) : (dark_bar ? 0x5E6E8Au : 0x8A909Cu);
    for (int i = 0; i < 4; i++) {
        uint8_t mask[12 * 2];
        memset(mask, 255, sizeof(mask));
        sl_blit_mask(t, mask, 2u, (uint32_t)hs[i], 2u, x + 12 + i * 4, cy + 6 - hs[i], (uint8_t)(col >> 16), (uint8_t)(col >> 8),
                     (uint8_t)col, 255u);
    }
    sl_mus_text(t, g_mus.f13, x + 12 + 14 + 8, cy + 5, title, 180, dark_bar ? 0xE8F4FFu : 0x1A1D26u);
    g_mus.pill_x = x;
    g_mus.pill_w = w;
    g_mus.pill_shown = 1;
    if (g_mus.open) sl_mus_place(ctx);
    return x;
}
