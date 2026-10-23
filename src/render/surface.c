#include "surface.h"

#include <stdlib.h>
#include <string.h>

// Coverage, 0..255, of a rounded rectangle at pixel (px, py): 4x4 samples
// per pixel, in eighths of a pixel, so no square roots are needed.
static uint8_t rrect_cov(int32_t px, int32_t py, int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t r) {
    int64_t W = (int64_t)w * 8, H = (int64_t)h * 8, R = (int64_t)r * 8;
    int32_t hits = 0;
    for (int sy = 0; sy < 4; sy++) {
        int64_t qy = (int64_t)(py - y0) * 8 + 2 * sy + 1;
        if (qy < 0 || qy >= H) continue;
        int64_t cy = qy < R ? R - qy : (qy > H - R ? qy - (H - R) : 0);
        for (int sx = 0; sx < 4; sx++) {
            int64_t qx = (int64_t)(px - x0) * 8 + 2 * sx + 1;
            if (qx < 0 || qx >= W) continue;
            int64_t cx = qx < R ? R - qx : (qx > W - R ? qx - (W - R) : 0);
            if (cx * cx + cy * cy <= R * R) hits++;
        }
    }
    return (uint8_t)(hits * 255 / 16);
}

// Box blur of one row or column, radius rad, edges clamped; in place via tmp.
static void box_line(uint16_t *v, uint32_t n, size_t step, int32_t rad, uint16_t *tmp) {
    if (rad <= 0 || n == 0) return;
    uint32_t span = (uint32_t)(2 * rad + 1);
    int64_t acc = 0;
    for (int32_t i = -rad; i <= rad; i++) {
        int32_t k = i < 0 ? 0 : (i >= (int32_t)n ? (int32_t)n - 1 : i);
        acc += v[(size_t)k * step];
    }
    for (uint32_t i = 0; i < n; i++) {
        tmp[i] = (uint16_t)(acc / span);
        int32_t out = (int32_t)i - rad, in = (int32_t)i + rad + 1;
        out = out < 0 ? 0 : out;
        in = in >= (int32_t)n ? (int32_t)n - 1 : in;
        acc += (int64_t)v[(size_t)in * step] - v[(size_t)out * step];
    }
    for (uint32_t i = 0; i < n; i++) v[(size_t)i * step] = tmp[i];
}

// Three box passes approximate a Gaussian of sigma = blur / 2 (CSS).
// Box radius for three passes: width sqrt(12 sigma^2 / 3 + 1) = sqrt(blur^2 + 1).
static int32_t blur_radius(uint32_t blur) {
    if (blur == 0) return 0;
    int32_t bw = 1;
    while ((bw + 1) * (bw + 1) <= (int32_t)(blur * blur + 1)) bw++;
    return bw / 2 < 1 ? 1 : bw / 2;
}

static void blur_mask(uint16_t *m, uint32_t w, uint32_t h, uint32_t blur, uint16_t *tmp) {
    int32_t rad = blur_radius(blur);
    if (rad == 0) return;
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t y = 0; y < h; y++) box_line(m + (size_t)y * w, w, 1, rad, tmp);
        for (uint32_t x = 0; x < w; x++) box_line(m + x, h, w, rad, tmp);
    }
}

static uint32_t shadow_reach(const sl_surf_shadow_t *s) {
    return (uint32_t)(3 * blur_radius(s->blur)) + 1u;   // three passes of the box
}

// Paint colour with coverage m (0..255 in 16 bits) scaled by alpha, over px.
static void paint(uint8_t *px, const uint16_t *m, size_t n, uint8_t r, uint8_t g, uint8_t b, uint32_t alpha) {
    for (size_t i = 0; i < n; i++) {
        uint32_t a = (uint32_t)m[i] * alpha / 255u;
        if (a == 0u) continue;
        if (a > 255u) a = 255u;
        uint8_t *p = px + i * 4u;
        uint32_t inv = 255u - a;
        p[0] = (uint8_t)((b * a + p[0] * inv + 127u) / 255u);
        p[1] = (uint8_t)((g * a + p[1] * inv + 127u) / 255u);
        p[2] = (uint8_t)((r * a + p[2] * inv + 127u) / 255u);
        p[3] = (uint8_t)(a + (p[3] * inv + 127u) / 255u);
    }
}

int sl_surf_render(const sl_surf_style_t *st, uint32_t sw, uint32_t sh, uint32_t r, sl_surf_t *out) {
    if (!st || !out || sw == 0u || sh == 0u || sw > 4096u || sh > 4096u) return 0;
    memset(out, 0, sizeof(*out));
    if (r * 2u > sw) r = sw / 2u;
    if (r * 2u > sh) r = sh / 2u;

    int32_t pl = 0, pt = 0, pr = 0, pb = 0;
    for (uint32_t i = 0; i < st->shadow_count; i++) {
        const sl_surf_shadow_t *s = &st->shadows[i];
        if (s->inset) continue;
        int32_t reach = (int32_t)shadow_reach(s);
        if (reach - s->dx > pl) pl = reach - s->dx;
        if (reach - s->dy > pt) pt = reach - s->dy;
        if (reach + s->dx > pr) pr = reach + s->dx;
        if (reach + s->dy > pb) pb = reach + s->dy;
    }
    uint32_t w = (uint32_t)pl + sw + (uint32_t)pr, h = (uint32_t)pt + sh + (uint32_t)pb;
    size_t n = (size_t)w * h;
    uint8_t *px = (uint8_t *)calloc(n, 4u);
    uint16_t *shape = (uint16_t *)malloc(n * sizeof(uint16_t));
    uint16_t *m = (uint16_t *)malloc(n * sizeof(uint16_t));
    uint16_t *tmp = (uint16_t *)malloc((w > h ? w : h) * sizeof(uint16_t));
    if (!px || !shape || !m || !tmp) {
        free(px);
        free(shape);
        free(m);
        free(tmp);
        return 0;
    }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            shape[(size_t)y * w + x] = rrect_cov((int32_t)x, (int32_t)y, pl, pt, (int32_t)sw, (int32_t)sh, (int32_t)r);
        }
    }

    // Outer shadows, bottom one first: the shape moved and blurred.
    for (int i = (int)st->shadow_count - 1; i >= 0; i--) {
        const sl_surf_shadow_t *s = &st->shadows[i];
        if (s->inset) continue;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                int32_t sx = (int32_t)x - s->dx, sy = (int32_t)y - s->dy;
                m[(size_t)y * w + x] = (sx >= 0 && sy >= 0 && sx < (int32_t)w && sy < (int32_t)h)
                                           ? shape[(size_t)sy * w + (size_t)sx] : 0u;
            }
        }
        blur_mask(m, w, h, s->blur, tmp);
        paint(px, m, n, s->r, s->g, s->b, s->a);
    }
    paint(px, shape, n, st->fill_r, st->fill_g, st->fill_b, 255u);

    // Inset shadows: cast by the outside of the moved shape, kept inside.
    for (int i = (int)st->shadow_count - 1; i >= 0; i--) {
        const sl_surf_shadow_t *s = &st->shadows[i];
        if (!s->inset) continue;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                int32_t sx = (int32_t)x - s->dx, sy = (int32_t)y - s->dy;
                uint16_t c = (sx >= 0 && sy >= 0 && sx < (int32_t)w && sy < (int32_t)h)
                                 ? shape[(size_t)sy * w + (size_t)sx] : 0u;
                m[(size_t)y * w + x] = (uint16_t)(255u - c);
            }
        }
        blur_mask(m, w, h, s->blur, tmp);
        for (size_t k = 0; k < n; k++) m[k] = (uint16_t)((uint32_t)m[k] * shape[k] / 255u);
        paint(px, m, n, s->r, s->g, s->b, s->a);
    }

    free(shape);
    free(m);
    free(tmp);
    out->w = w;
    out->h = h;
    out->pad_l = (uint32_t)pl;
    out->pad_t = (uint32_t)pt;
    out->px = px;
    return 1;
}

void sl_surf_free(sl_surf_t *s) {
    if (!s) return;
    free(s->px);
    memset(s, 0, sizeof(*s));
}

// ---------------------------------------------------------------- the cache

#define SL_SURF_CACHE 96u
#define SL_SURF_CACHE_BYTES (12u * 1024u * 1024u)

typedef struct {
    sl_surf_style_t style;
    uint32_t sw, sh, r;
    uint64_t used;
    sl_surf_t surf;
} sl_surf_slot_t;

static sl_surf_slot_t g_cache[SL_SURF_CACHE];
static uint64_t g_clock;
static uint32_t g_rendered;
static uint32_t g_bytes;

const sl_surf_t *sl_surf_get(const sl_surf_style_t *style, uint32_t sw, uint32_t sh, uint32_t r) {
    if (!style) return NULL;
    g_clock++;
    sl_surf_slot_t *free_slot = NULL, *oldest = NULL;
    for (uint32_t i = 0; i < SL_SURF_CACHE; i++) {
        sl_surf_slot_t *s = &g_cache[i];
        if (!s->surf.px) {
            if (!free_slot) free_slot = s;
            continue;
        }
        if (s->sw == sw && s->sh == sh && s->r == r && memcmp(&s->style, style, sizeof(*style)) == 0) {
            s->used = g_clock;
            return &s->surf;
        }
        if (!oldest || s->used < oldest->used) oldest = s;
    }
    sl_surf_t made;
    if (!sl_surf_render(style, sw, sh, r, &made)) return NULL;
    uint32_t size = made.w * made.h * 4u;
    // Room: a free slot, and the byte budget, evicting the least used.
    while ((!free_slot || g_bytes + size > SL_SURF_CACHE_BYTES) && oldest) {
        g_bytes -= oldest->surf.w * oldest->surf.h * 4u;
        sl_surf_free(&oldest->surf);
        free_slot = oldest;
        oldest = NULL;
        for (uint32_t i = 0; i < SL_SURF_CACHE; i++) {
            sl_surf_slot_t *s = &g_cache[i];
            if (s->surf.px && (!oldest || s->used < oldest->used)) oldest = s;
        }
    }
    if (!free_slot) {
        sl_surf_free(&made);
        return NULL;
    }
    free_slot->style = *style;
    free_slot->sw = sw;
    free_slot->sh = sh;
    free_slot->r = r;
    free_slot->used = g_clock;
    free_slot->surf = made;
    g_bytes += size;
    g_rendered++;
    return &free_slot->surf;
}

void sl_surf_stats(uint32_t *rendered, uint32_t *cached_bytes) {
    if (rendered) *rendered = g_rendered;
    if (cached_bytes) *cached_bytes = g_bytes;
}
