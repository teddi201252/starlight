#include "font.h"

#include "font8x8.h"
#include "image_decode.h"
#include "pixels.h"

#include "peos/alloc.h"
#include "peos/peos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sl_stbtt_ifloor(float x) {
    int i = (int)x;
    return (x < (float)i) ? (i - 1) : i;
}

static int sl_stbtt_iceil(float x) {
    int i = (int)x;
    return (x > (float)i) ? (i + 1) : i;
}

static double sl_stbtt_fabs(double x) {
    return (x < 0.0) ? -x : x;
}

static double sl_stbtt_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = (x >= 1.0) ? x : 1.0;
    for (uint32_t i = 0u; i < 16u; i++) {
        r = 0.5 * (r + (x / r));
    }
    return r;
}

static double sl_stbtt_fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    int q = (int)(x / y);
    return x - ((double)q * y);
}

static double sl_stbtt_pow(double x, double y) {
    int yi = (int)y;
    double out = 1.0;
    if ((double)yi != y) return 1.0;
    if (yi < 0) {
        if (x == 0.0) return 0.0;
        x = 1.0 / x;
        yi = -yi;
    }
    for (int i = 0; i < yi; i++) out *= x;
    return out;
}

static double sl_stbtt_cos(double x) {
    const double pi = 3.14159265358979323846;
    x = sl_stbtt_fmod(x, 2.0 * pi);
    double x2 = x * x;
    return 1.0 - (x2 * 0.5) + ((x2 * x2) / 24.0) - ((x2 * x2 * x2) / 720.0);
}

static double sl_stbtt_acos(double x) {
    (void)x;
    return 0.0;
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x,u) ((void)(u), malloc(x))
#define STBTT_free(x,u) ((void)(u), free(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_ifloor(x) sl_stbtt_ifloor(x)
#define STBTT_iceil(x) sl_stbtt_iceil(x)
#define STBTT_sqrt(x) sl_stbtt_sqrt(x)
#define STBTT_pow(x,y) sl_stbtt_pow((x),(y))
#define STBTT_fmod(x,y) sl_stbtt_fmod((x),(y))
#define STBTT_cos(x) sl_stbtt_cos(x)
#define STBTT_acos(x) sl_stbtt_acos(x)
#define STBTT_fabs(x) sl_stbtt_fabs(x)
#define STBTT_memcpy peos_memcpy
#define STBTT_memset peos_memset
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../third_party/stb/stb_truetype.h"
#pragma GCC diagnostic pop

enum {
    SL_FONT_KIND_BUILTIN = 0,
    SL_FONT_KIND_ATLAS = 1,
    SL_FONT_KIND_VECTOR = 2,
    SL_FONT_CFG_MAX = 4096u,
    SL_FONT_PATH_MAX = 256u,
    SL_FONT_TOKEN_MAX = 64u,
    SL_FONT_INVALID_GLYPH = 0xFFFFFFFFu,
    SL_FONT_DEFAULT_FALLBACK_CP = 63u, // '?'
    SL_FONT_VECTOR_DEFAULT_SIZE_PX = 14u,
    SL_FONT_VECTOR_DEFAULT_CACHE_GLYPHS = 512u,
    SL_FONT_VECTOR_CACHE_MAX = 2048u,
};

typedef struct {
    uint8_t used;
    uint32_t codepoint;
    uint32_t size_px;
    uint32_t width;
    uint32_t height;
    int32_t bearing_x;
    int32_t bearing_y;
    int32_t advance_x;
    uint64_t last_used;
    uint8_t *coverage;
} sl_font_cached_glyph_t;

typedef struct {
    uint8_t *data;
    size_t data_size;
    stbtt_fontinfo info;
    uint8_t initialized;
    uint8_t kerning;
    uint32_t default_size_px;
    uint32_t fallback_cp;
    uint32_t cache_limit;
    uint32_t cache_count;
    uint64_t cache_tick;
    int ascent_units;
    int descent_units;
    int line_gap_units;
    int32_t line_gap_px;
    int32_t baseline_shift_px;
    sl_font_cached_glyph_t *cache;
} sl_font_vector_t;

typedef struct {
    uint8_t kind;
    sl_image_t atlas;
    sl_font_vector_t vector;
    uint32_t first_char;
    uint32_t glyph_count;
    uint32_t glyph_cols;
    uint32_t glyph_w;
    uint32_t glyph_h;
    uint32_t render_w;
    uint32_t render_h;
    int32_t render_off_x;
    int32_t render_off_y;
    uint32_t advance_x;
    uint32_t line_h;
    uint8_t has_map;
    uint32_t fallback_cp;
    uint32_t fallback_index;
    uint32_t *map_codepoints; // len = glyph_count when has_map=1
    uint32_t map_lut[256];     // codepoint<256 -> glyph index or SL_FONT_INVALID_GLYPH
} sl_font_default_t;

static sl_font_default_t g_font;
static uint8_t g_font_init = 0u;

static void sl_font_map_lut_clear(sl_font_default_t *font) {
    if (!font) return;
    for (uint32_t i = 0u; i < 256u; i++) font->map_lut[i] = SL_FONT_INVALID_GLYPH;
}

static void sl_font_set_builtin_defaults(sl_font_default_t *font) {
    if (!font) return;
    font->kind = SL_FONT_KIND_BUILTIN;
    memset(&font->vector, 0, sizeof(font->vector));
    font->first_char = 32u;
    font->glyph_count = 96u;
    font->glyph_cols = 16u;
    font->glyph_w = 8u;
    font->glyph_h = 8u;
    font->render_w = 8u;
    font->render_h = 8u;
    font->render_off_x = 0;
    font->render_off_y = 0;
    font->advance_x = 8u;
    font->line_h = 8u;
    font->has_map = 0u;
    font->fallback_cp = SL_FONT_DEFAULT_FALLBACK_CP;
    font->fallback_index = 0u;
    font->map_codepoints = NULL;
    sl_font_map_lut_clear(font);
}

static void sl_font_vector_release(sl_font_vector_t *v) {
    if (!v) return;
    if (v->cache) {
        for (uint32_t i = 0u; i < v->cache_limit; i++) {
            if (v->cache[i].coverage) free(v->cache[i].coverage);
        }
        free(v->cache);
    }
    if (v->data) free(v->data);
    memset(v, 0, sizeof(*v));
}

static void sl_font_release(sl_font_default_t *font) {
    if (!font) return;
    if (font->kind == SL_FONT_KIND_ATLAS && font->atlas.data) {
        sl_image_free(&font->atlas);
    }
    if (font->kind == SL_FONT_KIND_VECTOR) {
        sl_font_vector_release(&font->vector);
    }
    if (font->map_codepoints) {
        free(font->map_codepoints);
        font->map_codepoints = NULL;
    }
    font->atlas.data = NULL;
    font->atlas.width = 0u;
    font->atlas.height = 0u;
    font->atlas.channels = 0u;
    font->atlas.stride = 0u;
    font->has_map = 0u;
    font->fallback_index = 0u;
    sl_font_map_lut_clear(font);
}

static char *sl_font_trim(char *s) {
    size_t n = 0u;
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    n = strlen(s);
    while (n > 0u) {
        char c = s[n - 1u];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[n - 1u] = '\0';
        n--;
    }
    return s;
}

static int sl_font_parse_u32(const char *s, uint32_t *out) {
    uint64_t v = 0u;
    size_t i = 0u;
    if (!s || !out || s[0] == '\0') return 0;
    while (s[i]) {
        char c = s[i];
        if (c < '0' || c > '9') return 0;
        v = (v * 10u) + (uint64_t)(c - '0');
        if (v > 0xFFFFFFFFu) return 0;
        i++;
    }
    *out = (uint32_t)v;
    return 1;
}

static int sl_font_parse_i32(const char *s, int32_t *out) {
    int sign = 1;
    uint64_t v = 0u;
    size_t i = 0u;
    if (!s || !out || s[0] == '\0') return 0;
    if (s[0] == '+' || s[0] == '-') {
        if (s[0] == '-') sign = -1;
        i = 1u;
        if (s[i] == '\0') return 0;
    }
    while (s[i]) {
        char c = s[i];
        if (c < '0' || c > '9') return 0;
        v = (v * 10u) + (uint64_t)(c - '0');
        if (v > 2147483648ull) return 0;
        i++;
    }
    if (sign > 0) {
        if (v > 2147483647ull) return 0;
        *out = (int32_t)v;
    } else {
        if (v > 2147483648ull) return 0;
        if (v == 2147483648ull) *out = (int32_t)0x80000000u;
        else *out = -(int32_t)v;
    }
    return 1;
}

static int sl_font_parse_hex_u32(const char *s, uint32_t *out) {
    uint64_t v = 0u;
    size_t i = 0u;
    if (!s || !out || s[0] == '\0') return 0;
    while (s[i]) {
        uint8_t nib = 0u;
        char c = s[i];
        if (c >= '0' && c <= '9') nib = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nib = (uint8_t)(10 + (c - 'a'));
        else if (c >= 'A' && c <= 'F') nib = (uint8_t)(10 + (c - 'A'));
        else return 0;
        v = (v << 4u) | (uint64_t)nib;
        if (v > 0xFFFFFFFFu) return 0;
        i++;
    }
    *out = (uint32_t)v;
    return 1;
}

static int sl_font_parse_codepoint(const char *s, uint32_t *out) {
    if (!s || !out || s[0] == '\0') return 0;

    if (s[0] == '\'' && s[1] && s[2] == '\'' && s[3] == '\0') {
        *out = (uint32_t)(uint8_t)s[1];
        return 1;
    }

    if ((s[0] == 'u' || s[0] == 'U') && s[1] == '+') {
        return sl_font_parse_hex_u32(s + 2, out);
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return sl_font_parse_hex_u32(s + 2, out);
    }

    if (s[1] == '\0' && (s[0] < '0' || s[0] > '9')) {
        *out = (uint32_t)(uint8_t)s[0];
        return 1;
    }

    return sl_font_parse_u32(s, out);
}

static void sl_font_copy_str(char *dst, size_t cap, const char *src) {
    size_t i = 0u;
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    while (i + 1u < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int sl_font_has_ext_ci(const char *path, const char *ext) {
    size_t pn = path ? strlen(path) : 0u;
    size_t en = ext ? strlen(ext) : 0u;
    if (!path || !ext || en == 0u || pn < en) return 0;
    path += pn - en;
    for (size_t i = 0u; i < en; i++) {
        char a = path[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int sl_font_is_vector_file_path(const char *path) {
    return sl_font_has_ext_ci(path, ".ttf") || sl_font_has_ext_ci(path, ".otf");
}

static char *sl_font_load_text_file(const char *path, size_t max_bytes) {
    FILE *f = NULL;
    long sz = 0;
    char *buf = NULL;
    size_t got = 0u;
    if (!path || !path[0] || max_bytes == 0u) return NULL;

    f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static uint8_t *sl_font_load_binary_file(const char *path, size_t max_bytes, size_t *out_size) {
    FILE *f = NULL;
    long sz = 0;
    uint8_t *buf = NULL;
    size_t got = 0u;
    if (out_size) *out_size = 0u;
    if (!path || !path[0] || max_bytes == 0u) return NULL;

    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz <= 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    if (out_size) *out_size = got;
    return buf;
}

static int32_t sl_font_round_float_to_i32(float v) {
    if (v >= 0.0f) return (int32_t)(v + 0.5f);
    return (int32_t)(v - 0.5f);
}

static void sl_font_resolve_path(char *out, size_t out_cap,
                                 const char *manifest_path, const char *value) {
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!value || !value[0]) return;

    if (value[0] == '/') {
        sl_font_copy_str(out, out_cap, value);
        return;
    }

    if (!manifest_path) {
        sl_font_copy_str(out, out_cap, value);
        return;
    }

    const char *slash = strrchr(manifest_path, '/');
    if (!slash) {
        sl_font_copy_str(out, out_cap, value);
        return;
    }

    size_t dir_len = (size_t)(slash - manifest_path);
    if (dir_len + 1u >= out_cap) return;
    memcpy(out, manifest_path, dir_len);
    out[dir_len] = '/';
    out[dir_len + 1u] = '\0';
    sl_font_copy_str(out + dir_len + 1u, out_cap - (dir_len + 1u), value);
}

static void sl_font_ensure_initialized(void) {
    if (g_font_init) return;
    memset(&g_font, 0, sizeof(g_font));
    sl_font_set_builtin_defaults(&g_font);
    g_font_init = 1u;
}

static uint32_t sl_font_map_index_range(const sl_font_default_t *font, uint32_t cp) {
    if (!font || font->glyph_count == 0u) return SL_FONT_INVALID_GLYPH;
    if (cp >= font->first_char && cp < (font->first_char + font->glyph_count)) {
        return cp - font->first_char;
    }
    return SL_FONT_INVALID_GLYPH;
}

static uint32_t sl_font_find_map_index(const sl_font_default_t *font, uint32_t cp) {
    if (!font || !font->has_map || !font->map_codepoints || font->glyph_count == 0u) {
        return SL_FONT_INVALID_GLYPH;
    }

    if (cp < 256u && font->map_lut[cp] != SL_FONT_INVALID_GLYPH) {
        return font->map_lut[cp];
    }

    for (uint32_t i = 0u; i < font->glyph_count; i++) {
        if (font->map_codepoints[i] == cp) return i;
    }
    return SL_FONT_INVALID_GLYPH;
}

static uint32_t sl_font_lookup_index(const sl_font_default_t *font, uint32_t cp) {
    uint32_t idx = SL_FONT_INVALID_GLYPH;
    if (!font || font->glyph_count == 0u) return 0u;

    if (font->has_map) {
        idx = sl_font_find_map_index(font, cp);
    } else {
        idx = sl_font_map_index_range(font, cp);
    }

    if (idx != SL_FONT_INVALID_GLYPH) return idx;
    if (font->fallback_index < font->glyph_count) return font->fallback_index;
    return 0u;
}

static void sl_font_init_lookup(sl_font_default_t *font) {
    if (!font) return;

    sl_font_map_lut_clear(font);
    font->fallback_index = 0u;
    if (font->glyph_count == 0u) return;

    if (font->has_map && font->map_codepoints) {
        for (uint32_t i = 0u; i < font->glyph_count; i++) {
            uint32_t cp = font->map_codepoints[i];
            if (cp < 256u && font->map_lut[cp] == SL_FONT_INVALID_GLYPH) {
                font->map_lut[cp] = i;
            }
        }

        uint32_t idx = sl_font_find_map_index(font, font->fallback_cp);
        if (idx == SL_FONT_INVALID_GLYPH) idx = sl_font_find_map_index(font, (uint32_t)'?');
        if (idx == SL_FONT_INVALID_GLYPH) idx = sl_font_find_map_index(font, (uint32_t)' ');
        font->fallback_index = (idx == SL_FONT_INVALID_GLYPH) ? 0u : idx;
        return;
    }

    uint32_t idx = sl_font_map_index_range(font, font->fallback_cp);
    if (idx == SL_FONT_INVALID_GLYPH) idx = sl_font_map_index_range(font, (uint32_t)'?');
    if (idx == SL_FONT_INVALID_GLYPH) idx = sl_font_map_index_range(font, (uint32_t)' ');
    font->fallback_index = (idx == SL_FONT_INVALID_GLYPH) ? 0u : idx;
}

static int sl_font_map_push_token(const char *tok,
                                  uint32_t *entries,
                                  uint32_t cap,
                                  uint32_t *count) {
    uint32_t cp = 0u;
    if (!tok || !count) return 0;
    if (!sl_font_parse_codepoint(tok, &cp)) return 0;
    if (*count < cap && entries) {
        entries[*count] = cp;
    }
    (*count)++;
    return 1;
}

static int sl_font_load_map_file(const char *path, uint32_t glyph_count, uint32_t **out_entries) {
    char *cfg = NULL;
    uint32_t *entries = NULL;
    uint32_t count = 0u;
    char tok[SL_FONT_TOKEN_MAX];
    uint32_t tok_len = 0u;
    uint8_t in_comment = 0u;

    if (!path || !path[0] || !out_entries || glyph_count == 0u) return 0;
    *out_entries = NULL;

    cfg = sl_font_load_text_file(path, SL_FONT_CFG_MAX);
    if (!cfg) return 0;

    entries = (uint32_t *)malloc((size_t)glyph_count * sizeof(uint32_t));
    if (!entries) {
        free(cfg);
        return 0;
    }

    for (size_t i = 0;; i++) {
        char c = cfg[i];

        if (in_comment) {
            if (c == '\n' || c == '\0') in_comment = 0u;
            if (c == '\0') break;
            continue;
        }

        if (c == '#' || c == ';') {
            if (tok_len > 0u) {
                tok[tok_len] = '\0';
                if (!sl_font_map_push_token(tok, entries, glyph_count, &count)) {
                    free(entries);
                    free(cfg);
                    return 0;
                }
                tok_len = 0u;
            }
            in_comment = 1u;
            continue;
        }

        if (c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
            if (tok_len > 0u) {
                tok[tok_len] = '\0';
                if (!sl_font_map_push_token(tok, entries, glyph_count, &count)) {
                    free(entries);
                    free(cfg);
                    return 0;
                }
                tok_len = 0u;
            }
            if (c == '\0') break;
            continue;
        }

        if (tok_len + 1u >= (uint32_t)sizeof(tok)) {
            free(entries);
            free(cfg);
            return 0;
        }
        tok[tok_len++] = c;
    }

    free(cfg);
    if (count < glyph_count) {
        free(entries);
        return 0;
    }

    *out_entries = entries;
    return 1;
}

static uint32_t sl_font_vector_size_px(const sl_font_vector_t *v, uint32_t font_px) {
    if (font_px > 0u) return font_px;
    if (v && v->default_size_px > 0u) return v->default_size_px;
    return SL_FONT_VECTOR_DEFAULT_SIZE_PX;
}

static float sl_font_vector_scale(const sl_font_vector_t *v, uint32_t font_px) {
    uint32_t size_px = sl_font_vector_size_px(v, font_px);
    if (!v || !v->initialized || size_px == 0u) return 1.0f;
    return stbtt_ScaleForPixelHeight(&v->info, (float)size_px);
}

static uint32_t sl_font_vector_line_height_px(const sl_font_vector_t *v, uint32_t font_px) {
    if (!v || !v->initialized) return 8u;
    float scale = sl_font_vector_scale(v, font_px);
    int32_t h = sl_font_round_float_to_i32((float)(v->ascent_units - v->descent_units + v->line_gap_units) * scale);
    h += v->line_gap_px;
    if (h < 1) h = 1;
    return (uint32_t)h;
}

static int32_t sl_font_vector_ascent_px(const sl_font_vector_t *v, uint32_t font_px) {
    if (!v || !v->initialized) return 8;
    int32_t ascent = sl_font_round_float_to_i32((float)v->ascent_units * sl_font_vector_scale(v, font_px));
    return ascent + v->baseline_shift_px;
}

static uint32_t sl_font_vector_cache_limit(uint32_t value) {
    if (value == 0u) return SL_FONT_VECTOR_DEFAULT_CACHE_GLYPHS;
    if (value > SL_FONT_VECTOR_CACHE_MAX) return SL_FONT_VECTOR_CACHE_MAX;
    return value;
}

static sl_font_cached_glyph_t *sl_font_vector_find_cached(sl_font_vector_t *v, uint32_t cp, uint32_t size_px) {
    if (!v || !v->cache) return NULL;
    for (uint32_t i = 0u; i < v->cache_limit; i++) {
        sl_font_cached_glyph_t *g = &v->cache[i];
        if (g->used && g->codepoint == cp && g->size_px == size_px) {
            g->last_used = ++v->cache_tick;
            return g;
        }
    }
    return NULL;
}

static sl_font_cached_glyph_t *sl_font_vector_alloc_cache_slot(sl_font_vector_t *v) {
    if (!v || !v->cache || v->cache_limit == 0u) return NULL;

    for (uint32_t i = 0u; i < v->cache_limit; i++) {
        if (!v->cache[i].used) {
            v->cache[i].last_used = ++v->cache_tick;
            v->cache_count++;
            return &v->cache[i];
        }
    }

    uint32_t oldest = 0u;
    uint64_t oldest_tick = v->cache[0].last_used;
    for (uint32_t i = 1u; i < v->cache_limit; i++) {
        if (v->cache[i].last_used < oldest_tick) {
            oldest = i;
            oldest_tick = v->cache[i].last_used;
        }
    }

    sl_font_cached_glyph_t *slot = &v->cache[oldest];
    if (slot->coverage) free(slot->coverage);
    memset(slot, 0, sizeof(*slot));
    slot->last_used = ++v->cache_tick;
    return slot;
}

static int32_t sl_font_vector_codepoint_advance(const sl_font_vector_t *v, uint32_t cp, uint32_t font_px) {
    if (!v || !v->initialized) return 8;
    int glyph = stbtt_FindGlyphIndex(&v->info, (int)cp);
    if (glyph == 0 && cp != v->fallback_cp) {
        cp = v->fallback_cp;
        glyph = stbtt_FindGlyphIndex(&v->info, (int)cp);
    }
    if (glyph == 0 && cp != (uint32_t)' ') cp = (uint32_t)' ';

    int advance = 0;
    int lsb = 0;
    stbtt_GetCodepointHMetrics(&v->info, (int)cp, &advance, &lsb);
    int32_t out = sl_font_round_float_to_i32((float)advance * sl_font_vector_scale(v, font_px));
    if (out < 1) out = 1;
    return out;
}

static int32_t sl_font_vector_kerning(const sl_font_vector_t *v, uint32_t left, uint32_t right, uint32_t font_px) {
    if (!v || !v->initialized || !v->kerning || left == 0u || right == 0u) return 0;
    int kern = stbtt_GetCodepointKernAdvance(&v->info, (int)left, (int)right);
    if (kern == 0) return 0;
    return sl_font_round_float_to_i32((float)kern * sl_font_vector_scale(v, font_px));
}

static sl_font_cached_glyph_t *sl_font_vector_get_glyph(sl_font_vector_t *v, uint32_t cp, uint32_t font_px) {
    if (!v || !v->initialized) return NULL;
    uint32_t size_px = sl_font_vector_size_px(v, font_px);
    sl_font_cached_glyph_t *cached = sl_font_vector_find_cached(v, cp, size_px);
    if (cached) return cached;

    uint32_t draw_cp = cp;
    int glyph = stbtt_FindGlyphIndex(&v->info, (int)draw_cp);
    if (glyph == 0 && draw_cp != v->fallback_cp) {
        draw_cp = v->fallback_cp;
        glyph = stbtt_FindGlyphIndex(&v->info, (int)draw_cp);
    }
    if (glyph == 0 && draw_cp != (uint32_t)' ') draw_cp = (uint32_t)' ';

    float scale = sl_font_vector_scale(v, size_px);
    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    stbtt_GetCodepointBitmapBox(&v->info, (int)draw_cp, scale, scale, &ix0, &iy0, &ix1, &iy1);

    int advance = 0;
    int lsb = 0;
    stbtt_GetCodepointHMetrics(&v->info, (int)draw_cp, &advance, &lsb);

    sl_font_cached_glyph_t *slot = sl_font_vector_alloc_cache_slot(v);
    if (!slot) return NULL;

    uint32_t w = (ix1 > ix0) ? (uint32_t)(ix1 - ix0) : 0u;
    uint32_t h = (iy1 > iy0) ? (uint32_t)(iy1 - iy0) : 0u;
    uint8_t *coverage = NULL;
    if (w > 0u && h > 0u) {
        uint64_t bytes = (uint64_t)w * (uint64_t)h;
        if (bytes > (1ull << 24)) return NULL;
        coverage = (uint8_t *)malloc((size_t)bytes);
        if (!coverage) return NULL;
        stbtt_MakeCodepointBitmap(&v->info, coverage, (int)w, (int)h, (int)w, scale, scale, (int)draw_cp);
    }

    slot->used = 1u;
    slot->codepoint = cp;
    slot->size_px = size_px;
    slot->width = w;
    slot->height = h;
    slot->bearing_x = ix0;
    slot->bearing_y = iy0;
    slot->advance_x = sl_font_round_float_to_i32((float)advance * scale);
    if (slot->advance_x < 1) slot->advance_x = 1;
    slot->coverage = coverage;
    return slot;
}

static int sl_font_load_vector(const char *manifest_path,
                               const char *file_value,
                               uint32_t default_size_px,
                               uint32_t fallback_cp,
                               uint32_t cache_limit,
                               uint8_t kerning,
                               int32_t line_gap_px,
                               int32_t baseline_shift_px) {
    char font_path[SL_FONT_PATH_MAX];
    size_t font_size = 0u;
    uint8_t *font_data = NULL;
    sl_font_vector_t vec;

    if (!file_value || !file_value[0]) return 0;
    sl_font_resolve_path(font_path, sizeof(font_path), manifest_path, file_value);
    if (!font_path[0]) return 0;

    font_data = sl_font_load_binary_file(font_path, 8u * 1024u * 1024u, &font_size);
    if (!font_data || font_size == 0u) return 0;

    memset(&vec, 0, sizeof(vec));
    vec.data = font_data;
    vec.data_size = font_size;
    vec.default_size_px = default_size_px ? default_size_px : SL_FONT_VECTOR_DEFAULT_SIZE_PX;
    vec.fallback_cp = fallback_cp;
    vec.cache_limit = sl_font_vector_cache_limit(cache_limit);
    vec.kerning = kerning ? 1u : 0u;
    vec.line_gap_px = line_gap_px;
    vec.baseline_shift_px = baseline_shift_px;

    int offset = stbtt_GetFontOffsetForIndex(vec.data, 0);
    if (offset < 0 || !stbtt_InitFont(&vec.info, vec.data, offset)) {
        sl_font_vector_release(&vec);
        return 0;
    }
    vec.initialized = 1u;
    stbtt_GetFontVMetrics(&vec.info, &vec.ascent_units, &vec.descent_units, &vec.line_gap_units);
    vec.cache = (sl_font_cached_glyph_t *)calloc(vec.cache_limit, sizeof(vec.cache[0]));
    if (!vec.cache) {
        sl_font_vector_release(&vec);
        return 0;
    }

    if (stbtt_FindGlyphIndex(&vec.info, (int)vec.fallback_cp) == 0) {
        vec.fallback_cp = (uint32_t)'?';
    }

    sl_font_release(&g_font);
    g_font.kind = SL_FONT_KIND_VECTOR;
    g_font.vector = vec;
    g_font.fallback_cp = vec.fallback_cp;
    return 1;
}

void sl_font_reset_default(void) {
    sl_font_ensure_initialized();
    sl_font_release(&g_font);
    sl_font_set_builtin_defaults(&g_font);
    sl_font_init_lookup(&g_font);
}

void sl_font_shutdown(void) {
    if (!g_font_init) return;
    sl_font_release(&g_font);
    memset(&g_font, 0, sizeof(g_font));
    g_font_init = 0u;
}

int sl_font_load_manifest_default(const char *manifest_path) {
    char *cfg = NULL;
    char line[256];
    size_t li = 0u;
    char backend[16];
    char image_value[SL_FONT_PATH_MAX];
    char file_value[SL_FONT_PATH_MAX];
    char image_path[SL_FONT_PATH_MAX];
    char map_value[SL_FONT_PATH_MAX];
    char map_path[SL_FONT_PATH_MAX];
    uint32_t first_char = 32u;
    uint32_t glyph_count = 96u;
    uint32_t glyph_cols = 16u;
    uint32_t glyph_w = 8u;
    uint32_t glyph_h = 8u;
    uint32_t render_w = 0u;
    uint32_t render_h = 0u;
    int32_t render_off_x = 0;
    int32_t render_off_y = 0;
    uint32_t advance_x = 0u;
    uint32_t line_h = 0u;
    uint32_t size_px = 0u;
    uint32_t cache_glyphs = 0u;
    uint32_t fallback_cp = SL_FONT_DEFAULT_FALLBACK_CP;
    int32_t vector_line_gap_px = 0;
    int32_t baseline_shift_px = 0;
    uint8_t vector_kerning = 1u;
    uint8_t use_map = 0u;
    uint32_t *map_entries = NULL;
    sl_image_t atlas;

    sl_font_ensure_initialized();
    if (!manifest_path || !manifest_path[0]) return 0;

    if (sl_font_is_vector_file_path(manifest_path)) {
        return sl_font_load_vector(NULL, manifest_path, 0u, SL_FONT_DEFAULT_FALLBACK_CP, 0u, 1u, 0, 0);
    }

    cfg = sl_font_load_text_file(manifest_path, SL_FONT_CFG_MAX);
    if (!cfg) return 0;

    sl_font_copy_str(backend, sizeof(backend), "builtin");
    file_value[0] = '\0';
    image_value[0] = '\0';
    map_value[0] = '\0';

    memset(&atlas, 0, sizeof(atlas));
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0u;

        char *p = sl_font_trim(line);
        if (p[0] && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                uint32_t v = 0u;
                *eq = '\0';
                char *key = sl_font_trim(p);
                char *val = sl_font_trim(eq + 1);
                if (strcmp(key, "backend") == 0) {
                    sl_font_copy_str(backend, sizeof(backend), val);
                } else if (strcmp(key, "file") == 0) {
                    sl_font_copy_str(file_value, sizeof(file_value), val);
                } else if (strcmp(key, "image") == 0) {
                    sl_font_copy_str(image_value, sizeof(image_value), val);
                } else if (strcmp(key, "map") == 0) {
                    sl_font_copy_str(map_value, sizeof(map_value), val);
                    use_map = map_value[0] ? 1u : 0u;
                } else if ((strcmp(key, "fallback_char") == 0 || strcmp(key, "fallback") == 0) &&
                           sl_font_parse_codepoint(val, &v)) {
                    fallback_cp = v;
                } else if (strcmp(key, "first") == 0 && sl_font_parse_u32(val, &v)) {
                    first_char = v;
                } else if (strcmp(key, "count") == 0 && sl_font_parse_u32(val, &v)) {
                    glyph_count = v;
                } else if (strcmp(key, "cols") == 0 && sl_font_parse_u32(val, &v)) {
                    glyph_cols = v;
                } else if (strcmp(key, "glyph_w") == 0 && sl_font_parse_u32(val, &v)) {
                    glyph_w = v;
                } else if (strcmp(key, "glyph_h") == 0 && sl_font_parse_u32(val, &v)) {
                    glyph_h = v;
                } else if (strcmp(key, "render_w") == 0 && sl_font_parse_u32(val, &v)) {
                    render_w = v;
                } else if (strcmp(key, "render_h") == 0 && sl_font_parse_u32(val, &v)) {
                    render_h = v;
                } else if ((strcmp(key, "render_off_x") == 0 || strcmp(key, "offset_x") == 0) &&
                           sl_font_parse_i32(val, &render_off_x)) {
                    // Parsed in condition.
                } else if ((strcmp(key, "render_off_y") == 0 || strcmp(key, "offset_y") == 0) &&
                           sl_font_parse_i32(val, &render_off_y)) {
                    // Parsed in condition.
                } else if (strcmp(key, "advance") == 0 && sl_font_parse_u32(val, &v)) {
                    advance_x = v;
                } else if (strcmp(key, "line_h") == 0 && sl_font_parse_u32(val, &v)) {
                    line_h = v;
                } else if (strcmp(key, "size_px") == 0 && sl_font_parse_u32(val, &v)) {
                    size_px = v;
                } else if (strcmp(key, "cache_glyphs") == 0 && sl_font_parse_u32(val, &v)) {
                    cache_glyphs = v;
                } else if (strcmp(key, "kerning") == 0 && sl_font_parse_u32(val, &v)) {
                    vector_kerning = v ? 1u : 0u;
                } else if ((strcmp(key, "line_gap") == 0 || strcmp(key, "line_gap_px") == 0) &&
                           sl_font_parse_i32(val, &vector_line_gap_px)) {
                    // Parsed in condition.
                } else if (strcmp(key, "baseline_shift") == 0 &&
                           sl_font_parse_i32(val, &baseline_shift_px)) {
                    // Parsed in condition.
                }
            }
        }

        if (c == '\0') break;
    }
    free(cfg);

    if (strcmp(backend, "builtin") == 0) {
        sl_font_reset_default();
        g_font.fallback_cp = fallback_cp;
        if (render_w > 0u) g_font.render_w = render_w;
        if (render_h > 0u) g_font.render_h = render_h;
        g_font.render_off_x = render_off_x;
        g_font.render_off_y = render_off_y;
        if (advance_x > 0u) g_font.advance_x = advance_x;
        if (line_h > 0u) g_font.line_h = line_h;
        sl_font_init_lookup(&g_font);
        return 1;
    }

    if (strcmp(backend, "vector") == 0) {
        return sl_font_load_vector(manifest_path, file_value, size_px, fallback_cp, cache_glyphs,
                                   vector_kerning, vector_line_gap_px, baseline_shift_px);
    }

    if (strcmp(backend, "atlas") != 0) return 0;
    if (!image_value[0] || glyph_count == 0u || glyph_cols == 0u || glyph_w == 0u || glyph_h == 0u) return 0;

    sl_font_resolve_path(image_path, sizeof(image_path), manifest_path, image_value);
    if (!image_path[0]) return 0;
    if (!sl_decode_image_file(image_path, &atlas)) return 0;

    uint64_t need_w = (uint64_t)glyph_cols * (uint64_t)glyph_w;
    uint64_t rows = ((uint64_t)glyph_count + (uint64_t)glyph_cols - 1u) / (uint64_t)glyph_cols;
    uint64_t need_h = rows * (uint64_t)glyph_h;
    if ((uint64_t)atlas.width < need_w || (uint64_t)atlas.height < need_h || atlas.channels < 4u) {
        sl_image_free(&atlas);
        return 0;
    }

    if (use_map) {
        sl_font_resolve_path(map_path, sizeof(map_path), manifest_path, map_value);
        if (!map_path[0] || !sl_font_load_map_file(map_path, glyph_count, &map_entries)) {
            sl_image_free(&atlas);
            return 0;
        }
    }

    sl_font_release(&g_font);
    g_font.kind = SL_FONT_KIND_ATLAS;
    g_font.atlas = atlas;
    g_font.first_char = first_char;
    g_font.glyph_count = glyph_count;
    g_font.glyph_cols = glyph_cols;
    g_font.glyph_w = glyph_w;
    g_font.glyph_h = glyph_h;
    g_font.render_w = render_w ? render_w : glyph_w;
    g_font.render_h = render_h ? render_h : glyph_h;
    g_font.render_off_x = render_off_x;
    g_font.render_off_y = render_off_y;
    g_font.advance_x = advance_x ? advance_x : g_font.render_w;
    g_font.line_h = line_h ? line_h : g_font.render_h;
    g_font.has_map = use_map;
    g_font.fallback_cp = fallback_cp;
    g_font.map_codepoints = map_entries;
    if (g_font.render_w == 0u) g_font.render_w = 1u;
    if (g_font.render_h == 0u) g_font.render_h = 1u;
    if (g_font.advance_x == 0u) g_font.advance_x = 1u;
    if (g_font.line_h == 0u) g_font.line_h = 1u;
    sl_font_init_lookup(&g_font);
    return 1;
}

uint32_t sl_font_advance_x(void) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        return (uint32_t)sl_font_vector_codepoint_advance(&g_font.vector, (uint32_t)' ', 0u);
    }
    return g_font.advance_x ? g_font.advance_x : 8u;
}

uint32_t sl_font_line_height(void) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        return sl_font_vector_line_height_px(&g_font.vector, 0u);
    }
    return g_font.line_h ? g_font.line_h : 8u;
}

static uint32_t sl_font_scale_for_px(uint32_t font_px) {
    uint32_t base_h = sl_font_line_height();
    if (base_h == 0u) base_h = 8u;
    if (font_px == 0u) return 1u;
    uint32_t scale = (font_px + (base_h / 2u)) / base_h;
    if (scale == 0u) scale = 1u;
    return scale;
}

uint32_t sl_font_advance_x_px(uint32_t font_px) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        return (uint32_t)sl_font_vector_codepoint_advance(&g_font.vector, (uint32_t)' ', font_px);
    }
    uint64_t adv = (uint64_t)sl_font_advance_x() * (uint64_t)sl_font_scale_for_px(font_px);
    if (adv == 0ull) adv = 1ull;
    if (adv > 0xFFFFFFFFull) adv = 0xFFFFFFFFull;
    return (uint32_t)adv;
}

uint32_t sl_font_line_height_px(uint32_t font_px) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        return sl_font_vector_line_height_px(&g_font.vector, font_px);
    }
    uint64_t h = (uint64_t)sl_font_line_height() * (uint64_t)sl_font_scale_for_px(font_px);
    if (h == 0ull) h = 1ull;
    if (h > 0xFFFFFFFFull) h = 0xFFFFFFFFull;
    return (uint32_t)h;
}

static uint32_t sl_font_utf8_next(const char *s, uint32_t *adv_out) {
    const uint8_t *p = (const uint8_t *)s;
    uint32_t cp = SL_FONT_DEFAULT_FALLBACK_CP;
    uint32_t adv = 1u;

    if (!s || !s[0]) {
        if (adv_out) *adv_out = 0u;
        return 0u;
    }

    uint8_t b0 = p[0];
    if (b0 < 0x80u) {
        cp = b0;
        adv = 1u;
    } else if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (p[1] == '\0') {
            if (adv_out) *adv_out = 1u;
            return SL_FONT_DEFAULT_FALLBACK_CP;
        }
        uint8_t b1 = p[1];
        if ((b1 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(b0 & 0x1Fu) << 6u) | (uint32_t)(b1 & 0x3Fu);
            adv = 2u;
        }
    } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
        if (p[1] == '\0' || p[2] == '\0') {
            if (adv_out) *adv_out = 1u;
            return SL_FONT_DEFAULT_FALLBACK_CP;
        }
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
            uint32_t t = ((uint32_t)(b0 & 0x0Fu) << 12u) |
                         ((uint32_t)(b1 & 0x3Fu) << 6u) |
                         (uint32_t)(b2 & 0x3Fu);
            if (t >= 0x800u && !(t >= 0xD800u && t <= 0xDFFFu)) {
                cp = t;
                adv = 3u;
            }
        }
    } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (p[1] == '\0' || p[2] == '\0' || p[3] == '\0') {
            if (adv_out) *adv_out = 1u;
            return SL_FONT_DEFAULT_FALLBACK_CP;
        }
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        uint8_t b3 = p[3];
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
            uint32_t t = ((uint32_t)(b0 & 0x07u) << 18u) |
                         ((uint32_t)(b1 & 0x3Fu) << 12u) |
                         ((uint32_t)(b2 & 0x3Fu) << 6u) |
                         (uint32_t)(b3 & 0x3Fu);
            if (t >= 0x10000u && t <= 0x10FFFFu) {
                cp = t;
                adv = 4u;
            }
        }
    }

    if (adv_out) *adv_out = adv;
    return cp;
}

uint32_t sl_font_measure_text(const char *text) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        return sl_font_measure_text_px(text, 0u);
    }
    uint32_t adv_x = sl_font_advance_x();
    uint32_t cur = 0u;
    uint32_t max = 0u;
    if (!text) return 0u;

    for (size_t i = 0u; text[i]; ) {
        uint32_t step = 1u;
        uint32_t cp = sl_font_utf8_next(text + i, &step);
        if (step == 0u) break;
        i += step;

        if (cp == '\n') {
            if (cur > max) max = cur;
            cur = 0u;
            continue;
        }
        if (cp == '\r') continue;
        cur += adv_x;
    }
    if (cur > max) max = cur;
    return max;
}

uint32_t sl_font_measure_text_px(const char *text, uint32_t font_px) {
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        int32_t cur = 0;
        int32_t max = 0;
        uint32_t prev_cp = 0u;
        if (!text) return 0u;
        for (size_t i = 0u; text[i]; ) {
            uint32_t step = 1u;
            uint32_t cp = sl_font_utf8_next(text + i, &step);
            if (step == 0u) break;
            i += step;

            if (cp == '\n') {
                if (cur > max) max = cur;
                cur = 0;
                prev_cp = 0u;
                continue;
            }
            if (cp == '\r') continue;
            cur += sl_font_vector_kerning(&g_font.vector, prev_cp, cp, font_px);
            cur += sl_font_vector_codepoint_advance(&g_font.vector, cp, font_px);
            prev_cp = cp;
        }
        if (cur > max) max = cur;
        return (max < 0) ? 0u : (uint32_t)max;
    }
    uint32_t adv_x = sl_font_advance_x_px(font_px);
    uint32_t cur = 0u;
    uint32_t max = 0u;
    if (!text) return 0u;

    for (size_t i = 0u; text[i]; ) {
        uint32_t step = 1u;
        uint32_t cp = sl_font_utf8_next(text + i, &step);
        if (step == 0u) break;
        i += step;

        if (cp == '\n') {
            if (cur > max) max = cur;
            cur = 0u;
            continue;
        }
        if (cp == '\r') continue;
        cur += adv_x;
    }
    if (cur > max) max = cur;
    return max;
}

static void sl_font_draw_builtin_codepoint(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t cp,
                                           uint8_t r, uint8_t g, uint8_t b, uint32_t scale) {
    uint32_t draw_cp = cp;
    if (draw_cp < 32u || draw_cp > 127u) draw_cp = g_font.fallback_cp;
    if (draw_cp < 32u || draw_cp > 127u) draw_cp = (uint32_t)'?';

    if (!ctx || !ctx->frame) return;
    if (scale == 0u) scale = 1u;
    const uint8_t *glyph = peos_font8x8_glyph((char)(uint8_t)draw_cp);
    for (uint32_t gy = 0u; gy < 8u; gy++) {
        uint8_t bits = glyph[gy];
        if (!bits) continue;
        for (uint32_t gx = 0u; gx < 8u; gx++) {
            if ((bits & (uint8_t)(1u << gx)) != 0u) {
                uint32_t px = x + (gx * scale);
                uint32_t py = y + (gy * scale);
                for (uint32_t sy = 0u; sy < scale; sy++) {
                    for (uint32_t sx = 0u; sx < scale; sx++) {
                        sl_put_pixel(ctx, px + sx, py + sy, r, g, b);
                    }
                }
            }
        }
    }
}

static void sl_font_draw_atlas_codepoint(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t cp,
                                         uint8_t r, uint8_t g, uint8_t b, uint32_t scale) {
    if (!ctx || !ctx->frame || !g_font.atlas.data) return;
    if (g_font.glyph_cols == 0u || g_font.glyph_w == 0u || g_font.glyph_h == 0u) return;
    if (g_font.render_w == 0u || g_font.render_h == 0u) return;
    if (scale == 0u) scale = 1u;

    uint32_t idx = sl_font_lookup_index(&g_font, cp);
    uint32_t cell_x = (idx % g_font.glyph_cols) * g_font.glyph_w;
    uint32_t cell_y = (idx / g_font.glyph_cols) * g_font.glyph_h;
    if (cell_x + g_font.glyph_w > g_font.atlas.width || cell_y + g_font.glyph_h > g_font.atlas.height) return;

    for (uint32_t dy = 0u; dy < g_font.render_h; dy++) {
        uint32_t sy = (dy * g_font.glyph_h) / g_font.render_h;
        const uint8_t *srow = g_font.atlas.data + ((size_t)(cell_y + sy) * (size_t)g_font.atlas.stride) +
                              ((size_t)cell_x * 4u);
        for (uint32_t dx = 0u; dx < g_font.render_w; dx++) {
            uint32_t sx = (dx * g_font.glyph_w) / g_font.render_w;
            const uint8_t *p = srow + ((size_t)sx * 4u);
            uint8_t sa = p[3];
            if (sa == 0u) continue;

            // Use source luminance as glyph coverage so white-on-transparent atlases tint correctly.
            uint32_t lum = ((uint32_t)p[0] * 77u + (uint32_t)p[1] * 150u + (uint32_t)p[2] * 29u) >> 8u;
            if (lum == 0u) continue;
            uint8_t a = (uint8_t)(((uint32_t)sa * lum + 127u) / 255u);
            int32_t base_x = (int32_t)x + (int32_t)(dx * scale) + (g_font.render_off_x * (int32_t)scale);
            int32_t base_y = (int32_t)y + (int32_t)(dy * scale) + (g_font.render_off_y * (int32_t)scale);
            if (base_x < 0 || base_y < 0) continue;
            uint32_t out_x = (uint32_t)base_x;
            uint32_t out_y = (uint32_t)base_y;
            if (a == 255u) {
                for (uint32_t sy = 0u; sy < scale; sy++) {
                    for (uint32_t sx = 0u; sx < scale; sx++) {
                        sl_put_pixel(ctx, out_x + sx, out_y + sy, r, g, b);
                    }
                }
            } else {
                sl_fill_rect_a(ctx, out_x, out_y, scale, scale, r, g, b, a);
            }
        }
    }
}

static void sl_font_draw_vector_codepoint(sl_ctx_t *ctx, uint32_t pen_x, int32_t baseline_y, uint32_t cp,
                                          uint8_t r, uint8_t g, uint8_t b, uint32_t font_px) {
    if (!ctx || !ctx->frame) return;
    sl_font_cached_glyph_t *glyph = sl_font_vector_get_glyph(&g_font.vector, cp, font_px);
    if (!glyph || !glyph->coverage || glyph->width == 0u || glyph->height == 0u) return;

    int32_t base_x = (int32_t)pen_x + glyph->bearing_x;
    int32_t base_y = baseline_y + glyph->bearing_y;
    for (uint32_t yy = 0u; yy < glyph->height; yy++) {
        int32_t out_y = base_y + (int32_t)yy;
        if (out_y < 0) continue;
        for (uint32_t xx = 0u; xx < glyph->width; xx++) {
            uint8_t a = glyph->coverage[(size_t)yy * (size_t)glyph->width + (size_t)xx];
            if (a == 0u) continue;
            int32_t out_x = base_x + (int32_t)xx;
            if (out_x < 0) continue;
            if (a == 255u) sl_put_pixel(ctx, (uint32_t)out_x, (uint32_t)out_y, r, g, b);
            else sl_fill_rect_a(ctx, (uint32_t)out_x, (uint32_t)out_y, 1u, 1u, r, g, b, a);
        }
    }
}

void sl_font_draw_char(sl_ctx_t *ctx, uint32_t x, uint32_t y, char c,
                       uint8_t r, uint8_t g, uint8_t b) {
    uint32_t cp = (uint32_t)(uint8_t)c;
    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        sl_font_draw_vector_codepoint(ctx, x, (int32_t)y + sl_font_vector_ascent_px(&g_font.vector, 0u),
                                      cp, r, g, b, 0u);
        return;
    }
    if (g_font.kind == SL_FONT_KIND_ATLAS) {
        sl_font_draw_atlas_codepoint(ctx, x, y, cp, r, g, b, 1u);
        return;
    }
    sl_font_draw_builtin_codepoint(ctx, x, y, cp, r, g, b, 1u);
}

void sl_font_draw_text(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                       uint8_t r, uint8_t g, uint8_t b) {
    sl_font_draw_text_px(ctx, x, y, text, r, g, b, 0u);
}

void sl_font_draw_text_px(sl_ctx_t *ctx, uint32_t x, uint32_t y, const char *text,
                          uint8_t r, uint8_t g, uint8_t b, uint32_t font_px) {
    uint32_t pen_x = x;
    uint32_t pen_y = y;
    uint32_t scale = sl_font_scale_for_px(font_px);
    uint32_t adv_x = sl_font_advance_x_px(font_px);
    uint32_t line_h = sl_font_line_height_px(font_px);
    uint32_t prev_cp = 0u;
    if (!ctx || !ctx->frame || !text) return;
    if (adv_x == 0u) adv_x = 1u;
    if (line_h == 0u) line_h = 1u;

    sl_font_ensure_initialized();
    if (g_font.kind == SL_FONT_KIND_VECTOR) {
        int32_t baseline_y = (int32_t)y + sl_font_vector_ascent_px(&g_font.vector, font_px);
        for (size_t i = 0u; text[i]; ) {
            uint32_t step = 1u;
            uint32_t cp = sl_font_utf8_next(text + i, &step);
            if (step == 0u) break;
            i += step;

            if (cp == '\n') {
                pen_x = x;
                baseline_y += (int32_t)line_h;
                prev_cp = 0u;
                continue;
            }
            if (cp == '\r') continue;
            int32_t kern = sl_font_vector_kerning(&g_font.vector, prev_cp, cp, font_px);
            if (kern < 0 && (uint32_t)(-kern) > pen_x) pen_x = 0u;
            else pen_x = (uint32_t)((int32_t)pen_x + kern);
            sl_font_draw_vector_codepoint(ctx, pen_x, baseline_y, cp, r, g, b, font_px);
            pen_x = (uint32_t)((int32_t)pen_x + sl_font_vector_codepoint_advance(&g_font.vector, cp, font_px));
            prev_cp = cp;
        }
        return;
    }

    for (size_t i = 0u; text[i]; ) {
        uint32_t step = 1u;
        uint32_t cp = sl_font_utf8_next(text + i, &step);
        if (step == 0u) break;
        i += step;

        if (cp == '\n') {
            pen_x = x;
            pen_y += line_h;
            prev_cp = 0u;
            continue;
        }
        if (cp == '\r') continue;

        if (g_font.kind == SL_FONT_KIND_ATLAS) {
            sl_font_draw_atlas_codepoint(ctx, pen_x, pen_y, cp, r, g, b, scale);
        } else {
            sl_font_draw_builtin_codepoint(ctx, pen_x, pen_y, cp, r, g, b, scale);
        }
        pen_x += adv_x;
    }
}
