#include "starlight_internal.h"
#include "render/pixels.h"

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
#define SL_CFG_FILE_MAX 4096u
#define SL_CFG_LINE_MAX 256u
#define SL_DIRTY_MAX_RECTS 128u

static uint8_t g_prev_mouse_buttons = 0u;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} sl_rect_t;

static struct {
    sl_rect_t rects[SL_DIRTY_MAX_RECTS];
    size_t count;
    uint8_t full;
} g_dirty = {0};

void sl_dirty_reset(void) {
    g_dirty.count = 0;
    g_dirty.full = 0u;
}

static int sl_rects_overlap_or_touch(const sl_rect_t *a, const sl_rect_t *b) {
    if (!a || !b) return 0;
    uint64_t ax2 = (uint64_t)a->x + (uint64_t)a->w;
    uint64_t ay2 = (uint64_t)a->y + (uint64_t)a->h;
    uint64_t bx2 = (uint64_t)b->x + (uint64_t)b->w;
    uint64_t by2 = (uint64_t)b->y + (uint64_t)b->h;
    return !((ax2 < b->x) || (bx2 < a->x) || (ay2 < b->y) || (by2 < a->y));
}

static sl_rect_t sl_rect_union(const sl_rect_t *a, const sl_rect_t *b) {
    sl_rect_t r = {0, 0, 0, 0};
    if (!a) return b ? *b : r;
    if (!b) return *a;

    uint32_t x1 = (a->x < b->x) ? a->x : b->x;
    uint32_t y1 = (a->y < b->y) ? a->y : b->y;
    uint64_t ax2 = (uint64_t)a->x + (uint64_t)a->w;
    uint64_t ay2 = (uint64_t)a->y + (uint64_t)a->h;
    uint64_t bx2 = (uint64_t)b->x + (uint64_t)b->w;
    uint64_t by2 = (uint64_t)b->y + (uint64_t)b->h;
    uint64_t x2 = (ax2 > bx2) ? ax2 : bx2;
    uint64_t y2 = (ay2 > by2) ? ay2 : by2;

    r.x = x1;
    r.y = y1;
    r.w = (uint32_t)(x2 - x1);
    r.h = (uint32_t)(y2 - y1);
    return r;
}

void sl_dirty_mark_full(void) {
    g_dirty.full = 1u;
    g_dirty.count = 0;
}

void sl_dirty_add(const sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!ctx || g_dirty.full) return;
    if (w == 0u || h == 0u) return;
    if (x >= ctx->fb.width || y >= ctx->fb.height) return;

    if (x + w > ctx->fb.width) w = ctx->fb.width - x;
    if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    if (w == 0u || h == 0u) return;

    sl_rect_t in = {x, y, w, h};

    for (size_t i = 0; i < g_dirty.count; i++) {
        if (sl_rects_overlap_or_touch(&g_dirty.rects[i], &in)) {
            g_dirty.rects[i] = sl_rect_union(&g_dirty.rects[i], &in);

            for (size_t j = 0; j < g_dirty.count;) {
                if (j == i) {
                    j++;
                    continue;
                }
                if (sl_rects_overlap_or_touch(&g_dirty.rects[i], &g_dirty.rects[j])) {
                    g_dirty.rects[i] = sl_rect_union(&g_dirty.rects[i], &g_dirty.rects[j]);
                    g_dirty.rects[j] = g_dirty.rects[g_dirty.count - 1u];
                    g_dirty.count--;
                    continue;
                }
                j++;
            }
            return;
        }
    }

    if (g_dirty.count >= SL_DIRTY_MAX_RECTS) {
        sl_dirty_mark_full();
        return;
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

static void sl_ensure_config_dirs(void) {
    (void)mkdir("/system", 0755);
    (void)mkdir(SL_OS_CONFIG_DIR, 0755);
    (void)mkdir(SL_RESOURCE_ROOT, 0755);
    (void)mkdir(SL_RESOURCE_CONFIG_DIR, 0755);
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
    (void)fprintf(f, "cursor=%s\n", SL_RESOURCE_CURSOR);
    (void)fprintf(f, "cursor_scale=100\n");
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
    ctx->hw_cursor_scale_pct = SL_HW_CURSOR_SCALE_DEFAULT;

    char *cfg = sl_load_text_file(SL_RESOURCE_CONFIG_FILE, SL_CFG_FILE_MAX);
    if (!cfg) {
        sl_write_default_starlight_config();
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
                } else if (strcmp(key, "cursor_scale") == 0) {
                    uint32_t n = 0;
                    if (sl_parse_u32(val, &n) && n >= 25u && n <= 400u) {
                        ctx->hw_cursor_scale_pct = n;
                    }
                }
            }
        }
        if (c == '\0') break;
    }
    free(cfg);
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

    sl_ensure_config_dirs();
    sl_load_display_config(ctx);
    sl_load_starlight_config(ctx);

    if ((ctx->display_width == 0u && ctx->display_height == 0u) ||
        (ctx->display_width > 0u && ctx->display_height > 0u)) {
        (void)peos_fb_set_mode(ctx->display_width, ctx->display_height);
    }

    if (!sl_update_fb_info(ctx)) return 0;

    if ((int64_t)peos_input_mode(PEOS_INPUT_MODE_RAW) == 0) {
        ctx->input_raw = 1;
    }

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
    if (ctx->frame) (void)peos_munmap((uint64_t)(uintptr_t)ctx->frame, ctx->map_bytes);
}

int sl_handle_input(sl_ctx_t *ctx) {
    peos_input_event_t evs[16];
    peos_mouse_event_t mev;
    uint64_t n = peos_input_poll(evs, 16);
    int64_t mr = peos_input_mouse(&mev);
    if ((int64_t)n < 0 || mr < 0) return 1;

    for (uint64_t i = 0; i < n; i++) {
        if (evs[i].pressed && evs[i].code == PEOS_KEY_ESC) return 1;
    }

    if (mr >= 0) {
        int64_t nx = (int64_t)ctx->cursor_x + (int64_t)mev.dx;
        int64_t ny = (int64_t)ctx->cursor_y + (int64_t)mev.dy;

        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (ctx->fb.width > 0 && nx >= (int64_t)ctx->fb.width) nx = (int64_t)ctx->fb.width - 1;
        if (ctx->fb.height > 0 && ny >= (int64_t)ctx->fb.height) ny = (int64_t)ctx->fb.height - 1;

        ctx->cursor_x = (uint32_t)nx;
        ctx->cursor_y = (uint32_t)ny;

        uint8_t left_now = (uint8_t)(mev.buttons & 0x01u);
        uint8_t left_prev = (uint8_t)(g_prev_mouse_buttons & 0x01u);
        if (left_now && !left_prev) {
            sl_windows_focus_at_cursor(ctx->cursor_x, ctx->cursor_y);
        }
        g_prev_mouse_buttons = mev.buttons;
    }
    return 0;
}

void sl_render_full(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame || !ctx->bg_loaded) return;
    if (ctx->bg_image.data && ctx->bg_image.channels >= 3u) {
        sl_blit_rgba_stretch(ctx, 0, 0, ctx->fb.width, ctx->fb.height,
                             ctx->bg_image.data, ctx->bg_image.width,
                             ctx->bg_image.height, ctx->bg_image.stride);
    }
}

int sl_present(sl_ctx_t *ctx) {
    if (!ctx || !ctx->frame) return 0;
    if (ctx->fb_direct) {
        if (!ctx->fb_front) return 0;
        if (g_dirty.full) {
            size_t rows = (size_t)ctx->fb.height;
            size_t row_bytes = (size_t)ctx->fb.width * (size_t)ctx->bytespp;
            for (size_t y = 0; y < rows; y++) {
                uint8_t *dst = ctx->fb_front + (y * ctx->fb_front_stride);
                const uint8_t *src = ctx->frame + (y * ctx->row_stride);
                peos_memcpy(dst, src, row_bytes);
            }
        } else {
            for (size_t i = 0; i < g_dirty.count; i++) {
                const sl_rect_t *r = &g_dirty.rects[i];
                size_t row_bytes = (size_t)r->w * (size_t)ctx->bytespp;
                for (uint32_t y = 0; y < r->h; y++) {
                    size_t yy = (size_t)r->y + (size_t)y;
                    uint8_t *dst = ctx->fb_front + (yy * ctx->fb_front_stride) +
                                   ((size_t)r->x * (size_t)ctx->bytespp);
                    const uint8_t *src = ctx->frame + (yy * ctx->row_stride) +
                                         ((size_t)r->x * (size_t)ctx->bytespp);
                    peos_memcpy(dst, src, row_bytes);
                }
            }
        }
        sl_dirty_reset();
        return 1;
    }
    uint64_t wr = peos_fbpresent(ctx->frame, ctx->frame_bytes, PEOS_FBPRESENT_EXCLUSIVE);
    sl_dirty_reset();
    return (int64_t)wr >= 0 && wr == (uint64_t)ctx->frame_bytes;
}

void sl_frame_pace(uint64_t *next_deadline_ns) {
    if (!next_deadline_ns) return;

    uint64_t now = peos_time_ns();
    if (*next_deadline_ns == 0) {
        *next_deadline_ns = now + SL_FRAME_NS;
        return;
    }

    if (now < *next_deadline_ns) {
        uint64_t remaining = *next_deadline_ns - now;
        if (remaining > (SL_PACER_SPIN_NS + SL_TICK_NS)) {
            uint64_t sleep_ns = remaining - SL_PACER_SPIN_NS;
            uint64_t sleep_ticks = sleep_ns / SL_TICK_NS;
            if (sleep_ticks) peos_sleep(sleep_ticks);
        }
        while ((now = peos_time_ns()) < *next_deadline_ns) {
            __asm__ volatile("pause");
        }
    } else if ((now - *next_deadline_ns) > (SL_FRAME_NS * 4ull)) {
        *next_deadline_ns = now;
    }

    *next_deadline_ns += SL_FRAME_NS;
}
