// Baking the skin's surfaces: see skin_bake.h. Moved here from tools/slskin
// (docs/skin-colors-plan.md, phase 1); the drawing is the tool's as it was.
#include "render/skin_bake.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKB_PI 3.14159265358979f

// Shadow tails below this alpha (of 255) cannot be seen on any background
// (at most 2 levels a channel), but cost a blend for every pixel of them at
// every frame: they are cleared, and the sprite cropped to what is left.
#define SHADOW_ALPHA_MIN 3u

static float absf(float v) { return v < 0 ? -v : v; }
static int roundi(float v) { return v < 0 ? -(int)(-v + 0.5f) : (int)(v + 0.5f); }

static void fail(skb_t *s, const char *what, const char *detail) {
    if (!s->error[0]) snprintf(s->error, sizeof(s->error), "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

void skb_init(skb_t *s) {
    memset(s, 0, sizeof(*s));
    s->ground = (skb_rgb_t){ 1.0f, 1.0f, 1.0f };
}

void skb_free(skb_t *s) {
    for (int i = 0; i < s->entry_count; i++) free(s->entries[i].data);
    free(s->entries);
    s->entries = NULL;
    s->entry_count = s->entry_cap = 0;
}

// ---------------------------------------------------------------- colours

static int hex_rgb(const char *t, skb_rgb_t *out) {
    unsigned v = 0;
    for (int i = 0; i < 6; i++) {
        char c = t[i];
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                   : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16u;
        if (d > 15u) return -1;
        v = v * 16u + d;
    }
    *out = (skb_rgb_t){ ((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f };
    return 0;
}

static skb_var_t *find_var(skb_t *s, const char *name, size_t len) {
    for (int i = 0; i < s->var_count; i++) {
        if (strlen(s->vars[i].name) == len && !strncmp(s->vars[i].name, name, len)) return &s->vars[i];
    }
    return NULL;
}

int skb_var_set(skb_t *s, const char *name, skb_rgb_t rgb, float alpha, int override) {
    skb_var_t *v = find_var(s, name, strlen(name));
    if (v) {
        if (override || !v->overridden) {
            v->rgb = rgb;
            v->alpha = alpha;
            v->overridden |= override;
        }
        return 0;
    }
    if (s->var_count >= SKB_MAX_VARS || strlen(name) >= SKB_NAME) {
        fail(s, "too many colours, or a name too long", name);
        return -1;
    }
    v = &s->vars[s->var_count++];
    memset(v, 0, sizeof(*v));
    memcpy(v->name, name, strlen(name));
    v->rgb = rgb;
    v->alpha = alpha;
    v->overridden = override;
    return 0;
}

int skb_color(skb_t *s, const char *t, skb_rgb_t *out, float *alpha) {
    if (alpha) *alpha = 1.0f;
    if (t[0] != '$') {
        if (strlen(t) < 6 || hex_rgb(t, out) != 0) {
            fail(s, "bad colour", t);
            return -1;
        }
        return 0;
    }
    const char *name = t + 1, *tilde = strchr(name, '~');
    size_t len = tilde ? (size_t)(tilde - name) : strlen(name);
    skb_var_t *v = find_var(s, name, len);
    if (!v) {
        fail(s, "unknown colour", t);
        return -1;
    }
    *out = v->rgb;
    if (alpha) *alpha = v->alpha;
    if (!tilde) return 0;
    float k = (float)atof(tilde + 1);
    int night = len > 2 && !strncmp(name + len - 2, "-n", 2);
    skb_var_t *g = find_var(s, night ? "ground-n" : "ground", night ? 8u : 6u);
    if (!g) {
        fail(s, "no ground to move from", t);
        return -1;
    }
    float c[3] = { v->rgb.r, v->rgb.g, v->rgb.b }, b[3] = { g->rgb.r, g->rgb.g, g->rgb.b };
    for (int i = 0; i < 3; i++) {
        // in 8-bit steps, as the recipe's hex colours were
        float x = b[i] * 255.0f + k * (c[i] - b[i]) * 255.0f;
        x = x < 0 ? 0 : x > 255 ? 255 : x;
        c[i] = (float)roundi(x) / 255.0f;
    }
    *out = (skb_rgb_t){ c[0], c[1], c[2] };
    return 0;
}

// DX,DY,BLUR,COLOUR[,ALPHA%[,SPREAD]]
static int parse_shadow(skb_t *s, const char *t, skb_shadow_t *sh) {
    memset(sh, 0, sizeof(*sh));
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", t);
    char *f[6];
    int n = 0;
    for (char *p = buf; n < 6;) {
        f[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    if (n < 4) {
        fail(s, "bad shadow", t);
        return -1;
    }
    sh->dx = (float)atof(f[0]);
    sh->dy = (float)atof(f[1]);
    sh->blur = (float)atof(f[2]);
    float var_alpha = 1.0f;
    if (skb_color(s, f[3], &sh->color, &var_alpha) != 0) return -1;
    sh->alpha = (n > 4 ? (float)atof(f[4]) / 100.0f : 1.0f) * var_alpha;
    sh->spread = n > 5 ? (float)atof(f[5]) : 0.0f;
    return 0;
}

const skb_style_t *skb_find_style(const skb_t *s, const char *name) {
    for (int i = 0; i < s->style_count; i++) {
        if (strcmp(s->styles[i].name, name) == 0) return &s->styles[i];
    }
    return NULL;
}

// ---------------------------------------------------------------- canvas

skb_canvas_t skb_canvas_new(int w, int h) {
    skb_canvas_t c = { w, h, calloc((size_t)w * h * 4, sizeof(float)) };
    return c;
}

void skb_canvas_fill(skb_canvas_t *c, skb_rgb_t col) {
    for (int i = 0; i < c->w * c->h; i++) {
        c->px[i * 4 + 0] = col.r;
        c->px[i * 4 + 1] = col.g;
        c->px[i * 4 + 2] = col.b;
        c->px[i * 4 + 3] = 1.0f;
    }
}

// Paint colour (r,g,b) with per-pixel coverage `cov` scaled by `alpha`, over.
void skb_canvas_paint(skb_canvas_t *c, const float *cov, skb_rgb_t col, float alpha) {
    for (int i = 0; i < c->w * c->h; i++) {
        float a = cov[i] * alpha;
        if (a <= 0.0f) continue;
        float *p = &c->px[i * 4];
        p[0] = col.r * a + p[0] * (1.0f - a);
        p[1] = col.g * a + p[1] * (1.0f - a);
        p[2] = col.b * a + p[2] * (1.0f - a);
        p[3] = a + p[3] * (1.0f - a);
    }
}

// Signed distance from a pixel centre to a rounded rectangle.
static float rrect_sd(float px, float py, float x, float y, float w, float h, float r) {
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float qx = absf(px - cx) - (w * 0.5f - r);
    float qy = absf(py - cy) - (h * 0.5f - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    if (inside > 0) inside = 0;
    return sqrtf(ox * ox + oy * oy) + inside - r;
}

static void rrect_coverage(float *out, int w, int h, float x, float y, float rw, float rh, float r) {
    if (r > rw * 0.5f) r = rw * 0.5f;
    if (r > rh * 0.5f) r = rh * 0.5f;
    if (r < 0) r = 0;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            float sd = rrect_sd(i + 0.5f, j + 0.5f, x, y, rw, rh, r);
            float c = 0.5f - sd;
            out[j * w + i] = c < 0 ? 0 : (c > 1 ? 1 : c);
        }
    }
}

// Separable Gaussian, edges clamped (the mask continues as it is at the border).
void skb_blur(float *m, int w, int h, float sigma) {
    if (sigma <= 0.01f) return;
    int rad = (int)ceilf(sigma * 3.0f);
    int n = rad * 2 + 1;
    float *k = calloc((size_t)n, sizeof(float));
    float *tmp = calloc((size_t)w * h, sizeof(float));
    if (!k || !tmp) {
        free(k);
        free(tmp);
        return;
    }
    float sum = 0;
    for (int i = 0; i < n; i++) {
        float x = (float)(i - rad);
        k[i] = expf(-(x * x) / (2 * sigma * sigma));
        sum += k[i];
    }
    for (int i = 0; i < n; i++) k[i] /= sum;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int t = -rad; t <= rad; t++) {
                int xx = x + t;
                xx = xx < 0 ? 0 : (xx >= w ? w - 1 : xx);
                acc += m[y * w + xx] * k[t + rad];
            }
            tmp[y * w + x] = acc;
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int t = -rad; t <= rad; t++) {
                int yy = y + t;
                yy = yy < 0 ? 0 : (yy >= h ? h - 1 : yy);
                acc += tmp[yy * w + x] * k[t + rad];
            }
            m[y * w + x] = acc;
        }
    }
    free(tmp);
    free(k);
}

static float shadow_reach(const skb_shadow_t *s) {
    return ceilf(s->blur * 0.5f * 3.0f) + s->spread;
}

// Draw a STYLE rounded rectangle at (x,y,w,h) radius r onto the canvas.
void skb_draw_surface(skb_canvas_t *c, const skb_style_t *st, float x, float y, float w, float h, float r) {
    size_t n = (size_t)c->w * c->h;
    float *m = calloc(n, sizeof(float));
    float *shape = calloc(n, sizeof(float));
    if (!m || !shape) {
        free(m);
        free(shape);
        return;
    }
    rrect_coverage(shape, c->w, c->h, x, y, w, h, r);

    // Outer shadows, bottom one first.
    for (int i = st->outer_count - 1; i >= 0; i--) {
        const skb_shadow_t *s = &st->outer[i];
        if (s->alpha <= 0.0f) continue;   // a transparent one (the theme's)
        rrect_coverage(m, c->w, c->h, x + s->dx - s->spread, y + s->dy - s->spread,
                       w + 2 * s->spread, h + 2 * s->spread, r + s->spread);
        skb_blur(m, c->w, c->h, s->blur * 0.5f);
        skb_canvas_paint(c, m, s->color, s->alpha);
    }

    // Background.
    if (!st->gradient) {
        skb_canvas_paint(c, shape, st->fill, 1.0f);
    } else {
        float rad = st->grad_deg * SKB_PI / 180.0f;
        float dx = sinf(rad), dy = -cosf(rad);
        float len = absf(w * dx) + absf(h * dy);
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        for (int j = 0; j < c->h; j++) {
            for (int i = 0; i < c->w; i++) {
                float a = shape[j * c->w + i];
                if (a <= 0) continue;
                float t = ((i + 0.5f - cx) * dx + (j + 0.5f - cy) * dy) / len + 0.5f;
                t = t / st->grad_stop;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                skb_rgb_t col = { st->fill.r + (st->grad_to.r - st->fill.r) * t,
                                  st->fill.g + (st->grad_to.g - st->fill.g) * t,
                                  st->fill.b + (st->grad_to.b - st->fill.b) * t };
                float *p = &c->px[(j * c->w + i) * 4];
                p[0] = col.r * a + p[0] * (1 - a);
                p[1] = col.g * a + p[1] * (1 - a);
                p[2] = col.b * a + p[2] * (1 - a);
                p[3] = a + p[3] * (1 - a);
            }
        }
    }

    // Inset shadows, bottom one first: cast by the outside of the (shrunk,
    // offset) shape, clipped to the shape.
    for (int i = st->inner_count - 1; i >= 0; i--) {
        const skb_shadow_t *s = &st->inner[i];
        if (s->alpha <= 0.0f) continue;
        rrect_coverage(m, c->w, c->h, x + s->dx + s->spread, y + s->dy + s->spread,
                       w - 2 * s->spread, h - 2 * s->spread, r - s->spread);
        for (size_t k = 0; k < n; k++) m[k] = 1.0f - m[k];
        skb_blur(m, c->w, c->h, s->blur * 0.5f);
        for (size_t k = 0; k < n; k++) m[k] *= shape[k];
        skb_canvas_paint(c, m, s->color, s->alpha);
    }

    // The outline: the shape less the shape one pixel smaller.
    if (st->stroke_alpha > 0.0f && w > 2 && h > 2) {
        rrect_coverage(m, c->w, c->h, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0);
        for (size_t k = 0; k < n; k++) m[k] = shape[k] > m[k] ? shape[k] - m[k] : 0.0f;
        skb_canvas_paint(c, m, st->stroke, st->stroke_alpha);
    }
    free(m);
    free(shape);
}

// Margins the shadows need around the shape on each side. A transparent
// shadow still keeps its margin: every theme's sprite has the same size,
// so the windows and the dock are laid out the same way.
void skb_style_pads(const skb_style_t *st, int pad[4]) {
    float l = 0, t = 0, r = 0, b = 0;
    for (int i = 0; i < st->outer_count; i++) {
        const skb_shadow_t *s = &st->outer[i];
        float reach = shadow_reach(s);
        if (reach - s->dx > l) l = reach - s->dx;
        if (reach - s->dy > t) t = reach - s->dy;
        if (reach + s->dx > r) r = reach + s->dx;
        if (reach + s->dy > b) b = reach + s->dy;
    }
    pad[0] = (int)ceilf(l);
    pad[1] = (int)ceilf(t);
    pad[2] = (int)ceilf(r);
    pad[3] = (int)ceilf(b);
}

// How far a corner disturbs the edge next to it: beyond this the edge
// profile is the same everywhere, so one pixel of it can be stretched.
static int style_settle(const skb_style_t *st) {
    float k = 1;
    for (int i = 0; i < st->outer_count; i++) {
        const skb_shadow_t *s = &st->outer[i];
        float v = shadow_reach(s) + fmaxf(absf(s->dx), absf(s->dy));
        if (v > k) k = v;
    }
    for (int i = 0; i < st->inner_count; i++) {
        const skb_shadow_t *s = &st->inner[i];
        float v = shadow_reach(s) + fmaxf(absf(s->dx), absf(s->dy)) + s->spread;
        if (v > k) k = v;
    }
    return (int)ceilf(k);
}

static uint8_t to8(float v) {
    v = v * 255.0f + 0.5f;
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Canvas to BGRA32 (premultiplied, or opaque when the canvas is).
uint8_t *skb_canvas_bgra(const skb_canvas_t *c) {
    uint8_t *out = calloc((size_t)c->w * c->h, 4);
    if (!out) return NULL;
    for (int i = 0; i < c->w * c->h; i++) {
        const float *p = &c->px[i * 4];
        out[i * 4 + 0] = to8(p[2]);
        out[i * 4 + 1] = to8(p[1]);
        out[i * 4 + 2] = to8(p[0]);
        out[i * 4 + 3] = to8(p[3]);
    }
    return out;
}

// ---------------------------------------------------------------- entries

void skb_clear_faint(uint8_t *bgra, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        if (bgra[i * 4 + 3] < SHADOW_ALPHA_MIN) memset(&bgra[i * 4], 0, 4);
    }
}

// Clear a premultiplied sprite's faint tails and crop it to what is left,
// keeping its pads and nine-slice insets in step.
void skb_trim_faint(skb_entry_t *en) {
    slsk_entry_t *e = &en->e;
    int w = e->width, h = e->height;
    skb_clear_faint(en->data, (size_t)w * h);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (!en->data[((size_t)y * w + x) * 4 + 3]) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    if (x1 < 0) return;
    // Never into the shape itself (or a nine-slice inset).
    if (x0 > e->pad_l) x0 = e->pad_l;
    if (y0 > e->pad_t) y0 = e->pad_t;
    if (x1 < w - 1 - e->pad_r) x1 = w - 1 - e->pad_r;
    if (y1 < h - 1 - e->pad_b) y1 = h - 1 - e->pad_b;
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    if (nw == w && nh == h) return;
    uint8_t *out = calloc((size_t)nw * nh, 4);
    if (!out) return;
    for (int y = 0; y < nh; y++) memcpy(out + (size_t)y * nw * 4, en->data + ((size_t)(y + y0) * w + x0) * 4, (size_t)nw * 4);
    free(en->data);
    en->data = out;
    e->pad_l = (uint16_t)(e->pad_l - x0);
    e->pad_t = (uint16_t)(e->pad_t - y0);
    e->pad_r = (uint16_t)(e->pad_r - (w - 1 - x1));
    e->pad_b = (uint16_t)(e->pad_b - (h - 1 - y1));
    if (e->slice_l || e->slice_r) {
        e->slice_l = (uint16_t)(e->slice_l - x0);
        e->slice_r = (uint16_t)(e->slice_r - (w - 1 - x1));
    }
    if (e->slice_t || e->slice_b) {
        e->slice_t = (uint16_t)(e->slice_t - y0);
        e->slice_b = (uint16_t)(e->slice_b - (h - 1 - y1));
    }
    e->width = (uint16_t)nw;
    e->height = (uint16_t)nh;
    e->size = (uint32_t)nw * nh * 4u;
}

skb_entry_t *skb_add_entry(skb_t *s, const char *name, uint16_t kind) {
    if (strlen(name) >= SLSK_NAME_MAX) {
        fail(s, "name too long", name);
        return NULL;
    }
    for (int i = 0; i < s->entry_count; i++) {
        if (strcmp(s->entries[i].e.name, name) == 0) {
            fail(s, "duplicate entry", name);
            return NULL;
        }
    }
    if (s->entry_count == s->entry_cap) {
        int cap = s->entry_cap ? s->entry_cap * 2 : 64;
        skb_entry_t *grown = realloc(s->entries, (size_t)cap * sizeof(*grown));
        if (!grown) {
            fail(s, "out of memory", NULL);
            return NULL;
        }
        s->entries = grown;
        s->entry_cap = cap;
    }
    skb_entry_t *en = &s->entries[s->entry_count++];
    memset(en, 0, sizeof(*en));
    memcpy(en->e.name, name, strlen(name));
    en->e.kind = kind;
    return en;
}

// A nine-slice surface. height 0 = stretches both ways.
int skb_surface(skb_t *s, const char *name, const skb_style_t *st, int radius, int height, int alpha) {
    int pad[4];
    skb_style_pads(st, pad);
    int settle = style_settle(st);
    int shape_w = 2 * (radius + settle) + 1;
    int shape_h = height ? height : shape_w;
    int w = pad[0] + shape_w + pad[2];
    int h = pad[1] + shape_h + pad[3];

    // Opaque on the ground, or (alpha) premultiplied on nothing as NAME-a.
    for (int pass = alpha ? 1 : 0; pass < (alpha ? 2 : 1); pass++) {
        skb_canvas_t c = skb_canvas_new(w, h);
        if (!c.px) {
            fail(s, "out of memory", name);
            return -1;
        }
        if (pass == 0) skb_canvas_fill(&c, s->ground);
        skb_draw_surface(&c, st, (float)pad[0], (float)pad[1], (float)shape_w, (float)shape_h, (float)radius);

        char full[SLSK_NAME_MAX + 4];
        snprintf(full, sizeof(full), pass == 0 ? "%s" : "%s-a", name);
        skb_entry_t *en = skb_add_entry(s, full, pass == 0 ? SLSK_KIND_OPAQUE : SLSK_KIND_PREMUL);
        if (!en) {
            free(c.px);
            return -1;
        }
        en->e.width = (uint16_t)w;
        en->e.height = (uint16_t)h;
        en->e.slice_l = (uint16_t)(pad[0] + radius + settle);
        en->e.slice_r = (uint16_t)(w - en->e.slice_l - 1);
        if (!height) {
            en->e.slice_t = (uint16_t)(pad[1] + radius + settle);
            en->e.slice_b = (uint16_t)(h - en->e.slice_t - 1);
        }
        en->e.pad_l = (uint16_t)pad[0];
        en->e.pad_t = (uint16_t)pad[1];
        en->e.pad_r = (uint16_t)pad[2];
        en->e.pad_b = (uint16_t)pad[3];
        en->data = skb_canvas_bgra(&c);
        en->e.size = (uint32_t)w * h * 4u;
        en->style = st;
        en->radius = radius;
        en->height = height;
        free(c.px);
        if (!en->data) {
            fail(s, "out of memory", name);
            return -1;
        }
        if (pass == 1) skb_trim_faint(en);
    }
    return 0;
}

// A tile, shadows included: on the ground, or (alpha) premultiplied on
// nothing, for tiles whose shadow reaches past the pearl they sit on. The
// icon, if any, is drawn by the caller's function over it.
int skb_tile(skb_t *s, const char *name, const skb_style_t *st, int size, int radius, int alpha,
             void (*icon)(skb_canvas_t *c, float x, float y, void *user), void *user) {
    int pad[4];
    skb_style_pads(st, pad);
    int w = pad[0] + size + pad[2];
    int h = pad[1] + size + pad[3];
    skb_canvas_t c = skb_canvas_new(w, h);
    if (!c.px) {
        fail(s, "out of memory", name);
        return -1;
    }
    if (!alpha) skb_canvas_fill(&c, s->ground);
    skb_draw_surface(&c, st, (float)pad[0], (float)pad[1], (float)size, (float)size, (float)radius);
    if (icon) icon(&c, (float)pad[0], (float)pad[1], user);

    skb_entry_t *en = skb_add_entry(s, name, alpha ? SLSK_KIND_PREMUL : SLSK_KIND_OPAQUE);
    if (!en) {
        free(c.px);
        return -1;
    }
    en->e.width = (uint16_t)w;
    en->e.height = (uint16_t)h;
    en->e.pad_l = (uint16_t)pad[0];
    en->e.pad_t = (uint16_t)pad[1];
    en->e.pad_r = (uint16_t)pad[2];
    en->e.pad_b = (uint16_t)pad[3];
    en->data = skb_canvas_bgra(&c);
    en->e.size = (uint32_t)w * h * 4u;
    free(c.px);
    if (!en->data) {
        fail(s, "out of memory", name);
        return -1;
    }
    if (alpha) skb_trim_faint(en);
    return 0;
}

// A shape from a coverage mask: what a CSS drop-shadow does with an SVG's
// alpha (the app manager's fin), the fill and the outline from the style.
int skb_shape(skb_t *s, const char *name, const skb_style_t *st, const uint8_t *cov, int sw, int sh, int open) {
    int pad[4];
    skb_style_pads(st, pad);
    int w = pad[0] + sw + pad[2], h = pad[1] + sh + pad[3];
    size_t n = (size_t)w * h;
    float *shape = calloc(n, sizeof(float));
    float *m = calloc(n, sizeof(float));
    skb_canvas_t c = skb_canvas_new(w, h);
    if (!shape || !m || !c.px) {
        free(shape);
        free(m);
        free(c.px);
        fail(s, "out of memory", name);
        return -1;
    }
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) shape[(size_t)(y + pad[1]) * w + x + pad[0]] = cov[(size_t)y * sw + x] / 255.0f;
    }
    for (int i = st->outer_count - 1; i >= 0; i--) {
        const skb_shadow_t *sd = &st->outer[i];
        if (sd->alpha <= 0.0f) continue;   // a transparent one (the theme's)
        int dx = roundi(sd->dx), dy = roundi(sd->dy);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int sx = x - dx, sy = y - dy;
                m[(size_t)y * w + x] = (sx >= 0 && sy >= 0 && sx < w && sy < h) ? shape[(size_t)sy * w + sx] : 0.0f;
            }
        }
        skb_blur(m, w, h, sd->blur * 0.5f);
        skb_canvas_paint(&c, m, sd->color, sd->alpha);
    }
    skb_canvas_paint(&c, shape, st->fill, 1.0f);
    if (st->stroke_alpha > 0.0f) {   // the outline: the shape less the shape a pixel thinner
        // On an open side (SKB_OPEN_*) the shape goes on in what it is drawn
        // against (the fin in the dock): no outline along it.
        int x0 = pad[0], y0 = pad[1], x1 = pad[0] + sw - 1, y1 = pad[1] + sh - 1;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float v = shape[(size_t)y * w + x], in = v;
                if (v <= 0.0f) {
                    m[(size_t)y * w + x] = 0.0f;
                    continue;
                }
                float l = (x == x0 && (open & SKB_OPEN_LEFT)) ? v : x > 0 ? shape[(size_t)y * w + x - 1] : 0.0f;
                float r = (x == x1 && (open & SKB_OPEN_RIGHT)) ? v : x < w - 1 ? shape[(size_t)y * w + x + 1] : 0.0f;
                float t = (y == y0 && (open & SKB_OPEN_TOP)) ? v : y > 0 ? shape[(size_t)(y - 1) * w + x] : 0.0f;
                float b = (y == y1 && (open & SKB_OPEN_BOTTOM)) ? v : y < h - 1 ? shape[(size_t)(y + 1) * w + x] : 0.0f;
                if (l < in) in = l;
                if (r < in) in = r;
                if (t < in) in = t;
                if (b < in) in = b;
                m[(size_t)y * w + x] = v - in;
            }
        }
        skb_canvas_paint(&c, m, st->stroke, st->stroke_alpha);
    }
    free(m);
    free(shape);
    skb_entry_t *en = skb_add_entry(s, name, SLSK_KIND_PREMUL);
    if (!en) {
        free(c.px);
        return -1;
    }
    en->e.width = (uint16_t)w;
    en->e.height = (uint16_t)h;
    en->e.pad_l = (uint16_t)pad[0];
    en->e.pad_t = (uint16_t)pad[1];
    en->e.pad_r = (uint16_t)pad[2];
    en->e.pad_b = (uint16_t)pad[3];
    en->data = skb_canvas_bgra(&c);
    en->e.size = (uint32_t)w * h * 4u;
    free(c.px);
    if (!en->data) {
        fail(s, "out of memory", name);
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------- the recipe

int skb_recipe_line(skb_t *s, char **tok, int n) {
    if (!strcmp(tok[0], "color") && (n == 3 || n == 4)) {
        skb_rgb_t c;
        unsigned a = 255u;
        if (strlen(tok[2]) < 6 || hex_rgb(tok[2], &c) != 0 ||
            (n == 4 && (strlen(tok[3]) != 2 || sscanf(tok[3], "%2x", &a) != 1))) {
            fail(s, "bad colour", tok[n == 4 ? 3 : 2]);
            return -1;
        }
        return skb_var_set(s, tok[1], c, (float)a / 255.0f, 0) == 0 ? 1 : -1;
    }
    if (!strcmp(tok[0], "ground") && n == 2) {
        return skb_color(s, tok[1], &s->ground, NULL) == 0 ? 1 : -1;
    }
    if (!strcmp(tok[0], "style") && n >= 3) {
        if (s->style_count >= SKB_MAX_STYLES) {
            fail(s, "too many styles", tok[1]);
            return -1;
        }
        skb_style_t *st = &s->styles[s->style_count++];
        memset(st, 0, sizeof(*st));
        snprintf(st->name, sizeof(st->name), "%s", tok[1]);
        for (int i = 2; i < n; i++) {
            const char *t = tok[i];
            if (!strncmp(t, "fill=", 5)) {
                if (skb_color(s, t + 5, &st->fill, NULL) != 0) return -1;
            } else if (!strncmp(t, "grad=", 5)) {
                char a[24], b[24];
                float deg = 0, stop = 0;
                if (sscanf(t + 5, "%23[^,],%23[^,],%f,%f", a, b, &deg, &stop) != 4 ||
                    skb_color(s, a, &st->fill, NULL) != 0 || skb_color(s, b, &st->grad_to, NULL) != 0) {
                    fail(s, "bad gradient", t);
                    return -1;
                }
                st->gradient = 1;
                st->grad_deg = deg;
                st->grad_stop = stop / 100.0f;
            } else if (!strncmp(t, "stroke=", 7)) {
                if (skb_color(s, t + 7, &st->stroke, &st->stroke_alpha) != 0) return -1;
            } else if (!strncmp(t, "shadow=", 7) || !strncmp(t, "inset=", 6)) {
                int outer = t[0] == 's';
                skb_shadow_t *list = outer ? st->outer : st->inner;
                int *count = outer ? &st->outer_count : &st->inner_count;
                if (*count >= SKB_MAX_SHADOWS) {
                    fail(s, "too many shadows", st->name);
                    return -1;
                }
                if (parse_shadow(s, t + (outer ? 7 : 6), &list[(*count)++]) != 0) return -1;
            } else {
                fail(s, "unknown style field", t);
                return -1;
            }
        }
        return 1;
    }
    if (!strcmp(tok[0], "surface") && n >= 4) {
        const skb_style_t *st = skb_find_style(s, tok[2]);
        if (!st) {
            fail(s, "unknown style", tok[2]);
            return -1;
        }
        int height = 0, alpha = 0;
        for (int i = 4; i < n; i++) {
            if (!strcmp(tok[i], "alpha")) alpha = 1;
            else height = atoi(tok[i]);
        }
        return skb_surface(s, tok[1], st, atoi(tok[3]), height, alpha) == 0 ? 1 : -1;
    }
    if (!strcmp(tok[0], "shape") && (n == 4 || n == 5) && s->mask) {   // shape NAME MASK STYLE [open=SIDES]
        int open = 0;
        if (n == 5) {
            if (strncmp(tok[4], "open=", 5) != 0) {
                fail(s, "unknown shape field", tok[4]);
                return -1;
            }
            for (const char *p = tok[4] + 5; *p; p++) {
                if (!strncmp(p, "left", 4)) open |= SKB_OPEN_LEFT, p += 3;
                else if (!strncmp(p, "right", 5)) open |= SKB_OPEN_RIGHT, p += 4;
                else if (!strncmp(p, "top", 3)) open |= SKB_OPEN_TOP, p += 2;
                else if (!strncmp(p, "bottom", 6)) open |= SKB_OPEN_BOTTOM, p += 5;
                else if (*p != ',') {
                    fail(s, "unknown side", tok[4]);
                    return -1;
                }
            }
        }
        const skb_style_t *st = skb_find_style(s, tok[3]);
        int mw = 0, mh = 0;
        const uint8_t *cov = s->mask(s->mask_user, tok[2], &mw, &mh);
        if (!st || !cov) {
            fail(s, st ? "unknown mask" : "unknown style", st ? tok[2] : tok[3]);
            return -1;
        }
        return skb_shape(s, tok[1], st, cov, mw, mh, open) == 0 ? 1 : -1;
    }
    if (!strcmp(tok[0], "tilebase") && (n == 5 || (n == 6 && !strcmp(tok[5], "alpha")))) {
        // The tile alone, as "SET-blank": the icon comes on top at run time.
        const skb_style_t *st = skb_find_style(s, tok[2]);
        if (!st) {
            fail(s, "unknown style", tok[2]);
            return -1;
        }
        char name[SLSK_NAME_MAX + 8];
        snprintf(name, sizeof(name), "%s-blank", tok[1]);
        return skb_tile(s, name, st, atoi(tok[3]), atoi(tok[4]), n == 6, NULL, NULL) == 0 ? 1 : -1;
    }
    return 0;
}

// ---------------------------------------------------------------- the file

size_t skb_write(const skb_t *s, uint8_t **out) {
    *out = NULL;
    uint32_t off = sizeof(slsk_header_t) + (uint32_t)s->entry_count * sizeof(slsk_entry_t);
    uint32_t *at = calloc((size_t)(s->entry_count ? s->entry_count : 1), sizeof(uint32_t));
    if (!at) return 0;
    for (int i = 0; i < s->entry_count; i++) {
        off = (off + 15u) & ~15u;   // rows start 16-byte aligned in memory
        at[i] = off;
        off += s->entries[i].e.size;
    }
    uint8_t *buf = calloc(off ? off : 1u, 1);
    if (!buf) {
        free(at);
        return 0;
    }
    slsk_header_t h = { SLSK_MAGIC, SLSK_VERSION, (uint32_t)s->entry_count, off };
    memcpy(buf, &h, sizeof(h));
    for (int i = 0; i < s->entry_count; i++) {
        slsk_entry_t e = s->entries[i].e;
        e.offset = at[i];
        memcpy(buf + sizeof(h) + (size_t)i * sizeof(e), &e, sizeof(e));
        memcpy(buf + at[i], s->entries[i].data, e.size);
    }
    free(at);
    *out = buf;
    return off;
}
