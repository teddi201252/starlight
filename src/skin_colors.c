// The skin's colours from the theme (docs/skin-colors-plan.md, phase 3).
//
// The theme's bg_canvas, shadow_dark and shadow_light (by day; by night the
// same keys of the night theme) are the skin recipe's $ground, $shadow and
// $light ($ground-n, ...); its outline is $outline, with the alpha
// sl_ui_outline_alpha gives it (none while the shadows are opaque). With the recipe's own values nothing is done:
// the skin baked at build time is the right one. With others, the surfaces
// whose styles use them are baked again (render/skin_bake.c, the recipe's
// color/ground/style/surface/tilebase lines of /starlight/skin/skin.cfg) into
// a small skin laid over the main one (sl_skin_set_overlay).
//
// What is baked is kept in /starlight/cache/skin-<print>.slsk, the print
// made of the mode, the three colours and the recipe: the same theme is
// never baked twice. At most SKINS_MAX files (the user's rule): a list,
// /starlight/cache/skins.cfg, newest first; the oldest goes.

#include "starlight_internal.h"
#include "ui_sdk.h"
#include "render/skin.h"
#include "render/skin_bake.h"

#include "peos/peos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECIPE SL_RESOURCE_ROOT "/skin/skin.cfg"
#define CACHE_DIR SL_RESOURCE_ROOT "/cache"
#define CACHE_LIST CACHE_DIR "/skins.cfg"
#define SKINS_MAX 5u
#define RECIPE_MAX (64u * 1024u)

static sl_skin_t g_overlay;
static uint8_t g_overlay_on;
static uint64_t g_overlay_print;
// The overlay taken off last: still alive, because windows, dock and tiles
// keep pointers to its sprites until they look them up again (the next
// frame, when they see the theme's new version). Freed at the next change.
static sl_skin_t g_retired;
static uint8_t g_retired_on;

static void overlay_retire(void) {
    sl_skin_set_overlay(g_sl_skin_overlay, NULL);   // the one retired before goes now
    if (g_retired_on) sl_skin_free(&g_retired);
    g_retired = g_overlay;
    g_retired_on = g_overlay_on;
    memset(&g_overlay, 0, sizeof(g_overlay));
    g_overlay_on = 0u;
    g_overlay_print = 0u;
}

static void overlay_drop(void) {
    if (!g_overlay_on) return;
    overlay_retire();
    sl_skin_set_overlay(NULL, g_retired_on ? &g_retired : NULL);
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

// The list of the kept skins, newest first, with `print` put first (or
// taken out with keep 0); the ones past SKINS_MAX are deleted.
static void cache_list_use(uint64_t print, int keep) {
    char text[512] = "", out[512] = "";
    size_t n = 0;
    if (peos_read_file(CACHE_LIST, (size_t)path_len(CACHE_LIST), text, sizeof(text) - 1u, &n) >= 0) text[n] = '\0';
    else text[0] = '\0';
    uint64_t list[16];
    unsigned count = 0;
    if (keep) list[count++] = print;
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

// The recipe's lines about colours baked with the theme's colours over it.
static uint8_t *bake(const char *recipe, const char *names[4], const sl_ui_color_t *cols[4], size_t *len) {
    static skb_t s;   // a few KB of styles: not on the stack
    skb_init(&s);
    for (int i = 0; i < 4; i++) {
        skb_rgb_t c = { cols[i]->r / 255.0f, cols[i]->g / 255.0f, cols[i]->b / 255.0f };
        (void)skb_var_set(&s, names[i], c, cols[i]->a / 255.0f, 1);
    }
    char *copy = malloc(strlen(recipe) + 1u);
    if (!copy) return NULL;
    strcpy(copy, recipe);
    int ok = 1;
    for (char *l = copy; l && *l && ok;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = '\0';
        char *hash = strchr(l, '#');
        if (hash) *hash = '\0';
        char *tok[24];
        int n = 0;
        for (char *t = strtok(l, " \t\r"); t && n < 24; t = strtok(NULL, " \t\r")) tok[n++] = t;
        if (n && (!strcmp(tok[0], "color") || !strcmp(tok[0], "ground") || !strcmp(tok[0], "style") ||
                  !strcmp(tok[0], "surface") || !strcmp(tok[0], "tilebase"))) {
            if (skb_recipe_line(&s, tok, n) < 0) ok = 0;
        }
        l = next;
    }
    free(copy);
    uint8_t *file = NULL;
    if (ok) *len = skb_write(&s, &file);
    else printf("starlight: skin colours: the recipe: %s\n", s.error);
    skb_free(&s);
    return ok && *len ? file : NULL;
}

void sl_skin_colors_apply(sl_ctx_t *ctx) {
    if (!ctx || !ctx->skin_loaded) return;
    uint8_t night = sl_ui_runtime_night();
    sl_ui_theme_t th;
    sl_ui_theme_runtime_get(&th);
    const char *names[4] = { night ? "ground-n" : "ground", night ? "shadow-n" : "shadow", night ? "light-n" : "light",
                             night ? "outline-n" : "outline" };
    // the outline as strong as the shadows are faint (0 with the shadows opaque)
    sl_ui_color_t outline = th.colors[SL_UI_COLOR_OUTLINE];
    outline.a = sl_ui_outline_alpha(&th.colors[SL_UI_COLOR_OUTLINE], &th.colors[SL_UI_COLOR_SHADOW_DARK],
                                    &th.colors[SL_UI_COLOR_SHADOW_LIGHT]);
    const sl_ui_color_t *cols[4] = { &th.colors[SL_UI_COLOR_BG_CANVAS], &th.colors[SL_UI_COLOR_SHADOW_DARK],
                                     &th.colors[SL_UI_COLOR_SHADOW_LIGHT], &outline };

    static char recipe[RECIPE_MAX];
    size_t rn = 0;
    if (peos_read_file(RECIPE, (size_t)path_len(RECIPE), recipe, sizeof(recipe) - 1u, &rn) < 0) {
        overlay_drop();   // no recipe in this image: the skin as it was baked
        return;
    }
    recipe[rn] = '\0';
    // The recipe's own colours: nothing to bake.
    int same = 1;
    for (int i = 0; i < 3; i++) {
        uint8_t rgb[3];
        if (recipe_color(recipe, names[i], rgb) != 0 || rgb[0] != cols[i]->r || rgb[1] != cols[i]->g ||
            rgb[2] != cols[i]->b || cols[i]->a != 255u)
            same = 0;
    }
    if (same) {
        if (g_overlay_on) printf("starlight: skin colours: the skin's own\n");
        overlay_drop();
        return;
    }
    uint64_t print = fnv(1469598103934665603ull, &night, 1);
    for (int i = 0; i < 4; i++) print = fnv(print, cols[i], sizeof(*cols[i]));
    print = fnv(print, recipe, rn);
    if (g_overlay_on && print == g_overlay_print) return;   // this one already

    char path[96];
    snprintf(path, sizeof(path), CACHE_DIR "/skin-%016llx.slsk", (unsigned long long)print);
    uint64_t t0 = peos_time_ns();
    sl_skin_t next;
    memset(&next, 0, sizeof(next));
    int loaded = peos_is_file(path, strlen(path)) > 0 && sl_skin_load(&next, path);
    if (!loaded) {
        size_t len = 0;
        uint8_t *file = bake(recipe, names, cols, &len);
        if (!file || !sl_skin_parse(&next, file, len)) {
            free(file);
            printf("starlight: skin colours: cannot bake them, the skin as it was\n");
            overlay_drop();
            return;
        }
        next.buf = file;   // owned now (sl_skin_free frees it)
        next.len = len;
        (void)peos_mkdir_p(CACHE_DIR, strlen(CACHE_DIR));
        if (peos_write_file(path, strlen(path), file, len, PEOS_O_CREAT | PEOS_O_TRUNC | PEOS_O_WRITE) < 0)
            printf("starlight: skin colours: cannot keep them in %s\n", CACHE_DIR);
    }
    printf("starlight: skin colours %016llx (%s): %u surfaces %s in %u ms\n", (unsigned long long)print,
           night ? "night" : "day", (unsigned)next.count, loaded ? "from the cache" : "baked",
           (unsigned)((peos_time_ns() - t0) / 1000000u));
    // Over the main skin; the old one is retired (overlay_retire): everything
    // that kept a pointer into it looks its sprites up again (the theme's
    // version changed) before it is freed.
    overlay_retire();
    g_overlay = next;
    g_overlay_on = 1u;
    g_overlay_print = print;
    sl_skin_set_overlay(&g_overlay, g_retired_on ? &g_retired : NULL);
    cache_list_use(print, 1);
}
