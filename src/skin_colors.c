// The skin's colours from the theme (docs/skin-colors-plan.md, phases 3, 4, 6).
//
// The theme's bg_canvas, shadow_dark and shadow_light (by day; by night the
// same keys of the night theme) are the skin recipe's $ground, $shadow and
// $light ($ground-n, ...); its outline is $outline, with the alpha
// sl_ui_outline_alpha gives it (none while the shadows are opaque). With the
// recipe's own values nothing is done: the skin baked at build time is the
// right one. With others, the surfaces whose styles use them are baked again
// (render/skin_bake.c, the recipe's color/ground/style/surface/tilebase
// lines of /starlight/skin/skin.cfg) into a small skin laid over the main one
// (sl_skin_set_overlay).
//
// What is baked is kept in /starlight/cache/skin-<print>.slsk, the print
// made of the mode, the colours and the recipe: the same theme is never
// baked twice. At most SKINS_MAX files (the user's rule): a list,
// /starlight/cache/skins.cfg, newest first; the oldest goes.
//
// A skin in the cache is put on at once (a few ms). Baking takes a second or
// two, so it runs on a thread of its own (phase 6) while the skin of before
// stays on; sl_skin_colors_tick, every frame, puts the new one on when the
// thread is done. The thread touches nothing of Starlight's: it gets a copy
// of the recipe and the colours and gives back a parsed skin.

#include "starlight_internal.h"
#include "ui_sdk.h"
#include "render/skin.h"
#include "render/skin_bake.h"
#include "desktop.h"

#include "peos/peos.h"
#include "peos/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECIPE SL_RESOURCE_ROOT "/skin/skin.cfg"
#define CACHE_DIR SL_RESOURCE_ROOT "/cache"
#define CACHE_LIST CACHE_DIR "/skins.cfg"
#define SKINS_MAX 5u
#define RECIPE_MAX (64u * 1024u)
#define NCOLS 4

static sl_skin_t g_overlay;
static uint8_t g_overlay_on;
static uint64_t g_overlay_print;
// The overlay taken off last: still alive, because windows, dock and tiles
// keep pointers to its sprites until they look them up again (the next
// frame, when they see the theme's new version). Freed at the next change.
static sl_skin_t g_retired;
static uint8_t g_retired_on;
// The print the theme asks for now (0: the skin's own); the last one that
// could not be baked (not tried again until the theme changes).
static uint64_t g_want, g_failed;

// The bake on its thread: what it is given, what it gives back.
static struct {
    int64_t tid;                 // > 0 while a thread is out
    volatile uint32_t done;      // set by the thread, last
    uint64_t print;
    uint8_t night;
    char *recipe;                // a copy, the thread's
    const char *names[NCOLS];
    sl_ui_color_t cols[NCOLS];
    char path[96];
    const sl_skin_t *base;       // the main skin: the masks of "shape" (read only)
    sl_skin_t next;
    int ok, kept;                // parsed; written to the cache
    uint64_t ns;                 // how long it took
} g_job;
static uint8_t g_job_stack[64u * 1024u] __attribute__((aligned(16)));

static void overlay_retire(void) {
    sl_skin_set_overlay(g_sl_skin_overlay, NULL);   // the one retired before goes now
    if (g_retired_on) sl_skin_free(&g_retired);
    g_retired = g_overlay;
    g_retired_on = g_overlay_on;
    memset(&g_overlay, 0, sizeof(g_overlay));
    g_overlay_on = 0u;
    g_overlay_print = 0u;
}

// Everything that kept a sprite of the skin looks it up again: the theme's
// version moves on, windows and the desktop are drawn again.
static void skin_changed(sl_ctx_t *ctx) {
    sl_ui_runtime_touch();
    sl_windows_theme_changed(ctx);
    sl_desk_build(ctx);
    sl_dirty_mark_full();
}

static void overlay_drop(sl_ctx_t *ctx) {
    if (!g_overlay_on) return;
    overlay_retire();
    sl_skin_set_overlay(NULL, g_retired_on ? &g_retired : NULL);
    skin_changed(ctx);
}

static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static int path_len(const char *p) { return (int)strlen(p); }

// The recipe's own value of a "color" line, or -1 if it has none.
static int recipe_color(const char *recipe, const char *name, uint8_t rgb[3]) {
    size_t nl = strlen(name);
    for (const char *l = recipe; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        if (strncmp(l, "color ", 6) != 0) continue;
        const char *p = l + 6;
        while (*p == ' ') p++;
        if (strncmp(p, name, nl) != 0 || (p[nl] != ' ' && p[nl] != '\t')) continue;
        p += nl;
        while (*p == ' ' || *p == '\t') p++;
        unsigned v = 0;
        if (sscanf(p, "%6x", &v) != 1) return -1;
        rgb[0] = (uint8_t)(v >> 16);
        rgb[1] = (uint8_t)(v >> 8);
        rgb[2] = (uint8_t)v;
        return 0;
    }
    return -1;
}

// The list of the kept skins, newest first, with `print` put first; the
// ones past SKINS_MAX are deleted.
static void cache_list_use(uint64_t print) {
    char text[512] = "", out[512] = "";
    size_t n = 0;
    if (peos_read_file(CACHE_LIST, (size_t)path_len(CACHE_LIST), text, sizeof(text) - 1u, &n) >= 0) text[n] = '\0';
    else text[0] = '\0';
    uint64_t list[16];
    unsigned count = 0;
    list[count++] = print;
    for (char *l = text; l && *l && count < 16u; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        unsigned long long v = 0;
        if (sscanf(l, "%16llx", &v) != 1 || (uint64_t)v == print) continue;
        list[count++] = (uint64_t)v;
    }
    size_t o = 0;
    for (unsigned i = 0; i < count; i++) {
        if (i >= SKINS_MAX) {   // too many: the oldest go, file and all
            char path[96];
            snprintf(path, sizeof(path), CACHE_DIR "/skin-%016llx.slsk", (unsigned long long)list[i]);
            (void)peos_unlink(path, strlen(path));
            printf("starlight: skin colours: %016llx out of the cache (%u at most)\n", (unsigned long long)list[i],
                   SKINS_MAX);
            continue;
        }
        o += (size_t)snprintf(out + o, sizeof(out) - o, "%016llx\n", (unsigned long long)list[i]);
    }
    (void)peos_write_file(CACHE_LIST, (size_t)path_len(CACHE_LIST), out, o, PEOS_O_CREAT | PEOS_O_TRUNC | PEOS_O_WRITE);
}

// A mask of the main skin by name, for "shape" (the fin). On the bake
// thread: the main skin's table only, never the overlay (the main thread's).
static const uint8_t *base_mask(void *user, const char *name, int *w, int *h) {
    const sl_skin_t *skin = user;
    for (uint32_t i = 0; skin && i < skin->count; i++) {
        const slsk_entry_t *e = &skin->entries[i];
        if (e->kind != SLSK_KIND_MASK || strncmp(e->name, name, SLSK_NAME_MAX) != 0) continue;
        *w = e->width;
        *h = e->height;
        return skin->buf + e->offset;
    }
    return NULL;
}

// The recipe's lines about colours baked with the theme's colours over it.
// On the bake thread: no strtok (one state for the whole program), no stdio.
static uint8_t *bake(char *recipe, const char *const names[NCOLS], const sl_ui_color_t cols[NCOLS], size_t *len) {
    static skb_t s;   // a few KB of styles: not on the stack
    skb_init(&s);
    s.mask = base_mask;
    s.mask_user = (void *)g_job.base;
    for (int i = 0; i < NCOLS; i++) {
        skb_rgb_t c = { cols[i].r / 255.0f, cols[i].g / 255.0f, cols[i].b / 255.0f };
        (void)skb_var_set(&s, names[i], c, cols[i].a / 255.0f, 1);
    }
    int ok = 1;
    for (char *l = recipe; l && *l && ok;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = '\0';
        char *hash = strchr(l, '#');
        if (hash) *hash = '\0';
        char *tok[24];
        int n = 0;
        for (char *p = l; *p && n < 24;) {
            while (*p == ' ' || *p == '\t' || *p == '\r') *p++ = '\0';
            if (!*p) break;
            tok[n++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
        }
        if (n && (!strcmp(tok[0], "color") || !strcmp(tok[0], "ground") || !strcmp(tok[0], "style") ||
                  !strcmp(tok[0], "surface") || !strcmp(tok[0], "tilebase") || !strcmp(tok[0], "shape"))) {
            if (skb_recipe_line(&s, tok, n) < 0) ok = 0;
        }
        l = next;
    }
    uint8_t *file = NULL;
    if (ok) *len = skb_write(&s, &file);
    skb_free(&s);
    return ok && *len ? file : NULL;
}

static void job_run(void) {
    uint64_t t0 = peos_time_ns();
    size_t len = 0;
    uint8_t *file = bake(g_job.recipe, g_job.names, g_job.cols, &len);
    memset(&g_job.next, 0, sizeof(g_job.next));
    g_job.ok = file && sl_skin_parse(&g_job.next, file, len);
    if (g_job.ok) {
        g_job.next.buf = file;   // owned now (sl_skin_free frees it)
        g_job.next.len = len;
        (void)peos_mkdir_p(CACHE_DIR, strlen(CACHE_DIR));
        g_job.kept = peos_write_file(g_job.path, strlen(g_job.path), file, len,
                                     PEOS_O_CREAT | PEOS_O_TRUNC | PEOS_O_WRITE) >= 0;
    } else {
        free(file);
    }
    g_job.ns = peos_time_ns() - t0;
}

static void job_thread(void *arg) {
    (void)arg;
    job_run();
    __atomic_store_n(&g_job.done, 1u, __ATOMIC_RELEASE);
    peos_thread_exit(0);
}

// The skin `next` (print) over the main one; the old one retired.
static void install(sl_ctx_t *ctx, sl_skin_t *next, uint64_t print, const char *how, uint64_t ns) {
    printf("starlight: skin colours %016llx (%s): %u surfaces %s in %u ms\n", (unsigned long long)print,
           sl_ui_runtime_night() ? "night" : "day", (unsigned)next->count, how, (unsigned)(ns / 1000000u));
    overlay_retire();
    g_overlay = *next;
    g_overlay_on = 1u;
    g_overlay_print = print;
    sl_skin_set_overlay(&g_overlay, g_retired_on ? &g_retired : NULL);
    cache_list_use(print);
    skin_changed(ctx);
}

void sl_skin_colors_apply(sl_ctx_t *ctx) {
    if (!ctx || !ctx->skin_loaded) return;
    uint8_t night = sl_ui_runtime_night();
    sl_ui_theme_t th;
    sl_ui_theme_runtime_get(&th);
    const char *names[NCOLS] = { night ? "ground-n" : "ground", night ? "shadow-n" : "shadow",
                                 night ? "light-n" : "light", night ? "outline-n" : "outline" };
    // the outline as strong as the shadows are faint (0 with the shadows opaque)
    sl_ui_color_t outline = th.colors[SL_UI_COLOR_OUTLINE];
    outline.a = sl_ui_outline_alpha(&th.colors[SL_UI_COLOR_OUTLINE], &th.colors[SL_UI_COLOR_SHADOW_DARK],
                                    &th.colors[SL_UI_COLOR_SHADOW_LIGHT]);
    const sl_ui_color_t cols[NCOLS] = { th.colors[SL_UI_COLOR_BG_CANVAS], th.colors[SL_UI_COLOR_SHADOW_DARK],
                                        th.colors[SL_UI_COLOR_SHADOW_LIGHT], outline };

    static char recipe[RECIPE_MAX];
    size_t rn = 0;
    if (peos_read_file(RECIPE, (size_t)path_len(RECIPE), recipe, sizeof(recipe) - 1u, &rn) < 0) {
        g_want = 0u;
        overlay_drop(ctx);   // no recipe in this image: the skin as it was baked
        return;
    }
    recipe[rn] = '\0';
    // The recipe's own colours (the outline then has no alpha): nothing to bake.
    int same = 1;
    for (int i = 0; i < 3; i++) {
        uint8_t rgb[3];
        if (recipe_color(recipe, names[i], rgb) != 0 || rgb[0] != cols[i].r || rgb[1] != cols[i].g ||
            rgb[2] != cols[i].b || cols[i].a != 255u)
            same = 0;
    }
    if (same) {
        if (g_overlay_on) printf("starlight: skin colours: the skin's own\n");
        g_want = 0u;
        overlay_drop(ctx);
        return;
    }
    uint64_t print = fnv(1469598103934665603ull, &night, 1);
    for (int i = 0; i < NCOLS; i++) print = fnv(print, &cols[i], sizeof(cols[i]));
    print = fnv(print, recipe, rn);
    g_want = print;
    if (g_overlay_on && print == g_overlay_print) return;   // this one already

    char path[96];
    snprintf(path, sizeof(path), CACHE_DIR "/skin-%016llx.slsk", (unsigned long long)print);
    uint64_t t0 = peos_time_ns();
    sl_skin_t next;
    memset(&next, 0, sizeof(next));
    if (peos_is_file(path, strlen(path)) > 0 && sl_skin_load(&next, path)) {
        install(ctx, &next, print, "from the cache", peos_time_ns() - t0);
        return;
    }
    if (g_job.tid) return;   // one bake at a time: the tick starts this one after it
    if (print == g_failed) return;   // it did not bake: the skin as it is

    // Baked on a thread: the skin of before stays on until it is done.
    g_job.recipe = malloc(rn + 1u);
    if (!g_job.recipe) return;
    memcpy(g_job.recipe, recipe, rn + 1u);
    g_job.print = print;
    g_job.night = night;
    for (int i = 0; i < NCOLS; i++) {
        g_job.names[i] = names[i];   // string literals
        g_job.cols[i] = cols[i];
    }
    snprintf(g_job.path, sizeof(g_job.path), "%s", path);
    g_job.base = &ctx->skin;
    g_job.ok = g_job.kept = 0;
    g_job.done = 0u;
    g_job.tid = peos_thread_spawn(job_thread, NULL, g_job_stack + sizeof(g_job_stack));
    if (g_job.tid <= 0) {   // no thread: bake here, the screen waits
        g_job.tid = 0;
        job_run();
        g_job.done = 1u;
        g_job.tid = -1;   // collected by the tick below, without a wait
        sl_skin_colors_tick(ctx);
        return;
    }
    printf("starlight: skin colours %016llx: baking on a thread\n", (unsigned long long)print);
}

void sl_skin_colors_tick(sl_ctx_t *ctx) {
    if (!g_job.tid || !__atomic_load_n(&g_job.done, __ATOMIC_ACQUIRE)) return;
    if (g_job.tid > 0) {
        int st = 0;
        (void)peos_waitpid((uint64_t)g_job.tid, &st);
    }
    g_job.tid = 0;
    free(g_job.recipe);
    g_job.recipe = NULL;
    if (!g_job.ok) {
        printf("starlight: skin colours: cannot bake them, the skin as it was\n");
        g_failed = g_job.print;
    } else {
        if (!g_job.kept) printf("starlight: skin colours: cannot keep them in %s\n", CACHE_DIR);
        if (g_job.print == g_want) {
            install(ctx, &g_job.next, g_job.print, "baked", g_job.ns);
        } else {   // the theme changed meanwhile: only kept for later
            sl_skin_free(&g_job.next);
            if (g_job.kept) cache_list_use(g_job.print);
        }
    }
    // The theme wants another one: its turn now.
    if (g_want && g_want != g_overlay_print) sl_skin_colors_apply(ctx);
}
