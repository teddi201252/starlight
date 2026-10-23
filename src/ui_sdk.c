#include "ui_sdk.h"

#include "keyboard_layout.h"
#include "render/surface.h"

#include <string.h>

static sl_ui_theme_t g_runtime_theme;
static uint32_t g_runtime_theme_version = 1u;
static uint8_t g_runtime_theme_initialized = 0u;

static sl_ui_color_t sl_ui_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    sl_ui_color_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

void sl_ui_theme_set_default(sl_ui_theme_t *theme) {
    if (!theme) return;
    theme->night = 0u;
    // Soft UI, day (design/starlight-soft-ui, tavola Componenti e colori):
    // pearl ground, ink text, night accent, neon focus.
    theme->colors[SL_UI_COLOR_BG_CANVAS] = sl_ui_rgba(0xE3u, 0xE6u, 0xECu, 255u);
    theme->colors[SL_UI_COLOR_BG_PANEL] = sl_ui_rgba(0xE3u, 0xE6u, 0xECu, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL] = sl_ui_rgba(0xE3u, 0xE6u, 0xECu, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_HOVER] = sl_ui_rgba(0xEAu, 0xEDu, 0xF2u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_ACTIVE] = sl_ui_rgba(0xDAu, 0xDEu, 0xE5u, 255u);
    theme->colors[SL_UI_COLOR_BORDER] = sl_ui_rgba(0xC9u, 0xCDu, 0xD6u, 255u);
    theme->colors[SL_UI_COLOR_BORDER_FOCUS] = sl_ui_rgba(0x59u, 0xD0u, 0xFFu, 255u);
    theme->colors[SL_UI_COLOR_TEXT_PRIMARY] = sl_ui_rgba(0x1Au, 0x1Du, 0x26u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_SECONDARY] = sl_ui_rgba(0x5Au, 0x60u, 0x6Eu, 255u);
    theme->colors[SL_UI_COLOR_TEXT_DISABLED] = sl_ui_rgba(0x9Au, 0xA0u, 0xACu, 255u);
    theme->colors[SL_UI_COLOR_ACCENT] = sl_ui_rgba(0x11u, 0x16u, 0x26u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_HOVER] = sl_ui_rgba(0x1Cu, 0x24u, 0x40u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_ACTIVE] = sl_ui_rgba(0x0Au, 0x11u, 0x26u, 255u);
    theme->colors[SL_UI_COLOR_DANGER] = sl_ui_rgba(0xD0u, 0x4Au, 0x55u, 255u);
    theme->colors[SL_UI_COLOR_SUCCESS] = sl_ui_rgba(0x2Fu, 0xA8u, 0x7Au, 255u);
    theme->colors[SL_UI_COLOR_WARNING] = sl_ui_rgba(0xD9u, 0x9Au, 0x2Bu, 255u);
    theme->colors[SL_UI_COLOR_SHADOW_DARK] = sl_ui_rgba(0xBCu, 0xC1u, 0xCBu, 255u);
    theme->colors[SL_UI_COLOR_SHADOW_LIGHT] = sl_ui_rgba(0xFFu, 0xFFu, 0xFFu, 255u);
    theme->colors[SL_UI_COLOR_NEON] = sl_ui_rgba(0x59u, 0xD0u, 0xFFu, 255u);
}

// Soft UI, night: the same surfaces on a deep navy ground.
void sl_ui_theme_set_night(sl_ui_theme_t *theme) {
    if (!theme) return;
    sl_ui_theme_set_default(theme);
    theme->night = 1u;
    theme->colors[SL_UI_COLOR_BG_CANVAS] = sl_ui_rgba(0x1Au, 0x21u, 0x36u, 255u);
    theme->colors[SL_UI_COLOR_BG_PANEL] = sl_ui_rgba(0x1Au, 0x21u, 0x36u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL] = sl_ui_rgba(0x1Au, 0x21u, 0x36u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_HOVER] = sl_ui_rgba(0x22u, 0x2Au, 0x42u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_ACTIVE] = sl_ui_rgba(0x14u, 0x1Au, 0x2Cu, 255u);
    theme->colors[SL_UI_COLOR_BORDER] = sl_ui_rgba(0x2Bu, 0x33u, 0x52u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_PRIMARY] = sl_ui_rgba(0xEAu, 0xF4u, 0xFFu, 255u);
    theme->colors[SL_UI_COLOR_TEXT_SECONDARY] = sl_ui_rgba(0x9Fu, 0xB4u, 0xD8u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_DISABLED] = sl_ui_rgba(0x5Au, 0x65u, 0x84u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT] = sl_ui_rgba(0x0Bu, 0x10u, 0x20u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_HOVER] = sl_ui_rgba(0x14u, 0x1Bu, 0x30u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_ACTIVE] = sl_ui_rgba(0x07u, 0x0Au, 0x16u, 255u);
    theme->colors[SL_UI_COLOR_DANGER] = sl_ui_rgba(0xE0u, 0x60u, 0x6Au, 255u);
    theme->colors[SL_UI_COLOR_SUCCESS] = sl_ui_rgba(0x4Fu, 0xE3u, 0xB0u, 255u);
    theme->colors[SL_UI_COLOR_WARNING] = sl_ui_rgba(0xFFu, 0xB5u, 0x47u, 255u);
    theme->colors[SL_UI_COLOR_SHADOW_DARK] = sl_ui_rgba(0x0Cu, 0x10u, 0x20u, 255u);
    theme->colors[SL_UI_COLOR_SHADOW_LIGHT] = sl_ui_rgba(0x2Au, 0x33u, 0x54u, 255u);
}

const sl_ui_color_t *sl_ui_runtime_color(sl_ui_color_token_t token) {
    static const sl_ui_color_t fallback = {255u, 0u, 255u, 255u};
    if (!g_runtime_theme_initialized) {
        sl_ui_theme_set_default(&g_runtime_theme);
        g_runtime_theme_initialized = 1u;
    }
    return token < SL_UI_COLOR_COUNT ? &g_runtime_theme.colors[token] : &fallback;
}

uint8_t sl_ui_runtime_night(void) {
    return g_runtime_theme_initialized ? g_runtime_theme.night : 0u;
}

uint32_t sl_ui_runtime_version(void) {
    return g_runtime_theme_version;
}

void sl_ui_theme_apply(sl_ui_ctx_t *ui, const sl_ui_theme_t *theme) {
    if (!ui || !theme) return;
    ui->theme = *theme;
    ui->theme_initialized = 1u;
    ui->theme_version = g_runtime_theme_version;
}

void sl_ui_theme_reset(sl_ui_ctx_t *ui) {
    if (!ui) return;
    sl_ui_theme_set_default(&ui->theme);
    ui->theme_initialized = 1u;
    ui->theme_version = g_runtime_theme_version;
}

void sl_ui_theme_set_accent(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b) {
    sl_ui_theme_set_accent_a(ui, r, g, b, 255u);
}

void sl_ui_theme_set_accent_a(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ui) return;
    if (!ui->theme_initialized) sl_ui_theme_reset(ui);
    ui->theme.colors[SL_UI_COLOR_ACCENT] = sl_ui_rgba(r, g, b, a);
    ui->theme.colors[SL_UI_COLOR_ACCENT_HOVER] = sl_ui_rgba(
        (uint8_t)((r + 28u > 255u) ? 255u : r + 28u),
        (uint8_t)((g + 28u > 255u) ? 255u : g + 28u),
        (uint8_t)((b + 28u > 255u) ? 255u : b + 28u), a);
    ui->theme.colors[SL_UI_COLOR_ACCENT_ACTIVE] = sl_ui_rgba(
        (uint8_t)((r > 24u) ? (r - 24u) : 0u),
        (uint8_t)((g > 24u) ? (g - 24u) : 0u),
        (uint8_t)((b > 24u) ? (b - 24u) : 0u), a);
}

const sl_ui_color_t *sl_ui_theme_color(const sl_ui_ctx_t *ui, sl_ui_color_token_t token) {
    static const sl_ui_color_t fallback = {255u, 0u, 255u, 255u};
    if (!ui || token >= SL_UI_COLOR_COUNT) return &fallback;
    return &ui->theme.colors[token];
}

void sl_ui_theme_runtime_set(const sl_ui_theme_t *theme) {
    if (!theme) return;
    g_runtime_theme = *theme;
    g_runtime_theme_initialized = 1u;
    g_runtime_theme_version++;
    if (g_runtime_theme_version == 0u) g_runtime_theme_version = 1u;
}

void sl_ui_theme_runtime_get(sl_ui_theme_t *out) {
    if (!out) return;
    if (!g_runtime_theme_initialized) {
        sl_ui_theme_set_default(&g_runtime_theme);
        g_runtime_theme_initialized = 1u;
    }
    *out = g_runtime_theme;
}

static void sl_ui_fill_tok(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h, sl_ui_color_token_t token) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    if (c->a >= 255u) sl_fill_rect(ui->ctx, x, y, w, h, c->r, c->g, c->b);
    else sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, c->a);
}

static void sl_ui_fill_selection(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, SL_UI_COLOR_NEON);
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, 90u);
}

// ------------------------------------------------------------ soft UI faces
//
// With the skin loaded, labels and controls use Outfit and text fields use
// JetBrains Mono on a fixed integer cell, so the column arithmetic of the
// fields (carets, selections, hits) keeps working. Without it, the global
// font as before.

#define SL_UI_MONO_CELL_W 8u

static struct {
    const sl_skin_t *skin;
    const slsk_font_t *ui[4];     // 14, 16, 20, 24 px
    const slsk_font_t *mono;
} g_faces;

static void sl_ui_faces_load(const sl_ctx_t *ctx) {
    if (!ctx || !ctx->skin_loaded || g_faces.skin == &ctx->skin) return;
    static const char *names[4] = { "ui-14", "ui-16", "ui-20", "ui-24" };
    memset(&g_faces, 0, sizeof(g_faces));
    g_faces.skin = &ctx->skin;
    for (int i = 0; i < 4; i++) {
        const slsk_entry_t *e = sl_skin_find(&ctx->skin, names[i]);
        g_faces.ui[i] = e ? sl_skin_font(&ctx->skin, e) : NULL;
    }
    const slsk_entry_t *m = sl_skin_find(&ctx->skin, "mono-13");
    g_faces.mono = m ? sl_skin_font(&ctx->skin, m) : NULL;
}

// The label face for the app's font size (0 = default).
static const slsk_font_t *sl_ui_face(const sl_ui_ctx_t *ui) {
    if (!ui || !ui->ctx || !ui->ctx->skin_loaded) return NULL;
    sl_ui_faces_load(ui->ctx);
    uint32_t px = ui->font_px;
    int i = (px == 0u || px <= 15u) ? 0 : (px <= 18u ? 1 : (px <= 22u ? 2 : 3));
    return g_faces.ui[i];
}

static const slsk_font_t *sl_ui_mono(const sl_ui_ctx_t *ui) {
    if (!ui || !ui->ctx || !ui->ctx->skin_loaded) return NULL;
    sl_ui_faces_load(ui->ctx);
    return g_faces.mono;
}

// Height of the capitals: proportional text is laid out by it, so a label
// placed at y covers about what the old 8 px font did and the layouts the
// apps made for it still fit (descenders hang below, as they would).
static uint32_t sl_ui_cap(const slsk_font_t *f) {
    return ((uint32_t)f->px * 72u + 50u) / 100u;
}

// Proportional text with its capitals' top at y.
static void sl_ui_text_tok(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text, sl_ui_color_token_t token) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    const slsk_font_t *f = sl_ui_face(ui);
    if (f) {
        (void)sl_draw_text_face(ui->ctx, f, (int32_t)x, (int32_t)(y + sl_ui_cap(f)), text, c->r, c->g, c->b);
        return;
    }
    sl_draw_text_px(ui->ctx, x, y, text, c->r, c->g, c->b, ui->font_px);
}

static uint32_t sl_ui_text_width(const sl_ui_ctx_t *ui, const char *text) {
    const slsk_font_t *f = sl_ui_face(ui);
    if (f) return sl_text_width(f, text);
    return sl_text_measure_width_px(text, ui ? ui->font_px : 0u);
}

// Cell metrics of the text fields.
static uint32_t sl_ui_font_w(const sl_ui_ctx_t *ui) {
    if (sl_ui_mono(ui)) return SL_UI_MONO_CELL_W;
    uint32_t w = sl_text_cell_w_px(ui ? ui->font_px : 0u);
    return w ? w : 8u;
}

static uint32_t sl_ui_font_h(const sl_ui_ctx_t *ui) {
    const slsk_font_t *m = sl_ui_mono(ui);
    if (m) return (uint32_t)(m->ascent + m->descent + 2);
    uint32_t h = sl_text_cell_h_px(ui ? ui->font_px : 0u);
    return h ? h : 8u;
}

// Mono text on the field cells, top at y: each character centred in its cell.
static void sl_ui_text_cells(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text, sl_ui_color_token_t token) {
    const slsk_font_t *m = sl_ui_mono(ui);
    if (!m) {
        sl_ui_text_tok(ui, x, y, text, token);
        return;
    }
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    sl_target_t t = sl_target_from_ctx(ui->ctx);
    const slsk_glyph_t *gl = sl_skin_glyphs(m);
    const uint8_t *atlas = sl_skin_atlas(m);
    int32_t base = (int32_t)y + 1 + m->ascent;
    int32_t pen = (int32_t)x;
    const char *s = text;
    while (*s) {
        int gi = sl_skin_glyph_index(m, sl_utf8_next(&s));
        if (gi >= 0 && gl[gi].w) {
            const slsk_glyph_t *g = &gl[gi];
            int32_t gx = pen + ((int32_t)SL_UI_MONO_CELL_W * 64 - (int32_t)g->advance) / 128 + g->xoff;
            sl_blit_mask(&t, atlas + (size_t)g->y * m->atlas_w + g->x, g->w, g->h, m->atlas_w,
                         gx, base + g->yoff, c->r, c->g, c->b, 255u);
        }
        pen += (int32_t)SL_UI_MONO_CELL_W;
    }
}

// The top of a line of proportional text centred in [y, y + h).
static uint32_t sl_ui_text_center_y(const sl_ui_ctx_t *ui, uint32_t y, uint32_t h) {
    const slsk_font_t *f = sl_ui_face(ui);
    uint32_t fh = f ? sl_ui_cap(f) : sl_ui_font_h(ui);
    return y + ((h > fh) ? ((h - fh) / 2u) : 0u);
}

// ----------------------------------------------------------- soft UI surfaces


static sl_surf_shadow_t sl_ui_shadow(const sl_ui_ctx_t *ui, int8_t d, uint8_t blur, uint8_t inset,
                                     sl_ui_color_token_t token, uint8_t a) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    sl_surf_shadow_t sh = { d, d, blur, inset, c->r, c->g, c->b, a };
    return sh;
}

// Draw a soft UI surface at the exact size: rendered once per size and
// theme, then copied from the cache.
static void sl_ui_surface(sl_ui_ctx_t *ui, sl_ui_surf_kind_t kind, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t r) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    if (!ui->ctx->skin_loaded) {   // no skin: the flat look of before
        sl_ui_fill_tok(ui, x, y, w, h, kind >= SL_UI_SURF_NIGHT ? SL_UI_COLOR_ACCENT : SL_UI_COLOR_BG_CONTROL);
        return;
    }
    sl_surf_style_t st;
    memset(&st, 0, sizeof(st));
    const sl_ui_color_t *fill = sl_ui_theme_color(ui, kind == SL_UI_SURF_NEON_DOT ? SL_UI_COLOR_NEON
                                                    : kind >= SL_UI_SURF_NIGHT ? SL_UI_COLOR_ACCENT
                                                    : (kind == SL_UI_SURF_INSET ? SL_UI_COLOR_BG_CANVAS
                                                                                : SL_UI_COLOR_BG_CONTROL));
    st.fill_r = fill->r;
    st.fill_g = fill->g;
    st.fill_b = fill->b;
    switch (kind) {
    case SL_UI_SURF_RAISED:
        st.shadow_count = 2;
        st.shadows[0] = sl_ui_shadow(ui, 3, 7, 0, SL_UI_COLOR_SHADOW_DARK, 255u);
        st.shadows[1] = sl_ui_shadow(ui, -3, 7, 0, SL_UI_COLOR_SHADOW_LIGHT, 255u);
        break;
    case SL_UI_SURF_RAISED_HI:
        st.shadow_count = 2;
        st.shadows[0] = sl_ui_shadow(ui, 4, 10, 0, SL_UI_COLOR_SHADOW_DARK, 255u);
        st.shadows[1] = sl_ui_shadow(ui, -4, 10, 0, SL_UI_COLOR_SHADOW_LIGHT, 255u);
        break;
    case SL_UI_SURF_INSET:
        st.shadow_count = 2;
        st.shadows[0] = sl_ui_shadow(ui, 3, 6, 1, SL_UI_COLOR_SHADOW_DARK, 255u);
        st.shadows[1] = sl_ui_shadow(ui, -3, 6, 1, SL_UI_COLOR_SHADOW_LIGHT, 255u);
        break;
    case SL_UI_SURF_FLAT:
        st.shadow_count = 2;
        st.shadows[0] = sl_ui_shadow(ui, 2, 4, 0, SL_UI_COLOR_SHADOW_DARK, 150u);
        st.shadows[1] = sl_ui_shadow(ui, -2, 4, 0, SL_UI_COLOR_SHADOW_LIGHT, 150u);
        break;
    case SL_UI_SURF_NIGHT:
        st.shadow_count = 1;
        st.shadows[0] = sl_ui_shadow(ui, 3, 7, 0, SL_UI_COLOR_SHADOW_DARK, 255u);
        break;
    case SL_UI_SURF_NIGHT_GLOW:
        st.shadow_count = 1;
        st.shadows[0] = sl_ui_shadow(ui, 0, 7, 0, SL_UI_COLOR_NEON, 255u);
        break;
    case SL_UI_SURF_NEON_DOT:
        st.shadow_count = 1;
        st.shadows[0] = sl_ui_shadow(ui, 0, 8, 0, SL_UI_COLOR_NEON, 200u);
        break;
    case SL_UI_SURF_NIGHT_FLAT:
        break;
    }
    const sl_surf_t *s = sl_surf_get(&st, w, h, r);
    if (!s) {
        sl_ui_fill_tok(ui, x, y, w, h, SL_UI_COLOR_BG_CONTROL);
        return;
    }
    slsk_entry_t e;
    memset(&e, 0, sizeof(e));
    e.kind = SLSK_KIND_PREMUL;
    e.width = (uint16_t)s->w;
    e.height = (uint16_t)s->h;
    sl_target_t t = sl_target_from_ctx(ui->ctx);
    int32_t sx = (int32_t)x - (int32_t)s->pad_l, sy = (int32_t)y - (int32_t)s->pad_t;

    // Away from its edges a surface is its fill: past the corners and the
    // reach of its inner shadows. A big one (a text area) fills that inside
    // and blends only the ring around it.
    uint32_t m = r;
    for (uint8_t i = 0; i < st.shadow_count; i++) {
        if (!st.shadows[i].inset) continue;
        int32_t d = st.shadows[i].dx < 0 ? -st.shadows[i].dx : st.shadows[i].dx;
        int32_t dy = st.shadows[i].dy < 0 ? -st.shadows[i].dy : st.shadows[i].dy;
        if (dy > d) d = dy;
        uint32_t reach = (uint32_t)d + (3u * st.shadows[i].blur + 1u) / 2u;
        if (reach > m) m = reach;
    }
    m += 2u;
    if (w < 2u * m + 64u || h < 2u * m + 32u) {
        sl_blit_sprite(&t, &e, s->px, sx, sy, s->w, s->h);
        return;
    }
    sl_fill_rect(ui->ctx, x + m, y + m, w - 2u * m, h - 2u * m, st.fill_r, st.fill_g, st.fill_b);
    int32_t ix0 = (int32_t)(x + m), iy0 = (int32_t)(y + m), ix1 = (int32_t)(x + w - m), iy1 = (int32_t)(y + h - m);
    int32_t ex = sx + (int32_t)s->w, ey = sy + (int32_t)s->h;
    const int32_t strips[4][4] = {
        { sx, sy, ex, iy0 }, { sx, iy1, ex, ey }, { sx, iy0, ix0, iy1 }, { ix1, iy0, ex, iy1 },
    };
    for (int k = 0; k < 4; k++) {
        sl_target_t tt = t;
        if (strips[k][0] > tt.clip_x0) tt.clip_x0 = strips[k][0];
        if (strips[k][1] > tt.clip_y0) tt.clip_y0 = strips[k][1];
        if (strips[k][2] < tt.clip_x1) tt.clip_x1 = strips[k][2];
        if (strips[k][3] < tt.clip_y1) tt.clip_y1 = strips[k][3];
        if (tt.clip_x1 <= tt.clip_x0 || tt.clip_y1 <= tt.clip_y0) continue;
        sl_blit_sprite(&tt, &e, s->px, sx, sy, s->w, s->h);
    }
}

// For Starlight's own popups (menus, cards): the runtime theme, no app.
void sl_ui_ctx_surface(sl_ctx_t *ctx, sl_ui_surf_kind_t kind, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t r) {
    static sl_ui_ctx_t tmp;
    tmp.ctx = ctx;
    sl_ui_theme_runtime_get(&tmp.theme);
    tmp.theme_initialized = 1u;
    sl_ui_surface(&tmp, kind, x, y, w, h, r);
}

const slsk_font_t *sl_ui_ctx_face(sl_ctx_t *ctx) {
    static sl_ui_ctx_t tmp;
    tmp.ctx = ctx;
    tmp.font_px = 0u;
    return sl_ui_face(&tmp);
}

uint32_t sl_ui_ctx_text_width(sl_ctx_t *ctx, const char *text) {
    const slsk_font_t *f = sl_ui_ctx_face(ctx);
    return f ? sl_text_width(f, text) : sl_text_measure_width(text);
}

int sl_ui_ctx_mono_cell(sl_ctx_t *ctx, uint32_t *cell_w, uint32_t *cell_h) {
    static sl_ui_ctx_t tmp;
    tmp.ctx = ctx;
    if (!sl_ui_mono(&tmp)) return 0;
    *cell_w = sl_ui_font_w(&tmp);
    *cell_h = sl_ui_font_h(&tmp);
    return 1;
}

void sl_ui_ctx_mono_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    static sl_ui_ctx_t tmp;
    tmp.ctx = ctx;
    tmp.theme_initialized = 1u;
    tmp.theme.colors[SL_UI_COLOR_TEXT_PRIMARY] = sl_ui_rgba(r, g, b, 255u);
    sl_ui_text_cells(&tmp, x, y, text, SL_UI_COLOR_TEXT_PRIMARY);
}

static uint32_t sl_ui_pill_r(uint32_t h, uint32_t max_r) {
    uint32_t r = h / 2u;
    return r > max_r ? max_r : r;
}

static void sl_ui_selection_collapse(sl_ui_ctx_t *ui, uint32_t id, uint32_t pos) {
    if (!ui) return;
    ui->text_select_id = id;
    ui->text_select_anchor_pos = pos;
    ui->text_select_focus_pos = pos;
}

static uint8_t sl_ui_selection_range(const sl_ui_ctx_t *ui, uint32_t id, uint32_t *out_start, uint32_t *out_end) {
    uint32_t a = 0u, b = 0u;
    if (!ui || ui->text_select_id != id) return 0u;
    a = ui->text_select_anchor_pos;
    b = ui->text_select_focus_pos;
    if (a == b) return 0u;
    if (a > b) {
        uint32_t tmp = a;
        a = b;
        b = tmp;
    }
    if (out_start) *out_start = a;
    if (out_end) *out_end = b;
    return 1u;
}

static void sl_ui_move_caret(sl_ui_ctx_t *ui, uint32_t id, uint32_t old_pos, uint32_t new_pos, uint8_t extend_selection) {
    if (!ui) return;
    ui->text_caret_id = id;
    ui->text_caret_pos = new_pos;
    if (extend_selection) {
        if (ui->text_select_id != id || ui->text_select_anchor_pos == ui->text_select_focus_pos) {
            ui->text_select_id = id;
            ui->text_select_anchor_pos = old_pos;
        }
        ui->text_select_focus_pos = new_pos;
        return;
    }
    sl_ui_selection_collapse(ui, id, new_pos);
}

static uint8_t sl_ui_text_delete_selection(sl_ui_ctx_t *ui, uint32_t id,
                                           char *text, uint32_t *len, uint32_t *caret);
static uint8_t sl_ui_text_apply_paste(sl_ui_ctx_t *ui, uint32_t id,
                                      char *text, uint32_t *len, uint32_t text_cap, uint32_t *caret);
static uint8_t sl_ui_utf8_is_cont(char c);
static uint32_t sl_ui_utf8_count_between(const char *text, uint32_t start, uint32_t end);
static uint32_t sl_ui_utf8_prev_pos(const char *text, uint32_t pos);
static uint32_t sl_ui_utf8_advance_cols(const char *text, uint32_t pos, uint32_t len, uint32_t cols);
static uint32_t sl_ui_text_line_col_at(const char *text, uint32_t pos, uint32_t *col_out);

static uint8_t sl_ui_clipboard_copy_selection(const sl_ui_ctx_t *ui, uint32_t id, const char *text) {
    uint32_t sel_start = 0u, sel_end = 0u;
    if (!ui || !text) return 0u;
    if (!sl_ui_selection_range(ui, id, &sel_start, &sel_end)) return 0u;
    return sl_clipboard_set_text(text + sel_start, sel_end - sel_start);
}

static void sl_ui_text_input_copy_point(const sl_ui_ctx_t *ui,
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                        const char *text, uint32_t len,
                                        int32_t *out_x, int32_t *out_y) {
    uint32_t fw = 0u, vis_chars = 0u, start = 0u, caret_draw = 0u, text_x = 0u, text_y = 0u;
    uint32_t input_caret_pos = 0u;
    if (!ui || !out_x || !out_y) return;
    fw = sl_ui_font_w(ui);
    vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    text_x = x + 6u;
    text_y = sl_ui_text_center_y(ui, y, h);
    input_caret_pos = ui->text_caret_pos;
    start = ui->text_input_scroll_col;
    if (start > len) start = len;
    if (start > 0u && sl_ui_utf8_is_cont(text[start])) start = sl_ui_utf8_prev_pos(text, start);
    if (input_caret_pos < start) input_caret_pos = start;
    caret_draw = sl_ui_utf8_count_between(text, start, input_caret_pos);
    if (vis_chars > 0u && caret_draw > vis_chars) caret_draw = vis_chars;
    *out_x = (int32_t)(text_x + (caret_draw * fw));
    *out_y = (int32_t)(text_y + (sl_ui_font_h(ui) / 2u));
}

static void sl_ui_text_area_copy_point(const sl_ui_ctx_t *ui,
                                       uint32_t x, uint32_t y,
                                       const char *text,
                                       int32_t *out_x, int32_t *out_y) {
    uint32_t caret_col = 0u, caret_line = 0u, fw = 0u, fh = 0u;
    uint32_t draw_col = 0u, draw_line = 0u;
    if (!ui || !out_x || !out_y) return;
    fw = sl_ui_font_w(ui);
    fh = sl_ui_font_h(ui);
    caret_line = sl_ui_text_line_col_at(text, ui->text_caret_pos, &caret_col);
    draw_col = (caret_col > ui->text_area_scroll_col) ? (caret_col - ui->text_area_scroll_col) : 0u;
    draw_line = (caret_line > ui->text_area_scroll_line) ? (caret_line - ui->text_area_scroll_line) : 0u;
    *out_x = (int32_t)(x + 6u + (draw_col * fw));
    *out_y = (int32_t)(y + 6u + (draw_line * fh) + (fh / 2u));
}

static void sl_ui_clipboard_notify_copy_feedback(const sl_ui_ctx_t *ui,
                                                 const char *text,
                                                 uint32_t sel_start, uint32_t sel_end,
                                                 int32_t start_x, int32_t start_y) {
    if (!ui || !ui->ctx || !text || sel_end <= sel_start) return;
    sl_clipboard_notify_copy(ui->ctx, text + sel_start, sel_end - sel_start,
                             start_x, start_y,
                             (int32_t)ui->ctx->cursor_x, (int32_t)ui->ctx->cursor_y);
}

static uint32_t sl_ui_text_input_hit_pos(sl_ui_ctx_t *ui,
                                         uint32_t x, uint32_t w,
                                         const char *text, uint32_t len) {
    uint32_t fw, vis_chars, start, max_scroll_col;
    if (!ui || !text) return 0u;
    fw = sl_ui_font_w(ui);
    vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    start = ui->text_input_scroll_col;
    max_scroll_col = (len > vis_chars) ? (len - vis_chars) : 0u;
    if (start > max_scroll_col) start = max_scroll_col;
    if (start > 0u && sl_ui_utf8_is_cont(text[start])) start = sl_ui_utf8_prev_pos(text, start);
    if (ui->cursor_x <= x + 6u) return start;
    if (vis_chars == 0u) return start;
    {
        uint32_t rel = ui->cursor_x - (x + 6u);
        uint32_t col = rel / fw;
        if (col > vis_chars) col = vis_chars;
        return sl_ui_utf8_advance_cols(text, start, len, col);
    }
}

void sl_ui_set_font_px(sl_ui_ctx_t *ui, uint32_t font_px) {
    if (!ui) return;
    ui->font_px = font_px;
}

uint32_t sl_ui_get_font_px(const sl_ui_ctx_t *ui) {
    if (!ui) return 0u;
    return ui->font_px;
}

static sl_ui_color_token_t sl_ui_state_text_token(sl_ui_control_state_t state) {
    return (state == SL_UI_STATE_DISABLED) ? SL_UI_COLOR_TEXT_DISABLED : SL_UI_COLOR_TEXT_PRIMARY;
}

static int sl_ui_point_in_rect(uint32_t px, uint32_t py,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released,
                 int32_t wheel_x, int32_t wheel_y,
                 uint32_t clipboard_cmds, uint8_t caps_lock_on) {
    if (!ui) return;
    ui->ctx = ctx;
    ui->win = win;
    ui->cursor_x = cursor_x;
    ui->cursor_y = cursor_y;
    ui->mouse_down = mouse_down;
    ui->mouse_pressed = mouse_pressed;
    ui->mouse_released = mouse_released;
    ui->wheel_x = wheel_x;
    ui->wheel_y = wheel_y;
    ui->clipboard_cmds = clipboard_cmds;
    ui->caps_lock_on = caps_lock_on;
    ui->wheel_consumed = 0u;
    ui->select_overlay_pending = 0u;
    ui->select_overlay_options_pipe = NULL;
    if (!g_runtime_theme_initialized) {
        sl_ui_theme_set_default(&g_runtime_theme);
        g_runtime_theme_initialized = 1u;
    }
    if (!ui->theme_initialized || ui->theme_version != g_runtime_theme_version) {
        ui->theme = g_runtime_theme;
        ui->theme_initialized = 1u;
        ui->theme_version = g_runtime_theme_version;
    }
}

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 sl_ui_color_token_t token) {
    if (!ui || !ui->ctx || !text) return;
    sl_ui_text_tok(ui, x, y, text, token);
}

void sl_ui_label_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || !text) return;
    const slsk_font_t *f = sl_ui_face(ui);
    if (f) {
        (void)sl_draw_text_face(ui->ctx, f, (int32_t)x, (int32_t)(y + sl_ui_cap(f)), text, r, g, b);
        return;
    }
    sl_draw_text_px(ui->ctx, x, y, text, r, g, b, ui->font_px);
}

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text) {
    return sl_ui_button_state(ui, id, x, y, w, h, text, 1u);
}

uint8_t sl_ui_button_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !text || w < 8u || h < 8u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) {
        ui->active_id = id;
    }

    int active = (ui->active_id == id);
    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;

    // Raised, higher under the mouse, sunk while pressed, flat if disabled;
    // the label centred.
    sl_ui_surf_kind_t kind = SL_UI_SURF_RAISED;
    if (state == SL_UI_STATE_DISABLED) kind = SL_UI_SURF_FLAT;
    else if (state == SL_UI_STATE_ACTIVE) kind = SL_UI_SURF_INSET;
    else if (state == SL_UI_STATE_HOVER) kind = SL_UI_SURF_RAISED_HI;
    sl_ui_surface(ui, kind, x, y, w, h, sl_ui_pill_r(h, 14u));
    {
        uint32_t tw = sl_ui_text_width(ui, text);
        uint32_t tx = (tw + 12u < w) ? x + (w - tw) / 2u : x + 6u;
        sl_ui_text_tok(ui, tx, sl_ui_text_center_y(ui, y, h), text, sl_ui_state_text_token(state));
    }
    if (!enabled && active) ui->active_id = 0u;

    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }

    return 0u;
}

uint8_t sl_ui_button_primary_state(sl_ui_ctx_t *ui, uint32_t id,
                                   uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                   const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !text || w < 8u || h < 8u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);
    int pressed = enabled && active && ui->mouse_down;

    // Night at rest, lit under the mouse, without its shadow while pressed;
    // flat and grey if disabled.
    sl_ui_surf_kind_t kind = !enabled ? SL_UI_SURF_FLAT
                             : pressed ? SL_UI_SURF_NIGHT_FLAT
                             : hovered ? SL_UI_SURF_NIGHT_GLOW : SL_UI_SURF_NIGHT;
    sl_ui_surface(ui, kind, x, y, w, h, sl_ui_pill_r(h, 24u));
    {
        uint32_t tw = sl_ui_text_width(ui, text);
        uint32_t tx = (tw + 12u < w) ? x + (w - tw) / 2u : x + 6u;
        sl_ui_text_tok(ui, tx, sl_ui_text_center_y(ui, y, h), text,
                       enabled ? SL_UI_COLOR_NEON : SL_UI_COLOR_TEXT_DISABLED);
    }
    if (!enabled && active) ui->active_id = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }
    return 0u;
}

uint8_t sl_ui_swatch_state(sl_ui_ctx_t *ui, uint32_t id, uint32_t x, uint32_t y, uint32_t size,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t chosen, uint8_t enabled) {
    if (!ui || !ui->ctx || size < 12u) return 0u;
    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, size, size);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);
    sl_ui_surf_kind_t kind = !enabled ? SL_UI_SURF_FLAT
                             : (active && ui->mouse_down) ? SL_UI_SURF_INSET
                             : hovered ? SL_UI_SURF_RAISED_HI : SL_UI_SURF_RAISED;
    sl_ui_surface(ui, kind, x, y, size, size, size / 2u);
    // The dot (tavola Finestre: 22 on 44, chosen 26 with a glow of its colour).
    uint32_t d = chosen ? (size * 13u) / 22u : size / 2u;
    if (ui->ctx->skin_loaded) {
        sl_surf_style_t st;
        memset(&st, 0, sizeof(st));
        st.fill_r = r;
        st.fill_g = g;
        st.fill_b = b;
        if (chosen) {
            st.shadow_count = 1;
            st.shadows[0] = (sl_surf_shadow_t){ 0, 0, 10, 0, r, g, b, 255u };
        }
        const sl_surf_t *sf = sl_surf_get(&st, d, d, d / 2u);
        if (sf) {
            slsk_entry_t e;
            memset(&e, 0, sizeof(e));
            e.kind = SLSK_KIND_PREMUL;
            e.width = (uint16_t)sf->w;
            e.height = (uint16_t)sf->h;
            sl_target_t t = sl_target_from_ctx(ui->ctx);
            int32_t dx = (int32_t)(x + (size - d) / 2u), dy = (int32_t)(y + (size - d) / 2u);
            sl_blit_sprite(&t, &e, sf->px, dx - (int32_t)sf->pad_l, dy - (int32_t)sf->pad_t, sf->w, sf->h);
        }
    } else {
        sl_fill_rect(ui->ctx, x + (size - d) / 2u, y + (size - d) / 2u, d, d, r, g, b);
    }
    if (!enabled && active) ui->active_id = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }
    return 0u;
}

// Option i of "A|B|C" into out.
static void sl_ui_pipe_option(const char *pipe, uint32_t index, char *out, size_t cap) {
    size_t n = 0u;
    uint32_t at = 0u;
    if (cap == 0u) return;
    for (const char *p = pipe ? pipe : ""; *p; p++) {
        if (*p == '|') {
            if (at++ == index) break;
            continue;
        }
        if (at == index && n + 1u < cap) out[n++] = *p;
    }
    out[n] = '\0';
}

uint8_t sl_ui_segment_state(sl_ui_ctx_t *ui, uint32_t id,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t option_count, uint32_t *selected,
                            const char *options_pipe, uint8_t enabled) {
    if (!ui || !ui->ctx || !selected || option_count == 0u || w < option_count * 16u || h < 16u) return 0u;
    if (*selected >= option_count) *selected = 0u;

    // The pill (tavola Componenti: padding 6, radius 26 on 52) and the cells.
    uint32_t pad = h >= 40u ? 6u : 3u;
    uint32_t cell_w = (w - 2u * pad) / option_count;
    uint32_t cell_h = h - 2u * pad;
    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    uint32_t over = (uint32_t)-1;
    if (hovered && ui->cursor_x >= x + pad && cell_w > 0u) {
        over = (ui->cursor_x - x - pad) / cell_w;
        if (over >= option_count) over = option_count - 1u;
    }
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    sl_ui_surface(ui, enabled ? SL_UI_SURF_RAISED : SL_UI_SURF_FLAT, x, y, w, h, sl_ui_pill_r(h, 26u));
    for (uint32_t i = 0u; i < option_count; i++) {
        uint32_t cx = x + pad + i * cell_w;
        char label[64];
        sl_ui_pipe_option(options_pipe, i, label, sizeof(label));
        sl_ui_color_token_t tok = SL_UI_COLOR_TEXT_SECONDARY;
        if (!enabled) {
            tok = SL_UI_COLOR_TEXT_DISABLED;
        } else if (i == *selected) {
            sl_ui_surface(ui, SL_UI_SURF_NIGHT_FLAT, cx, y + pad, cell_w, cell_h, sl_ui_pill_r(cell_h, 20u));
            tok = SL_UI_COLOR_NEON;
        } else if (i == over) {
            tok = SL_UI_COLOR_TEXT_PRIMARY;
        }
        uint32_t tw = sl_ui_text_width(ui, label);
        uint32_t tx = (tw + 8u < cell_w) ? cx + (cell_w - tw) / 2u : cx + 4u;
        sl_ui_text_tok(ui, tx, sl_ui_text_center_y(ui, y + pad, cell_h), label, tok);
    }
    if (!enabled && active) ui->active_id = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (over < option_count && over != *selected) {
            *selected = over;
            return 1u;
        }
    }
    return 0u;
}

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value) {
    return sl_ui_slider_u32_state(ui, id, x, y, w, min_v, max_v, value, 1u);
}

uint8_t sl_ui_slider_u32_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w,
                               uint32_t min_v, uint32_t max_v, uint32_t *value,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 24u || min_v > max_v) return 0u;

    uint32_t h = 16u;
    uint32_t knob_w = 10u;   // the round knob is 16 px, centred on these 10
    uint32_t track_x = x + 2u;
    uint32_t track_w = w - 4u;

    if (enabled && sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h) && ui->mouse_pressed) {
        ui->active_id = id;
    }

    uint8_t changed = 0u;
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t cx = ui->cursor_x;
        if (cx < track_x) cx = track_x;
        if (cx > track_x + track_w) cx = track_x + track_w;

        uint32_t range = max_v - min_v;
        uint32_t nv = min_v;
        if (range > 0u && track_w > 0u) {
            nv = min_v + (uint32_t)(((uint64_t)(cx - track_x) * (uint64_t)range) / (uint64_t)track_w);
        }
        if (nv != *value) {
            *value = nv;
            changed = 1u;
        }
    }

    if (ui->mouse_released && ui->active_id == id) {
        ui->active_id = 0u;
    }

    sl_ui_control_state_t state = enabled ? SL_UI_STATE_NORMAL : SL_UI_STATE_DISABLED;
    if (enabled && ui->active_id == id && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    // An inset track, filled with night up to the knob.
    sl_ui_surface(ui, SL_UI_SURF_INSET, x, y + 4u, w, 8u, 4u);

    uint32_t knob_x = track_x;
    if (max_v > min_v) {
        uint32_t range = max_v - min_v;
        uint32_t rel = *value;
        if (rel < min_v) rel = min_v;
        if (rel > max_v) rel = max_v;
        rel -= min_v;
        knob_x = track_x + (uint32_t)(((uint64_t)rel * (uint64_t)track_w) / (uint64_t)range);
    }
    if (knob_x + knob_w > x + w) knob_x = (x + w > knob_w) ? (x + w - knob_w) : x;

    if (knob_x > x + 4u && state != SL_UI_STATE_DISABLED) {
        sl_ui_surface(ui, SL_UI_SURF_NIGHT, x + 1u, y + 5u, knob_x + knob_w / 2u - x - 1u, 6u, 3u);
    }
    sl_ui_surface(ui, state == SL_UI_STATE_DISABLED ? SL_UI_SURF_FLAT
                      : (state == SL_UI_STATE_ACTIVE ? SL_UI_SURF_RAISED_HI : SL_UI_SURF_RAISED),
                  knob_x - 3u, y, knob_w + 6u, 16u, 8u);
    return changed;
}

static uint16_t sl_ui_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) {
        return (uint16_t)(code & 0x00FFu);
    }
    return code;
}

static uint32_t sl_ui_strlen(const char *s) {
    uint32_t n = 0u;
    if (!s) return 0u;
    while (s[n]) n++;
    return n;
}

static uint8_t sl_ui_utf8_is_cont(char c) {
    return (((uint8_t)c & 0xC0u) == 0x80u) ? 1u : 0u;
}

static uint32_t sl_ui_utf8_prev_pos(const char *text, uint32_t pos) {
    if (!text || pos == 0u) return 0u;
    pos--;
    while (pos > 0u && sl_ui_utf8_is_cont(text[pos])) pos--;
    return pos;
}

static uint32_t sl_ui_utf8_next_pos(const char *text, uint32_t pos, uint32_t len) {
    if (!text || pos >= len) return len;
    pos++;
    while (pos < len && sl_ui_utf8_is_cont(text[pos])) pos++;
    return pos;
}

static uint32_t sl_ui_utf8_count_between(const char *text, uint32_t start, uint32_t end) {
    uint32_t count = 0u;
    if (!text) return 0u;
    while (start < end && text[start]) {
        start = sl_ui_utf8_next_pos(text, start, end);
        count++;
    }
    return count;
}

static uint32_t sl_ui_utf8_advance_cols(const char *text, uint32_t pos, uint32_t len, uint32_t cols) {
    while (cols > 0u && pos < len && text[pos]) {
        pos = sl_ui_utf8_next_pos(text, pos, len);
        cols--;
    }
    return pos;
}

uint8_t sl_ui_toggle(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint8_t *value, const char *text) {
    return sl_ui_toggle_state(ui, id, x, y, w, h, value, text, 1u);
}

uint8_t sl_ui_toggle_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint8_t *value, const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 20u || h < 12u) return 0u;
    if (!text) text = "";

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            *value = *value ? 0u : 1u;
            changed = 1u;
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    uint32_t sw_h = (h > 6u) ? (h - 6u) : 6u;
    uint32_t sw_w = sw_h * 2u;
    if (sw_w < 20u) sw_w = 20u;
    if (sw_w + 8u > w) sw_w = (w > 8u) ? (w - 8u) : w;
    uint32_t sw_x = x + w - sw_w - 4u;
    uint32_t sw_y = y + ((h > sw_h) ? ((h - sw_h) / 2u) : 0u);

    // An inset pill; the knob raised when off, night lit with neon when on.
    sl_ui_surface(ui, SL_UI_SURF_INSET, sw_x, sw_y, sw_w, sw_h, sw_h / 2u);
    uint32_t knob = (sw_h > 6u) ? (sw_h - 6u) : sw_h;
    uint32_t knob_x = *value ? (sw_x + sw_w - knob - 3u) : (sw_x + 3u);
    sl_ui_surface(ui, !enabled ? SL_UI_SURF_FLAT : (*value ? SL_UI_SURF_NIGHT_GLOW
                                                    : (state == SL_UI_STATE_HOVER ? SL_UI_SURF_RAISED_HI : SL_UI_SURF_RAISED)),
                  knob_x, sw_y + 3u, knob, knob, knob / 2u);

    if (text[0]) {
        uint32_t text_x = x + 6u;
        uint32_t text_y = sl_ui_text_center_y(ui, y, h);
        if (text_x + sl_ui_font_w(ui) < sw_x) {
            sl_ui_text_tok(ui, text_x, text_y, text,
                           enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        }
    }
    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

uint8_t sl_ui_select_cycle(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t option_count, uint32_t *selected,
                           const char *current_text,
                           const char *options_pipe) {
    return sl_ui_select_cycle_state(ui, id, x, y, w, h, option_count, selected, current_text, options_pipe, 1u);
}

static void sl_ui_select_option_text(const char *options_pipe, uint32_t index,
                                     char *out, uint32_t out_cap) {
    uint32_t idx = 0u;
    uint32_t n = 0u;
    const char *start = options_pipe;
    const char *p = options_pipe;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!options_pipe || options_pipe[0] == '\0') return;

    while (1) {
        if (*p == '|' || *p == '\0') {
            if (idx == index) {
                const char *q = start;
                while (q < p && n + 1u < out_cap) {
                    out[n++] = *q++;
                }
                out[n] = '\0';
                return;
            }
            if (*p == '\0') break;
            idx++;
            start = p + 1;
        }
        p++;
    }
}

uint8_t sl_ui_select_cycle_state(sl_ui_ctx_t *ui, uint32_t id,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 uint32_t option_count, uint32_t *selected,
                                 const char *current_text, const char *options_pipe,
                                 uint8_t enabled) {
    return sl_ui_select_cycle_state_ex(ui, id,
                                       x, y, w, h,
                                       option_count, selected,
                                       current_text, options_pipe,
                                       NULL, 0u, enabled);
}

uint8_t sl_ui_select_cycle_state_ex(sl_ui_ctx_t *ui, uint32_t id,
                                    uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    uint32_t option_count, uint32_t *selected,
                                    const char *current_text, const char *options_pipe,
                                    const peos_input_event_t *events, uint8_t event_count,
                                    uint8_t enabled) {
    if (!ui || !ui->ctx || !selected || !current_text || w < 16u || h < 12u) return 0u;
    if (option_count == 0u) return 0u;
    if (*selected >= option_count) *selected = 0u;

    uint32_t row_h = (h < 20u) ? 20u : h;
    uint32_t max_items = 6u;
    uint32_t desired_items = (option_count < max_items) ? option_count : max_items;

    uint32_t body_top = 0u;
    uint32_t body_bottom = ui->ctx->fb.height;
    if (ui->win) {
        uint32_t by, bh;
        sl_window_body_rect(ui->win, NULL, &by, NULL, &bh);
        if (bh > 0u) {
            body_top = by;
            body_bottom = by + bh;
        }
    }

    uint32_t trigger_bottom = y + h;
    uint32_t space_below = (body_bottom > trigger_bottom) ? (body_bottom - trigger_bottom) : 0u;
    uint32_t space_above = (y > body_top) ? (y - body_top) : 0u;
    uint32_t fit_below = (space_below > 2u) ? ((space_below - 2u) / row_h) : 0u;
    uint32_t fit_above = (space_above > 2u) ? ((space_above - 2u) / row_h) : 0u;

    uint8_t open_above = 0u;
    uint32_t fit_items = fit_below;
    if (fit_items == 0u && fit_above > 0u) {
        fit_items = fit_above;
        open_above = 1u;
    } else if (fit_above > fit_below && fit_above > 0u) {
        fit_items = fit_above;
        open_above = 1u;
    }
    if (fit_items == 0u) fit_items = 1u;

    uint32_t visible_items = desired_items;
    if (visible_items > fit_items) visible_items = fit_items;
    if (visible_items == 0u) visible_items = 1u;

    uint32_t popup_h = visible_items * row_h + 2u;
    uint32_t popup_y = open_above ? ((y >= popup_h) ? (y - popup_h) : body_top) : (y + h);
    if (!open_above && popup_y + popup_h > body_bottom) {
        popup_y = (y >= popup_h) ? (y - popup_h) : body_top;
    }
    if (popup_y < body_top) popup_y = body_top;
    if (popup_y + popup_h > body_bottom && body_bottom > popup_h) {
        popup_y = body_bottom - popup_h;
    }
    int was_open = (ui->select_open_id == id) ? 1 : 0;
    int open = was_open;
    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    uint8_t has_scrollbar = (option_count > visible_items && w > 20u) ? 1u : 0u;
    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
    uint32_t list_w = (w > scrollbar_w) ? (w - scrollbar_w) : w;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    int popup_hovered = open ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, popup_y, w, popup_h) : 0;
    int popup_list_hovered = open ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, popup_y, list_w, popup_h) : 0;
    int popup_scroll_hovered = (open && has_scrollbar)
        ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x + list_w, popup_y, w - list_w, popup_h) : 0;
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            if (open) ui->select_open_id = 0u;
            else ui->select_open_id = id;
            open = (ui->select_open_id == id) ? 1 : 0;
        }
    }
    if (!enabled && open) {
        ui->select_open_id = 0u;
        open = 0;
    }
    if (open && !was_open) {
        uint32_t start = 0u;
        if (*selected >= visible_items) {
            start = *selected - (visible_items - 1u);
            if (start > max_start) start = max_start;
        }
        ui->select_scroll_idx = start;
    }
    if (ui->select_scroll_idx > max_start) ui->select_scroll_idx = max_start;

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (open || (active && ui->mouse_down)) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_surface(ui, state == SL_UI_STATE_DISABLED ? SL_UI_SURF_FLAT
                      : (state == SL_UI_STATE_ACTIVE ? SL_UI_SURF_INSET
                         : (state == SL_UI_STATE_HOVER ? SL_UI_SURF_RAISED_HI : SL_UI_SURF_RAISED)),
                  x, y, w, h, sl_ui_pill_r(h, 14u));

    uint32_t text_y = sl_ui_text_center_y(ui, y, h);
    sl_ui_text_tok(ui, x + 12u, text_y, current_text,
                   enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
    sl_ui_text_tok(ui, x + w - 18u, text_y, open ? "v" : ">",
                   enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);
    if (!enabled && active) ui->active_id = 0u;

    if (open) {
        if (enabled && popup_hovered && !ui->wheel_consumed && max_start > 0u) {
            int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
            if (wheel != 0) {
                int64_t nv = (int64_t)ui->select_scroll_idx - (int64_t)wheel;
                if (nv < 0) nv = 0;
                if (nv > (int64_t)max_start) nv = (int64_t)max_start;
                ui->select_scroll_idx = (uint32_t)nv;
                ui->wheel_consumed = 1u;
            }
        }

        if (enabled && popup_scroll_hovered && has_scrollbar && ui->mouse_pressed) {
            uint32_t track_y = popup_y + 2u;
            uint32_t track_h = (popup_h > 4u) ? (popup_h - 4u) : 0u;
            if (track_h > 0u && max_start > 0u) {
                uint32_t thumb_h = (uint32_t)(((uint64_t)visible_items * (uint64_t)track_h) / (uint64_t)option_count);
                if (thumb_h < 8u) thumb_h = 8u;
                if (thumb_h > track_h) thumb_h = track_h;
                uint32_t travel = track_h - thumb_h;
                uint32_t cy = ui->cursor_y;
                if (cy < track_y) cy = track_y;
                if (cy > track_y + track_h) cy = track_y + track_h;
                uint32_t pos = (cy > (track_y + (thumb_h / 2u))) ? (cy - track_y - (thumb_h / 2u)) : 0u;
                if (pos > travel) pos = travel;
                ui->select_scroll_idx = (travel > 0u)
                    ? (uint32_t)(((uint64_t)pos * (uint64_t)max_start) / (uint64_t)travel)
                    : 0u;
            }
        }

        for (uint8_t i = 0u; i < event_count; i++) {
            uint16_t code = sl_ui_norm_keycode(events[i].code);
            if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
                ui->shift_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LCTRL || code == PEOS_KEY_RCTRL_NORM) {
                ui->ctrl_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LALT || code == PEOS_KEY_RALT_NORM) {
                ui->alt_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
                ui->super_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (!enabled || !events[i].pressed) continue;

            if (code == PEOS_KEY_ESC) {
                ui->select_open_id = 0u;
                open = 0;
                break;
            }
            if (code == PEOS_KEY_ENTER) {
                ui->select_open_id = 0u;
                open = 0;
                break;
            }
            if (code == PEOS_KEY_UP_NORM) {
                if (*selected > 0u) {
                    (*selected)--;
                    changed = 1u;
                }
            } else if (code == PEOS_KEY_DOWN_NORM) {
                if ((*selected + 1u) < option_count) {
                    (*selected)++;
                    changed = 1u;
                }
            } else {
                continue;
            }

            if (*selected < ui->select_scroll_idx) {
                ui->select_scroll_idx = *selected;
            } else if (*selected >= ui->select_scroll_idx + visible_items) {
                ui->select_scroll_idx = *selected - visible_items + 1u;
            }
            if (ui->select_scroll_idx > max_start) ui->select_scroll_idx = max_start;
        }

        uint32_t start_idx = ui->select_scroll_idx;
        if (start_idx > max_start) start_idx = max_start;
        ui->select_scroll_idx = start_idx;

        ui->select_overlay_pending = 1u;
        ui->select_overlay_id = id;
        ui->select_overlay_x = x;
        ui->select_overlay_y = popup_y;
        ui->select_overlay_w = w;
        ui->select_overlay_row_h = row_h;
        ui->select_overlay_start_idx = start_idx;
        ui->select_overlay_visible_items = visible_items;
        ui->select_overlay_option_count = option_count;
        ui->select_overlay_selected = *selected;
        ui->select_overlay_options_pipe = options_pipe;

        if (enabled && ui->mouse_released && !active) {
            if (popup_list_hovered) {
                uint32_t rel = (ui->cursor_y >= popup_y + 1u) ? (ui->cursor_y - (popup_y + 1u)) : 0u;
                uint32_t item = rel / row_h;
                if (item < visible_items) {
                    uint32_t picked = start_idx + item;
                    if (picked < option_count && *selected != picked) {
                        *selected = picked;
                        changed = 1u;
                    }
                }
                ui->select_open_id = 0u;
            } else if (!hovered) {
                ui->select_open_id = 0u;
            }
        }
    }

    return changed;
}

void sl_ui_draw_overlays(sl_ui_ctx_t *ui) {
    if (!ui || !ui->ctx || !ui->select_overlay_pending) return;

    uint32_t x = ui->select_overlay_x;
    uint32_t y = ui->select_overlay_y;
    uint32_t w = ui->select_overlay_w;
    uint32_t row_h = ui->select_overlay_row_h;
    uint32_t start_idx = ui->select_overlay_start_idx;
    uint32_t visible_items = ui->select_overlay_visible_items;
    uint32_t option_count = ui->select_overlay_option_count;
    uint32_t selected = ui->select_overlay_selected;
    const char *options_pipe = ui->select_overlay_options_pipe;
    uint32_t popup_h = visible_items * row_h + 2u;
    uint8_t has_scrollbar = (option_count > visible_items && w > 20u) ? 1u : 0u;
    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
    uint32_t list_w = (w > scrollbar_w) ? (w - scrollbar_w) : w;
    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    if (start_idx > max_start) start_idx = max_start;

    sl_ui_surface(ui, SL_UI_SURF_RAISED_HI, x, y, w, popup_h, 12u);

    for (uint32_t i = 0u; i < visible_items; i++) {
        uint32_t opt_idx = start_idx + i;
        uint32_t iy = y + 1u + i * row_h;
        if (opt_idx >= option_count) break;

        int item_hover = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x + 1u, iy, list_w - 2u, row_h);
        if (item_hover) sl_ui_surface(ui, SL_UI_SURF_INSET, x + 4u, iy + 1u, list_w - 8u, row_h - 2u, sl_ui_pill_r(row_h - 2u, 10u));
        else if (opt_idx == selected) sl_ui_surface(ui, SL_UI_SURF_NIGHT, x + 4u, iy + 1u, list_w - 8u, row_h - 2u, sl_ui_pill_r(row_h - 2u, 10u));

        char opt_text[128];
        sl_ui_select_option_text(options_pipe, opt_idx, opt_text, (uint32_t)sizeof(opt_text));
        sl_ui_text_tok(ui, x + 14u, sl_ui_text_center_y(ui, iy, row_h), opt_text,
                       (opt_idx == selected && !item_hover) ? SL_UI_COLOR_NEON : SL_UI_COLOR_TEXT_PRIMARY);
    }

    if (has_scrollbar) {
        uint32_t sx = x + list_w;
        uint32_t sy = y + 1u;
        uint32_t sw = w - list_w;
        uint32_t sh = (popup_h > 2u) ? (popup_h - 2u) : 0u;
        if (sw > 0u && sh > 0u) {
            uint32_t track_x = sx + 2u;
            uint32_t track_y = sy + 1u;
            uint32_t track_w = (sw > 4u) ? (sw - 4u) : sw;
            uint32_t track_h = (sh > 2u) ? (sh - 2u) : sh;
            if (track_w > 0u && track_h > 0u) {
                sl_ui_surface(ui, SL_UI_SURF_INSET, track_x, track_y, track_w, track_h, track_w / 2u);
                uint32_t thumb_h = (uint32_t)(((uint64_t)visible_items * (uint64_t)track_h) / (uint64_t)option_count);
                if (thumb_h < 8u) thumb_h = 8u;
                if (thumb_h > track_h) thumb_h = track_h;
                uint32_t travel = track_h - thumb_h;
                uint32_t thumb_y = (max_start > 0u)
                    ? (track_y + (uint32_t)(((uint64_t)start_idx * (uint64_t)travel) / (uint64_t)max_start))
                    : track_y;
                sl_ui_surface(ui, SL_UI_SURF_NIGHT, track_x, thumb_y, track_w, thumb_h, track_w / 2u);
            }
        }
    }
}

uint8_t sl_ui_text_input(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         char *text, uint32_t text_cap,
                         const peos_input_event_t *events, uint8_t event_count) {
    return sl_ui_text_input_state(ui, id, x, y, w, h, text, text_cap, events, event_count, 1u);
}

uint8_t sl_ui_text_input_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               char *text, uint32_t text_cap,
                               const peos_input_event_t *events, uint8_t event_count,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !text || text_cap < 2u || w < 16u || h < 12u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && ui->mouse_pressed) {
        if (hovered) {
            ui->focused_id = id;
            ui->active_id = id;
        }
        else if (ui->focused_id == id) ui->focused_id = 0u;
    }
    if (!enabled && ui->focused_id == id) ui->focused_id = 0u;
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    int focused = (ui->focused_id == id);

    uint32_t len = sl_ui_strlen(text);
    if (len >= text_cap) {
        len = text_cap - 1u;
        text[len] = '\0';
    }
    if (focused && ui->text_caret_id != id) {
        ui->text_caret_id = id;
        ui->text_caret_pos = len;
        ui->text_input_scroll_col = 0u;
        sl_ui_selection_collapse(ui, id, len);
    }
    if (ui->text_caret_id == id && ui->text_caret_pos > len) ui->text_caret_pos = len;
    if (ui->text_caret_id == id && ui->text_caret_pos > 0u &&
        sl_ui_utf8_is_cont(text[ui->text_caret_pos])) {
        ui->text_caret_pos = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
    }
    if (ui->text_select_id == id) {
        if (ui->text_select_anchor_pos > len) ui->text_select_anchor_pos = len;
        if (ui->text_select_focus_pos > len) ui->text_select_focus_pos = len;
    }

    if (enabled && ui->mouse_pressed && hovered) {
        ui->text_caret_pos = sl_ui_text_input_hit_pos(ui, x, w, text, len);
        if (ui->text_caret_pos > len) ui->text_caret_pos = len;
        sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
    }
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t old_pos = ui->text_caret_pos;
        uint32_t new_pos = sl_ui_text_input_hit_pos(ui, x, w, text, len);
        if (new_pos > len) new_pos = len;
        sl_ui_move_caret(ui, id, old_pos, new_pos, 1u);
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    uint8_t changed = 0u;
    if (enabled && focused) {
        if (ui->clipboard_cmds & SL_CLIP_CMD_COPY) {
            uint32_t sel_start = 0u, sel_end = 0u;
            int32_t fx = 0, fy = 0;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end) &&
                sl_ui_clipboard_copy_selection(ui, id, text)) {
                sl_ui_text_input_copy_point(ui, x, y, w, h, text, len, &fx, &fy);
                sl_ui_clipboard_notify_copy_feedback(ui, text, sel_start, sel_end, fx, fy);
            }
            ui->clipboard_cmds &= ~SL_CLIP_CMD_COPY;
        }
        if (ui->clipboard_cmds & SL_CLIP_CMD_PASTE) {
            if (sl_ui_text_apply_paste(ui, id, text, &len, text_cap, &ui->text_caret_pos)) changed = 1u;
            ui->clipboard_cmds &= ~SL_CLIP_CMD_PASTE;
        }
    }
    for (uint8_t i = 0u; i < event_count; i++) {
        uint16_t code = sl_ui_norm_keycode(events[i].code);
        if (sl_keyboard_event_is_altgr(&events[i])) {
            ui->altgr_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
            ui->shift_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LCTRL) {
            ui->ctrl_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LALT) {
            ui->alt_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
            ui->super_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (!enabled || !focused || !events[i].pressed) continue;
        if (ui->ctrl_down && code == PEOS_KEY_A) {
            ui->text_caret_pos = len;
            ui->text_select_id = id;
            ui->text_select_anchor_pos = 0u;
            ui->text_select_focus_pos = len;
            continue;
        }
        if (code == PEOS_KEY_BACKSPACE) {
            if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) {
                changed = 1u;
            } else if (ui->text_caret_pos > 0u && len > 0u) {
                uint32_t prev = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
                uint32_t removed = ui->text_caret_pos - prev;
                for (uint32_t j = prev; j + removed <= len; j++) {
                    text[j] = text[j + removed];
                }
                ui->text_caret_pos = prev;
                len -= removed;
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (code == PEOS_KEY_LEFT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_start, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos > 0u) new_pos = sl_ui_utf8_prev_pos(text, new_pos);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_RIGHT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_end, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos < len) new_pos = sl_ui_utf8_next_pos(text, new_pos, len);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_HOME_NORM) {
            sl_ui_move_caret(ui, id, ui->text_caret_pos, 0u, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_END_NORM) {
            sl_ui_move_caret(ui, id, ui->text_caret_pos, len, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_ESC) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (code == PEOS_KEY_ENTER) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (ui->ctrl_down || ui->alt_down || ui->super_down) {
            continue;
        }

        char input[8];
        size_t input_len = sl_keyboard_translate_event(&events[i], ui->shift_down,
                                                       ui->caps_lock_on,
                                                       ui->altgr_down,
                                                       input, sizeof(input));
        if (input_len > 0u && sl_ui_selection_range(ui, id, NULL, NULL)) {
            if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
        }
        if (input_len > 0u && len + input_len < text_cap) {
            for (uint32_t j = len + 1u; j > ui->text_caret_pos; j--) {
                text[j + (uint32_t)input_len - 1u] = text[j - 1u];
            }
            for (uint32_t j = 0u; j < (uint32_t)input_len; j++) {
                text[ui->text_caret_pos + j] = input[j];
            }
            ui->text_caret_pos += (uint32_t)input_len;
            len += (uint32_t)input_len;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            changed = 1u;
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (focused) state = SL_UI_STATE_FOCUSED;
    else if (hovered) state = SL_UI_STATE_HOVER;
    // Fields are sunk into the pearl; the focused one gets a neon underline.
    sl_ui_surface(ui, state == SL_UI_STATE_DISABLED ? SL_UI_SURF_FLAT : SL_UI_SURF_INSET, x, y, w, h,
                  sl_ui_pill_r(h, 12u));
    if (state == SL_UI_STATE_FOCUSED && w > 24u) {
        sl_ui_fill_tok(ui, x + 12u, y + h - 2u, w - 24u, 2u, SL_UI_COLOR_NEON);
    }

    uint32_t fw = sl_ui_font_w(ui);
    uint32_t fh = sl_ui_font_h(ui);
    uint32_t vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    if (vis_chars > 0u) {
        uint8_t caret_visible = (enabled && focused &&
                                 ((peos_time_ns() / 500000000ull) & 1ull)) ? 1u : 0u;
        uint32_t input_caret_pos = (ui->text_caret_id == id) ? ui->text_caret_pos : len;
        if (ui->text_input_scroll_col > len) ui->text_input_scroll_col = len;
        if (ui->text_input_scroll_col > 0u && sl_ui_utf8_is_cont(text[ui->text_input_scroll_col])) {
            ui->text_input_scroll_col = sl_ui_utf8_prev_pos(text, ui->text_input_scroll_col);
        }
        if (focused) {
            if (input_caret_pos < ui->text_input_scroll_col) ui->text_input_scroll_col = input_caret_pos;
            while (sl_ui_utf8_count_between(text, ui->text_input_scroll_col, input_caret_pos) >= vis_chars &&
                   ui->text_input_scroll_col < input_caret_pos) {
                ui->text_input_scroll_col = sl_ui_utf8_next_pos(text, ui->text_input_scroll_col, len);
            }
        }
        uint32_t start = focused ? ui->text_input_scroll_col : 0u;
        char draw_buf[192];
        uint32_t n = 0u;
        uint32_t pos = start;
        uint32_t cols = 0u;
        while (text[pos] && n + 5u < (uint32_t)sizeof(draw_buf) && cols < vis_chars) {
            uint32_t next = sl_ui_utf8_next_pos(text, pos, len);
            while (pos < next && n + 1u < (uint32_t)sizeof(draw_buf)) {
                draw_buf[n++] = text[pos++];
            }
            cols++;
        }
        uint32_t visible_end = pos;
        uint32_t caret_draw = (input_caret_pos >= start) ?
            sl_ui_utf8_count_between(text, start, input_caret_pos) : 0u;
        if (caret_draw > vis_chars) caret_draw = vis_chars;
        draw_buf[n] = '\0';
        uint32_t text_x = x + 6u;
        uint32_t text_y = sl_ui_text_center_y(ui, y, h);
        {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                uint32_t clip_start = (sel_start > start) ? sel_start : start;
                uint32_t clip_end = (sel_end < visible_end) ? sel_end : visible_end;
                if (clip_end > clip_start) {
                    uint32_t sel_col0 = sl_ui_utf8_count_between(text, start, clip_start);
                    uint32_t sel_col1 = sl_ui_utf8_count_between(text, start, clip_end);
                    if (sel_col1 > sel_col0) {
                        sl_ui_fill_selection(ui, text_x + sel_col0 * fw, text_y,
                                             (sel_col1 - sel_col0) * fw, fh);
                    }
                }
            }
        }
        sl_ui_text_cells(ui, text_x, text_y, draw_buf,
                         enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        if (caret_visible) {
            uint32_t caret_px = text_x + (caret_draw * fw);
            uint32_t caret_max = text_x + (vis_chars * fw);
            if (caret_px >= caret_max) caret_px = caret_max - 1u;
            sl_ui_fill_tok(ui, caret_px, text_y, 2u, fh, SL_UI_COLOR_NEON);
        }
    }
    return changed;
}

uint8_t sl_ui_text_area(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        char *text, uint32_t text_cap,
                        const peos_input_event_t *events, uint8_t event_count) {
    return sl_ui_text_area_state(ui, id, x, y, w, h, text, text_cap, events, event_count, 1u);
}

static uint32_t sl_ui_text_line_start(const char *text, uint32_t pos) {
    while (pos > 0u && text[pos - 1u] != '\n') pos--;
    return pos;
}

static uint32_t sl_ui_text_line_end(const char *text, uint32_t start) {
    uint32_t i = start;
    while (text[i] && text[i] != '\n') i++;
    return i;
}

static uint32_t sl_ui_text_total_lines(const char *text) {
    uint32_t lines = 1u;
    for (uint32_t i = 0u; text[i]; i++) {
        if (text[i] == '\n') lines++;
    }
    return lines;
}

static uint32_t sl_ui_text_max_line_len(const char *text) {
    if (!text) return 0u;
    uint32_t max_len = 0u;
    uint32_t cur_len = 0u;
    for (uint32_t i = 0u;;) {
        char c = text[i];
        if (c == '\0' || c == '\n') {
            if (cur_len > max_len) max_len = cur_len;
            if (c == '\0') break;
            cur_len = 0u;
            i++;
            continue;
        }
        i = sl_ui_utf8_next_pos(text, i, sl_ui_strlen(text));
        cur_len++;
    }
    return max_len;
}

static uint32_t sl_ui_text_line_col_at(const char *text, uint32_t pos, uint32_t *col_out) {
    uint32_t line = 0u;
    uint32_t col = 0u;
    uint32_t len = sl_ui_strlen(text);
    for (uint32_t i = 0u; i < pos && text[i];) {
        if (text[i] == '\n') {
            line++;
            col = 0u;
            i++;
        } else {
            i = sl_ui_utf8_next_pos(text, i, len);
            col++;
        }
    }
    if (col_out) *col_out = col;
    return line;
}

static uint32_t sl_ui_text_pos_from_line_col(const char *text, uint32_t target_line, uint32_t target_col) {
    uint32_t line = 0u;
    uint32_t i = 0u;
    if (!text) return 0u;

    while (text[i] && line < target_line) {
        if (text[i] == '\n') line++;
        i++;
    }
    if (line < target_line) return i;

    uint32_t line_start = i;
    uint32_t line_end = sl_ui_text_line_end(text, line_start);
    uint32_t line_len = sl_ui_utf8_count_between(text, line_start, line_end);
    if (target_col > line_len) target_col = line_len;
    return sl_ui_utf8_advance_cols(text, line_start, line_end, target_col);
}

static uint32_t sl_ui_text_area_hit_pos(sl_ui_ctx_t *ui,
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                        const char *text) {
    uint32_t content_x, content_y, content_w, content_h, fw, fh, vis_cols, vis_lines;
    uint32_t click_row = 0u, click_col = 0u, click_line;
    if (!ui || !text) return 0u;
    content_x = x + 6u;
    content_y = y + 6u;
    content_w = (w > 12u) ? (w - 12u) : 0u;
    content_h = (h > 12u) ? (h - 12u) : 0u;
    fw = sl_ui_font_w(ui);
    fh = sl_ui_font_h(ui);
    vis_cols = content_w / fw;
    vis_lines = content_h / fh;
    if (vis_cols == 0u || vis_lines == 0u) return 0u;
    if (ui->cursor_y > content_y) click_row = (ui->cursor_y - content_y) / fh;
    if (click_row >= vis_lines) click_row = vis_lines - 1u;
    if (ui->cursor_x > content_x) click_col = (ui->cursor_x - content_x) / fw;
    if (click_col > vis_cols) click_col = vis_cols;
    click_col += ui->text_area_scroll_col;
    click_line = ui->text_area_scroll_line + click_row;
    return sl_ui_text_pos_from_line_col(text, click_line, click_col);
}

static void sl_ui_text_insert_bytes(char *text, uint32_t *len, uint32_t cap,
                                    uint32_t *caret, const char *bytes, uint32_t byte_len) {
    if (!text || !len || !caret || !bytes || byte_len == 0u || *len + byte_len >= cap) return;
    for (uint32_t i = *len + 1u; i > *caret; i--) {
        text[i + byte_len - 1u] = text[i - 1u];
    }
    for (uint32_t i = 0u; i < byte_len; i++) text[*caret + i] = bytes[i];
    *len += byte_len;
    *caret += byte_len;
}

static void sl_ui_text_insert_char(char *text, uint32_t *len, uint32_t cap, uint32_t *caret, char c) {
    sl_ui_text_insert_bytes(text, len, cap, caret, &c, 1u);
}

static uint8_t sl_ui_text_delete_range(char *text, uint32_t *len, uint32_t start, uint32_t end) {
    if (!text || !len || start >= end || end > *len) return 0u;
    uint32_t removed = end - start;
    for (uint32_t i = start; i + removed <= *len; i++) {
        text[i] = text[i + removed];
    }
    *len -= removed;
    return 1u;
}

static uint8_t sl_ui_text_delete_selection(sl_ui_ctx_t *ui, uint32_t id,
                                           char *text, uint32_t *len, uint32_t *caret) {
    uint32_t sel_start = 0u;
    uint32_t sel_end = 0u;
    if (!ui || !text || !len || !caret) return 0u;
    if (!sl_ui_selection_range(ui, id, &sel_start, &sel_end)) return 0u;
    if (!sl_ui_text_delete_range(text, len, sel_start, sel_end)) return 0u;
    *caret = sel_start;
    sl_ui_selection_collapse(ui, id, sel_start);
    return 1u;
}

static uint8_t sl_ui_text_apply_paste(sl_ui_ctx_t *ui, uint32_t id,
                                      char *text, uint32_t *len, uint32_t text_cap, uint32_t *caret) {
    const char *clip_text = NULL;
    uint32_t clip_len = 0u;
    uint32_t old_len = 0u;
    if (!ui || !text || !len || !caret || text_cap < 2u) return 0u;
    clip_text = sl_clipboard_current_text(&clip_len);
    if (!clip_text || clip_len == 0u) return 0u;
    if (sl_ui_selection_range(ui, id, NULL, NULL)) {
        (void)sl_ui_text_delete_selection(ui, id, text, len, caret);
    }
    old_len = *len;
    sl_ui_text_insert_bytes(text, len, text_cap, caret, clip_text, clip_len);
    if (*len == old_len) return 0u;
    sl_ui_selection_collapse(ui, id, *caret);
    return 1u;
}

static void sl_ui_text_backspace(char *text, uint32_t *len, uint32_t *caret) {
    if (!text || !len || !caret || *caret == 0u || *len == 0u) return;
    uint32_t prev = sl_ui_utf8_prev_pos(text, *caret);
    uint32_t removed = *caret - prev;
    for (uint32_t i = prev; i + removed <= *len; i++) {
        text[i] = text[i + removed];
    }
    *caret = prev;
    *len -= removed;
}

uint8_t sl_ui_text_area_state(sl_ui_ctx_t *ui, uint32_t id,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              char *text, uint32_t text_cap,
                              const peos_input_event_t *events, uint8_t event_count,
                              uint8_t enabled) {
    if (!ui || !ui->ctx || !text || text_cap < 2u || w < 24u || h < 24u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && ui->mouse_pressed) {
        if (hovered) {
            ui->focused_id = id;
            ui->active_id = id;
        }
        else if (ui->focused_id == id) ui->focused_id = 0u;
    }
    if (!enabled && ui->focused_id == id) ui->focused_id = 0u;
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    int focused = (ui->focused_id == id);

    uint32_t len = sl_ui_strlen(text);
    if (len >= text_cap) {
        len = text_cap - 1u;
        text[len] = '\0';
    }

    if (focused && ui->text_caret_id != id) {
        ui->text_caret_id = id;
        ui->text_caret_pos = len;
        ui->text_area_scroll_line = 0u;
        ui->text_area_scroll_col = 0u;
        sl_ui_selection_collapse(ui, id, len);
    }
    if (ui->text_caret_id == id && ui->text_caret_pos > len) ui->text_caret_pos = len;
    if (ui->text_caret_id == id && ui->text_caret_pos > 0u &&
        sl_ui_utf8_is_cont(text[ui->text_caret_pos])) {
        ui->text_caret_pos = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
    }
    if (ui->text_select_id == id) {
        if (ui->text_select_anchor_pos > len) ui->text_select_anchor_pos = len;
        if (ui->text_select_focus_pos > len) ui->text_select_focus_pos = len;
    }

    uint8_t changed = 0u;
    if (enabled && focused) {
        if (ui->clipboard_cmds & SL_CLIP_CMD_COPY) {
            uint32_t sel_start = 0u, sel_end = 0u;
            int32_t fx = 0, fy = 0;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end) &&
                sl_ui_clipboard_copy_selection(ui, id, text)) {
                sl_ui_text_area_copy_point(ui, x, y, text, &fx, &fy);
                sl_ui_clipboard_notify_copy_feedback(ui, text, sel_start, sel_end, fx, fy);
            }
            ui->clipboard_cmds &= ~SL_CLIP_CMD_COPY;
        }
        if (ui->clipboard_cmds & SL_CLIP_CMD_PASTE) {
            if (sl_ui_text_apply_paste(ui, id, text, &len, text_cap, &ui->text_caret_pos)) changed = 1u;
            ui->clipboard_cmds &= ~SL_CLIP_CMD_PASTE;
        }
    }
    for (uint8_t i = 0u; i < event_count; i++) {
        uint16_t code = sl_ui_norm_keycode(events[i].code);
        if (sl_keyboard_event_is_altgr(&events[i])) {
            ui->altgr_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
            ui->shift_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LCTRL) {
            ui->ctrl_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LALT) {
            ui->alt_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
            ui->super_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (!enabled || !focused || !events[i].pressed) continue;
        if (ui->ctrl_down && code == PEOS_KEY_A) {
            ui->text_caret_pos = len;
            ui->text_select_id = id;
            ui->text_select_anchor_pos = 0u;
            ui->text_select_focus_pos = len;
            continue;
        }

        if (code == PEOS_KEY_BACKSPACE) {
            uint32_t old_len = len;
            if (!sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) {
                sl_ui_text_backspace(text, &len, &ui->text_caret_pos);
                if (len != old_len) sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            }
            if (len != old_len) changed = 1u;
            continue;
        }
        if (code == PEOS_KEY_ESC) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (code == PEOS_KEY_ENTER) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_char(text, &len, text_cap, &ui->text_caret_pos, '\n');
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (code == PEOS_KEY_LEFT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_start, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos > 0u) new_pos = sl_ui_utf8_prev_pos(text, new_pos);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_RIGHT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_end, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos < len) new_pos = sl_ui_utf8_next_pos(text, new_pos, len);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_UP_NORM || code == PEOS_KEY_DOWN_NORM) {
            uint32_t old_pos = ui->text_caret_pos;
            uint32_t new_pos = old_pos;
            uint32_t line_start = sl_ui_text_line_start(text, old_pos);
            uint32_t col = sl_ui_utf8_count_between(text, line_start, old_pos);
            if (code == PEOS_KEY_UP_NORM) {
                if (line_start > 0u) {
                    uint32_t prev_end = line_start - 1u;
                    uint32_t prev_start = sl_ui_text_line_start(text, prev_end);
                    uint32_t prev_len = sl_ui_utf8_count_between(text, prev_start, prev_end);
                    new_pos = sl_ui_utf8_advance_cols(text, prev_start, prev_end,
                                                      (col < prev_len) ? col : prev_len);
                }
            } else {
                uint32_t line_end = sl_ui_text_line_end(text, line_start);
                if (text[line_end] == '\n') {
                    uint32_t next_start = line_end + 1u;
                    uint32_t next_end = sl_ui_text_line_end(text, next_start);
                    uint32_t next_len = sl_ui_utf8_count_between(text, next_start, next_end);
                    new_pos = sl_ui_utf8_advance_cols(text, next_start, next_end,
                                                      (col < next_len) ? col : next_len);
                }
            }
            sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_TAB) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_char(text, &len, text_cap, &ui->text_caret_pos, '\t');
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (ui->ctrl_down || ui->alt_down || ui->super_down) {
            continue;
        }

        char input[8];
        size_t input_len = sl_keyboard_translate_event(&events[i], ui->shift_down,
                                                       ui->caps_lock_on,
                                                       ui->altgr_down,
                                                       input, sizeof(input));
        if (input_len > 0u) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_bytes(text, &len, text_cap, &ui->text_caret_pos,
                                    input, (uint32_t)input_len);
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (focused) state = SL_UI_STATE_FOCUSED;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_surface(ui, state == SL_UI_STATE_DISABLED ? SL_UI_SURF_FLAT : SL_UI_SURF_INSET, x, y, w, h, 14u);

    uint32_t content_x = x + 6u;
    uint32_t content_y = y + 6u;
    uint32_t content_w = (w > 12u) ? (w - 12u) : 0u;
    uint32_t content_h = (h > 12u) ? (h - 12u) : 0u;
    uint32_t fw = sl_ui_font_w(ui);
    uint32_t fh = sl_ui_font_h(ui);
    uint32_t vis_cols = content_w / fw;
    uint32_t vis_lines = content_h / fh;
    if (vis_cols == 0u || vis_lines == 0u) return changed;

    uint32_t total_lines = sl_ui_text_total_lines(text);
    uint32_t max_scroll = (total_lines > vis_lines) ? (total_lines - vis_lines) : 0u;
    if (ui->text_area_scroll_line > max_scroll) ui->text_area_scroll_line = max_scroll;
    uint32_t max_line_len = sl_ui_text_max_line_len(text);
    uint32_t max_scroll_col = (max_line_len > vis_cols) ? (max_line_len - vis_cols) : 0u;
    if (ui->text_area_scroll_col > max_scroll_col) ui->text_area_scroll_col = max_scroll_col;

    if (enabled && ui->mouse_pressed && hovered) {
        ui->text_caret_pos = sl_ui_text_area_hit_pos(ui, x, y, w, h, text);
        if (ui->text_caret_pos > len) ui->text_caret_pos = len;
        sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
    }
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t old_pos = ui->text_caret_pos;
        uint32_t new_pos = sl_ui_text_area_hit_pos(ui, x, y, w, h, text);
        if (new_pos > len) new_pos = len;
        sl_ui_move_caret(ui, id, old_pos, new_pos, 1u);
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    if (enabled && hovered && !ui->wheel_consumed && max_scroll > 0u) {
        int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
        if (wheel != 0) {
            int64_t nv = (int64_t)ui->text_area_scroll_line - (int64_t)wheel;
            if (nv < 0) nv = 0;
            if (nv > (int64_t)max_scroll) nv = (int64_t)max_scroll;
            ui->text_area_scroll_line = (uint32_t)nv;
            ui->wheel_consumed = 1u;
        }
    }

    uint32_t area_caret_pos = (ui->text_caret_id == id) ? ui->text_caret_pos : len;
    uint32_t caret_col = 0u;
    uint32_t caret_line = sl_ui_text_line_col_at(text, area_caret_pos, &caret_col);
    uint8_t caret_visible = (enabled && focused &&
                             (((peos_time_ns() / 500000000ull) & 1ull) != 0ull)) ? 1u : 0u;
    if (caret_line < ui->text_area_scroll_line) ui->text_area_scroll_line = caret_line;
    if (caret_line >= ui->text_area_scroll_line + vis_lines) {
        ui->text_area_scroll_line = caret_line - vis_lines + 1u;
    }
    if (caret_col < ui->text_area_scroll_col) ui->text_area_scroll_col = caret_col;
    if (caret_col >= ui->text_area_scroll_col + vis_cols) {
        ui->text_area_scroll_col = caret_col - vis_cols + 1u;
    }
    if (ui->text_area_scroll_col > max_scroll_col) ui->text_area_scroll_col = max_scroll_col;

    uint32_t line_idx = 0u;
    uint32_t start = 0u;
    while (line_idx < ui->text_area_scroll_line && text[start]) {
        if (text[start] == '\n') line_idx++;
        start++;
    }

    for (uint32_t row = 0u; row < vis_lines; row++) {
        uint32_t ly = content_y + (row * fh);
        if (!text[start] && !(line_idx == caret_line && caret_col == 0u)) {
            line_idx++;
            continue;
        }

        uint32_t end = sl_ui_text_line_end(text, start);
        char draw_buf[256];
        uint32_t dn = 0u;
        uint32_t i = start;
        uint32_t col = 0u;
        while (i < end && col < ui->text_area_scroll_col) {
            i = sl_ui_utf8_next_pos(text, i, end);
            col++;
        }
        while (i < end && dn + 5u < (uint32_t)sizeof(draw_buf) && col < ui->text_area_scroll_col + vis_cols) {
            uint32_t next = sl_ui_utf8_next_pos(text, i, end);
            if (text[i] == '\t') {
                draw_buf[dn++] = ' ';
            } else {
                while (i < next && dn + 1u < (uint32_t)sizeof(draw_buf)) {
                    draw_buf[dn++] = text[i++];
                }
            }
            i = next;
            col++;
        }

        draw_buf[dn] = '\0';

        {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                uint32_t clip_start = (sel_start > start) ? sel_start : start;
                uint32_t clip_end = (sel_end < end) ? sel_end : end;
                if (clip_end > clip_start) {
                    uint32_t col0 = sl_ui_utf8_count_between(text, start, clip_start);
                    uint32_t col1 = sl_ui_utf8_count_between(text, start, clip_end);
                    if (col1 > col0) {
                        uint32_t draw_col0 = (col0 > ui->text_area_scroll_col) ? (col0 - ui->text_area_scroll_col) : 0u;
                        uint32_t draw_col1 = (col1 > ui->text_area_scroll_col) ? (col1 - ui->text_area_scroll_col) : 0u;
                        if (draw_col0 > vis_cols) draw_col0 = vis_cols;
                        if (draw_col1 > vis_cols) draw_col1 = vis_cols;
                        if (draw_col1 > draw_col0) {
                            sl_ui_fill_selection(ui, content_x + draw_col0 * fw, ly,
                                                 (draw_col1 - draw_col0) * fw, fh);
                        }
                    }
                }
            }
        }
        sl_ui_text_cells(ui, content_x, ly, draw_buf,
                         enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        if (caret_visible && line_idx == caret_line) {
            uint32_t caret_x = 0u;
            if (caret_col > ui->text_area_scroll_col) {
                caret_x = caret_col - ui->text_area_scroll_col;
            }
            if (caret_x > vis_cols) caret_x = vis_cols;
            uint32_t caret_px = content_x + (caret_x * fw);
            uint32_t caret_max = content_x + (vis_cols * fw);
            if (caret_px >= caret_max) caret_px = caret_max - 1u;
            sl_ui_fill_tok(ui, caret_px, ly, 2u, fh, SL_UI_COLOR_NEON);
        }

        if (text[end] == '\n') start = end + 1u;
        else start = end;
        line_idx++;
    }

    return changed;
}

uint8_t sl_ui_scroll_v(sl_ui_ctx_t *ui, uint32_t id,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t content_h, uint32_t *offset,
                       uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h) {
    return sl_ui_scroll_v_state(ui, id, x, y, w, h, content_h, offset, hit_x, hit_y, hit_w, hit_h, 1u);
}

uint8_t sl_ui_scroll_v_state(sl_ui_ctx_t *ui, uint32_t id,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t content_h, uint32_t *offset,
                             uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h,
                             uint8_t enabled) {
    if (!ui || !ui->ctx || !offset || w < 8u || h < 20u) return 0u;

    uint32_t max_off = 0u;
    if (content_h > h) max_off = content_h - h;
    if (*offset > max_off) *offset = max_off;

    uint32_t track_x = x + 2u;
    uint32_t track_y = y + 2u;
    uint32_t track_w = (w > 4u) ? (w - 4u) : w;
    uint32_t track_h = (h > 4u) ? (h - 4u) : h;
    if (track_w == 0u || track_h < 8u) return 0u;

    uint32_t knob_h = track_h;
    if (content_h > h && content_h > 0u) {
        knob_h = (uint32_t)(((uint64_t)track_h * (uint64_t)h) / (uint64_t)content_h);
        if (knob_h < 12u) knob_h = 12u;
        if (knob_h > track_h) knob_h = track_h;
    }
    uint32_t travel = (track_h > knob_h) ? (track_h - knob_h) : 0u;
    uint32_t knob_y = track_y;
    if (travel > 0u && max_off > 0u) {
        knob_y = track_y + (uint32_t)(((uint64_t)(*offset) * (uint64_t)travel) / (uint64_t)max_off);
    }

    if (hit_w == 0u || hit_h == 0u) {
        hit_x = x;
        hit_y = y;
        hit_w = w;
        hit_h = h;
    }

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, hit_x, hit_y, hit_w, hit_h);
    int bar_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    int knob_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, track_x, knob_y, track_w, knob_h);
    if (enabled && bar_hovered && ui->mouse_pressed) {
        ui->active_id = id;
        if (!knob_hovered && travel > 0u && max_off > 0u) {
            uint32_t cy = ui->cursor_y;
            if (cy < track_y) cy = track_y;
            if (cy > track_y + track_h) cy = track_y + track_h;
            uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
            if (knob_pos > travel) knob_pos = travel;
            *offset = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        }
    }

    uint8_t changed = 0u;
    if (enabled && hovered && !ui->wheel_consumed && max_off > 0u) {
        int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
        if (wheel != 0) {
            int32_t step = (int32_t)(h / 5u);
            if (step < 12) step = 12;
            if (step > 64) step = 64;

            int64_t nv = (int64_t)(*offset) - ((int64_t)wheel * (int64_t)step);
            if (nv < 0) nv = 0;
            if (nv > (int64_t)max_off) nv = (int64_t)max_off;
            if ((uint32_t)nv != *offset) {
                *offset = (uint32_t)nv;
                changed = 1u;
            }
            ui->wheel_consumed = 1u;
        }
    }

    if (enabled && ui->active_id == id && ui->mouse_down && travel > 0u && max_off > 0u) {
        uint32_t cy = ui->cursor_y;
        if (cy < track_y) cy = track_y;
        if (cy > track_y + track_h) cy = track_y + track_h;
        uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
        if (knob_pos > travel) knob_pos = travel;
        uint32_t nv = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        if (nv != *offset) {
            *offset = nv;
            changed = 1u;
        }
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    (void)bar_hovered;
    // A thin inset track with a night thumb, lit under the mouse.
    sl_ui_surface(ui, SL_UI_SURF_INSET, track_x, track_y, track_w, track_h, track_w / 2u);
    sl_ui_surf_kind_t knob_k = SL_UI_SURF_NIGHT;
    if (!enabled) knob_k = SL_UI_SURF_FLAT;
    else if ((ui->active_id == id && ui->mouse_down) || knob_hovered) knob_k = SL_UI_SURF_NIGHT_GLOW;
    sl_ui_surface(ui, knob_k, track_x, knob_y, track_w, knob_h, track_w / 2u);
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    return changed;
}

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ui || !ui->ctx || !rgba || w == 0u || h == 0u || src_w == 0u || src_h == 0u) return;
    sl_blit_rgba_stretch(ui->ctx, x, y, w, h, rgba, src_w, src_h, src_stride);
}

void sl_ui_image(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const sl_image_t *image) {
    if (!ui || !ui->ctx || !image || !image->data || w == 0u || h == 0u) return;
    sl_blit_image_stretch(ui->ctx, x, y, w, h, image);
}

void sl_ui_rect(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                sl_ui_color_token_t token) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_ui_fill_tok(ui, x, y, w, h, token);
}

void sl_ui_rect_a(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  sl_ui_color_token_t token, uint8_t a) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    uint32_t aa = ((uint32_t)c->a * (uint32_t)a) / 255u;
    sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, (uint8_t)aa);
}

void sl_ui_rect_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect(ui->ctx, x, y, w, h, r, g, b);
}

void sl_ui_rect_a_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect_a(ui->ctx, x, y, w, h, r, g, b, a);
}

void sl_ui_separator(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w) {
    if (!ui || !ui->ctx || w == 0u) return;
    // Engraved: a shadow line over a light one.
    sl_ui_fill_tok(ui, x, y, w, 1u, SL_UI_COLOR_BORDER);
    sl_ui_fill_tok(ui, x, y + 1u, w, 1u, SL_UI_COLOR_SHADOW_LIGHT);
}

uint8_t sl_ui_list_item(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const char *title, const char *subtitle, uint8_t enabled) {
    if (!title) title = "";
    if (!subtitle) subtitle = "";
    if (!ui || !ui->ctx || w < 16u || h < 16u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) changed = 1u;
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    // Rows sit on the pearl; under the mouse or pressed they sink.
    if (state == SL_UI_STATE_HOVER || state == SL_UI_STATE_ACTIVE) {
        sl_ui_surface(ui, SL_UI_SURF_INSET, x, y, w, h, sl_ui_pill_r(h, 14u));
    }

    const slsk_font_t *face = sl_ui_face(ui);
    uint32_t fh = face ? sl_ui_cap(face) + 5u : sl_ui_font_h(ui);
    uint32_t title_y = subtitle[0] ? sl_ui_text_center_y(ui, y, h > fh ? h - fh : h) : sl_ui_text_center_y(ui, y, h);
    sl_ui_text_tok(ui, x + 14u, title_y, title,
                   enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
    if (subtitle[0]) {
        uint32_t sub_y = title_y + fh;
        sl_ui_text_tok(ui, x + 14u, sub_y, subtitle,
                       enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);
    }
    sl_ui_text_tok(ui, x + w - 18u, sl_ui_text_center_y(ui, y, h), ">",
                   enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);

    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

void sl_ui_stack_begin(sl_ui_stack_t *stack,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       sl_ui_stack_axis_t axis, uint32_t spacing) {
    if (!stack) return;
    stack->x = x;
    stack->y = y;
    stack->w = w;
    stack->h = h;
    stack->spacing = spacing;
    stack->cursor = 0u;
    stack->axis = axis;
}

uint8_t sl_ui_stack_next(sl_ui_stack_t *stack, uint32_t primary_size,
                         uint32_t *out_x, uint32_t *out_y,
                         uint32_t *out_w, uint32_t *out_h) {
    if (!stack || !out_x || !out_y || !out_w || !out_h || primary_size == 0u) return 0u;

    if (stack->axis == SL_UI_STACK_VERTICAL) {
        if (stack->cursor > stack->h) return 0u;
        if (primary_size > (stack->h - stack->cursor)) return 0u;

        *out_x = stack->x;
        *out_y = stack->y + stack->cursor;
        *out_w = stack->w;
        *out_h = primary_size;
    } else {
        if (stack->cursor > stack->w) return 0u;
        if (primary_size > (stack->w - stack->cursor)) return 0u;

        *out_x = stack->x + stack->cursor;
        *out_y = stack->y;
        *out_w = primary_size;
        *out_h = stack->h;
    }

    uint32_t next_cursor = stack->cursor + primary_size;
    if (next_cursor < stack->cursor) return 0u;
    if (stack->spacing > 0u) {
        if (next_cursor > 0xFFFFFFFFu - stack->spacing) return 0u;
        next_cursor += stack->spacing;
    }
    stack->cursor = next_cursor;
    return 1u;
}

void sl_ui_grid_begin(sl_ui_grid_t *grid,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t cols, uint32_t rows,
                      uint32_t gap_x, uint32_t gap_y) {
    if (!grid) return;
    grid->x = x;
    grid->y = y;
    grid->w = w;
    grid->h = h;
    grid->cols = cols;
    grid->rows = rows;
    grid->gap_x = gap_x;
    grid->gap_y = gap_y;
    grid->cursor = 0u;
}

static uint8_t sl_ui_grid_metrics(const sl_ui_grid_t *grid,
                                  uint32_t *cell_w, uint32_t *cell_h) {
    if (!grid || !cell_w || !cell_h || grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t gaps_x = (uint64_t)(grid->cols - 1u) * (uint64_t)grid->gap_x;
    uint64_t gaps_y = (uint64_t)(grid->rows - 1u) * (uint64_t)grid->gap_y;
    if (gaps_x >= (uint64_t)grid->w || gaps_y >= (uint64_t)grid->h) return 0u;

    *cell_w = (uint32_t)(((uint64_t)grid->w - gaps_x) / (uint64_t)grid->cols);
    *cell_h = (uint32_t)(((uint64_t)grid->h - gaps_y) / (uint64_t)grid->rows);
    return (*cell_w > 0u && *cell_h > 0u) ? 1u : 0u;
}

uint8_t sl_ui_grid_cell(const sl_ui_grid_t *grid, uint32_t col, uint32_t row,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (col >= grid->cols || row >= grid->rows) return 0u;

    uint32_t cell_w = 0u;
    uint32_t cell_h = 0u;
    if (!sl_ui_grid_metrics(grid, &cell_w, &cell_h)) return 0u;

    uint64_t step_x = (uint64_t)cell_w + (uint64_t)grid->gap_x;
    uint64_t step_y = (uint64_t)cell_h + (uint64_t)grid->gap_y;
    uint64_t px = (uint64_t)grid->x + ((uint64_t)col * step_x);
    uint64_t py = (uint64_t)grid->y + ((uint64_t)row * step_y);
    if (px > 0xFFFFFFFFu || py > 0xFFFFFFFFu) return 0u;

    *out_x = (uint32_t)px;
    *out_y = (uint32_t)py;
    *out_w = cell_w;
    *out_h = cell_h;
    return 1u;
}

uint8_t sl_ui_grid_next(sl_ui_grid_t *grid,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t total = (uint64_t)grid->cols * (uint64_t)grid->rows;
    if ((uint64_t)grid->cursor >= total) return 0u;

    uint32_t col = grid->cursor % grid->cols;
    uint32_t row = grid->cursor / grid->cols;
    if (!sl_ui_grid_cell(grid, col, row, out_x, out_y, out_w, out_h)) return 0u;

    grid->cursor++;
    return 1u;
}
