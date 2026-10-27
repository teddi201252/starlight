#pragma once

// Soft UI surfaces drawn at an exact size: a rounded rectangle with a fill
// and CSS-like box-shadows, premultiplied BGRA. The controls of the apps come
// in many small sizes that nine slices cannot cut (their corners overlap),
// so each (style, size) is rendered once, the first time it is drawn, and
// kept in a small cache: after that, drawing it is copying it. A big one is
// rendered small, with a middle one pixel wide and high, and that pixel is
// repeated (the same result: far from the edges every column and every row
// is the same), so a new size costs a copy, not a render.
//
// Pure C with integer blurs and no libm, so it also builds on the host for
// tests.

#include <stddef.h>
#include <stdint.h>

#define SL_SURF_MAX_SHADOWS 3

typedef struct {
    int8_t dx, dy;
    uint8_t blur;          // CSS blur radius
    uint8_t inset;         // 1 = inset shadow
    uint8_t r, g, b, a;
} sl_surf_shadow_t;

typedef struct {
    uint8_t fill_r, fill_g, fill_b;
    uint8_t shadow_count;
    sl_surf_shadow_t shadows[SL_SURF_MAX_SHADOWS];   // first is on top
} sl_surf_style_t;

typedef struct {
    uint32_t w, h;                 // the sprite, shadows included
    uint32_t pad_l, pad_t;         // where the shape starts in it
    uint8_t *px;                   // premultiplied BGRA, w * h * 4
} sl_surf_t;

// Render style at shape size sw x sh with radius r. Returns 1 on success;
// out->px is malloc'd (free it with sl_surf_free).
int sl_surf_render(const sl_surf_style_t *style, uint32_t sw, uint32_t sh, uint32_t r, sl_surf_t *out);
// The same, rendered at full size: slower, for the tests.
int sl_surf_render_exact(const sl_surf_style_t *style, uint32_t sw, uint32_t sh, uint32_t r, sl_surf_t *out);
void sl_surf_free(sl_surf_t *s);

// The cache: the surface for (style, sw, sh, r), rendered on first use.
// NULL if it cannot be made. Pointers stay valid until the next call that
// renders a new surface (an old one may be evicted then).
const sl_surf_t *sl_surf_get(const sl_surf_style_t *style, uint32_t sw, uint32_t sh, uint32_t r);
// Surfaces rendered and served from the cache, for the perf log.
void sl_surf_stats(uint32_t *rendered, uint32_t *cached_bytes);
