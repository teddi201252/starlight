#include "blit.h"

#include <string.h>

#include "render_ops.h"
#include "skin.h"

// x * a / 255, rounded, for 8-bit values.
static inline uint32_t mul255(uint32_t x, uint32_t a) {
    uint32_t v = x * a + 128u;
    return (v + (v >> 8)) >> 8;
}

// ---------------------------------------------------------------- row kernels

static void copy_row(uint8_t *dst, const uint8_t *src, uint32_t n, uint8_t bpp) {
    if (bpp == 4u) {
        memcpy(dst, src, (size_t)n * 4u);
        return;
    }
    for (uint32_t i = 0; i < n; i++, dst += 3, src += 4) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }
}

// Premultiplied source over the destination.
static void blend_row(uint8_t *dst, const uint8_t *src, uint32_t n, uint8_t bpp) {
    if (bpp == 4u) {
        sl_render_blend_premul_bgra32((uint32_t *)(void *)dst, (const uint32_t *)(const void *)src, n);
        return;
    }
    for (uint32_t i = 0; i < n; i++, dst += bpp, src += 4) {
        uint32_t a = src[3];
        if (a == 0u) continue;
        if (a == 255u) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        } else {
            uint32_t inv = 255u - a;
            dst[0] = (uint8_t)(src[0] + mul255(dst[0], inv));
            dst[1] = (uint8_t)(src[1] + mul255(dst[1], inv));
            dst[2] = (uint8_t)(src[2] + mul255(dst[2], inv));
        }
        if (bpp == 4u) dst[3] = 0xFFu;
    }
}

// n copies of one source pixel: the stretched middle.
static void fill_row(uint8_t *dst, const uint8_t *px, uint32_t n, uint8_t bpp, int premul) {
    if (premul && px[3] != 255u) {
        if (px[3] == 0u) return;
        if (bpp == 4u) {   // the shadow above and below a window: one colour across
            uint32_t v;
            memcpy(&v, px, 4);
            sl_render_blend_premul_const_bgra32((uint32_t *)(void *)dst, v, n);
            return;
        }
        for (uint32_t i = 0; i < n; i++, dst += bpp) blend_row(dst, px, 1u, bpp);
        return;
    }
    if (bpp == 4u) {
        uint32_t v;
        memcpy(&v, px, 4);
        sl_render_fill_bgra32((uint32_t *)(void *)dst, n, v | 0xFF000000u);
        return;
    }
    for (uint32_t i = 0; i < n; i++, dst += 3) {
        dst[0] = px[0];
        dst[1] = px[1];
        dst[2] = px[2];
    }
}

static void mask_row(uint8_t *dst, const uint8_t *mask, uint32_t n, uint8_t bpp,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    for (uint32_t i = 0; i < n; i++, dst += bpp) {
        uint32_t a = mask[i];
        if (alpha != 255u) a = mul255(a, alpha);
        if (a == 0u) continue;
        if (a == 255u) {
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
        } else {
            uint32_t inv = 255u - a;
            dst[0] = (uint8_t)(mul255(b, a) + mul255(dst[0], inv));
            dst[1] = (uint8_t)(mul255(g, a) + mul255(dst[1], inv));
            dst[2] = (uint8_t)(mul255(r, a) + mul255(dst[2], inv));
        }
        if (bpp == 4u) dst[3] = 0xFFu;
    }
}

// ------------------------------------------------------------- nine slices

// Source index along an axis of `size` drawn `target` long with insets lo/hi.
static inline uint32_t slice_src(uint32_t d, uint32_t size, uint32_t lo, uint32_t hi, uint32_t target) {
    if (d < lo) return d;
    if (d >= target - hi) return size - (target - d);
    uint32_t mid = size - lo - hi;
    return lo + (mid > 1u ? (d - lo) % mid : 0u);
}

void sl_blit_sprite(const sl_target_t *t, const slsk_entry_t *e, const uint8_t *data,
                    int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!t || !t->px || !e || !data || (t->bpp != 3u && t->bpp != 4u)) return;
    if (e->kind != SLSK_KIND_OPAQUE && e->kind != SLSK_KIND_PREMUL) return;
    int premul = e->kind == SLSK_KIND_PREMUL;

    // Insets per axis; an axis that does not stretch keeps the sprite size.
    uint32_t sl = e->slice_l, sr = e->slice_r, st = e->slice_t, sb = e->slice_b;
    if (sl == 0u && sr == 0u) w = e->width;
    if (st == 0u && sb == 0u) h = e->height;
    if (w == 0u || h == 0u) return;
    // Smaller than its corners: the corners are cut, nothing stretches.
    if (sl + sr > w) {
        sl = w / 2u;
        sr = w - sl;
    }
    if (st + sb > h) {
        st = h / 2u;
        sb = h - st;
    }

    int64_t x0 = x, y0 = y, x1 = (int64_t)x + w, y1 = (int64_t)y + h;
    if (x0 < t->clip_x0) x0 = t->clip_x0;
    if (y0 < t->clip_y0) y0 = t->clip_y0;
    if (x1 > t->clip_x1) x1 = t->clip_x1;
    if (y1 > t->clip_y1) y1 = t->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    uint32_t a = (uint32_t)(x0 - x), b = (uint32_t)(x1 - x);   // columns to draw, box-relative

    // The three column runs, box-relative: [0,sl) [sl,w-sr) [w-sr,w).
    const uint32_t run_lo[3] = { 0u, sl, w - sr };
    const uint32_t run_hi[3] = { sl, w - sr, w };
    size_t src_stride = (size_t)e->width * 4u;

    for (int64_t yy = y0; yy < y1; yy++) {
        uint32_t sy = slice_src((uint32_t)(yy - y), e->height, st, sb, h);
        const uint8_t *srow = data + (size_t)sy * src_stride;
        uint8_t *drow = t->px + (size_t)yy * t->stride;
        for (int k = 0; k < 3; k++) {
            uint32_t lo = run_lo[k] > a ? run_lo[k] : a;
            uint32_t hi = run_hi[k] < b ? run_hi[k] : b;
            if (hi <= lo) continue;
            uint8_t *d = drow + ((size_t)x + lo) * t->bpp;
            if (k == 1 && w != e->width) {
                uint32_t mid = e->width - e->slice_l - e->slice_r;
                if (mid <= 1u) {
                    fill_row(d, srow + (size_t)e->slice_l * 4u, hi - lo, t->bpp, premul);
                    continue;
                }
                // A wider middle repeats: one source pixel at a time.
                for (uint32_t c = lo; c < hi; c++, d += t->bpp) {
                    const uint8_t *s = srow + (size_t)slice_src(c, e->width, sl, sr, w) * 4u;
                    if (premul) blend_row(d, s, 1u, t->bpp);
                    else copy_row(d, s, 1u, t->bpp);
                }
                continue;
            }
            uint32_t sx = slice_src(lo, e->width, sl, sr, w);
            const uint8_t *s = srow + (size_t)sx * 4u;
            if (premul) blend_row(d, s, hi - lo, t->bpp);
            else copy_row(d, s, hi - lo, t->bpp);
        }
    }
}

// ------------------------------------------------------------------- masks

void sl_blit_mask(const sl_target_t *t, const uint8_t *mask, uint32_t mw, uint32_t mh, size_t mstride,
                  int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    if (!t || !t->px || !mask || mw == 0u || mh == 0u || alpha == 0u) return;
    if (t->bpp != 3u && t->bpp != 4u) return;
    int64_t x0 = x, y0 = y, x1 = (int64_t)x + mw, y1 = (int64_t)y + mh;
    if (x0 < t->clip_x0) x0 = t->clip_x0;
    if (y0 < t->clip_y0) y0 = t->clip_y0;
    if (x1 > t->clip_x1) x1 = t->clip_x1;
    if (y1 > t->clip_y1) y1 = t->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    for (int64_t yy = y0; yy < y1; yy++) {
        const uint8_t *m = mask + (size_t)(yy - y) * mstride + (size_t)(x0 - x);
        uint8_t *d = t->px + (size_t)yy * t->stride + (size_t)x0 * t->bpp;
        mask_row(d, m, (uint32_t)(x1 - x0), t->bpp, r, g, b, alpha);
    }
}

// -------------------------------------------------------------------- text

uint32_t sl_utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = p[0];
    int extra;
    if (c < 0x80u) {
        *s += 1;
        return c;
    } else if ((c & 0xE0u) == 0xC0u) {
        extra = 1;
        c &= 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
        extra = 2;
        c &= 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
        extra = 3;
        c &= 0x07u;
    } else {
        *s += 1;
        return 0xFFFDu;
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0u) != 0x80u) {
            *s += i;   // stop before the byte that broke it (may be the NUL)
            return 0xFFFDu;
        }
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    *s += extra + 1;
    return c;
}

// Walk the text: pen in 26.6, kerning between known glyphs. Draws when t is set.
static int32_t text_walk(const sl_target_t *t, const slsk_font_t *font, int32_t x, int32_t baseline,
                         const char *utf8, uint8_t r, uint8_t g, uint8_t b) {
    if (!font || !utf8) return x;
    const slsk_glyph_t *glyphs = sl_skin_glyphs(font);
    const uint8_t *atlas = sl_skin_atlas(font);
    int64_t pen = (int64_t)x * 64;
    int prev = -1;
    const char *s = utf8;
    while (*s) {
        int gi = sl_skin_glyph_index(font, sl_utf8_next(&s));
        if (gi < 0) continue;
        if (prev >= 0 && font->kern_count) pen += sl_skin_kern(font, prev, gi);
        const slsk_glyph_t *gl = &glyphs[gi];
        if (t && gl->w && gl->h) {
            int32_t gx = (int32_t)((pen + 32) >> 6) + gl->xoff;
            sl_blit_mask(t, atlas + (size_t)gl->y * font->atlas_w + gl->x, gl->w, gl->h, font->atlas_w,
                         gx, baseline + gl->yoff, r, g, b, 255u);
        }
        pen += gl->advance;
        prev = gi;
    }
    return (int32_t)((pen + 32) >> 6);
}

int32_t sl_text_draw(const sl_target_t *t, const slsk_font_t *font, int32_t x, int32_t baseline,
                     const char *utf8, uint8_t r, uint8_t g, uint8_t b) {
    if (!t || !t->px) return x;
    return text_walk(t, font, x, baseline, utf8, r, g, b);
}

uint32_t sl_text_width(const slsk_font_t *font, const char *utf8) {
    int32_t end = text_walk(NULL, font, 0, 0, utf8, 0, 0, 0);
    return end > 0 ? (uint32_t)end : 0u;
}
