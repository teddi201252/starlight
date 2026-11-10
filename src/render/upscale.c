#include "upscale.h"

#include <stdlib.h>
#include <string.h>

void sl_upscale_logical_size(uint32_t pw, uint32_t ph, uint32_t scale_pct, uint32_t *lw, uint32_t *lh) {
    if (scale_pct < 100u) scale_pct = 100u;
    if (scale_pct > 300u) scale_pct = 300u;
    uint32_t w = (uint32_t)((uint64_t)pw * 100u / scale_pct);
    uint32_t h = (uint32_t)((uint64_t)ph * 100u / scale_pct);
    if (w == 0u) w = 1u;
    if (h == 0u) h = 1u;
    if (lw) *lw = w;
    if (lh) *lh = h;
}

// Screen pixel i of n, its centre mapped onto m logical pixels.
static void map_axis(uint32_t n, uint32_t m, uint32_t *idx, uint8_t *frac, int whole) {
    for (uint32_t i = 0; i < n; i++) {
        if (whole) {
            idx[i] = (uint32_t)((uint64_t)i * m / n);
            frac[i] = 0u;
            continue;
        }
        // (i + 0.5) * m / n - 0.5, in 1/256
        int64_t pos = (int64_t)(((uint64_t)(2u * i + 1u) * m * 256u) / (2u * (uint64_t)n)) - 128;
        if (pos < 0) pos = 0;
        uint32_t k = (uint32_t)(pos >> 8);
        uint8_t f = (uint8_t)(pos & 255);
        if (k >= m - 1u) {
            k = m - 1u;
            f = 0u;
        }
        idx[i] = k;
        frac[i] = f;
    }
}

int sl_upscale_init(sl_upscale_t *u, uint32_t lw, uint32_t lh, uint32_t pw, uint32_t ph) {
    if (!u || !lw || !lh || pw < lw || ph < lh) return 0;
    memset(u, 0, sizeof(*u));
    u->lw = lw;
    u->lh = lh;
    u->pw = pw;
    u->ph = ph;
    u->whole = (pw % lw) == 0u && (ph % lh) == 0u;
    u->x0 = (uint32_t *)malloc(sizeof(uint32_t) * pw);
    u->y0 = (uint32_t *)malloc(sizeof(uint32_t) * ph);
    u->fx = (uint8_t *)malloc(pw);
    u->fy = (uint8_t *)malloc(ph);
    u->line0 = (uint32_t *)malloc(sizeof(uint32_t) * pw);
    u->line1 = (uint32_t *)malloc(sizeof(uint32_t) * pw);
    if (!u->x0 || !u->y0 || !u->fx || !u->fy || !u->line0 || !u->line1) {
        sl_upscale_free(u);
        return 0;
    }
    map_axis(pw, lw, u->x0, u->fx, u->whole);
    map_axis(ph, lh, u->y0, u->fy, u->whole);
    return 1;
}

void sl_upscale_free(sl_upscale_t *u) {
    if (!u) return;
    free(u->x0);
    free(u->y0);
    free(u->fx);
    free(u->fy);
    free(u->line0);
    free(u->line1);
    memset(u, 0, sizeof(*u));
}

int sl_upscale_rect(const sl_upscale_t *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t *px,
                    uint32_t *py, uint32_t *pw, uint32_t *ph) {
    if (!u || !u->lw || w == 0u || h == 0u || x >= u->lw || y >= u->lh) return 0;
    uint64_t x1 = (uint64_t)x + w, y1 = (uint64_t)y + h;
    if (x1 > u->lw) x1 = u->lw;
    if (y1 > u->lh) y1 = u->lh;
    int64_t a = (int64_t)((uint64_t)x * u->pw / u->lw) - 1;
    int64_t b = (int64_t)((uint64_t)y * u->ph / u->lh) - 1;
    uint64_t c = (x1 * u->pw + u->lw - 1u) / u->lw + 1u;
    uint64_t d = (y1 * u->ph + u->lh - 1u) / u->lh + 1u;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (c > u->pw) c = u->pw;
    if (d > u->ph) d = u->ph;
    if ((uint64_t)a >= c || (uint64_t)b >= d) return 0;
    *px = (uint32_t)a;
    *py = (uint32_t)b;
    *pw = (uint32_t)(c - (uint64_t)a);
    *ph = (uint32_t)(d - (uint64_t)b);
    return 1;
}

// a + (b - a) * f / 256 on all four channels at once: red/blue and
// alpha/green as two pairs of 16-bit lanes (the weights add up to 256, so
// no lane overflows into the next).
static inline uint32_t lerp2(uint32_t a, uint32_t b, uint32_t f) {
    uint32_t g = 256u - f;
    uint32_t rb = (((a & 0x00FF00FFu) * g + (b & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu;
    uint32_t ag = (((a >> 8) & 0x00FF00FFu) * g + ((b >> 8) & 0x00FF00FFu) * f) & 0xFF00FF00u;
    return rb | ag;
}

// One logical row enlarged across the screen columns px..px+pw: copied
// (whole ratios) or interpolated horizontally.
static void line_32(const sl_upscale_t *u, const uint32_t *row, uint32_t *out, uint32_t px, uint32_t pw) {
    const uint32_t *x0 = u->x0 + px;
    if (u->whole) {
        if (u->pw == 2u * u->lw) {
            uint32_t i = 0;
            if (px & 1u) out[i++] = row[px >> 1];
            for (; i + 1u < pw; i += 2u) {
                uint32_t v = row[(px + i) >> 1];
                out[i] = v;
                out[i + 1u] = v;
            }
            if (i < pw) out[i] = row[(px + i) >> 1];
            return;
        }
        for (uint32_t i = 0; i < pw; i++) out[i] = row[x0[i]];
        return;
    }
    const uint8_t *fx = u->fx + px;
    for (uint32_t i = 0; i < pw; i++) {
        uint32_t k = x0[i], f = fx[i];
        out[i] = f ? lerp2(row[k], row[k + 1u], f) : row[k];
    }
}

static void blit_32(const sl_upscale_t *u, const uint8_t *src, size_t src_stride, uint8_t *dst, size_t dst_stride,
                    uint32_t px, uint32_t py, uint32_t pw, uint32_t ph) {
    // The screen rows are built in memory and then written out in one go:
    // a row repeated (whole ratios) or shared by two output rows is built
    // once, and the framebuffer is only ever written, never read back.
    uint32_t *ha = u->line0, *hb = u->line1;
    uint32_t ta = UINT32_MAX, tb = UINT32_MAX;
    size_t bytes = (size_t)pw * 4u;
    for (uint32_t j = 0; j < ph; j++) {
        uint32_t Y = py + j;
        uint32_t sy = u->y0[Y];
        uint8_t *out = dst + (size_t)Y * dst_stride + (size_t)px * 4u;
        if (ta != sy) {
            if (tb == sy) {
                uint32_t *t = ha; ha = hb; hb = t;
                tb = ta;
                ta = sy;
            } else {
                line_32(u, (const uint32_t *)(const void *)(src + (size_t)sy * src_stride), ha, px, pw);
                ta = sy;
            }
        }
        uint32_t fy = u->whole ? 0u : u->fy[Y];
        if (!fy) {
            memcpy(out, ha, bytes);
            continue;
        }
        uint32_t sy1 = sy + 1u;   // fy is 0 on the last row
        if (tb != sy1) {
            line_32(u, (const uint32_t *)(const void *)(src + (size_t)sy1 * src_stride), hb, px, pw);
            tb = sy1;
        }
        uint32_t *o = (uint32_t *)(void *)out;
        for (uint32_t i = 0; i < pw; i++) o[i] = lerp2(ha[i], hb[i], fy);
    }
}

void sl_upscale_blit(const sl_upscale_t *u, const uint8_t *src, size_t src_stride, uint8_t *dst, size_t dst_stride,
                     uint32_t bytespp, uint32_t px, uint32_t py, uint32_t pw, uint32_t ph) {
    if (!u || !src || !dst || pw == 0u || ph == 0u) return;
    if (px + pw > u->pw || py + ph > u->ph) return;
    if (bytespp == 4u && u->line0) {
        blit_32(u, src, src_stride, dst, dst_stride, px, py, pw, ph);
        return;
    }
    // 16 and 24-bit screens: the nearest pixel.
    for (uint32_t j = 0; j < ph; j++) {
        uint32_t Y = py + j;
        const uint8_t *row0 = src + (size_t)u->y0[Y] * src_stride;
        uint8_t *out = dst + (size_t)Y * dst_stride + (size_t)px * bytespp;
        const uint32_t *x0 = u->x0 + px;
        if (bytespp == 2u) {
            uint16_t *o = (uint16_t *)(void *)out;
            const uint16_t *s = (const uint16_t *)(const void *)row0;
            for (uint32_t i = 0; i < pw; i++) o[i] = s[x0[i]];
        } else if (bytespp == 3u) {
            for (uint32_t i = 0; i < pw; i++) {
                const uint8_t *s = row0 + (size_t)x0[i] * 3u;
                out[i * 3u] = s[0];
                out[i * 3u + 1u] = s[1];
                out[i * 3u + 2u] = s[2];
            }
        } else {
            for (uint32_t i = 0; i < pw; i++) {
                memcpy(out + (size_t)i * bytespp, row0 + (size_t)x0[i] * bytespp, bytespp);
            }
        }
    }
}

uint32_t sl_upscale_to_logical_x(const sl_upscale_t *u, int64_t px) {
    if (!u || !u->pw) return 0u;
    if (px < 0) px = 0;
    uint64_t v = (uint64_t)px * u->lw / u->pw;
    return v >= u->lw ? u->lw - 1u : (uint32_t)v;
}

uint32_t sl_upscale_to_logical_y(const sl_upscale_t *u, int64_t py) {
    if (!u || !u->ph) return 0u;
    if (py < 0) py = 0;
    uint64_t v = (uint64_t)py * u->lh / u->ph;
    return v >= u->lh ? u->lh - 1u : (uint32_t)v;
}
