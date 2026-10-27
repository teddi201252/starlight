#include "starlight_internal.h"
#include "keyboard_layout.h"
#include "starlight_input.h"
#include "ui_sdk.h"
#include "render/font.h"
#include "render/pixels.h"
#include "perf_stats.h"
#include "damage.h"
#include "desktop.h"
#include "notify.h"
#include "sysmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SL_TARGET_FPS
#define SL_TARGET_FPS 144u
#endif

#if SL_TARGET_FPS == 0
#error "SL_TARGET_FPS must be > 0"
#endif

#define SL_FRAME_NS (1000000000ull / (uint64_t)(SL_TARGET_FPS))
#define SL_TICK_NS 1000000ull
#define SL_PACER_SPIN_NS 300000ull
#define SL_HW_CURSOR_SCALE_DEFAULT 100u
#define SL_ANIMATIONS_ENABLED_DEFAULT 1u
#define SL_ANIMATION_SPEED_DEFAULT 100u
#define SL_ANIMATION_SPEED_MIN 10u
#define SL_ANIMATION_SPEED_MAX 400u
#define SL_DOUBLE_CLICK_MS_DEFAULT 300u
#define SL_DOUBLE_CLICK_MS_MIN 80u
#define SL_DOUBLE_CLICK_MS_MAX 1000u
#define SL_KEY_REPEAT_DELAY_MS_DEFAULT 250u
#define SL_KEY_REPEAT_DELAY_MS_MIN 100u
#define SL_KEY_REPEAT_DELAY_MS_MAX 1000u
#define SL_KEY_REPEAT_RATE_HZ_DEFAULT 30u
#define SL_KEY_REPEAT_RATE_HZ_MIN 5u
#define SL_KEY_REPEAT_RATE_HZ_MAX 60u
#define SL_CLIPBOARD_MANAGER_HOLD_MS_DEFAULT 400u
#define SL_CLIPBOARD_MANAGER_HOLD_MS_MIN 100u
#define SL_CLIPBOARD_MANAGER_HOLD_MS_MAX 3000u
#define SL_CLIPBOARD_FEEDBACK_AUDIO_DEFAULT 1u
#define SL_CLIPBOARD_FEEDBACK_VISUAL_DEFAULT 1u
#define SL_CFG_FILE_MAX 4096u
#define SL_CFG_LINE_MAX 256u
#define SL_DIRTY_MAX_RECTS 256u
#define SL_PRESENT_MAX_RECTS 32u
#define SL_DIRTY_FULL_AREA_THRESHOLD_PCT 65u

typedef sl_damage_rect_t sl_rect_t;

static struct {
    sl_rect_t rects[SL_DIRTY_MAX_RECTS];
    size_t count;
    uint8_t full;
} g_dirty = {0};

static sl_window_t *g_drag_window = NULL;
static uint8_t g_drag_active = 0u;

static void sl_drag_stop(void) {
    if (g_drag_window) g_drag_window->dragging = 0u;
    g_drag_active = 0u;
    g_drag_window = NULL;
}
static int32_t g_drag_grab_offset_x = 0;
static int32_t g_drag_grab_offset_y = 0;

void sl_dirty_reset(void) {
    g_dirty.count = 0;
    g_dirty.full = 0u;
}

static int sl_rects_overlap_or_touch(const sl_rect_t *a, const sl_rect_t *b) {
    return sl_damage_overlap_or_touch(a, b);
}

static uint64_t sl_rect_area(const sl_rect_t *r) {
    return sl_damage_area(r);
}

int sl_dirty_intersects_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (g_dirty.full) return 1;
    if (g_dirty.count == 0u) return 0;

    sl_rect_t in = { x, y, w, h };
    for (size_t i = 0; i < g_dirty.count; i++) {
        if (sl_rects_overlap_or_touch(&g_dirty.rects[i], &in)) return 1;
    }
    return 0;
}

void sl_dirty_for_each_intersection(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    sl_dirty_rect_fn fn, void *userdata) {
    if (!fn || w == 0u || h == 0u) return;
    uint64_t x2 = (uint64_t)x + w;
    uint64_t y2 = (uint64_t)y + h;
    if (g_dirty.full) {
        fn(x, y, w, h, userdata);
        return;
    }
    for (size_t i = 0u; i < g_dirty.count; i++) {
        const sl_rect_t *r = &g_dirty.rects[i];
        uint32_t ix0 = x > r->x ? x : r->x;
        uint32_t iy0 = y > r->y ? y : r->y;
        uint64_t rx2 = (uint64_t)r->x + r->w;
        uint64_t ry2 = (uint64_t)r->y + r->h;
        uint64_t ix2 = x2 < rx2 ? x2 : rx2;
        uint64_t iy2 = y2 < ry2 ? y2 : ry2;
        if (ix2 > ix0 && iy2 > iy0) {
            fn(ix0, iy0, (uint32_t)(ix2 - ix0), (uint32_t)(iy2 - iy0), userdata);
        }
    }
}

static sl_rect_t sl_rect_union(const sl_rect_t *a, const sl_rect_t *b) {
    return sl_damage_union(a, b);
}

static size_t sl_dirty_build_present_rects(const sl_ctx_t *ctx,
                                           peos_fb_rect_t out[SL_PRESENT_MAX_RECTS],
                                           uint64_t *out_pixels, uint8_t *out_full) {
    if (!ctx || !out || !out_pixels || !out_full) return 0u;
    *out_pixels = 0u;
    *out_full = g_dirty.full;
    if (g_dirty.full) {
        out[0] = (peos_fb_rect_t){ 0u, 0u, ctx->fb.width, ctx->fb.height };
        *out_pixels = (uint64_t)ctx->fb.width * ctx->fb.height;
        return 1u;
    }
    if (g_dirty.count == 0u) return 0u;

    sl_rect_t work[SL_PRESENT_MAX_RECTS];
    size_t count = sl_damage_reduce(g_dirty.rects, g_dirty.count, work,
                                    SL_PRESENT_MAX_RECTS,
                                    ctx->fb.width, ctx->fb.height,
                                    SL_DIRTY_FULL_AREA_THRESHOLD_PCT, out_full);
    uint64_t pixels = 0u;
    for (size_t i = 0u; i < count; i++) pixels += sl_rect_area(&work[i]);
    for (size_t i = 0u; i < count; i++) {
        out[i] = (peos_fb_rect_t){ work[i].x, work[i].y, work[i].w, work[i].h };
    }
    *out_pixels = pixels;
    return count;
}

// Grow rects[i] over every other rect it overlaps or touches, so the list
// stays disjoint: what blends over the restored background (window shadows)
// must be drawn once per pixel.
static void sl_dirty_absorb(size_t i) {
    for (size_t j = 0; j < g_dirty.count;) {
        if (j != i && sl_rects_overlap_or_touch(&g_dirty.rects[i], &g_dirty.rects[j])) {
            g_dirty.rects[i] = sl_rect_union(&g_dirty.rects[i], &g_dirty.rects[j]);
            g_dirty.rects[j] = g_dirty.rects[g_dirty.count - 1u];
            g_dirty.count--;
            if (i == g_dirty.count) i = j;   // rects[i] was the one moved into j
            j = 0;                            // the union grew: look again
            continue;
        }
        j++;
    }
}

static int sl_dirty_coalesce_best_pair(void) {
    if (g_dirty.count < 2u) return 0;

    size_t best_i = 0u;
    size_t best_j = 1u;
    uint64_t best_penalty = UINT64_MAX;

    for (size_t i = 0; i + 1u < g_dirty.count; i++) {
        for (size_t j = i + 1u; j < g_dirty.count; j++) {
            sl_rect_t u = sl_rect_union(&g_dirty.rects[i], &g_dirty.rects[j]);
            uint64_t ua = sl_rect_area(&u);
            uint64_t a = sl_rect_area(&g_dirty.rects[i]);
            uint64_t b = sl_rect_area(&g_dirty.rects[j]);
            uint64_t ab = a + b;
            uint64_t penalty = (ua > ab) ? (ua - ab) : 0ull;
            if (penalty < best_penalty) {
                best_penalty = penalty;
                best_i = i;
                best_j = j;
            }
        }
    }

    g_dirty.rects[best_i] = sl_rect_union(&g_dirty.rects[best_i], &g_dirty.rects[best_j]);
    g_dirty.rects[best_j] = g_dirty.rects[g_dirty.count - 1u];
    g_dirty.count--;
    if (best_i == g_dirty.count) best_i = best_j;   // it was the last one, now moved
    sl_dirty_absorb(best_i);
    return 1;
}

// Bumped by every damage request, so the main loop can tell an idle frame.
static uint32_t g_dirty_requests;

uint32_t sl_dirty_request_count(void) {
    return g_dirty_requests;
}

void sl_dirty_mark_full(void) {
    g_dirty_requests++;
    g_dirty.full = 1u;
    g_dirty.count = 0;
}

void sl_dirty_add(const sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    g_dirty_requests++;
    if (!ctx || g_dirty.full) return;
    if (w == 0u || h == 0u) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    uint64_t x2 = (uint64_t)x + (uint64_t)w;
    uint64_t y2 = (uint64_t)y + (uint64_t)h;
    if (x2 > (uint64_t)ctx->fb.width) w = ctx->fb.width - x;
    if (y2 > (uint64_t)ctx->fb.height) h = ctx->fb.height - y;
    if (w == 0u || h == 0u) return;

    sl_rect_t in = {x, y, w, h};

    for (size_t i = 0; i < g_dirty.count; i++) {
        if (sl_rects_overlap_or_touch(&g_dirty.rects[i], &in)) {
            g_dirty.rects[i] = sl_rect_union(&g_dirty.rects[i], &in);
            sl_dirty_absorb(i);
            return;
        }
    }

    if (g_dirty.count >= SL_DIRTY_MAX_RECTS) {
        while (g_dirty.count >= SL_DIRTY_MAX_RECTS && g_dirty.count > 1u) {
            if (!sl_dirty_coalesce_best_pair()) break;
        }
        if (g_dirty.count >= SL_DIRTY_MAX_RECTS) {
            sl_dirty_mark_full();
            return;
        }
    }
    g_dirty.rects[g_dirty.count++] = in;
}

static void sl_copy_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = 0;
    while (n + 1 < cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int sl_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *sl_trim(char *s) {
    if (!s) return s;
    while (*s && sl_is_space(*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && sl_is_space(s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
    return s;
}

static int sl_parse_u32(const char *s, uint32_t *out) {
    if (!s || !out) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || (uint64_t)v > 0xFFFFFFFFull) return 0;
    *out = (uint32_t)v;
    return 1;
}

static int sl_hex_nibble(char c, uint8_t *out) {
    if (!out) return 0;
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (uint8_t)(10 + (c - 'a'));
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint8_t)(10 + (c - 'A'));
        return 1;
    }
    return 0;
}

static int sl_parse_hex_byte(const char *s, uint8_t *out) {
    if (!s || !out) return 0;
    uint8_t hi = 0u;
    uint8_t lo = 0u;
    if (!sl_hex_nibble(s[0], &hi) || !sl_hex_nibble(s[1], &lo)) return 0;
    *out = (uint8_t)((hi << 4u) | lo);
    return 1;
}

static int sl_parse_color_hex(const char *s, sl_ui_color_t *out) {
    if (!s || !out) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    if (strlen(s) != 6u && strlen(s) != 8u) return 0;
    uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
    if (!sl_parse_hex_byte(s, &r) ||
        !sl_parse_hex_byte(s + 2, &g) ||
        !sl_parse_hex_byte(s + 4, &b)) return 0;
    if (strlen(s) == 8u && !sl_parse_hex_byte(s + 6, &a)) return 0;
    out->r = r;
    out->g = g;
    out->b = b;
    out->a = a;
    return 1;
}

static int sl_parse_color_rgb_triplet(const char *s, sl_ui_color_t *out) {
    if (!s || !out) return 0;
    char tmp[48];
    sl_copy_text(tmp, sizeof(tmp), s);

    char *a = sl_trim(tmp);
    char *comma1 = strchr(a, ',');
    if (!comma1) return 0;
    *comma1 = '\0';
    char *b = sl_trim(comma1 + 1);
    char *comma2 = strchr(b, ',');
    if (!comma2) return 0;
    *comma2 = '\0';
    char *c = sl_trim(comma2 + 1);
    char *comma3 = strchr(c, ',');

    uint32_t rv = 0u, gv = 0u, bv = 0u, av = 255u;
    if (comma3) {
        *comma3 = '\0';
        if (!sl_parse_u32(sl_trim(comma3 + 1), &av)) return 0;
    }
    if (!sl_parse_u32(sl_trim(a), &rv) ||
        !sl_parse_u32(sl_trim(b), &gv) ||
        !sl_parse_u32(sl_trim(c), &bv)) return 0;
    if (rv > 255u || gv > 255u || bv > 255u || av > 255u) return 0;

    out->r = (uint8_t)rv;
    out->g = (uint8_t)gv;
    out->b = (uint8_t)bv;
    out->a = (uint8_t)av;
    return 1;
}

static int sl_parse_theme_color(const char *s, sl_ui_color_t *out) {
    if (!s || !out) return 0;
    if (strchr(s, ',')) return sl_parse_color_rgb_triplet(s, out);
    return sl_parse_color_hex(s, out);
}

static int sl_theme_token_from_key(const char *key, sl_ui_color_token_t *out) {
    if (!key || !out) return 0;
    if (strcmp(key, "bg_canvas") == 0) *out = SL_UI_COLOR_BG_CANVAS;
    else if (strcmp(key, "bg_panel") == 0) *out = SL_UI_COLOR_BG_PANEL;
    else if (strcmp(key, "bg_control") == 0) *out = SL_UI_COLOR_BG_CONTROL;
    else if (strcmp(key, "bg_control_hover") == 0) *out = SL_UI_COLOR_BG_CONTROL_HOVER;
    else if (strcmp(key, "bg_control_active") == 0) *out = SL_UI_COLOR_BG_CONTROL_ACTIVE;
    else if (strcmp(key, "border") == 0) *out = SL_UI_COLOR_BORDER;
    else if (strcmp(key, "border_focus") == 0) *out = SL_UI_COLOR_BORDER_FOCUS;
    else if (strcmp(key, "text_primary") == 0) *out = SL_UI_COLOR_TEXT_PRIMARY;
    else if (strcmp(key, "text_secondary") == 0) *out = SL_UI_COLOR_TEXT_SECONDARY;
    else if (strcmp(key, "text_disabled") == 0) *out = SL_UI_COLOR_TEXT_DISABLED;
    else if (strcmp(key, "accent") == 0) *out = SL_UI_COLOR_ACCENT;
    else if (strcmp(key, "accent_hover") == 0) *out = SL_UI_COLOR_ACCENT_HOVER;
    else if (strcmp(key, "accent_active") == 0) *out = SL_UI_COLOR_ACCENT_ACTIVE;
    else if (strcmp(key, "danger") == 0) *out = SL_UI_COLOR_DANGER;
    else if (strcmp(key, "success") == 0) *out = SL_UI_COLOR_SUCCESS;
    else if (strcmp(key, "warning") == 0) *out = SL_UI_COLOR_WARNING;
    else if (strcmp(key, "shadow_dark") == 0) *out = SL_UI_COLOR_SHADOW_DARK;
    else if (strcmp(key, "shadow_light") == 0) *out = SL_UI_COLOR_SHADOW_LIGHT;
    else if (strcmp(key, "neon") == 0) *out = SL_UI_COLOR_NEON;
    else return 0;
    return 1;
}

static void sl_ensure_config_dirs(void) {
    (void)mkdir("/system", 0755);
    (void)mkdir(SL_OS_CONFIG_DIR, 0755);
    (void)mkdir("/system/apps", 0755);
    (void)mkdir(SL_RESOURCE_ROOT, 0755);
    (void)mkdir(SL_RESOURCE_CONFIG_DIR, 0755);
    (void)mkdir(SL_RESOURCE_FONT_DIR, 0755);
    (void)mkdir(SL_RESOURCE_KEYMAP_DIR, 0755);
}

static char *sl_load_text_file(const char *path, size_t max_bytes) {
    if (!path || max_bytes == 0) return NULL;
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static void sl_write_default_display_config(void) {
    FILE *f = fopen(SL_OS_DISPLAY_CONFIG_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# PeOS global display config\n");
    (void)fprintf(f, "# 0 0 means host/default mode\n");
    (void)fprintf(f, "width=0\n");
    (void)fprintf(f, "height=0\n");
    (void)fclose(f);
}

static void sl_write_default_starlight_config(void) {
    FILE *f = fopen(SL_RESOURCE_CONFIG_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# Starlight desktop config\n");
    (void)fprintf(f, "background=%s\n", SL_RESOURCE_BACKGROUND_ALT);
    (void)fprintf(f, "cursor_theme=soft\n");
    (void)fprintf(f, "cursor=%s\n", SL_RESOURCE_CURSOR);
    (void)fprintf(f, "cursor_scale=100\n");
    (void)fprintf(f, "font=%s\n", SL_RESOURCE_FONT_DEFAULT);
    (void)fprintf(f, "animations_enabled=1\n");
    (void)fprintf(f, "animation_speed_pct=100\n");
    (void)fprintf(f, "double_click_ms=300\n");
    (void)fprintf(f, "keyboard_layout=us\n");
    (void)fprintf(f, "keyboard_repeat_delay_ms=250\n");
    (void)fprintf(f, "keyboard_repeat_rate_hz=30\n");
    (void)fprintf(f, "clipboard_manager_hold_ms=400\n");
    (void)fprintf(f, "clipboard_feedback_audio=1\n");
    (void)fprintf(f, "clipboard_feedback_visual=1\n");
    (void)fprintf(f, "graphics_perf=0\n");
    (void)fprintf(f, "show_performance=0\n");
    (void)fprintf(f, "timezone=Europe/Rome\n");
    (void)fclose(f);
}

static void sl_write_default_font_manifest(void) {
    FILE *f = fopen(SL_RESOURCE_FONT_DEFAULT, "w");
    if (!f) return;
    (void)fprintf(f, "# Starlight font manifest\n");
    (void)fprintf(f, "# backend: builtin | atlas\n");
    (void)fprintf(f, "backend=builtin\n");
    (void)fprintf(f, "# For backend=atlas:\n");
    (void)fprintf(f, "# image=/starlight/fonts/your_font.png\n");
    (void)fprintf(f, "# map=/starlight/fonts/your_font.map\n");
    (void)fprintf(f, "# first=32\n");
    (void)fprintf(f, "# count=96\n");
    (void)fprintf(f, "# cols=16\n");
    (void)fprintf(f, "# glyph_w=8\n");
    (void)fprintf(f, "# glyph_h=8\n");
    (void)fprintf(f, "# render_w=8\n");
    (void)fprintf(f, "# render_h=8\n");
    (void)fprintf(f, "# render_off_x=0\n");
    (void)fprintf(f, "# render_off_y=0\n");
    (void)fprintf(f, "# advance=8\n");
    (void)fprintf(f, "# line_h=8\n");
    (void)fprintf(f, "# fallback_char=63\n");
    (void)fclose(f);
}

static void sl_write_default_theme_config(void) {
    FILE *f = fopen(SL_RESOURCE_THEME_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# Starlight UI theme: Soft UI, day (design/starlight-soft-ui).\n");
    (void)fprintf(f, "# Accepted value formats: #RRGGBB, RRGGBB, or R,G,B\n");
    (void)fprintf(f, "bg_canvas=E3E6EC\n");
    (void)fprintf(f, "bg_panel=E3E6EC\n");
    (void)fprintf(f, "bg_control=E3E6EC\n");
    (void)fprintf(f, "bg_control_hover=EAEDF2\n");
    (void)fprintf(f, "bg_control_active=DADEE5\n");
    (void)fprintf(f, "border=C9CDD6\n");
    (void)fprintf(f, "border_focus=59D0FF\n");
    (void)fprintf(f, "text_primary=1A1D26\n");
    (void)fprintf(f, "text_secondary=5A606E\n");
    (void)fprintf(f, "text_disabled=9AA0AC\n");
    (void)fprintf(f, "accent=111626\n");
    (void)fprintf(f, "accent_hover=1C2440\n");
    (void)fprintf(f, "accent_active=0A1126\n");
    (void)fprintf(f, "danger=D04A55\n");
    (void)fprintf(f, "success=2FA87A\n");
    (void)fprintf(f, "warning=D99A2B\n");
    (void)fprintf(f, "shadow_dark=BCC1CB\n");
    (void)fprintf(f, "shadow_light=FFFFFF\n");
    (void)fprintf(f, "neon=59D0FF\n");
    (void)fclose(f);
}

static void sl_load_display_config(sl_ctx_t *ctx) {
    if (!ctx) return;
    ctx->display_width = 0u;
    ctx->display_height = 0u;

    char *cfg = sl_load_text_file(SL_OS_DISPLAY_CONFIG_FILE, SL_CFG_FILE_MAX);
    if (!cfg) {
        sl_write_default_display_config();
        return;
    }

    char line[SL_CFG_LINE_MAX];
    size_t li = 0;
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0;

        char *p = sl_trim(line);
        if (p[0] != '\0' && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = sl_trim(p);
                char *val = sl_trim(eq + 1);
                uint32_t n = 0;
                if (strcmp(key, "width") == 0 && sl_parse_u32(val, &n)) {
                    ctx->display_width = n;
                } else if (strcmp(key, "height") == 0 && sl_parse_u32(val, &n)) {
                    ctx->display_height = n;
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);
}

static void sl_load_starlight_config(sl_ctx_t *ctx) {
    if (!ctx) return;
    sl_copy_text(ctx->background_path, sizeof(ctx->background_path), SL_RESOURCE_BACKGROUND_ALT);
    sl_copy_text(ctx->cursor_path, sizeof(ctx->cursor_path), SL_RESOURCE_CURSOR);
    sl_copy_text(ctx->font_manifest_path, sizeof(ctx->font_manifest_path), SL_RESOURCE_FONT_DEFAULT);
    sl_copy_text(ctx->keyboard_layout_path, sizeof(ctx->keyboard_layout_path), "us");
    ctx->hw_cursor_scale_pct = SL_HW_CURSOR_SCALE_DEFAULT;
    ctx->cursor_theme = 1u;   // the Soft UI set, unless cursor_theme=image
    ctx->animations_enabled = SL_ANIMATIONS_ENABLED_DEFAULT;
    ctx->animation_speed_pct = SL_ANIMATION_SPEED_DEFAULT;
    ctx->double_click_ms = SL_DOUBLE_CLICK_MS_DEFAULT;
    ctx->keyboard_repeat_delay_ms = SL_KEY_REPEAT_DELAY_MS_DEFAULT;
    ctx->keyboard_repeat_rate_hz = SL_KEY_REPEAT_RATE_HZ_DEFAULT;
    ctx->clipboard_manager_hold_ms = SL_CLIPBOARD_MANAGER_HOLD_MS_DEFAULT;
    ctx->clipboard_feedback_audio_enabled = SL_CLIPBOARD_FEEDBACK_AUDIO_DEFAULT;
    ctx->clipboard_feedback_visual_enabled = SL_CLIPBOARD_FEEDBACK_VISUAL_DEFAULT;
    ctx->graphics_perf_enabled = 0u;
    ctx->show_perf = 0u;
    ctx->timezone_eu_central = 1u;

    char *cfg = sl_load_text_file(SL_RESOURCE_CONFIG_FILE, SL_CFG_FILE_MAX);
    if (!cfg) {
        sl_write_default_starlight_config();
        sl_write_default_font_manifest();
        return;
    }

    char line[SL_CFG_LINE_MAX];
    size_t li = 0;
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0;

        char *p = sl_trim(line);
        if (p[0] != '\0' && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = sl_trim(p);
                char *val = sl_trim(eq + 1);
                if (strcmp(key, "background") == 0 && val[0]) {
                    sl_copy_text(ctx->background_path, sizeof(ctx->background_path), val);
                } else if (strcmp(key, "cursor") == 0 && val[0]) {
                    sl_copy_text(ctx->cursor_path, sizeof(ctx->cursor_path), val);
                } else if (strcmp(key, "cursor_theme") == 0) {
                    ctx->cursor_theme = strcmp(val, "image") == 0 ? 0u : 1u;
                } else if (strcmp(key, "font") == 0 && val[0]) {
                    sl_copy_text(ctx->font_manifest_path, sizeof(ctx->font_manifest_path), val);
                } else if (strcmp(key, "cursor_scale") == 0) {
                    uint32_t n = 0;
                    // Backward-compatible: accept legacy tiny scales (eg. "2")
                    // in addition to the newer UI range.
                    if (sl_parse_u32(val, &n) && n >= 1u && n <= 400u) {
                        ctx->hw_cursor_scale_pct = n;
                    }
                } else if (strcmp(key, "animations_enabled") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        ctx->animations_enabled = (n != 0u) ? 1u : 0u;
                    }
                } else if (strcmp(key, "animation_speed_pct") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        if (n < SL_ANIMATION_SPEED_MIN) n = SL_ANIMATION_SPEED_MIN;
                        if (n > SL_ANIMATION_SPEED_MAX) n = SL_ANIMATION_SPEED_MAX;
                        ctx->animation_speed_pct = n;
                    }
                } else if (strcmp(key, "double_click_ms") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        if (n < SL_DOUBLE_CLICK_MS_MIN) n = SL_DOUBLE_CLICK_MS_MIN;
                        if (n > SL_DOUBLE_CLICK_MS_MAX) n = SL_DOUBLE_CLICK_MS_MAX;
                        ctx->double_click_ms = n;
                    }
                } else if (strcmp(key, "keyboard_layout") == 0 && val[0]) {
                    sl_copy_text(ctx->keyboard_layout_path, sizeof(ctx->keyboard_layout_path), val);
                } else if (strcmp(key, "keyboard_repeat_delay_ms") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        if (n < SL_KEY_REPEAT_DELAY_MS_MIN) n = SL_KEY_REPEAT_DELAY_MS_MIN;
                        if (n > SL_KEY_REPEAT_DELAY_MS_MAX) n = SL_KEY_REPEAT_DELAY_MS_MAX;
                        ctx->keyboard_repeat_delay_ms = n;
                    }
                } else if (strcmp(key, "keyboard_repeat_rate_hz") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        if (n < SL_KEY_REPEAT_RATE_HZ_MIN) n = SL_KEY_REPEAT_RATE_HZ_MIN;
                        if (n > SL_KEY_REPEAT_RATE_HZ_MAX) n = SL_KEY_REPEAT_RATE_HZ_MAX;
                        ctx->keyboard_repeat_rate_hz = n;
                    }
                } else if (strcmp(key, "clipboard_manager_hold_ms") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        if (n < SL_CLIPBOARD_MANAGER_HOLD_MS_MIN) n = SL_CLIPBOARD_MANAGER_HOLD_MS_MIN;
                        if (n > SL_CLIPBOARD_MANAGER_HOLD_MS_MAX) n = SL_CLIPBOARD_MANAGER_HOLD_MS_MAX;
                        ctx->clipboard_manager_hold_ms = n;
                    }
                } else if (strcmp(key, "clipboard_feedback_audio") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        ctx->clipboard_feedback_audio_enabled = n ? 1u : 0u;
                    }
                } else if (strcmp(key, "clipboard_feedback_visual") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) {
                        ctx->clipboard_feedback_visual_enabled = n ? 1u : 0u;
                    }
                } else if (strcmp(key, "timezone") == 0) {
                    ctx->timezone_eu_central = (strcmp(val, "UTC") != 0) ? 1u : 0u;
                } else if (strcmp(key, "show_performance") == 0) {
                    uint32_t n = 0u;
                    if (sl_parse_u32(val, &n)) ctx->show_perf = n ? 1u : 0u;
                } else if (strcmp(key, "graphics_perf") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n)) ctx->graphics_perf_enabled = n ? 1u : 0u;
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);
}

static void sl_load_theme_config(void) {
    sl_ui_theme_t theme;
    sl_ui_theme_set_default(&theme);

    char *cfg = sl_load_text_file(SL_RESOURCE_THEME_FILE, SL_CFG_FILE_MAX);
    // mode=night starts from the night palette; the colour keys then
    // override single tokens in either mode.
    if (cfg) {
        const char *m = strstr(cfg, "mode=");
        if (m && (m == cfg || m[-1] == '\n') && strncmp(m + 5, "night", 5) == 0) sl_ui_theme_set_night(&theme);
    }
    if (!cfg) {
        sl_write_default_theme_config();
        sl_ui_theme_runtime_set(&theme);
        return;
    }

    char line[SL_CFG_LINE_MAX];
    size_t li = 0;
    for (size_t i = 0;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0;

        char *p = sl_trim(line);
        if (p[0] != '\0' && p[0] != '#' && p[0] != ';') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *key = sl_trim(p);
                char *val = sl_trim(eq + 1);
                sl_ui_color_token_t token;
                sl_ui_color_t color;
                if (sl_theme_token_from_key(key, &token) && sl_parse_theme_color(val, &color)) {
                    theme.colors[token] = color;
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);

    sl_ui_theme_runtime_set(&theme);
}

static void sl_apply_animation_config(const sl_ctx_t *ctx) {
    uint32_t speed = 100u;
    uint32_t duration_scale_pct = 100u;
    if (!ctx) return;

    sl_tween_set_enabled(ctx->animations_enabled ? 1u : 0u);
    speed = ctx->animation_speed_pct;
    if (speed < SL_ANIMATION_SPEED_MIN) speed = SL_ANIMATION_SPEED_MIN;
    if (speed > SL_ANIMATION_SPEED_MAX) speed = SL_ANIMATION_SPEED_MAX;

    /* Intuitive mapping: lower speed => slower animations (longer duration). */
    duration_scale_pct = (10000u + (speed / 2u)) / speed; /* speed=100 -> 100% */
    if (duration_scale_pct < 10u) duration_scale_pct = 10u;
    if (duration_scale_pct > 2000u) duration_scale_pct = 2000u;
    sl_tween_set_duration_scale_pct(duration_scale_pct);
}

static void sl_apply_keyboard_config(const sl_ctx_t *ctx) {
    uint32_t delay_ms = SL_KEY_REPEAT_DELAY_MS_DEFAULT;
    uint32_t rate_hz = SL_KEY_REPEAT_RATE_HZ_DEFAULT;
    uint32_t hold_ms = SL_CLIPBOARD_MANAGER_HOLD_MS_DEFAULT;
    if (!ctx) return;
    delay_ms = ctx->keyboard_repeat_delay_ms;
    rate_hz = ctx->keyboard_repeat_rate_hz;
    hold_ms = ctx->clipboard_manager_hold_ms;
    if (delay_ms < SL_KEY_REPEAT_DELAY_MS_MIN) delay_ms = SL_KEY_REPEAT_DELAY_MS_MIN;
    if (delay_ms > SL_KEY_REPEAT_DELAY_MS_MAX) delay_ms = SL_KEY_REPEAT_DELAY_MS_MAX;
    if (rate_hz < SL_KEY_REPEAT_RATE_HZ_MIN) rate_hz = SL_KEY_REPEAT_RATE_HZ_MIN;
    if (rate_hz > SL_KEY_REPEAT_RATE_HZ_MAX) rate_hz = SL_KEY_REPEAT_RATE_HZ_MAX;
    if (hold_ms < SL_CLIPBOARD_MANAGER_HOLD_MS_MIN) hold_ms = SL_CLIPBOARD_MANAGER_HOLD_MS_MIN;
    if (hold_ms > SL_CLIPBOARD_MANAGER_HOLD_MS_MAX) hold_ms = SL_CLIPBOARD_MANAGER_HOLD_MS_MAX;
    (void)sl_keyboard_layout_load(ctx->keyboard_layout_path);
    sl_input_set_key_repeat_config(delay_ms, rate_hz);
    sl_input_set_actionmod_hold_config(hold_ms);
}

static void sl_apply_clipboard_feedback_config(const sl_ctx_t *ctx) {
    if (!ctx) return;
    sl_clipboard_set_feedback_config(ctx->clipboard_feedback_audio_enabled,
                                     ctx->clipboard_feedback_visual_enabled);
}

char *sl_u32_to_str(uint32_t value, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return NULL;

    if (value == 0) {
        if (out_cap < 2) return NULL;
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    char tmp[10];
    size_t n = 0;
    while (value != 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    if (n + 1 > out_cap) return NULL;

    for (size_t i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    return out;
}

static int sl_update_fb_info(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    if ((int64_t)peos_fbinfo(&ctx->fb) < 0) return 0;
    if (ctx->fb.bpp != 24 && ctx->fb.bpp != 32) return 0;

    ctx->bytespp = (uint8_t)(ctx->fb.bpp / 8u);
    ctx->row_stride = (size_t)ctx->fb.width * (size_t)ctx->bytespp;
    ctx->frame_bytes = (size_t)ctx->fb.width * (size_t)ctx->fb.height * (size_t)ctx->bytespp;
    ctx->map_bytes = (size_t)align_up((uint64_t)ctx->frame_bytes, 4096u);
    ctx->fb_front = NULL;
    ctx->fb_front_stride = 0;
    ctx->fb_front_bytes = 0;

    peos_fb_map_t fbm;
    size_t fb_row_stride = ctx->fb.pitch ? (size_t)ctx->fb.pitch : ctx->row_stride;
    size_t fb_needed = fb_row_stride * (size_t)ctx->fb.height;
    if (peos_fbmap(&fbm, PEOS_FBMAP_EXCLUSIVE) == 0 &&
        fbm.addr != 0 &&
        fbm.len >= fb_needed) {
        ctx->fb_front = (uint8_t *)(uintptr_t)fbm.addr;
        ctx->fb_front_stride = fb_row_stride;
        ctx->fb_front_bytes = (size_t)fbm.len;
        ctx->fb_direct = 1;
    }

    uint64_t map = peos_mmap(0, ctx->map_bytes,
                             PEOS_PROT_READ | PEOS_PROT_WRITE,
                             PEOS_MAP_ANON | PEOS_MAP_PRIVATE);
    if ((int64_t)map < 0) return 0;
    ctx->frame = (uint8_t *)(uintptr_t)map;

    return 1;
}

static void sl_load_background(sl_ctx_t *ctx, const char *path) {
    int ok = 0;
    if (path && path[0]) {
        ok = sl_decode_image_file(path, &ctx->bg_image);
    }
    if (!ok) {
        ok = sl_decode_image_file(SL_RESOURCE_BACKGROUND_ALT, &ctx->bg_image);
    }
    if (!ok) {
        ok = sl_decode_image_file(SL_RESOURCE_BACKGROUND, &ctx->bg_image);
    }
    ctx->bg_loaded = ok;
}

int sl_init(sl_ctx_t *ctx) {
    if (!ctx) return 0;
    peos_memset(ctx, 0, sizeof(*ctx));
    ctx->input_fd = -1;

    sl_ensure_config_dirs();
    sl_load_display_config(ctx);
    sl_load_starlight_config(ctx);
    sl_apply_animation_config(ctx);
    sl_apply_keyboard_config(ctx);
    sl_apply_clipboard_feedback_config(ctx);
    sl_load_theme_config();
    sl_font_reset_default();
    if (ctx->font_manifest_path[0]) {
        (void)sl_font_load_manifest_default(ctx->font_manifest_path);
    }

    if ((ctx->display_width == 0u && ctx->display_height == 0u) ||
        (ctx->display_width > 0u && ctx->display_height > 0u)) {
        (void)peos_fb_set_mode(ctx->display_width, ctx->display_height);
    }

    if (!sl_update_fb_info(ctx)) return 0;

    // Input arrives on an endpoint Starlight can wait on; older kernels keep raw polling.
    ctx->input_fd = peos_input_open();
    if (ctx->input_fd < 0 && (int64_t)peos_input_mode(PEOS_INPUT_MODE_RAW) == 0) {
        ctx->input_raw = 1;
    }
    sl_input_state_reset();

    ctx->cursor_x = ctx->fb.width / 2u;
    ctx->cursor_y = ctx->fb.height / 2u;
    sl_cursor_init(ctx);

    ctx->fps_last_ns = peos_time_ns();
    ctx->fps_frames = 0;
    ctx->fps_value = 0;

    sl_load_background(ctx, ctx->background_path);

    return 1;
}

void sl_shutdown(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->hw_cursor) (void)peos_cursor_set_visible(0);
    if (ctx->input_raw) (void)peos_input_mode(PEOS_INPUT_MODE_COOKED);
    if (ctx->input_fd >= 0) {
        (void)peos_fclose((uint64_t)ctx->input_fd);
        ctx->input_fd = -1;
    }
    if (ctx->frame) (void)peos_munmap((uint64_t)(uintptr_t)ctx->frame, ctx->map_bytes);
    sl_font_shutdown();
}

void sl_reload_display(sl_ctx_t *ctx) {
    if (!ctx) return;

    sl_load_starlight_config(ctx);
    sl_apply_animation_config(ctx);
    sl_apply_keyboard_config(ctx);
    sl_apply_clipboard_feedback_config(ctx);
    sl_load_theme_config();
    sl_font_reset_default();
    if (ctx->font_manifest_path[0]) {
        (void)sl_font_load_manifest_default(ctx->font_manifest_path);
    }

    if (ctx->bg_loaded) {
        sl_image_free(&ctx->bg_image);
        ctx->bg_loaded = 0;
    }
    free(ctx->bg_frame);   // baked again from the new image
    ctx->bg_frame = NULL;
    sl_load_background(ctx, ctx->background_path);

    sl_cursor_init(ctx);
    if (ctx->hw_cursor) sl_hw_cursor_sync_pos(ctx);

    sl_dirty_mark_full();
}

void sl_reload_theme(sl_ctx_t *ctx) {
    sl_load_theme_config();
    // Everything drawn from the theme comes again: window caches, the
    // desktop layer (its neon), the dock and frames (they look up their
    // sprites again when the theme version changes).
    sl_windows_theme_changed(ctx);
    sl_desk_build(ctx);
    sl_dirty_mark_full();
}

// The cursor for what is under the mouse, from the top: a window being
// moved, the notification centre, the dock and app manager, the windows (and
// inside an app, its control), the desktop's widgets. While an app is
// starting, the arrow and the hand are the waiting one.
void sl_cursor_update(sl_ctx_t *ctx) {
    static uint32_t last_x = 0xFFFFFFFFu, last_y;
    static uint32_t frames;
    if (!ctx) return;
    uint32_t x = ctx->cursor_x, y = ctx->cursor_y;
    // Worked out again when the mouse moves, and every few frames (an app
    // starts, a control under a still mouse changes).
    if (x == last_x && y == last_y && (++frames % 8u) != 0u) return;
    last_x = x;
    last_y = y;
    int k = -1;
    if (g_drag_active) k = SL_CUR_MOVE;
    if (k < 0) k = sl_sysmenu_cursor(ctx, x, y);
    if (k < 0) k = sl_notify_cursor(ctx, x, y);
    if (k < 0) k = sl_launcher_cursor(ctx, x, y);
    if (k < 0) k = sl_windows_cursor(ctx, x, y);
    if (k < 0) k = sl_desk_cursor(x, y);
    if (k < 0) k = SL_CUR_ARROW;
    // Over the tile just clicked too: the hand would say "click again".
    if ((k == SL_CUR_ARROW || k == SL_CUR_HAND) && sl_windows_launching()) k = SL_CUR_WAIT;
    sl_cursor_set(ctx, (sl_cursor_kind_t)k);
}

int sl_handle_input(sl_ctx_t *ctx) {
    sl_input_frame_t input;
    uint8_t clipboard_changed = 0u;
    uint8_t clipboard_full_redraw = 0u;
    if (sl_input_poll(ctx, &input) < 0) return 1;
    if (sl_clipboard_handle_input(ctx, &input, &clipboard_changed, &clipboard_full_redraw)) {
        if (clipboard_changed) {
            if (clipboard_full_redraw) {
                sl_dirty_mark_full();
            } else {
                uint32_t x = 0u, y = 0u, w = 0u, h = 0u;
                sl_clipboard_overlay_bounds(ctx, &x, &y, &w, &h);
                sl_dirty_add(ctx, x, y, w, h);
            }
        }
        return input.quit_requested ? 1 : 0;
    }

    // A window being resized by an edge owns the mouse until it is let go.
    if (sl_windows_resizing()) {
        if (input.mouse_moved) sl_windows_resize_move(ctx, ctx->cursor_x, ctx->cursor_y);
        if (input.mouse_left_released) sl_windows_resize_release(ctx);
        return input.quit_requested ? 1 : 0;
    }

    // A widget held down owns the mouse until it is let go (the volume
    // follows a drag).
    if (sl_desk_held()) {
        if (input.mouse_moved) sl_desk_drag(ctx, ctx->cursor_x, ctx->cursor_y);
        if (input.mouse_left_released) sl_desk_release(ctx, ctx->cursor_x, ctx->cursor_y);
        return input.quit_requested ? 1 : 0;
    }

    if (input.mouse_moved) {
        sl_launcher_handle_context_hover(ctx);
        sl_launcher_handle_hover(ctx);
    }

    uint32_t reserved_left = sl_launcher_reserved_left(ctx);
    uint8_t mouse_on_launcher = ((reserved_left > 0u && ctx->cursor_x < reserved_left) ||
                                 sl_launcher_covers_point(ctx, ctx->cursor_x, ctx->cursor_y)) ? 1u : 0u;
    // The desktop's widgets are under the mouse when nothing else is.
    uint8_t on_desktop = (!mouse_on_launcher && !sl_windows_any_at(ctx->cursor_x, ctx->cursor_y)) ? 1u : 0u;
    if (input.mouse_moved) sl_desk_hover(ctx, ctx->cursor_x, ctx->cursor_y, (uint8_t)!on_desktop);
    if (on_desktop && input.mouse_wheel_y != 0 &&
        sl_desk_wheel(ctx, ctx->cursor_x, ctx->cursor_y, input.mouse_wheel_y)) {
        input.mouse_wheel_y = 0;
    }
    if (input.mouse_left_released) sl_launcher_handle_release(ctx);
    // A title bar button held down owns the mouse until it is let go.
    if (input.mouse_left_released && sl_windows_title_button_release(ctx, ctx->cursor_x, ctx->cursor_y)) return 0;
    if (input.mouse_moved && (input.mouse_buttons & 0x01u) &&
        sl_windows_title_button_held(ctx, ctx->cursor_x, ctx->cursor_y)) return 0;
    uint8_t clipboard_cmds_pending = (input.clipboard_cmds != 0u) ? 1u : 0u;

    sl_window_t *focused = sl_window_get_focused();
    // While a window is dragged by its title bar the moves are the drag's,
    // not the app's: forwarding them would make it redraw every frame.
    uint8_t drag_move_only = g_drag_active && !input.mouse_left_pressed && !input.mouse_left_released;
    if (focused && focused->task && focused->task->on_input &&
        !mouse_on_launcher && !drag_move_only &&
        (input.mouse_updated || input.mouse_left_pressed || input.mouse_left_released ||
         input.mouse_moved || clipboard_cmds_pending)) {
        focused->task->on_input(ctx, &input, NULL, focused->task->userdata);
        sl_windows_mark_focused_activity();
        if (input.mouse_left_pressed || input.mouse_left_released ||
            input.mouse_right_pressed || input.mouse_right_released) {
            sl_window_dirty_body(ctx, focused);
        } else {
            uint32_t dx = 0u, dy = 0u, dw = 0u, dh = 0u;
            if (sl_app_host_input_dirty_rect(focused, &input, focused->task->userdata, &dx, &dy, &dw, &dh)) {
                sl_dirty_add(ctx, dx, dy, dw, dh);
            } else if (clipboard_cmds_pending) {
                sl_window_dirty_body(ctx, focused);
            } else if (input.mouse_wheel_x != 0 || input.mouse_wheel_y != 0 ||
                       (input.mouse_moved && (input.mouse_buttons & 0x01u))) {
                sl_window_dirty_body(ctx, focused);
            }
        }
    }

    for (uint8_t i = 0u; i < input.key_event_count; i++) {   // Esc closes the notification centre
        if (sl_sysmenu_handle_key(ctx, &input.key_events[i]) || sl_notify_handle_key(ctx, &input.key_events[i])) {
            input.key_events[i] = input.key_events[--input.key_event_count];
            i--;
        }
    }

    if (input.key_event_count && sl_launcher_manager_open()) {   // the app manager's search takes the keys
        for (uint8_t i = 0u; i < input.key_event_count; i++) sl_launcher_handle_key(ctx, &input.key_events[i], input.caps_lock_on);
        input.key_event_count = 0u;
    }

    if (focused && focused->task && focused->task->on_input && input.key_event_count) {
        for (uint8_t i = 0u; i < input.key_event_count; i++) {
            focused->task->on_input(ctx, &input, &input.key_events[i], focused->task->userdata);
        }
        sl_windows_mark_focused_activity();
        sl_window_dirty_body(ctx, focused);   // typing changes the app, not the frame
    }

    if (input.quit_requested) return 1;

    if (input.mouse_right_pressed) {
        if (sl_launcher_handle_right_click(ctx, ctx->cursor_x, ctx->cursor_y)) {
            sl_drag_stop();
            return 0;
        }
    }

    if (input.mouse_left_pressed) {
        // A click can move the focus: then both frames change (raised or
        // flat). Windows are reordered on focus, so remember the task.
        const app_instance_t *prev_task = focused ? focused->task : NULL;
        sl_win_rect_t prev_outer = focused ? sl_window_outer_rect(focused) : (sl_win_rect_t){ 0, 0, 0u, 0u };

        if (sl_sysmenu_handle_click(ctx, ctx->cursor_x, ctx->cursor_y) ||
            sl_notify_handle_click(ctx, ctx->cursor_x, ctx->cursor_y)) {
            sl_drag_stop();
            return 0;
        }

        if (sl_launcher_handle_click(ctx, ctx->cursor_x, ctx->cursor_y)) {
            sl_drag_stop();
            return 0;
        }

        if (sl_windows_close_at_cursor(ctx, ctx->cursor_x, ctx->cursor_y)) {
            sl_drag_stop();
            return 0;
        }

        if (sl_windows_resize_press(ctx, ctx->cursor_x, ctx->cursor_y)) {   // an edge or a corner
            sl_drag_stop();
            return 0;
        }

        if (on_desktop && sl_desk_press(ctx, ctx->cursor_x, ctx->cursor_y)) {
            sl_drag_stop();
            return 0;
        }

        sl_windows_focus_at_cursor(ctx->cursor_x, ctx->cursor_y);
        focused = sl_window_get_focused();
        if (focused && focused->task && focused->task->on_input) {
            focused->task->on_input(ctx, &input, NULL, focused->task->userdata);
        }
        sl_windows_mark_focused_activity();
        if (focused && focused->task != prev_task) {
            int64_t px = prev_outer.x < 0 ? 0 : prev_outer.x, py = prev_outer.y < 0 ? 0 : prev_outer.y;
            int64_t pw = (int64_t)prev_outer.x + prev_outer.w - px, ph = (int64_t)prev_outer.y + prev_outer.h - py;
            if (pw > 0 && ph > 0) sl_dirty_add(ctx, (uint32_t)px, (uint32_t)py, (uint32_t)pw, (uint32_t)ph);
            sl_window_dirty_all(ctx, focused);
        }

        sl_drag_stop();
        if (focused) {
            uint32_t top_bar_h = focused->top_bar_height;
            if (top_bar_h > focused->height) top_bar_h = focused->height;

            uint8_t inside_x = (ctx->cursor_x >= focused->x) &&
                               (ctx->cursor_x < (focused->x + focused->width));
            uint8_t inside_topbar = (ctx->cursor_y >= focused->y) &&
                                    (ctx->cursor_y < (focused->y + top_bar_h));
            // A double click on the title bar maximizes (or goes back).
            static uint64_t last_title_ns;
            static const void *last_title_task;
            uint64_t now = peos_time_ns();
            if (inside_x && inside_topbar && last_title_task == focused->task &&
                now - last_title_ns <= (uint64_t)ctx->double_click_ms * 1000000ull) {
                last_title_task = NULL;
                sl_windows_toggle_maximize(ctx);
                return 0;
            }
            if (inside_x && inside_topbar) {
                last_title_ns = now;
                last_title_task = focused->task;
            }
            if (inside_x && inside_topbar) {
                g_drag_active = 1u;
                g_drag_window = focused;
                focused->dragging = 1u;
                g_drag_grab_offset_x = (int32_t)(ctx->cursor_x - focused->x);
                g_drag_grab_offset_y = (int32_t)(ctx->cursor_y - focused->y);
            }
        }
    }

    if (input.mouse_left_released) {
        if (g_drag_active && g_drag_window) sl_windows_remember(g_drag_window);   // opens there next time
        sl_drag_stop();
    }

    if (input.mouse_moved && (input.mouse_buttons & 0x01u) && g_drag_active && g_drag_window) {
        uint32_t old_x = g_drag_window->x;
        uint32_t old_y = g_drag_window->y;

        int64_t new_x = (int64_t)ctx->cursor_x - (int64_t)g_drag_grab_offset_x;
        int64_t new_y = (int64_t)ctx->cursor_y - (int64_t)g_drag_grab_offset_y;
        if (new_x < (int64_t)reserved_left) new_x = (int64_t)reserved_left;
        if (new_y < 0) new_y = 0;

        sl_window_move(g_drag_window, (uint32_t)new_x, (uint32_t)new_y);
        if (g_drag_window->x != old_x || g_drag_window->y != old_y) {
            // Moving does not change what the app shows: its cached inside is
            // copied to the new place, the app is not asked to redraw.
            // Where it was and where it is, shadows included.
            sl_window_dirty_all(ctx, &(sl_window_t){ .x = old_x, .y = old_y,
                                                     .width = g_drag_window->width,
                                                     .height = g_drag_window->height });
            sl_window_dirty_all(ctx, g_drag_window);
        }
    }
    return 0;
}

// Scale the background to the screen once (the mode is set at start-up and
// never changes) and keep it in the frame's format.
static int sl_bg_bake(sl_ctx_t *ctx) {
    if (ctx->bg_frame) return 1;
    if (!ctx->bg_image.data || ctx->bg_image.channels < 3u) return 0;
    size_t stride = (size_t)ctx->fb.width * ctx->bytespp;
    uint8_t *buf = (uint8_t *)malloc(stride * ctx->fb.height);
    if (!buf) return 0;
    uint8_t clip_enabled = ctx->clip_enabled;
    sl_clip_clear(ctx);
    sl_blit_image_stretch(ctx, 0, 0, ctx->fb.width, ctx->fb.height, &ctx->bg_image);
    for (uint32_t y = 0; y < ctx->fb.height; y++) {
        memcpy(buf + (size_t)y * stride, ctx->frame + (size_t)y * ctx->row_stride, stride);
    }
    ctx->clip_enabled = clip_enabled;
    ctx->bg_frame = buf;
    ctx->bg_frame_stride = stride;
    sl_desk_build(ctx);
    // The source image is no longer needed: the baked copy replaces it.
    sl_image_free(&ctx->bg_image);
    return 1;
}

static void sl_render_bg_rect(sl_ctx_t *ctx, const sl_rect_t *r) {
    if (!ctx || !r || !ctx->frame || !ctx->bg_frame) return;
    const uint8_t *src = ctx->desk_frame ? ctx->desk_frame : ctx->bg_frame;
    uint32_t x0 = r->x, y0 = r->y;
    if (x0 >= ctx->fb.width || y0 >= ctx->fb.height || r->w == 0u || r->h == 0u) return;
    uint32_t w = r->w, h = r->h;
    if (w > ctx->fb.width - x0) w = ctx->fb.width - x0;
    if (h > ctx->fb.height - y0) h = ctx->fb.height - y0;
    size_t off = (size_t)x0 * ctx->bytespp, bytes = (size_t)w * ctx->bytespp;
    for (uint32_t y = y0; y < y0 + h; y++) {
        memcpy(ctx->frame + (size_t)y * ctx->row_stride + off, src + (size_t)y * ctx->bg_frame_stride + off, bytes);
    }
}

void sl_render_full(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->bg_loaded) return;
    if (!sl_bg_bake(ctx)) return;
    sl_rect_t all = { 0u, 0u, ctx->fb.width, ctx->fb.height };
    sl_render_bg_rect(ctx, &all);
}

// The background of r, except where `skip` (the opaque inside of the top
// window, painted over anyway this frame) covers it: up to four strips.
static void sl_render_bg_rect_except(sl_ctx_t *ctx, const sl_rect_t *r, const sl_rect_t *skip) {
    uint64_t x0 = r->x, y0 = r->y, x1 = (uint64_t)r->x + r->w, y1 = (uint64_t)r->y + r->h;
    uint64_t sx0 = skip->x, sy0 = skip->y, sx1 = (uint64_t)skip->x + skip->w, sy1 = (uint64_t)skip->y + skip->h;
    if (!skip->w || !skip->h || sx1 <= x0 || sx0 >= x1 || sy1 <= y0 || sy0 >= y1) {
        sl_render_bg_rect(ctx, r);
        return;
    }
    uint64_t my0 = sy0 > y0 ? sy0 : y0, my1 = sy1 < y1 ? sy1 : y1;
    sl_rect_t strips[4] = {
        { r->x, r->y, r->w, (uint32_t)(my0 - y0) },                                   // above
        { r->x, (uint32_t)my1, r->w, (uint32_t)(y1 - my1) },                          // below
        { r->x, (uint32_t)my0, (uint32_t)(sx0 > x0 ? sx0 - x0 : 0u), (uint32_t)(my1 - my0) },   // left
        { (uint32_t)(sx1 < x1 ? sx1 : x1), (uint32_t)my0, (uint32_t)(sx1 < x1 ? x1 - sx1 : 0u), (uint32_t)(my1 - my0) },
    };
    for (int k = 0; k < 4; k++) {
        if (strips[k].w && strips[k].h) sl_render_bg_rect(ctx, &strips[k]);
    }
}

void sl_render_dirty_background(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->bg_loaded) return;
    if (!sl_bg_bake(ctx)) return;

    if (g_dirty.full) {
        sl_render_full(ctx);
        return;
    }

    sl_rect_t skip = { 0u, 0u, 0u, 0u };
    (void)sl_windows_top_opaque_rect(ctx, &skip.x, &skip.y, &skip.w, &skip.h);
    for (size_t i = 0; i < g_dirty.count; i++) {
        sl_render_bg_rect_except(ctx, &g_dirty.rects[i], &skip);
    }
}

int sl_present(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return 0;
    peos_fb_rect_t rects[SL_PRESENT_MAX_RECTS];
    uint64_t pixels = 0u;
    uint8_t full = 0u;
    size_t rect_count = sl_dirty_build_present_rects(ctx, rects, &pixels, &full);
    (void)pixels;
    (void)full;
    if (rect_count == 0u) {
        sl_perf_record_present(0u, 0u, 0u, 0u, ctx->bytespp, 0u);
        sl_dirty_reset();
        return 1;
    }

    if (ctx->fb_direct) {
        if (!ctx->fb_front) return 0;
        uint64_t copy_start_ns = peos_time_ns();
        for (size_t i = 0; i < rect_count; i++) {
            const peos_fb_rect_t *r = &rects[i];
            size_t row_bytes = (size_t)r->width * (size_t)ctx->bytespp;
            for (uint32_t y = 0; y < r->height; y++) {
                size_t yy = (size_t)r->y + (size_t)y;
                uint8_t *dst = ctx->fb_front + (yy * ctx->fb_front_stride) +
                               ((size_t)r->x * (size_t)ctx->bytespp);
                const uint8_t *src = ctx->frame + (yy * ctx->row_stride) +
                                     ((size_t)r->x * (size_t)ctx->bytespp);
                peos_memcpy(dst, src, row_bytes);
            }
        }
        uint64_t copy_ns = peos_time_ns() - copy_start_ns;
        uint64_t flush_start_ns = peos_time_ns();
        if (peos_fbpresent_damage(NULL, 0u, rects, rect_count,
                                  PEOS_FBPRESENT_EXCLUSIVE) < 0) {
            sl_dirty_mark_full();
            return 1;
        }
        uint64_t flush_ns = peos_time_ns() - flush_start_ns;
        sl_perf_record_present(copy_ns, flush_ns, rect_count, pixels,
                               ctx->bytespp, full);
        sl_dirty_reset();
        return 1;
    }
    uint64_t present_start_ns = peos_time_ns();
    int64_t wr = peos_fbpresent_damage(ctx->frame, ctx->frame_bytes,
                                      rects, rect_count, PEOS_FBPRESENT_EXCLUSIVE);
    if (wr < 0) {
        sl_dirty_mark_full();
        return 1;
    }
    sl_perf_record_present(0u, peos_time_ns() - present_start_ns,
                           rect_count, pixels, ctx->bytespp, full);
    sl_dirty_reset();
    return 1;
}

// An input wake starts a frame early, but not sooner than this after the last one.
#define SL_INPUT_MIN_FRAME_NS 4000000ull
// After this many frames that drew nothing, tick slowly; input still wakes at once.
#define SL_IDLE_AFTER_FRAMES 30u
#define SL_IDLE_FRAME_NS 50000000ull

// Wait for the next frame deadline. With an input endpoint the wait ends early
// when input arrives (lower latency, no busy wait); without one, sleep then spin.
void sl_frame_pace(uint64_t *next_deadline_ns, int64_t input_fd, int drew) {
    static uint64_t last_frame_ns;
    static uint32_t idle_frames;
    if (!next_deadline_ns) return;

    if (drew) idle_frames = 0;
    else if (idle_frames < SL_IDLE_AFTER_FRAMES) idle_frames++;
    if (input_fd >= 0 && idle_frames >= SL_IDLE_AFTER_FRAMES && *next_deadline_ns != 0) {
        uint64_t idle_deadline = last_frame_ns + SL_IDLE_FRAME_NS;
        if (idle_deadline > *next_deadline_ns) *next_deadline_ns = idle_deadline;
    }

    uint64_t now = peos_time_ns();
    if (*next_deadline_ns == 0) {
        *next_deadline_ns = now + SL_FRAME_NS;
        last_frame_ns = now;
        return;
    }

    if (now < *next_deadline_ns && input_fd >= 0) {
        uint64_t earliest = last_frame_ns + SL_INPUT_MIN_FRAME_NS;
        if (earliest > now && earliest < *next_deadline_ns) {
            uint64_t ticks = (earliest - now + SL_TICK_NS - 1u) / SL_TICK_NS;
            peos_sleep(ticks);
            now = peos_time_ns();
        }
        if (now < *next_deadline_ns) {
            peos_wait_item_t it = { (int32_t)input_fd, PEOS_WAIT_READ, 0 };
            if (peos_wait(&it, 1u, *next_deadline_ns - now) > 0) {
                last_frame_ns = peos_time_ns();
                idle_frames = 0;
                if (*next_deadline_ns > last_frame_ns + SL_FRAME_NS) *next_deadline_ns = last_frame_ns + SL_FRAME_NS;
                return;   // input: render now, back to the regular frame rate
            }
        }
    } else if (now < *next_deadline_ns) {
        uint64_t remaining = *next_deadline_ns - now;
        if (remaining > (SL_PACER_SPIN_NS + SL_TICK_NS)) {
            uint64_t sleep_ns = remaining - SL_PACER_SPIN_NS;
            uint64_t sleep_ticks = sleep_ns / SL_TICK_NS;
            if (sleep_ticks) peos_sleep(sleep_ticks);
        }
        while ((now = peos_time_ns()) < *next_deadline_ns) {
            __asm__ volatile("pause");
        }
    }
    now = peos_time_ns();
    if (now > *next_deadline_ns && (now - *next_deadline_ns) > (SL_FRAME_NS * 4ull)) {
        *next_deadline_ns = now;
    }

    *next_deadline_ns += SL_FRAME_NS;
    last_frame_ns = now;
}
