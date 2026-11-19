#include "ui_sdk.h"
#include "notify.h"
#include "music.h"
#include "starlight_internal.h"
#include "app_host.h"
#include "context_menu_renderer.h"
#include "render/font.h"
#include "render/pixels.h"
#include "window.h"
#include "peos/capability_parse.h"
#include "peos/path_scope_parse.h"
#include "peos/lang.h"
#include "i18n.h"
#include "keyboard_layout.h"
#include "desktop.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SL_APP_DIR "/system/apps"
// Apps installed later (appget, the kit's `make serve`): writable, also on
// the live ISO. A bundle with the name of a system one is left out.
#define SL_USER_APP_DIR "/apps"
#define SL_APP_DIR_COUNT 2u
static const char *const g_app_dirs[SL_APP_DIR_COUNT] = {SL_APP_DIR, SL_USER_APP_DIR};
#define SL_APP_MANIFEST "app.ini"
#define SL_APP_CATALOG_SCAN_INTERVAL_NS 1000000000ull
// With the app manager closed only the dock shows the apps: a look now and then.
#define SL_APP_CATALOG_SCAN_IDLE_NS 5000000000ull
#define SL_APP_DIRECTORY_BUFFER_INITIAL 4096u
#define SL_APP_DIRECTORY_BUFFER_MAX (1024u * 1024u)
#define SL_APP_CATALOG_INITIAL_CAP 16u
#define SL_WIN_ANIM_MAX 32u
#define SL_WIN_ANIM_OPEN_OFFSET_Y 28.0f
#define SL_WIN_ANIM_OPEN_MS 140u
#define SL_WIN_ANIM_CLOSE_MS 120u

#define SL_LAUNCHER_MARGIN_LEFT 20u
#define SL_LAUNCHER_RESERVED_GAP 12u
#define SL_LAUNCHER_PAD_X 10u
#define SL_LAUNCHER_PAD_Y 14u
#define SL_LAUNCHER_HEADER_H 12u
#define SL_LAUNCHER_ITEM_H 44u
#define SL_LAUNCHER_ITEM_GAP 10u
#define SL_LAUNCHER_ICON_SIZE 60u
#define SL_LAUNCHER_COLUMN_GAP 10u
#define SL_LAUNCHER_ARROW_GAP 2u
#define SL_LAUNCHER_ARROW_W 20u
#define SL_LAUNCHER_ARROW_H 40u
#define SL_LAUNCHER_ANIM_MS 220u
#define SL_LAUNCHER_TWEEN_ID 0xA1200000u
#define SL_LAUNCHER_TOOLTIP_PAD_X 8u
#define SL_LAUNCHER_TOOLTIP_W_MAX 210u
#define SL_LAUNCHER_TOOLTIP_H 22u
#define SL_APP_RESOURCE_MAX 16u

typedef struct {
    char key[48];
    char path[160];
} sl_bundle_resource_t;

typedef struct {
    char title[64];
    char exec[160];
    char args[160];
    char icon[160];
    uint32_t width;
    uint32_t height;
    uint64_t caps_mask;
    peos_path_grant_t paths[PEOS_PATH_MAX_GRANTS];
    uint32_t path_count;
    uint8_t category;               // SL_CAT_*: app.ini category=sistema|strumenti|svago
    // The body's limits when the window is resized (0: none) and whether it
    // may be (resizable=0: a fixed size).
    uint32_t min_width, min_height, max_width, max_height;
    uint8_t fixed_size;
    // dock=yes: among the apps the dock shows before anything is pinned;
    // any other app is in the app manager only, until the user pins it.
    uint8_t in_dock;
    // hidden=yes: not in the app manager nor its search (a window another
    // program opens, as File operations or Welcome); it still shows in the
    // dock while it runs.
    uint8_t hidden;
    char version[24];               // app.ini version= and author=: the app's card
    char author[96];
} sl_app_bundle_t;

// The app manager's filters (tavola App manager aperto): every app, or one
// category.
enum { SL_CAT_ALL = 0, SL_CAT_SYSTEM = 1, SL_CAT_TOOLS = 2, SL_CAT_FUN = 3, SL_CAT_COUNT = 4 };

typedef struct {
    sl_app_bundle_t app;
    char bundle_path[256];
    uint64_t signature;
} sl_scanned_app_t;

typedef struct {
    size_t slot;
    char title[64];
    char bundle_path[256];
    char icon_path[160];
    sl_image_t icon_image;
    uint8_t icon_loaded;
    uint8_t icon_failed;
    uint32_t width;
    uint32_t height;
    uint8_t category;               // SL_CAT_*
    uint32_t min_width, min_height, max_width, max_height;   // app.ini, body (0: none)
    uint8_t fixed_size;
    uint8_t in_dock;                // app.ini dock=yes
    uint8_t hidden;                 // app.ini hidden=yes: not in the app manager
    uint64_t signature;
    app_instance_t *instance;
    // Dock and app manager tiles (render/slsk.h), resolved on first draw.
#define SL_DOCK_TILE_SETS 5
    uint8_t tiles_ready;
    uint32_t tiles_version;         // theme version the tiles were resolved for
    uint8_t tile_custom;            // a baked icon of its own (tiles are all blank)
    const slsk_entry_t *tiles[SL_DOCK_TILE_SETS];
    const slsk_entry_t *icons[SL_DOCK_TILE_SETS];   // its baked icon at each set's size
    uint8_t *own_icon[2];           // its own icon scaled to 44 and 66 px, or NULL
    char initial[8];                // else its initial, on the blank tile
} sl_launcher_item_t;

typedef struct {
    uint8_t used;
    uint8_t closing;
    uint8_t close_ready;
    uint8_t _pad;
    app_instance_t *task;
    float y_value;
    float opacity;
} sl_window_anim_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint8_t valid;
} sl_window_rect_snapshot_t;

static sl_window_t *g_windows = NULL;
static size_t g_window_count = 0;
static size_t g_focused_window = (size_t)-1;
// A title bar button held down: which one (the action comes on the release).
static uint8_t g_title_press_active;
static int g_title_press_btn = SL_WIN_BTN_NONE;

static sl_launcher_item_t *g_launcher_items = NULL;
static size_t g_launcher_count = 0u;
static size_t g_launcher_cap = 0u;
static size_t g_launcher_hover_index = (size_t)-1;
static uint8_t g_launcher_ctx_open = 0u;
static size_t g_launcher_ctx_item_index = (size_t)-1;
static uint32_t g_launcher_ctx_x = 0u;
static uint32_t g_launcher_ctx_y = 0u;
static uint32_t g_launcher_ctx_w = 0u;
static uint32_t g_launcher_ctx_h = 0u;
static int32_t g_launcher_ctx_hover_row = -1;
// The popup lists either the actions of the menu or the lines of the
// Informazioni card; both are fixed when it opens.
#define SL_LAUNCHER_CTX_ROWS_MAX 16u
static uint8_t g_launcher_ctx_info = 0u;
static uint8_t g_launcher_ctx_desk = 0u;   // the desktop's menu (right click on the desktop), not an app's
static uint32_t g_launcher_ctx_rows = 0u;
static uint8_t g_launcher_ctx_actions[SL_LAUNCHER_CTX_ROWS_MAX];
static char g_launcher_info_lines[SL_LAUNCHER_CTX_ROWS_MAX][128];
static int32_t g_launcher_info_details_row = -1;   // the card's "Details" row (-1: none, details shown)
static uint8_t g_launcher_expanded = 0u;
static uint32_t g_launcher_extra_width = 0u;
static sl_window_rect_snapshot_t g_launcher_last_rect;
static uint64_t g_launcher_next_scan_ns = 0ull;
static uint8_t g_launcher_force_scan = 0u;      // the language changed: the titles too
static peos_stat_t g_launcher_dir_stat[SL_APP_DIR_COUNT];   // g_app_dirs as of the last catalog scan
static uint8_t g_launcher_dir_known = 0u;    // a bit per directory
static sl_window_anim_t g_window_anims[SL_WIN_ANIM_MAX];

static void sl_window_refresh_zorder(void);
static void sl_launcher_compact_label(const char *src, char *dst, size_t cap);
static size_t sl_window_index_for_task(const app_instance_t *task);
static void sl_focus_window(size_t index);
static void sl_focus_top_visible(void);
static int sl_window_is_closing(const sl_window_t *win);
static void sl_anim_start_close(app_instance_t *task, int32_t current_y);
static int sl_windows_open_launcher_item(sl_ctx_t *ctx, size_t launcher_index);

static sl_window_rect_snapshot_t sl_window_snapshot(const sl_window_t *win) {
    sl_window_rect_snapshot_t snap = {0u, 0u, 0u, 0u, 0u};
    if (!win || win->width == 0u || win->height == 0u) return snap;
    // Everything the window paints, shadow included.
    sl_win_rect_t o = sl_window_outer_rect(win);
    int64_t x0 = o.x < 0 ? 0 : o.x, y0 = o.y < 0 ? 0 : o.y;
    int64_t x1 = (int64_t)o.x + o.w, y1 = (int64_t)o.y + o.h;
    if (x1 <= x0 || y1 <= y0) return snap;
    snap.x = (uint32_t)x0;
    snap.y = (uint32_t)y0;
    snap.w = (uint32_t)(x1 - x0);
    snap.h = (uint32_t)(y1 - y0);
    snap.valid = 1u;
    return snap;
}

static sl_window_rect_snapshot_t sl_window_snapshot_index(size_t index) {
    if (!g_windows || index >= g_window_count) {
        sl_window_rect_snapshot_t empty = {0u, 0u, 0u, 0u, 0u};
        return empty;
    }
    return sl_window_snapshot(&g_windows[index]);
}

static void sl_dirty_add_snapshot(const sl_ctx_t *ctx, const sl_window_rect_snapshot_t *snap) {
    if (!ctx || !snap || !snap->valid) return;
    sl_dirty_add(ctx, snap->x, snap->y, snap->w, snap->h);
}

#define SL_LAUNCHER_CTX_ROW_H 30u
#define SL_LAUNCHER_CTX_MIN_W 108u
#define SL_LAUNCHER_CTX_PAD_X 14u

static uint32_t sl_anim_tween_id(size_t slot, uint32_t channel) {
    return (uint32_t)(0xA1100000u + ((uint32_t)slot * 8u) + channel);
}

static int32_t sl_anim_find_slot(app_instance_t *task) {
    if (!task) return -1;
    for (size_t i = 0u; i < SL_WIN_ANIM_MAX; i++) {
        if (g_window_anims[i].used && g_window_anims[i].task == task) return (int32_t)i;
    }
    return -1;
}

static int32_t sl_anim_find_or_alloc_slot(app_instance_t *task) {
    int32_t idx;
    if (!task) return -1;
    idx = sl_anim_find_slot(task);
    if (idx >= 0) return idx;
    for (size_t i = 0u; i < SL_WIN_ANIM_MAX; i++) {
        if (!g_window_anims[i].used) {
            g_window_anims[i].used = 1u;
            g_window_anims[i].closing = 0u;
            g_window_anims[i].close_ready = 0u;
            g_window_anims[i].task = task;
            g_window_anims[i].y_value = 0.0f;
            g_window_anims[i].opacity = 255.0f;
            return (int32_t)i;
        }
    }
    return -1;
}

static void sl_anim_clear_slot(size_t slot) {
    if (slot >= SL_WIN_ANIM_MAX) return;
    sl_tween_cancel_id(sl_anim_tween_id(slot, 0u));
    sl_tween_cancel_id(sl_anim_tween_id(slot, 1u));
    g_window_anims[slot].used = 0u;
    g_window_anims[slot].closing = 0u;
    g_window_anims[slot].close_ready = 0u;
    g_window_anims[slot].task = NULL;
    g_window_anims[slot].y_value = 0.0f;
    g_window_anims[slot].opacity = 255.0f;
}

static void sl_anim_clear_task(app_instance_t *task) {
    int32_t idx = sl_anim_find_slot(task);
    if (idx >= 0) sl_anim_clear_slot((size_t)idx);
}

static void sl_anim_on_close_done(uint32_t id, void *userdata) {
    (void)id;
    sl_window_anim_t *anim = (sl_window_anim_t *)userdata;
    if (!anim || !anim->used) return;
    anim->close_ready = 1u;
}

static void sl_anim_start_open(sl_window_t *win, app_instance_t *task, int32_t target_y) {
    if (!sl_tween_is_enabled()) {
        int32_t idx_now = sl_anim_find_slot(task);
        if (idx_now >= 0) sl_anim_clear_slot((size_t)idx_now);
        if (win) {
            win->y = (target_y < 0) ? 0u : (uint32_t)target_y;
            win->anim_opacity = 255u;
            win->force_redraw = 1u;
            win->next_render_ns = 0ull;
        }
        return;
    }

    int32_t idx = sl_anim_find_or_alloc_slot(task);
    if (idx < 0) return;
    sl_window_anim_t *anim = &g_window_anims[(size_t)idx];
    float target_yf = (float)target_y;
    anim->closing = 0u;
    anim->close_ready = 0u;
    anim->y_value = target_yf + SL_WIN_ANIM_OPEN_OFFSET_Y;
    anim->opacity = 0.0f;
    if (win) {
        int32_t y_px = (int32_t)(anim->y_value + 0.5f);
        if (y_px < 0) y_px = 0;
        win->y = (uint32_t)y_px;
        win->anim_opacity = 0u;
        win->force_redraw = 1u;
        win->next_render_ns = 0ull;
    }
    (void)sl_tween_to_f32(sl_anim_tween_id((size_t)idx, 0u), &anim->y_value, target_yf,
                          SL_WIN_ANIM_OPEN_MS, SL_TWEEN_EASE_OUT_CUBIC, NULL, NULL);
    (void)sl_tween_to_f32(sl_anim_tween_id((size_t)idx, 1u), &anim->opacity, 255.0f,
                          SL_WIN_ANIM_OPEN_MS, SL_TWEEN_EASE_OUT_CUBIC, NULL, NULL);
}

static void sl_anim_start_close(app_instance_t *task, int32_t current_y) {
    int32_t idx = sl_anim_find_or_alloc_slot(task);
    if (idx < 0) return;
    sl_window_anim_t *anim = &g_window_anims[(size_t)idx];
    float current_yf = (float)current_y;
    if (anim->closing) return;
    anim->closing = 1u;
    anim->close_ready = 0u;
    anim->y_value = current_yf;
    if (anim->opacity > 255.0f) anim->opacity = 255.0f;
    if (anim->opacity < 0.0f) anim->opacity = 0.0f;

    if (!sl_tween_is_enabled()) {
        anim->opacity = 0.0f;
        anim->close_ready = 1u;
        return;
    }

    (void)sl_tween_to_f32(sl_anim_tween_id((size_t)idx, 0u), &anim->y_value,
                          current_yf + SL_WIN_ANIM_OPEN_OFFSET_Y,
                          SL_WIN_ANIM_CLOSE_MS, SL_TWEEN_EASE_IN_OUT_CUBIC, NULL, NULL);
    (void)sl_tween_to_f32(sl_anim_tween_id((size_t)idx, 1u), &anim->opacity,
                          0.0f, SL_WIN_ANIM_CLOSE_MS,
                          SL_TWEEN_EASE_IN_OUT_CUBIC, sl_anim_on_close_done, anim);
}

static int sl_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *sl_trim(char *s) {
    if (!s) return s;
    while (*s && sl_is_space(*s)) s++;
    size_t n = strlen(s);
    while (n > 0u && sl_is_space(s[n - 1u])) {
        s[n - 1u] = '\0';
        n--;
    }
    return s;
}

static void sl_copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    size_t i = 0u;
    while (i + 1u < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int sl_parse_u32(const char *s, uint32_t *out) {
    if (!s || !out) return 0;
    uint64_t v = 0u;
    size_t i = 0u;
    if (s[0] == '\0') return 0;
    while (s[i]) {
        char c = s[i++];
        if (c < '0' || c > '9') return 0;
        v = v * 10u + (uint64_t)(c - '0');
        if (v > 0xFFFFFFFFu) return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int sl_has_suffix(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    if (n < m) return 0;
    return (memcmp(s + (n - m), suffix, m) == 0) ? 1 : 0;
}

static size_t sl_trim_trailing_slash(char *s) {
    if (!s) return 0u;
    size_t n = strlen(s);
    while (n > 0u && s[n - 1u] == '/') {
        s[n - 1u] = '\0';
        n--;
    }
    return n;
}

static int sl_join_path(char *out, size_t out_cap, const char *base, const char *name) {
    if (!out || out_cap == 0u || !base || !name) return 0;
    size_t w = 0u;
    while (base[w] && w + 1u < out_cap) {
        out[w] = base[w];
        w++;
    }
    if (base[w] != '\0') return 0;
    if (w == 0u || out[w - 1u] != '/') {
        if (w + 1u >= out_cap) return 0;
        out[w++] = '/';
    }
    size_t i = 0u;
    while (name[i] && w + 1u < out_cap) {
        out[w++] = name[i++];
    }
    if (name[i] != '\0') return 0;
    out[w] = '\0';
    return 1;
}

static int sl_resolve_bundle_path(const char *bundle_dir, const char *path_val,
                                  char *out, size_t out_cap) {
    if (!bundle_dir || !path_val || !out || out_cap == 0u) return 0;
    const char *val = path_val;
    while (*val == ' ') val++;
    if (*val == '\0') return 0;

    if (val[0] == '/') {
        sl_copy_str(out, out_cap, val);
        return out[0] != '\0';
    }

    if (val[0] == '.' && val[1] == '/') val += 2;
    return sl_join_path(out, out_cap, bundle_dir, val);
}

static int sl_resolve_exec_path(const char *bundle_dir, const char *exec_val,
                                char *out, size_t out_cap) {
    return sl_resolve_bundle_path(bundle_dir, exec_val, out, out_cap);
}

static const char *sl_resource_lookup(const sl_bundle_resource_t *resources,
                                      size_t resource_count,
                                      const char *key) {
    if (!resources || !key || key[0] == '\0') return NULL;
    for (size_t i = 0u; i < resource_count; i++) {
        if (strcmp(resources[i].key, key) == 0) {
            return resources[i].path;
        }
    }
    return NULL;
}

// The title in the chosen language: title= of the bundle's
// resources/<language>/String.lang, else of resources/en/, else app.ini's.
static void sl_localize_title(const char *bundle_dir, char *title, size_t cap) {
    const char *codes[2] = {sl_i18n_code(), PEOS_LANG_FALLBACK};
    for (uint32_t i = 0u; i < 2u; i++) {
        char path[320];
        snprintf(path, sizeof(path), "%s/resources/%s/" PEOS_LANG_FILE, bundle_dir, codes[i]);
        if (peos_is_file(path, strlen(path)) <= 0) continue;
        peos_lang_t lang = {0};
        if (peos_lang_load(&lang, path) == 0) {
            const char *t = peos_lang_get(&lang, "title");
            if (t && t[0]) {
                sl_copy_str(title, cap, t);
                peos_lang_free(&lang);
                return;
            }
        }
        peos_lang_free(&lang);
    }
}

static int sl_load_app_bundle(const char *manifest_path, const char *bundle_dir, sl_app_bundle_t *out) {
    if (!manifest_path || !out) return 0;
    FILE *f = fopen(manifest_path, "r");
    if (!f) return 0;

    peos_memset(out, 0, sizeof(*out));
    sl_copy_str(out->title, sizeof(out->title), "App");
    // Without a `capabilities` key an app only gets read access to the filesystem.
    out->caps_mask = PEOS_CAP_FS_READ;
    // Likewise the filesystem is read-only unless the manifest says otherwise.
    out->paths[0].rights = PEOS_PATH_READ;
    out->paths[0].prefix[0] = '/';
    out->paths[0].prefix[1] = '\0';
    out->path_count = 1u;

    char raw_exec[160];
    raw_exec[0] = '\0';
    char raw_icon[160];
    raw_icon[0] = '\0';
    sl_bundle_resource_t resources[SL_APP_RESOURCE_MAX];
    size_t resource_count = 0u;
    uint8_t section_resources = 0u;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = sl_trim(line);
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        if (p[0] == '[') {
            size_t n = strlen(p);
            if (n >= 2u && p[n - 1u] == ']') {
                p[n - 1u] = '\0';
                char *section = sl_trim(p + 1);
                section_resources = (strcmp(section, "resources") == 0) ? 1u : 0u;
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = sl_trim(p);
        char *val = sl_trim(eq + 1);

        if (section_resources) {
            if (resource_count >= SL_APP_RESOURCE_MAX) continue;
            if (key[0] == '\0' || val[0] == '\0') continue;
            sl_bundle_resource_t *res = &resources[resource_count];
            sl_copy_str(res->key, sizeof(res->key), key);
            if (bundle_dir && bundle_dir[0]) {
                if (!sl_resolve_bundle_path(bundle_dir, val, res->path, sizeof(res->path))) continue;
            } else {
                sl_copy_str(res->path, sizeof(res->path), val);
            }
            if (res->path[0] == '\0') continue;
            resource_count++;
            continue;
        }

        if (strcmp(key, "title") == 0) {
            sl_copy_str(out->title, sizeof(out->title), val);
        } else if (strcmp(key, "exec") == 0) {
            sl_copy_str(raw_exec, sizeof(raw_exec), val);
        } else if (strcmp(key, "args") == 0) {
            sl_copy_str(out->args, sizeof(out->args), val);
        } else if (strcmp(key, "icon") == 0) {
            sl_copy_str(raw_icon, sizeof(raw_icon), val);
        } else if (strcmp(key, "capabilities") == 0) {
            peos_cap_mask_t parsed = 0u;
            // An unknown capability name rejects the bundle instead of guessing.
            if (peos_cap_parse_list(val, &parsed) != 0) {
                fclose(f);
                return 0;
            }
            out->caps_mask = parsed;
        } else if (strcmp(key, "paths") == 0) {
            // A malformed path list rejects the bundle instead of guessing.
            int n = peos_path_parse_list(val, out->paths, PEOS_PATH_MAX_GRANTS);
            if (n < 0) {
                fclose(f);
                return 0;
            }
            out->path_count = (uint32_t)n;
        } else if (strcmp(key, "category") == 0) {
            out->category = strcmp(val, "sistema") == 0 ? SL_CAT_SYSTEM
                            : strcmp(val, "strumenti") == 0 ? SL_CAT_TOOLS
                            : strcmp(val, "svago") == 0 ? SL_CAT_FUN : SL_CAT_ALL;
        } else if (strcmp(key, "min_width") == 0) {
            (void)sl_parse_u32(val, &out->min_width);
        } else if (strcmp(key, "min_height") == 0) {
            (void)sl_parse_u32(val, &out->min_height);
        } else if (strcmp(key, "max_width") == 0) {
            (void)sl_parse_u32(val, &out->max_width);
        } else if (strcmp(key, "max_height") == 0) {
            (void)sl_parse_u32(val, &out->max_height);
        } else if (strcmp(key, "resizable") == 0) {
            out->fixed_size = (strcmp(val, "0") == 0 || strcmp(val, "no") == 0) ? 1u : 0u;
        } else if (strcmp(key, "dock") == 0) {
            out->in_dock = (strcmp(val, "1") == 0 || strcmp(val, "yes") == 0) ? 1u : 0u;
        } else if (strcmp(key, "hidden") == 0) {
            out->hidden = (strcmp(val, "1") == 0 || strcmp(val, "yes") == 0) ? 1u : 0u;
        } else if (strcmp(key, "version") == 0) {
            sl_copy_str(out->version, sizeof(out->version), val);
        } else if (strcmp(key, "author") == 0) {
            sl_copy_str(out->author, sizeof(out->author), val);
        } else if (strcmp(key, "width") == 0) {
            (void)sl_parse_u32(val, &out->width);
        } else if (strcmp(key, "height") == 0) {
            (void)sl_parse_u32(val, &out->height);
        }
    }

    fclose(f);
    if (raw_exec[0] == '\0') return 0;
    if (bundle_dir && bundle_dir[0]) sl_localize_title(bundle_dir, out->title, sizeof(out->title));
    if (bundle_dir && bundle_dir[0]) {
        if (!sl_resolve_exec_path(bundle_dir, raw_exec, out->exec, sizeof(out->exec))) {
            return 0;
        }
    } else {
        sl_copy_str(out->exec, sizeof(out->exec), raw_exec);
    }

    if (raw_icon[0] != '\0') {
        const char *icon_src = raw_icon;
        const char *resolved_icon = NULL;
        uint8_t icon_is_resource_ref = 0u;

        if (strncmp(icon_src, "res:", 4u) == 0) {
            icon_is_resource_ref = 1u;
            resolved_icon = sl_resource_lookup(resources, resource_count, icon_src + 4u);
        } else {
            resolved_icon = sl_resource_lookup(resources, resource_count, icon_src);
        }

        if (resolved_icon) {
            sl_copy_str(out->icon, sizeof(out->icon), resolved_icon);
        } else if (icon_is_resource_ref) {
            out->icon[0] = '\0';
        } else if (bundle_dir && bundle_dir[0]) {
            (void)sl_resolve_bundle_path(bundle_dir, icon_src, out->icon, sizeof(out->icon));
        } else {
            sl_copy_str(out->icon, sizeof(out->icon), icon_src);
        }
    }
    return (out->exec[0] != '\0') ? 1 : 0;
}

static uint64_t sl_catalog_hash_bytes(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t sl_catalog_hash_text(uint64_t hash, const char *text) {
    if (!text) return sl_catalog_hash_bytes(hash, "", 1u);
    return sl_catalog_hash_bytes(hash, text, strlen(text) + 1u);
}

static uint64_t sl_catalog_signature(const sl_scanned_app_t *entry) {
    uint64_t hash = 1469598103934665603ull;
    if (!entry) return hash;
    hash = sl_catalog_hash_text(hash, entry->bundle_path);
    hash = sl_catalog_hash_text(hash, entry->app.title);
    hash = sl_catalog_hash_text(hash, entry->app.exec);
    hash = sl_catalog_hash_bytes(hash, &entry->app.caps_mask, sizeof(entry->app.caps_mask));
    hash = sl_catalog_hash_bytes(hash, entry->app.paths, sizeof(entry->app.paths));
    hash = sl_catalog_hash_bytes(hash, &entry->app.path_count, sizeof(entry->app.path_count));
    hash = sl_catalog_hash_text(hash, entry->app.args);
    hash = sl_catalog_hash_text(hash, entry->app.icon);
    hash = sl_catalog_hash_bytes(hash, &entry->app.width, sizeof(entry->app.width));
    hash = sl_catalog_hash_bytes(hash, &entry->app.category, sizeof(entry->app.category));
    hash = sl_catalog_hash_bytes(hash, &entry->app.min_width, sizeof(entry->app.min_width) * 4u);
    hash = sl_catalog_hash_bytes(hash, &entry->app.fixed_size, sizeof(entry->app.fixed_size));
    hash = sl_catalog_hash_bytes(hash, &entry->app.hidden, sizeof(entry->app.hidden));
    return sl_catalog_hash_bytes(hash, &entry->app.height, sizeof(entry->app.height));
}

static int sl_scan_reserve(sl_scanned_app_t **entries, size_t *cap, size_t needed) {
    if (!entries || !cap) return 0;
    if (needed <= *cap) return 1;
    size_t next_cap = *cap ? *cap : SL_APP_CATALOG_INITIAL_CAP;
    while (next_cap < needed) {
        if (next_cap > SIZE_MAX / 2u) return 0;
        next_cap *= 2u;
    }
    if (next_cap > SIZE_MAX / sizeof(**entries)) return 0;
    sl_scanned_app_t *next = (sl_scanned_app_t *)realloc(
        *entries, next_cap * sizeof(**entries));
    if (!next) return 0;
    *entries = next;
    *cap = next_cap;
    return 1;
}

static char *sl_read_app_directory(const char *dir, size_t *out_len) {
    size_t cap = SL_APP_DIRECTORY_BUFFER_INITIAL;
    if (out_len) *out_len = 0u;

    while (cap <= SL_APP_DIRECTORY_BUFFER_MAX) {
        char *buf = (char *)malloc(cap);
        if (!buf) return NULL;
        uint64_t result = peos_readdir(dir, strlen(dir), buf, cap - 1u);
        if ((int64_t)result < 0) {
            free(buf);
            return NULL;
        }
        size_t len = (size_t)result;
        if (len < cap - 1u || cap == SL_APP_DIRECTORY_BUFFER_MAX) {
            if (len >= cap) len = cap - 1u;
            buf[len] = '\0';
            if (out_len) *out_len = len;
            return buf;
        }
        free(buf);
        if (cap > SL_APP_DIRECTORY_BUFFER_MAX / 2u) cap = SL_APP_DIRECTORY_BUFFER_MAX;
        else cap *= 2u;
    }
    return NULL;
}

// a before b, ignoring case.
static int sl_title_before(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb || !ca) return (unsigned char)ca < (unsigned char)cb;
    }
}

// The bundles of one directory, appended to entries. `system_count` entries
// (from SL_APP_DIR) come first: a bundle with one of their names is left out.
// 0: the directory cannot be read, or out of memory (entries is then freed).
static int sl_scan_app_dir(const char *dir, sl_scanned_app_t **entries, size_t *count, size_t *cap,
                           size_t system_count) {
    size_t list_len = 0u;
    char *list = sl_read_app_directory(dir, &list_len);
    if (!list) return 0;

    size_t pos = 0u;
    while (pos < list_len) {
        size_t start = pos;
        while (pos < list_len && list[pos] != '\n' && list[pos] != '\r') pos++;
        size_t len = pos - start;
        while (pos < list_len && (list[pos] == '\n' || list[pos] == '\r')) pos++;
        if (len == 0u || len >= 128u) continue;

        char name[128];
        peos_memcpy(name, &list[start], len);
        name[len] = '\0';
        if (sl_trim_trailing_slash(name) == 0u || !sl_has_suffix(name, ".app")) continue;

        uint8_t shadowed = 0u;
        for (size_t k = 0u; k < system_count && !shadowed; k++) {
            const char *base = (*entries)[k].bundle_path + sizeof(SL_APP_DIR);   // past "/system/apps/"
            shadowed = strcmp(base, name) == 0;
        }
        if (shadowed) continue;

        sl_scanned_app_t entry;
        peos_memset(&entry, 0, sizeof(entry));
        if (!sl_join_path(entry.bundle_path, sizeof(entry.bundle_path), dir, name)) continue;

        char manifest_path[320];
        if (!sl_join_path(manifest_path, sizeof(manifest_path), entry.bundle_path,
                          SL_APP_MANIFEST)) {
            continue;
        }
        if (!sl_load_app_bundle(manifest_path, entry.bundle_path, &entry.app) &&
            !sl_load_app_bundle(entry.bundle_path, NULL, &entry.app)) {
            continue;
        }
        entry.signature = sl_catalog_signature(&entry);
        peos_stat_t stat_info;
        peos_memset(&stat_info, 0, sizeof(stat_info));
        if ((int64_t)peos_stat(manifest_path, strlen(manifest_path),
                               &stat_info, sizeof(stat_info)) >= 0) {
            entry.signature = sl_catalog_hash_bytes(entry.signature, &stat_info,
                                                     sizeof(stat_info));
        }
        if (entry.app.icon[0] != '\0') {
            peos_memset(&stat_info, 0, sizeof(stat_info));
            if ((int64_t)peos_stat(entry.app.icon, strlen(entry.app.icon),
                                   &stat_info, sizeof(stat_info)) >= 0) {
                entry.signature = sl_catalog_hash_bytes(entry.signature, &stat_info,
                                                         sizeof(stat_info));
            }
        }
        if (!sl_scan_reserve(entries, cap, *count + 1u)) {
            free(*entries);
            *entries = NULL;
            free(list);
            return 0;
        }
        (*entries)[(*count)++] = entry;
    }

    free(list);
    return 1;
}

static int sl_scan_apps_from_system(sl_scanned_app_t **out_entries, size_t *out_count) {
    if (!out_entries || !out_count) return 0;
    *out_entries = NULL;
    *out_count = 0u;

    sl_scanned_app_t *entries = NULL;
    size_t count = 0u;
    size_t cap = 0u;
    // No system apps: not a catalog to show. Without /apps, the system ones.
    if (!sl_scan_app_dir(SL_APP_DIR, &entries, &count, &cap, 0u)) {
        free(entries);
        return 0;
    }
    size_t system_count = count;
    if (!sl_scan_app_dir(SL_USER_APP_DIR, &entries, &count, &cap, system_count) && !entries) {
        // out of memory (an unreadable /apps leaves entries as they were)
        return 0;
    }

    // By title, whatever order the file system lists them in (the tar, the
    // live ramfs and XFS differ): the dock and the manager look the same
    // everywhere.
    for (size_t i = 1u; i < count; i++) {
        sl_scanned_app_t e = entries[i];
        size_t j = i;
        while (j > 0u && sl_title_before(e.app.title, entries[j - 1u].app.title)) {
            entries[j] = entries[j - 1u];
            j--;
        }
        entries[j] = e;
    }
    *out_entries = entries;
    *out_count = count;
    return 1;
}

static void sl_launcher_items_reset(void) {
    for (size_t i = 0u; i < g_launcher_count; i++) {
        if (g_launcher_items[i].icon_image.data) {
            sl_image_free(&g_launcher_items[i].icon_image);
        }
        free(g_launcher_items[i].own_icon[0]);
        free(g_launcher_items[i].own_icon[1]);
    }
    if (g_launcher_items && g_launcher_cap > 0u) {
        peos_memset(g_launcher_items, 0,
                    sizeof(*g_launcher_items) * g_launcher_cap);
    }
    g_launcher_count = 0u;
    g_launcher_hover_index = (size_t)-1;
    g_launcher_ctx_open = 0u;
    g_launcher_ctx_item_index = (size_t)-1;
    g_launcher_ctx_x = 0u;
    g_launcher_ctx_y = 0u;
    g_launcher_ctx_w = 0u;
    g_launcher_ctx_h = 0u;
    g_launcher_expanded = 0u;
    sl_tween_set_u32_immediate(&g_launcher_extra_width, 0u);
    g_launcher_last_rect.valid = 0u;
}

static int sl_launcher_reserve(size_t needed) {
    if (needed <= g_launcher_cap) return 1;
    size_t cap = g_launcher_cap ? g_launcher_cap : SL_APP_CATALOG_INITIAL_CAP;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) return 0;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(*g_launcher_items)) return 0;
    sl_launcher_item_t *items =
        (sl_launcher_item_t *)realloc(g_launcher_items, cap * sizeof(*items));
    if (!items) return 0;
    peos_memset(items + g_launcher_cap, 0,
                (cap - g_launcher_cap) * sizeof(*items));
    g_launcher_items = items;
    g_launcher_cap = cap;
    return 1;
}

static int sl_launcher_add_item(size_t slot, const sl_app_bundle_t *app,
                                const char *bundle_path) {
    if (!app || !sl_launcher_reserve(g_launcher_count + 1u)) return 0;
    sl_launcher_item_t *item = &g_launcher_items[g_launcher_count++];
    item->slot = slot;
    item->width = app->width;
    item->height = app->height;
    item->category = app->category;
    item->min_width = app->min_width;
    item->min_height = app->min_height;
    item->max_width = app->max_width;
    item->max_height = app->max_height;
    item->fixed_size = app->fixed_size;
    item->in_dock = app->in_dock;
    item->hidden = app->hidden;
    item->instance = NULL;
    item->icon_loaded = 0u;
    item->icon_failed = 0u;
    sl_copy_str(item->title, sizeof(item->title), app->title);
    sl_copy_str(item->bundle_path, sizeof(item->bundle_path), bundle_path);
    sl_copy_str(item->icon_path, sizeof(item->icon_path), app->icon);
    sl_scanned_app_t entry;
    peos_memset(&entry, 0, sizeof(entry));
    entry.app = *app;
    sl_copy_str(entry.bundle_path, sizeof(entry.bundle_path), bundle_path);
    item->signature = sl_catalog_signature(&entry);
    if (item->title[0] == '\0') {
        sl_copy_str(item->title, sizeof(item->title), "App");
    }
    return 1;
}

static void sl_launcher_try_load_icon(sl_launcher_item_t *item);

static int sl_point_in_rect(uint32_t px, uint32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

// A context menu (opaque, old style) was or will be at this rect.
static void sl_launcher_invalidate_overlay(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!ctx || w == 0u || h == 0u) return;
    // with the popup's shadow (the raised card's, past its edges)
    sl_dirty_add(ctx, x > 24u ? x - 24u : 0u, y > 24u ? y - 24u : 0u, w + 48u, h + 48u);
}

// ------------------------------------------------------------------- dock
//
// The soft UI dock (design/starlight-soft-ui, tavole Desktop e App manager):
// a raised pill on the left, centred, with the app tiles baked by slskin
// (rest, under the mouse, pressed) and Impostazioni after a separator. A fin
// grows out of its right edge while the mouse is on it; a click widens the
// dock into the app manager, a grid of every app in the folder.
//
// Everything is a ready sprite: the dock and its tiles blend over the
// wallpaper, so they are drawn only inside dirty rects.

#define SL_DOCK_X 28u
#define SL_DOCK_W 92u
#define SL_DOCK_PAD 22u
#define SL_DOCK_TILE 60u
#define SL_DOCK_HI 66u
#define SL_DOCK_GAP 18u
// On a short screen the dock packs its tiles closer before it leaves any out.
#define SL_DOCK_PAD_TIGHT 14u
#define SL_DOCK_GAP_TIGHT 8u
#define SL_DOCK_SEP_W 44u
#define SL_DOCK_MARGIN 28u          // from the screen edges, top and bottom
#define SL_DOCK_MAX 32u
#define SL_FIN_W 26u
#define SL_FIN_H 146u
#define SL_PILL_H 36u
#define SL_PILL_PAD 18u
// The app manager (the dock fully widened).
#define SL_MGR_RIGHT 46u            // margin to the right edge of the screen
#define SL_MGR_LINE_X 112u          // the vertical line, from the dock's left edge
#define SL_MGR_GRID_X 152u          // the grid, from the dock's left edge
#define SL_MGR_TILE 96u
#define SL_MGR_HI 104u
#define SL_MGR_CELL_MIN 150u
#define SL_MGR_COLS_MAX 8u
#define SL_MGR_ROW_H (SL_MGR_TILE + 12u + 20u + 34u)
#define SL_MGR_ITEMS_MAX 128u
// The header row: the category pill and the search field (tavola App
// manager aperto: 50 px high, the field 340 px wide).
#define SL_MGR_HDR_H 50u
#define SL_MGR_SEG_PAD 6u
#define SL_MGR_SEG_CELL_PAD 20u
#define SL_MGR_SEARCH_W 340u
#define SL_MGR_SEARCH_MIN_W 200u
#define SL_DOCK_ANIM_FULL 1000u     // g_launcher_extra_width: 0 = dock, 1000 = app manager

static struct {
    uint8_t ready;
    uint32_t theme_version;          // the sprites below are this theme's
    const slsk_entry_t *panel, *pill, *fin, *chev_open, *chev_close;
    const slsk_entry_t *hi_pad_ref;   // a dock-hi tile, for its shadow margins
    const slsk_entry_t *ring, *ring_big;   // the accent ring of dock-hi, manager-hi
    const slsk_entry_t *search_icon;       // the app manager's magnifier
    const slsk_font_t *label, *initial, *initial_big, *title, *search;
} g_dock;

static uint8_t g_dock_hot = 0u;            // the mouse is on the dock: the fin shows
static size_t g_dock_pressed = (size_t)-1; // item whose tile is pressed
// An app held down in the dock or the app manager (docs/system-apps-ux-plan.md,
// Starlight big item 6): let go where it was, a click; moved, it follows the
// mouse, and let go on the dock it is pinned there (or moved there).
static struct {
    uint8_t armed;      // held down: the click comes on the release
    uint8_t active;     // moved far enough: a drag
    uint8_t from_mgr;   // taken in the open app manager
    uint8_t from_dock;  // from a dock tile (the manager open or not): dragged off, it leaves the dock
    uint8_t movable;    // it may be pinned (not Impostazioni)
    size_t item;
    uint32_t sx, sy;    // where it was taken
    uint32_t x, y;      // the mouse now
    int32_t slot;       // where it would go among the pinned tiles (-1: not over the dock)
} g_app_drag;
#define SL_APP_DRAG_START 8u   // px the mouse moves before a press is a drag
static uint64_t g_dock_indicators = 0u;    // running/focused bits last drawn
static uint64_t g_dock_items_sig = 0u;     // the tiles last drawn (which apps, in order)
static uint8_t g_dock_hover_in_mgr = 0u;   // the hovered item is a manager tile, not a dock one

static int sl_dock_ready(const sl_ctx_t *ctx) {
    if (g_dock.ready && g_dock.theme_version == sl_ui_runtime_version()) return g_dock.ready == 1u;
    g_dock.ready = 2u;
    g_dock.theme_version = sl_ui_runtime_version();
    uint8_t night = sl_ui_runtime_night();
    if (!ctx || !ctx->skin_loaded) return 0;
    const sl_skin_t *s = &ctx->skin;
    const slsk_entry_t *f1 = sl_skin_find(s, "ui-15");
    const slsk_entry_t *f2 = sl_skin_find(s, "ui-22-mid");
    const slsk_entry_t *f3 = sl_skin_find(s, "ui-34-light");
    const slsk_entry_t *f4 = sl_skin_find(s, "ui-30-light");
    g_dock.panel = sl_skin_find(s, night ? "dock-n-a" : "dock-a");
    g_dock.pill = sl_skin_find(s, night ? "pill-n-a" : "pill-a");
    g_dock.fin = sl_skin_find(s, night ? "fin-n" : "fin");
    g_dock.chev_open = sl_skin_find(s, "chev-right");
    g_dock.chev_close = sl_skin_find(s, "chev-left");
    g_dock.hi_pad_ref = sl_skin_find(s, "dock-hi-blank");
    g_dock.ring = sl_skin_find(s, "ring-66");
    g_dock.ring_big = sl_skin_find(s, "ring-104");
    g_dock.search_icon = sl_skin_find(s, "search-18");
    const slsk_entry_t *f5 = sl_skin_find(s, "ui-16");
    if (!f1 || !f2 || !f3 || !f4 || !f5 || !g_dock.panel || !g_dock.pill || !g_dock.ring || !g_dock.ring_big ||
        !g_dock.search_icon ||
        !g_dock.fin || !g_dock.chev_open || !g_dock.chev_close || !g_dock.hi_pad_ref) {
        peos_puts("starlight: skin lacks the dock\n");
        return 0;
    }
    g_dock.label = sl_skin_font(s, f1);
    g_dock.initial = sl_skin_font(s, f2);
    g_dock.title = sl_skin_font(s, f3);
    g_dock.initial_big = sl_skin_font(s, f4);
    g_dock.search = sl_skin_font(s, f5);
    g_dock.ready = 1u;
    return 1;
}

// The baked tile set of an app: by its bundle (<id>.app), as the icons are
// named; not by the title, which follows the language.
static const char *sl_dock_skin_key(const char *bundle_path) {
    static const struct { const char *id, *key; } map[] = {
        { "terminale", "orbit" }, { "voyager", "voyager" }, { "calcolatrice", "calcolatrice" },
        { "curiosity", "curiosity" }, { "impostazioni", "impostazioni" }, { "installer", "installer" },
        { "monitor", "monitor" }, { "calendario", "calendario" }, { "consigli", "consigli" },
        { "pegasus", "pegasus" }, { "orion", "orion" }, { "pestore", "pestore" },
    };
    const char *base = bundle_path ? strrchr(bundle_path, '/') : NULL;
    base = base ? base + 1 : (bundle_path ? bundle_path : "");
    for (size_t k = 0u; k < sizeof(map) / sizeof(map[0]); k++) {
        size_t n = strlen(map[k].id);
        if (strncmp(base, map[k].id, n) == 0 && strcmp(base + n, ".app") == 0) return map[k].key;
    }
    return NULL;
}

// An icon of the app's own, scaled once to size x size, premultiplied BGRA.
static uint8_t *sl_dock_scale_icon(const sl_image_t *img, uint32_t size) {
    if (!img || !img->data || img->width == 0u || img->height == 0u || img->channels < 3u) return NULL;
    uint8_t *out = (uint8_t *)malloc((size_t)size * size * 4u);
    if (!out) return NULL;
    for (uint32_t y = 0; y < size; y++) {
        uint32_t sy0 = y * img->height / size, sy1 = (y + 1u) * img->height / size;
        if (sy1 <= sy0) sy1 = sy0 + 1u;
        for (uint32_t x = 0; x < size; x++) {
            uint32_t sx0 = x * img->width / size, sx1 = (x + 1u) * img->width / size;
            if (sx1 <= sx0) sx1 = sx0 + 1u;
            uint32_t acc[4] = { 0u, 0u, 0u, 0u }, n = 0u;
            for (uint32_t sy = sy0; sy < sy1 && sy < img->height; sy++) {
                const uint8_t *row = img->data + (size_t)sy * img->stride;
                for (uint32_t sx = sx0; sx < sx1 && sx < img->width; sx++) {
                    const uint8_t *p = row + (size_t)sx * img->channels;
                    uint32_t a = img->channels >= 4u ? p[3] : 255u;
                    acc[0] += p[2] * a;   // B
                    acc[1] += p[1] * a;   // G
                    acc[2] += p[0] * a;   // R
                    acc[3] += a;
                    n++;
                }
            }
            uint8_t *d = out + ((size_t)y * size + x) * 4u;
            d[0] = (uint8_t)(acc[0] / (255u * n));
            d[1] = (uint8_t)(acc[1] / (255u * n));
            d[2] = (uint8_t)(acc[2] / (255u * n));
            d[3] = (uint8_t)(acc[3] / n);
        }
    }
    return out;
}

// Resolve once: the item's tiles, or a blank tile with its icon or initial.
static void sl_dock_resolve(const sl_ctx_t *ctx, sl_launcher_item_t *item) {
    // Resolved again when the theme changes: night has its own tiles.
    if ((item->tiles_ready && item->tiles_version == sl_ui_runtime_version()) || !ctx->skin_loaded) return;
    item->tiles_ready = 1u;
    item->tiles_version = sl_ui_runtime_version();
    static const char *day[SL_DOCK_TILE_SETS] = { "dock", "dock-hi", "dock-down", "manager", "manager-hi" };
    static const char *night[SL_DOCK_TILE_SETS] = { "dock-n", "dock-hi-n", "dock-down", "manager-n", "manager-hi-n" };
    static const char *icon_set[SL_DOCK_TILE_SETS] = { "icon44", "icon48", "icon41", "icon66", "icon72" };
    const char *const *sets = sl_ui_runtime_night() ? night : day;
    const char *key = sl_dock_skin_key(item->bundle_path);
    item->tile_custom = 0u;
    for (int k = 0; k < SL_DOCK_TILE_SETS; k++) {
        char name[SLSK_NAME_MAX];
        snprintf(name, sizeof(name), "%s-blank", sets[k]);
        item->tiles[k] = sl_skin_find(&ctx->skin, name);
        item->icons[k] = NULL;
        if (key) {
            snprintf(name, sizeof(name), "%s-%s", icon_set[k], key);
            item->icons[k] = sl_skin_find(&ctx->skin, name);
        }
        if (k == 0) item->tile_custom = (uint8_t)(item->icons[k] != NULL);
    }
    if (item->tile_custom) return;
    // No baked tile: its own icon if it has one, else its initial. Scaled
    // once; read again only when the scaled copies are gone (the decoded
    // image is freed after scaling, so "loaded" alone does not mean it is there).
    if (!item->own_icon[0]) {
        if (!item->icon_image.data) item->icon_loaded = 0u;
        sl_launcher_try_load_icon(item);
        if (item->icon_loaded && item->icon_image.data) {
            item->own_icon[0] = sl_dock_scale_icon(&item->icon_image, 44u);
            item->own_icon[1] = sl_dock_scale_icon(&item->icon_image, 66u);
            sl_image_free(&item->icon_image);   // the scaled copies are all that is drawn
        }
    }
    unsigned char c = (unsigned char)item->title[0];
    item->initial[0] = (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    item->initial[1] = '\0';
    if (c >= 0x80u) {   // keep a whole UTF-8 character
        size_t n = 1u;
        while (n < sizeof(item->initial) - 1u && ((unsigned char)item->title[n] & 0xC0u) == 0x80u) {
            item->initial[n] = item->title[n];
            n++;
        }
        item->initial[n] = '\0';
    }
}

static size_t sl_dock_settings_item(void) {
    for (size_t i = 0u; i < g_launcher_count; i++) {
        const char *key = sl_dock_skin_key(g_launcher_items[i].bundle_path);
        if (key && strcmp(key, "impostazioni") == 0) return i;
    }
    return (size_t)-1;
}

// The apps pinned to the dock, by bundle path, top to bottom. Until
// dock.cfg exists the dock takes the first apps that fit; the first pin or
// unpin starts from what it shows.
static char g_dock_pins[SL_DOCK_MAX][256];
static size_t g_dock_pin_count = 0u;
static uint8_t g_dock_pins_set = 0u;

static size_t sl_launcher_find_bundle(const char *bundle_path);
static size_t sl_launcher_find_id(const char *id);

static void sl_dock_pins_load(void) {
    g_dock_pin_count = 0u;
    g_dock_pins_set = 0u;
    FILE *f = fopen(SL_RESOURCE_DOCK_FILE, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0u && (line[n - 1u] == '\n' || line[n - 1u] == '\r' || line[n - 1u] == ' ')) line[--n] = '\0';
        if (n == 0u || line[0] == '#' || g_dock_pin_count >= SL_DOCK_MAX) continue;
        sl_copy_str(g_dock_pins[g_dock_pin_count++], sizeof(g_dock_pins[0]), line);
    }
    fclose(f);
    g_dock_pins_set = 1u;
}

static void sl_dock_pins_save(void) {
    FILE *f = fopen(SL_RESOURCE_DOCK_FILE, "w");
    if (!f) return;
    (void)fprintf(f, "# Starlight dock: the pinned app bundles, top to bottom.\n");
    for (size_t i = 0u; i < g_dock_pin_count; i++) (void)fprintf(f, "%s\n", g_dock_pins[i]);
    fclose(f);
}

static size_t sl_dock_pin_find(const char *bundle_path) {
    for (size_t i = 0u; i < g_dock_pin_count; i++) {
        if (strcmp(g_dock_pins[i], bundle_path) == 0) return i;
    }
    return (size_t)-1;
}

// How many tiles fit in the dock, Impostazioni excluded.
static size_t sl_dock_capacity(const sl_ctx_t *ctx, size_t settings, uint32_t pad, uint32_t gap) {
    uint32_t fh = ctx->fb.height;
    uint32_t room = fh > 2u * (SL_DOCK_MARGIN + pad) + gap + 2u
                        ? fh - 2u * (SL_DOCK_MARGIN + pad) - gap - 2u
                        : 0u;
    size_t cap = (room + gap) / (SL_DOCK_TILE + gap);
    if (cap < 1u) cap = 1u;
    if (cap > SL_DOCK_MAX) cap = SL_DOCK_MAX;
    return settings != (size_t)-1 ? cap - 1u : cap;
}

typedef struct {
    size_t count;                   // tiles in the dock
    size_t pinned;                  // the first ones: the pinned apps (then the open ones, then Impostazioni)
    size_t item[SL_DOCK_MAX];       // launcher item of each
    uint32_t tile_y[SL_DOCK_MAX];
    uint8_t has_sep;
    uint32_t sep_y;
    int32_t gap_at;                 // an empty place among the pinned, for a dragged app (-1: none)
    uint32_t cell0, cell_h;         // the first tile's top, and a tile with its gap
    uint32_t px, py, pw, ph;        // the dock
    uint32_t ex, ey, ew, eh;        // the app manager
    uint32_t cx, cy, cw, ch;        // now (while it widens or narrows)
    uint32_t gx, gy, gw, cols, cell_w, grid_top;
    uint32_t grid_bottom;           // the grid shows tiles between grid_top and this
    uint32_t scroll, max_scroll;    // the grid scrolled up by, in pixels
    size_t mgr_count;               // the app manager's tiles: the apps that pass the filters
    size_t mgr_item[SL_MGR_ITEMS_MAX];
} sl_dock_layout_t;

// The app manager's filters: the search text (typed while it is open) and
// the category.
static char g_mgr_query[40];
static uint8_t g_mgr_cat = SL_CAT_ALL;
static uint32_t g_mgr_scroll = 0u;   // the grid, scrolled (more apps than fit)
static uint8_t g_mgr_shift = 0u, g_mgr_altgr = 0u;
static const char *const g_mgr_cat_keys[SL_CAT_COUNT] = { "apps.cat.all", "apps.cat.system", "apps.cat.tools",
                                                          "apps.cat.fun" };
#define g_mgr_cat_labels(c) sl_tr(g_mgr_cat_keys[c])

// For the search: lower case, and the Latin-1 accented letters (UTF-8
// C3 xx) as their plain letter, so "citta" finds "Città"; up to ".app".
static void sl_mgr_fold(const char *in, char *out, size_t cap) {
    // C3 80..9F (upper) and A0..BF (lower) by (byte & 0x1F); '.' keeps it
    static const char k_plain[] = "aaaaaaaceeeeiiiidnooooo.ouuuuy..";
    size_t o = 0u;
    for (size_t i = 0u; in[i] && o + 2u < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '.' && strcmp(in + i, ".app") == 0) break;
        if (c == 0xC3u && (unsigned char)in[i + 1] >= 0x80u && (unsigned char)in[i + 1] <= 0xBFu) {
            char p = k_plain[(unsigned char)in[i + 1] & 0x1Fu];
            if (p != '.') {
                out[o++] = p;
                i++;
                continue;
            }
        }
        out[o++] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    out[o] = '\0';
}

// The query in the title or in the bundle's name, case and accents aside.
static int sl_mgr_matches(const sl_launcher_item_t *item, const char *needle) {
    if (!needle[0]) return 1;
    char n[sizeof(g_mgr_query) * 2u], h[sizeof(item->title)];
    sl_mgr_fold(needle, n, sizeof(n));
    sl_mgr_fold(item->title, h, sizeof(h));
    if (strstr(h, n)) return 1;
    const char *base = strrchr(item->bundle_path, '/');
    sl_mgr_fold(base ? base + 1 : item->bundle_path, h, sizeof(h));
    return h[0] && strstr(h, n) != NULL;
}

static void sl_mgr_filter(sl_dock_layout_t *L) {
    L->mgr_count = 0u;
    for (size_t i = 0u; i < g_launcher_count && L->mgr_count < SL_MGR_ITEMS_MAX; i++) {
        const sl_launcher_item_t *item = &g_launcher_items[i];
        if (item->hidden) continue;
        // A search looks in every category (the chosen one stays for later).
        if (!g_mgr_query[0] && g_mgr_cat != SL_CAT_ALL && item->category != g_mgr_cat) continue;
        if (!sl_mgr_matches(item, g_mgr_query)) continue;
        L->mgr_item[L->mgr_count++] = i;
    }
}

static uint32_t sl_lerp_u32(uint32_t a, uint32_t b, uint32_t t) {
    int64_t v = (int64_t)a + ((int64_t)b - (int64_t)a) * (int64_t)t / (int64_t)SL_DOCK_ANIM_FULL;
    return (uint32_t)(v < 0 ? 0 : v);
}

// The dock as it is, or as it shows during a drag: without <hide> (the app
// being dragged), with an empty place at <gap_at> among the pinned tiles
// (-1: none; none either when the dock has no room for one more).
static void sl_dock_layout_ex(const sl_ctx_t *ctx, sl_dock_layout_t *L, size_t hide, int32_t gap_at) {
    peos_memset(L, 0, sizeof(*L));
    L->gap_at = -1;
    if (!ctx || g_launcher_count == 0u) return;
    uint32_t fh = ctx->fb.height, fw = ctx->fb.width;

    // The pinned apps (else the first ones) as far as they fit; Impostazioni
    // always last, after a separator.
    size_t settings = sl_dock_settings_item();
    // As many as fit packed tight; the design's spacing if they fit with it.
    size_t take = sl_dock_capacity(ctx, settings, SL_DOCK_PAD_TIGHT, SL_DOCK_GAP_TIGHT);
    size_t pins[SL_DOCK_MAX], npins = 0u;
    if (g_dock_pins_set) {
        for (size_t p = 0u; p < g_dock_pin_count && npins < SL_DOCK_MAX; p++) {
            size_t i = sl_launcher_find_bundle(g_dock_pins[p]);
            if (i != (size_t)-1 && i != settings) pins[npins++] = i;
        }
    }
    // No pins yet, or none of them left and nothing else to show: the apps
    // that asked to be there (dock=yes), in order.
    if (!g_dock_pins_set || (npins == 0u && settings == (size_t)-1)) {
        for (size_t i = 0u; i < g_launcher_count && npins < SL_DOCK_MAX; i++) {
            if (i != settings && g_launcher_items[i].in_dock) pins[npins++] = i;
        }
    }
    // The app being dragged leaves its place: it follows the mouse.
    uint8_t hid_pinned = 0u;
    if (hide != (size_t)-1) {
        size_t n = 0u;
        for (size_t p = 0u; p < npins; p++) {
            if (pins[p] == hide) hid_pinned = p < take ? 1u : 0u;   // past a full dock's end: not shown
            else pins[n++] = pins[p];
        }
        npins = n;
    }
    // The empty place for it takes the room of one tile.
    if (gap_at >= 0 && (hid_pinned || npins < take) && take > 1u) take--;
    else gap_at = -1;
    // The apps open and not pinned, after the pinned ones, for as long as
    // they are open (a minimized window is found again there). They come
    // first when the dock is full: the last pinned ones give way.
    // An open app counts as shown only when its pinned tile fits; one pinned
    // but past the end of a full dock gets an open tile too. The two depend
    // on each other: a few rounds settle them.
    size_t open[SL_DOCK_MAX], nopen = 0u, shown_pins = npins < take ? npins : take;
    for (int round = 0; round < 4; round++) {
        nopen = 0u;
        for (size_t i = 0u; i < g_launcher_count && nopen < SL_DOCK_MAX; i++) {
            if (i == settings || i == hide) continue;
            app_instance_t *app = sl_app_host_instance_peek_at(g_launcher_items[i].slot);
            if (!app || sl_window_index_for_task(app) == (size_t)-1) continue;
            uint8_t pinned = 0u;
            for (size_t p = 0u; p < shown_pins && !pinned; p++) pinned = pins[p] == i ? 1u : 0u;
            if (!pinned) open[nopen++] = i;
        }
        if (nopen > take) nopen = take;
        size_t fit = take - nopen;
        size_t next = npins < fit ? npins : fit;
        if (next == shown_pins) break;
        shown_pins = next;
    }
    for (size_t p = 0u; p < shown_pins; p++) L->item[L->count++] = pins[p];
    L->pinned = L->count;
    for (size_t o = 0u; o < nopen; o++) L->item[L->count++] = open[o];
    if (gap_at > (int32_t)L->pinned) gap_at = (int32_t)L->pinned;
    size_t extra = gap_at >= 0 ? 1u : 0u;
    uint8_t tight = L->count + extra > sl_dock_capacity(ctx, settings, SL_DOCK_PAD, SL_DOCK_GAP);
    uint32_t pad = tight ? SL_DOCK_PAD_TIGHT : SL_DOCK_PAD, gap = tight ? SL_DOCK_GAP_TIGHT : SL_DOCK_GAP;
    L->has_sep = (settings != (size_t)-1 && L->count > 0u) ? 1u : 0u;
    if (settings != (size_t)-1) L->item[L->count++] = settings;
    if (L->count == 0u) return;

    size_t cells = L->count + extra;
    uint32_t h = 2u * pad + (uint32_t)cells * SL_DOCK_TILE + (uint32_t)(cells - 1u) * gap;
    if (L->has_sep) h += 2u + gap;
    L->px = SL_DOCK_X;
    L->pw = SL_DOCK_W;
    L->ph = h;
    L->py = fh > h ? (fh - h) / 2u : 0u;
    L->gap_at = gap_at;
    L->cell0 = L->py + pad;
    L->cell_h = SL_DOCK_TILE + gap;
    uint32_t y = L->py + pad;
    for (size_t k = 0u; k < L->count; k++) {
        if (gap_at >= 0 && k == (size_t)gap_at) y += SL_DOCK_TILE + gap;   // the empty place
        if (L->has_sep && k == L->count - 1u) {
            L->sep_y = y;
            y += 2u + gap;
        }
        L->tile_y[k] = y;
        y += SL_DOCK_TILE + gap;
    }

    L->ex = SL_DOCK_X;
    L->ey = SL_DOCK_MARGIN;
    L->ew = fw > SL_DOCK_X + SL_MGR_RIGHT + SL_DOCK_W ? fw - SL_DOCK_X - SL_MGR_RIGHT : SL_DOCK_W;
    L->eh = fh > 2u * SL_DOCK_MARGIN ? fh - 2u * SL_DOCK_MARGIN : fh;
    uint32_t t = g_launcher_extra_width;
    L->cx = L->px;
    L->cy = sl_lerp_u32(L->py, L->ey, t);
    L->cw = sl_lerp_u32(L->pw, L->ew, t);
    L->ch = sl_lerp_u32(L->ph, L->eh, t);

    L->gx = L->ex + SL_MGR_GRID_X;
    L->gy = L->ey + 32u;
    L->gw = L->ex + L->ew > L->gx + 38u ? L->ex + L->ew - L->gx - 38u : 0u;
    L->cols = (L->gw + 20u) / SL_MGR_CELL_MIN;
    if (L->cols > SL_MGR_COLS_MAX) L->cols = SL_MGR_COLS_MAX;
    if (L->cols < 1u) L->cols = 1u;
    L->cell_w = L->gw / L->cols;
    L->grid_top = L->gy + 34u + 40u;
    sl_mgr_filter(L);
    // More apps than fit: the grid scrolls (the wheel, or the bar at its right).
    L->grid_bottom = L->ey + L->eh > L->grid_top + 52u ? L->ey + L->eh - 52u : L->grid_top;
    uint32_t rows = (uint32_t)((L->mgr_count + L->cols - 1u) / L->cols);
    uint32_t content_h = rows * SL_MGR_ROW_H;
    uint32_t view_h = L->grid_bottom - L->grid_top;
    L->max_scroll = content_h > view_h ? content_h - view_h : 0u;
    if (g_mgr_scroll > L->max_scroll) g_mgr_scroll = L->max_scroll;
    L->scroll = g_mgr_scroll;
}

// The app being dragged, out of its place in the dock: always when it was
// taken from the dock; from the app manager, only while it is over the dock.
static size_t sl_app_drag_hidden(void) {
    if (!g_app_drag.active || !g_app_drag.movable || g_app_drag.item >= g_launcher_count) return (size_t)-1;
    return (g_app_drag.from_dock || g_app_drag.slot >= 0) ? g_app_drag.item : (size_t)-1;
}

// The dock's tiles and its panel slide to their new places (a drag opens or
// closes an empty place, an app pinned, moved or taken out): from where they
// were shown to where they go, as <t> goes from 0 to SL_DOCK_ANIM_FULL.
#define SL_DOCK_MOVE_TWEEN_ID (SL_LAUNCHER_TWEEN_ID + 1u)
#define SL_DOCK_MOVE_MS 180u
static struct {
    uint8_t ready;          // <to> holds the dock as last laid out
    uint32_t t, last_t;
    size_t n;
    size_t item[SL_DOCK_MAX];
    uint32_t from_y[SL_DOCK_MAX], to_y[SL_DOCK_MAX];
    uint32_t from_py, from_ph, from_sep, to_py, to_ph, to_sep;
} g_dock_move;

static void sl_dock_move_apply(sl_dock_layout_t *L) {
    // Always as the last tick laid it out (its damage), even when the dock
    // changed since (an app opened): the next tick catches up.
    if (!g_dock_move.ready) return;
    uint32_t t = g_dock_move.t > SL_DOCK_ANIM_FULL ? SL_DOCK_ANIM_FULL : g_dock_move.t;
    size_t n = 0u, pinned = 0u;
    for (size_t k = 0u; k < L->count; k++) {
        size_t j = 0u;
        while (j < g_dock_move.n && g_dock_move.item[j] != L->item[k]) j++;
        if (j == g_dock_move.n) continue;   // new since the tick: it shows from the next one
        L->item[n] = L->item[k];
        L->tile_y[n] = sl_lerp_u32(g_dock_move.from_y[j], g_dock_move.to_y[j], t);
        if (k < L->pinned) pinned++;
        n++;
    }
    L->count = n;
    L->pinned = pinned;
    L->py = sl_lerp_u32(g_dock_move.from_py, g_dock_move.to_py, t);
    L->ph = sl_lerp_u32(g_dock_move.from_ph, g_dock_move.to_ph, t);
    if (L->has_sep) L->sep_y = sl_lerp_u32(g_dock_move.from_sep, g_dock_move.to_sep, t);
    uint32_t w = g_launcher_extra_width;
    L->cy = sl_lerp_u32(L->py, L->ey, w);
    L->ch = sl_lerp_u32(L->ph, L->eh, w);
}

static void sl_dock_layout(const sl_ctx_t *ctx, sl_dock_layout_t *L) {
    int32_t gap = g_app_drag.active && g_app_drag.slot >= 0 ? g_app_drag.slot : -1;
    sl_dock_layout_ex(ctx, L, sl_app_drag_hidden(), gap);
    sl_dock_move_apply(L);
}

// Once a frame: the dock laid out again; if its tiles or its size changed,
// they slide there from where they are shown now. 1 while they move.
static int sl_dock_move_tick(const sl_ctx_t *ctx) {
    sl_dock_layout_t T;
    int32_t gap = g_app_drag.active && g_app_drag.slot >= 0 ? g_app_drag.slot : -1;
    sl_dock_layout_ex(ctx, &T, sl_app_drag_hidden(), gap);
    uint8_t same = g_dock_move.ready && T.count == g_dock_move.n && T.py == g_dock_move.to_py &&
                   T.ph == g_dock_move.to_ph;
    for (size_t k = 0u; same && k < T.count; k++) {
        same = T.item[k] == g_dock_move.item[k] && T.tile_y[k] == g_dock_move.to_y[k];
    }
    if (!same) {
        uint8_t animate = g_dock_move.ready && T.count > 0u && g_dock_move.n > 0u;
        uint32_t t = g_dock_move.t;
        uint32_t from_y[SL_DOCK_MAX];
        for (size_t k = 0u; k < T.count; k++) {
            from_y[k] = T.tile_y[k];   // a tile new in the dock shows at its place
            for (size_t j = 0u; animate && j < g_dock_move.n; j++) {
                if (g_dock_move.item[j] != T.item[k]) continue;
                from_y[k] = sl_lerp_u32(g_dock_move.from_y[j], g_dock_move.to_y[j], t);
                break;
            }
        }
        if (animate) {
            g_dock_move.from_py = sl_lerp_u32(g_dock_move.from_py, g_dock_move.to_py, t);
            g_dock_move.from_ph = sl_lerp_u32(g_dock_move.from_ph, g_dock_move.to_ph, t);
            g_dock_move.from_sep = sl_lerp_u32(g_dock_move.from_sep, g_dock_move.to_sep, t);
        } else {
            g_dock_move.from_py = T.py;
            g_dock_move.from_ph = T.ph;
            g_dock_move.from_sep = T.sep_y;
        }
        g_dock_move.n = T.count;
        for (size_t k = 0u; k < T.count; k++) {
            g_dock_move.item[k] = T.item[k];
            g_dock_move.from_y[k] = from_y[k];
            g_dock_move.to_y[k] = T.tile_y[k];
        }
        g_dock_move.to_py = T.py;
        g_dock_move.to_ph = T.ph;
        g_dock_move.to_sep = T.sep_y;
        g_dock_move.ready = 1u;
        g_dock_move.t = 0u;
        if (!animate || !sl_tween_to_u32(SL_DOCK_MOVE_TWEEN_ID, &g_dock_move.t, SL_DOCK_ANIM_FULL, SL_DOCK_MOVE_MS,
                                         SL_TWEEN_EASE_OUT_CUBIC, NULL, NULL)) {
            sl_tween_set_u32_immediate(&g_dock_move.t, SL_DOCK_ANIM_FULL);
        }
    }
    // A new layout is drawn at once even when <t> stays (an open app's tile
    // in or out of it): the column again.
    int moved = !same || g_dock_move.t != g_dock_move.last_t;
    g_dock_move.last_t = g_dock_move.t;
    return moved;
}

// The launcher items changed (a new scan): no slide from the old indices.
static void sl_dock_move_reset(void) {
    sl_tween_cancel_id(SL_DOCK_MOVE_TWEEN_ID);
    g_dock_move.ready = 0u;
    g_dock_move.n = 0u;
    g_dock_move.t = g_dock_move.last_t = SL_DOCK_ANIM_FULL;
}

// Where the header's pieces go: the search field at the right edge of the
// grid, the category pill left of it, the title and count at the left.
typedef struct {
    uint32_t y;                     // top of the row
    uint32_t seg_x, seg_w;
    uint32_t cell_x[SL_CAT_COUNT], cell_w[SL_CAT_COUNT];
    uint32_t search_x, search_w;
    uint8_t show_count;             // room for "N nella cartella"
} sl_mgr_header_t;

static void sl_mgr_header(const sl_dock_layout_t *L, sl_mgr_header_t *H) {
    peos_memset(H, 0, sizeof(*H));
    H->y = L->gy > 8u ? L->gy - 8u : 0u;
    uint32_t seg_w = 2u * SL_MGR_SEG_PAD;
    for (int c = 0; c < SL_CAT_COUNT; c++) {
        H->cell_w[c] = sl_text_width(g_dock.label, g_mgr_cat_labels(c)) + 2u * SL_MGR_SEG_CELL_PAD;
        seg_w += H->cell_w[c];
    }
    uint32_t right = L->gx + L->gw;
    uint32_t title_w = sl_text_width(g_dock.title, sl_tr("apps.header"));
    char widest[48];
    snprintf(widest, sizeof(widest), sl_tr_n("apps.count_all", 888u), 888u);
    uint32_t count_w = 14u + sl_text_width(g_dock.label, widest);
    uint32_t left = L->gx + title_w + 22u;
    uint32_t room = right > left ? right - left : 0u;
    H->search_w = SL_MGR_SEARCH_W;
    if (room < seg_w + 22u + H->search_w) {
        H->search_w = room > seg_w + 22u + SL_MGR_SEARCH_MIN_W ? room - seg_w - 22u : SL_MGR_SEARCH_MIN_W;
    }
    H->show_count = room >= count_w + seg_w + 44u + H->search_w;
    H->search_x = right - H->search_w;
    H->seg_w = seg_w;
    H->seg_x = H->search_x > seg_w + 22u ? H->search_x - 22u - seg_w : 0u;
    uint32_t x = H->seg_x + SL_MGR_SEG_PAD;
    for (int c = 0; c < SL_CAT_COUNT; c++) {
        H->cell_x[c] = x;
        x += H->cell_w[c];
    }
}

static void sl_dock_tile_rect(const sl_dock_layout_t *L, size_t k, uint32_t *x, uint32_t *y) {
    *x = L->px + (SL_DOCK_W - SL_DOCK_TILE) / 2u;
    *y = L->tile_y[k];
}

// The tile at position i of the filtered grid.
// Its y is where it is now, the grid scrolled: 0 when it is above the grid's
// top (scrolled away), so check sl_mgr_tile_shown first.
static int64_t sl_mgr_tile_y(const sl_dock_layout_t *L, size_t i) {
    return (int64_t)L->grid_top + (int64_t)(i / L->cols) * SL_MGR_ROW_H - (int64_t)L->scroll;
}

// Some of the tile (with its label) is in the grid's view.
static int sl_mgr_tile_shown(const sl_dock_layout_t *L, size_t i) {
    int64_t y = sl_mgr_tile_y(L, i);
    return y + (int64_t)SL_MGR_ROW_H > (int64_t)L->grid_top && y < (int64_t)L->grid_bottom;
}

static void sl_mgr_tile_rect(const sl_dock_layout_t *L, size_t i, uint32_t *x, uint32_t *y) {
    uint32_t col = (uint32_t)(i % L->cols);
    int64_t ty = sl_mgr_tile_y(L, i);
    *x = L->gx + col * L->cell_w + (L->cell_w > SL_MGR_TILE ? (L->cell_w - SL_MGR_TILE) / 2u : 0u);
    *y = ty > 0 ? (uint32_t)ty : 0u;
}

static void sl_fin_rect(const sl_dock_layout_t *L, uint32_t *x, uint32_t *y) {
    *x = L->cx + L->cw - 1u;
    *y = L->cy + (L->ch > SL_FIN_H ? (L->ch - SL_FIN_H) / 2u : 0u);
}

static uint8_t sl_dock_expanded_fully(void) {
    return g_launcher_extra_width >= SL_DOCK_ANIM_FULL ? 1u : 0u;
}

// Hit tests: a dock tile (launcher item) or a manager tile.
static int sl_dock_hit_tile(const sl_dock_layout_t *L, uint32_t x, uint32_t y, size_t *out) {
    for (size_t k = 0u; k < L->count; k++) {
        uint32_t tx, ty;
        sl_dock_tile_rect(L, k, &tx, &ty);
        if (sl_point_in_rect(x, y, tx, ty, SL_DOCK_TILE, SL_DOCK_TILE)) {
            *out = L->item[k];
            return 1;
        }
    }
    return 0;
}

// The grid's scroll bar: track x, y, height, width; the thumb's height and y.
static void sl_mgr_scrollbar(const sl_dock_layout_t *L, uint32_t *bx, uint32_t *by, uint32_t *bh,
                             uint32_t *bw, uint32_t *th, uint32_t *ty) {
    uint32_t view_h = L->grid_bottom - L->grid_top;
    *bw = 8u;
    *bx = L->ex + L->ew - 30u;
    *by = L->grid_top;
    *bh = view_h;
    uint32_t content_h = view_h + L->max_scroll;
    *th = content_h ? (uint32_t)(((uint64_t)view_h * view_h) / content_h) : view_h;
    if (*th < 32u) *th = 32u;
    if (*th > view_h) *th = view_h;
    uint32_t travel = view_h - *th;
    *ty = *by + (L->max_scroll ? (uint32_t)(((uint64_t)travel * L->scroll) / L->max_scroll) : 0u);
}

static int sl_mgr_hit_tile(const sl_dock_layout_t *L, uint32_t x, uint32_t y, size_t *out) {
    if (!sl_dock_expanded_fully()) return 0;
    if (y < L->grid_top || y >= L->grid_bottom) return 0;   // the part of a tile scrolled out
    for (size_t i = 0u; i < L->mgr_count; i++) {
        uint32_t tx, ty;
        if (!sl_mgr_tile_shown(L, i)) continue;
        sl_mgr_tile_rect(L, i, &tx, &ty);
        if (sl_point_in_rect(x, y, tx, ty, SL_MGR_TILE, SL_MGR_TILE + 32u)) {
            *out = L->mgr_item[i];
            return 1;
        }
    }
    return 0;
}

static int sl_launcher_hit_test(const sl_ctx_t *ctx, uint32_t x, uint32_t y, size_t *out_index) {
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    size_t i = 0u;
    if (sl_dock_hit_tile(&L, x, y, &i) || sl_mgr_hit_tile(&L, x, y, &i)) {
        if (out_index) *out_index = i;
        return 1;
    }
    return 0;
}

// Dirty: a sprite's whole footprint (shadow included) for a shape at x,y.
static void sl_dirty_add_i64(const sl_ctx_t *ctx, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w > 0 && h > 0) sl_dirty_add(ctx, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

static void sl_dirty_sprite(const sl_ctx_t *ctx, const slsk_entry_t *e, int64_t x, int64_t y, uint32_t w, uint32_t h) {
    if (!e) return;
    sl_dirty_add_i64(ctx, x - e->pad_l, y - e->pad_t, (int64_t)w + e->pad_l + e->pad_r, (int64_t)h + e->pad_t + e->pad_b);
}

static uint32_t sl_dock_pill_width(const sl_launcher_item_t *item) {
    char label[26];
    sl_launcher_compact_label(item->title, label, sizeof(label));
    return sl_text_width(g_dock.label, label) + 2u * SL_PILL_PAD;
}

// The pill with the app's name, right of the fin, level with its tile.
static int sl_dock_pill_rect(const sl_dock_layout_t *L, size_t item, uint32_t *x, uint32_t *y, uint32_t *w) {
    if (item >= g_launcher_count || g_dock.ready != 1u) return 0;
    for (size_t k = 0u; k < L->count; k++) {
        if (L->item[k] != item) continue;
        *x = L->px + L->pw + SL_FIN_W + 10u;
        *y = L->tile_y[k] + (SL_DOCK_TILE - SL_PILL_H) / 2u;
        *w = sl_dock_pill_width(&g_launcher_items[item]);
        return 1;
    }
    return 0;
}

// Everything a hovered or pressed item touches: its tile (grown) and its pill.
static void sl_dock_dirty_item(const sl_ctx_t *ctx, size_t item) {
    if (item >= g_launcher_count || g_dock.ready != 1u) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    for (size_t k = 0u; k < L.count; k++) {
        if (L.item[k] != item) continue;
        uint32_t tx, ty;
        sl_dock_tile_rect(&L, k, &tx, &ty);
        sl_dirty_sprite(ctx, g_dock.hi_pad_ref, (int64_t)tx - 3, (int64_t)ty - 3, SL_DOCK_HI, SL_DOCK_HI);
        uint32_t px, py, pw;
        if (sl_dock_pill_rect(&L, item, &px, &py, &pw)) sl_dirty_sprite(ctx, g_dock.pill, px, py, pw, SL_PILL_H);
    }
    if (sl_dock_expanded_fully()) {
        for (size_t i = 0u; i < L.mgr_count; i++) {
            if (L.mgr_item[i] != item) continue;
            uint32_t tx, ty;
            sl_mgr_tile_rect(&L, i, &tx, &ty);
            // tile-hi's shadow: 12 + 34 px down and right, 10 + 31 up and left
            sl_dirty_add_i64(ctx, (int64_t)tx - 48, (int64_t)ty - 48, SL_MGR_HI + 96, SL_MGR_HI + 96);
        }
    }
}

static void sl_dock_dirty_fin(const sl_ctx_t *ctx, const sl_dock_layout_t *L) {
    uint32_t fx, fy;
    sl_fin_rect(L, &fx, &fy);
    sl_dirty_sprite(ctx, g_dock.fin, fx, fy, SL_FIN_W, SL_FIN_H);
}

static void sl_dock_dirty_panel(const sl_ctx_t *ctx, const sl_dock_layout_t *L) {
    sl_dirty_sprite(ctx, g_dock.panel, L->cx, L->cy, L->cw, L->ch);
}

static void sl_dock_set_hover_at(sl_ctx_t *ctx, size_t next, uint8_t in_mgr) {
    if (next == g_launcher_hover_index && in_mgr == g_dock_hover_in_mgr) return;
    sl_dock_dirty_item(ctx, g_launcher_hover_index);
    sl_dock_dirty_item(ctx, next);
    g_launcher_hover_index = next;
    g_dock_hover_in_mgr = in_mgr;
}

static void sl_dock_set_hover(sl_ctx_t *ctx, size_t next) {
    sl_dock_set_hover_at(ctx, next, 0u);
}

static void sl_launcher_toggle(sl_ctx_t *ctx) {
    if (!ctx) return;
    sl_dock_set_hover(ctx, (size_t)-1);
    g_launcher_expanded = g_launcher_expanded ? 0u : 1u;
    if (!g_launcher_expanded) {   // opened again, it shows every app, from the top
        g_mgr_query[0] = '\0';
        g_mgr_scroll = 0u;
    }
    uint32_t target = g_launcher_expanded ? SL_DOCK_ANIM_FULL : 0u;
    if (!sl_tween_to_u32(SL_LAUNCHER_TWEEN_ID, &g_launcher_extra_width, target,
                         SL_LAUNCHER_ANIM_MS, SL_TWEEN_EASE_IN_OUT_CUBIC, NULL, NULL)) {
        sl_tween_set_u32_immediate(&g_launcher_extra_width, target);
    }
}

// Running and focused apps, one bit each per dock tile: when they change,
// the indicators left of the tiles are redrawn.
static uint64_t sl_dock_indicator_bits(const sl_dock_layout_t *L) {
    uint64_t bits = 0u;
    size_t orfeus = sl_music_active() ? sl_launcher_find_id("orfeus") : (size_t)-1;
    for (size_t k = 0u; k < L->count && k < 32u; k++) {
        app_instance_t *app = sl_app_host_instance_peek_at(g_launcher_items[L->item[k]].slot);
        size_t w = app ? sl_window_index_for_task(app) : (size_t)-1;
        if (w == (size_t)-1) {
            // Orfeus's music goes on with its window closed (design/orfeus): the dot stays.
            if (L->item[k] == orfeus) bits |= 1ull << (2u * k);
            continue;
        }
        bits |= 1ull << (2u * k);
        if (w == g_focused_window && !g_windows[w].minimized) bits |= 2ull << (2u * k);
    }
    return bits;
}

static void sl_launcher_track_animation_dirty(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    int sliding = sl_dock_move_tick(ctx);
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (sliding) {
        // The tiles move inside it; a raised one takes its shadow and its
        // name (right of the fin) along: the dock's whole column.
        sl_dock_dirty_panel(ctx, &L);
        uint32_t right = L.px + L.pw + SL_FIN_W + 10u + 48u;
        if (g_launcher_hover_index < g_launcher_count) right += sl_dock_pill_width(&g_launcher_items[g_launcher_hover_index]);
        sl_dirty_add_i64(ctx, 0, 0, right, ctx->fb.height);
    }

    sl_window_rect_snapshot_t current = { L.cx, L.cy, L.cw, L.ch, (uint8_t)(L.cw && L.ch) };
    uint8_t changed = current.valid != g_launcher_last_rect.valid ||
                      (current.valid && (current.x != g_launcher_last_rect.x || current.y != g_launcher_last_rect.y ||
                                         current.w != g_launcher_last_rect.w || current.h != g_launcher_last_rect.h));
    if (changed) {
        // Where the panel and its fin were, and where they are.
        if (g_launcher_last_rect.valid) {
            sl_dock_layout_t old = L;
            old.cx = g_launcher_last_rect.x;
            old.cy = g_launcher_last_rect.y;
            old.cw = g_launcher_last_rect.w;
            old.ch = g_launcher_last_rect.h;
            sl_dock_dirty_panel(ctx, &old);
            sl_dock_dirty_fin(ctx, &old);
        }
        sl_dock_dirty_panel(ctx, &L);
        sl_dock_dirty_fin(ctx, &L);
        g_launcher_last_rect = current;
    }

    // The tiles themselves: an app opened or closed changes the open apps'
    // tiles, even when the dock keeps its height.
    uint64_t sig = 1469598103934665603ull;
    for (size_t k = 0u; k < L.count; k++) sig = (sig ^ (uint64_t)(L.item[k] + 1u)) * 1099511628211ull;
    if (sig != g_dock_items_sig) {
        g_dock_items_sig = sig;
        sl_dock_dirty_panel(ctx, &L);
        size_t settings = sl_dock_settings_item();
        size_t open = L.count - L.pinned - (settings != (size_t)-1 && L.count > L.pinned ? 1u : 0u);
        printf("starlight: dock: %u pinned + %u open\n", (unsigned)L.pinned, (unsigned)open);
    }

    uint64_t bits = sl_dock_indicator_bits(&L);
    if (bits != g_dock_indicators) {
        g_dock_indicators = bits;
        sl_dirty_add_i64(ctx, (int64_t)L.px - 4, L.py, 40, L.ph);
    }
}

static void sl_launcher_context_close(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (!g_launcher_ctx_open) return;
    if (g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u) {
        sl_launcher_invalidate_overlay(ctx, g_launcher_ctx_x, g_launcher_ctx_y,
                                       g_launcher_ctx_w + 2u, g_launcher_ctx_h + 2u);
    }
    g_launcher_ctx_open = 0u;
    g_launcher_ctx_item_index = (size_t)-1;
    g_launcher_ctx_x = 0u;
    g_launcher_ctx_y = 0u;
    g_launcher_ctx_w = 0u;
    g_launcher_ctx_h = 0u;
    g_launcher_ctx_hover_row = -1;
    g_launcher_ctx_info = 0u;
    g_launcher_ctx_desk = 0u;
    g_launcher_ctx_rows = 0u;
}

// SL_CTX_FULL: "the dock is full" in place of Keep in Dock; it does nothing.
// SL_CTX_WALLPAPER, _ARRANGE, _SETTINGS: the desktop's menu.
enum { SL_CTX_OPEN, SL_CTX_SHOW, SL_CTX_CLOSE, SL_CTX_PIN, SL_CTX_UNPIN, SL_CTX_INFO, SL_CTX_FULL,
       SL_CTX_WALLPAPER, SL_CTX_ARRANGE, SL_CTX_SETTINGS };

static const char *sl_launcher_context_action_name(uint8_t action) {
    switch (action) {
        case SL_CTX_OPEN: return sl_tr("apps.ctx.open");
        case SL_CTX_SHOW: return sl_tr("apps.ctx.show");
        case SL_CTX_CLOSE: return sl_tr("apps.ctx.close");
        case SL_CTX_PIN: return sl_tr("apps.ctx.pin");
        case SL_CTX_UNPIN: return sl_tr("apps.ctx.unpin");
        case SL_CTX_FULL: return sl_tr("apps.ctx.dock_full");
        case SL_CTX_WALLPAPER: return sl_tr("desk.menu.wallpaper");
        case SL_CTX_ARRANGE: return sl_tr("desk.menu.arrange");
        case SL_CTX_SETTINGS: return sl_tr("desk.menu.settings");
        default: return sl_tr("apps.ctx.info");
    }
}

// Pinned: listed in dock.cfg or, until it exists, shown in the dock.
static uint8_t sl_dock_is_pinned(const sl_dock_layout_t *L, size_t item_index) {
    if (g_dock_pins_set) return sl_dock_pin_find(g_launcher_items[item_index].bundle_path) != (size_t)-1;
    for (size_t k = 0u; k < L->pinned; k++) {   // an open app's own tile is not a pin
        if (L->item[k] == item_index) return 1u;
    }
    return 0u;
}

static uint32_t sl_launcher_context_build(const sl_ctx_t *ctx, size_t item_index, uint8_t *out) {
    const sl_launcher_item_t *item = &g_launcher_items[item_index];
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (!app) return 0u;
    uint32_t n = 0u;
    if (sl_window_index_for_task(app) != (size_t)-1) {
        out[n++] = SL_CTX_SHOW;
        out[n++] = SL_CTX_CLOSE;
    } else {
        out[n++] = SL_CTX_OPEN;
    }
    size_t settings = sl_dock_settings_item();
    if (item->bundle_path[0] != '\0' && item_index != settings) {
        sl_dock_layout_t L;
        sl_dock_layout(ctx, &L);
        size_t shown = L.pinned;   // the open apps' tiles do not take a pin's place
        if (sl_dock_is_pinned(&L, item_index)) {
            // Without Impostazioni the last tile stays: the dock never empties.
            if (settings != (size_t)-1 || shown > 1u) out[n++] = SL_CTX_UNPIN;
        } else if (shown < sl_dock_capacity(ctx, settings, SL_DOCK_PAD_TIGHT, SL_DOCK_GAP_TIGHT)) {
            out[n++] = SL_CTX_PIN;
        } else {
            out[n++] = SL_CTX_FULL;
        }
    }
    out[n++] = SL_CTX_INFO;
    return n;
}

// The first change starts from what the dock shows.
static void sl_dock_pins_start(sl_ctx_t *ctx) {
    if (g_dock_pins_set) return;
    sl_dock_layout_t L;
    sl_dock_layout_ex(ctx, &L, (size_t)-1, -1);   // the dock as it is, not as last drawn
    size_t settings = sl_dock_settings_item();
    g_dock_pin_count = 0u;
    for (size_t k = 0u; k < L.pinned; k++) {   // not the open apps after them
        const sl_launcher_item_t *shown = &g_launcher_items[L.item[k]];
        if (L.item[k] == settings || shown->bundle_path[0] == '\0') continue;
        sl_copy_str(g_dock_pins[g_dock_pin_count++], sizeof(g_dock_pins[0]), shown->bundle_path);
    }
    g_dock_pins_set = 1u;
}

static void sl_dock_pin_set(sl_ctx_t *ctx, size_t item_index, uint8_t pin) {
    const char *path = g_launcher_items[item_index].bundle_path;
    sl_dock_pins_start(ctx);
    size_t at = sl_dock_pin_find(path);
    if (pin && at == (size_t)-1 && g_dock_pin_count < SL_DOCK_MAX) {
        sl_copy_str(g_dock_pins[g_dock_pin_count++], sizeof(g_dock_pins[0]), path);
    } else if (!pin && at != (size_t)-1) {
        for (size_t i = at; i + 1u < g_dock_pin_count; i++) {
            peos_memcpy(g_dock_pins[i], g_dock_pins[i + 1u], sizeof(g_dock_pins[0]));
        }
        g_dock_pin_count--;
    } else {
        return;
    }
    sl_dock_pins_save();
    sl_dirty_mark_full();   // the dock changes height
}

static void sl_launcher_info_append(uint32_t *n, const char *fmt, ...) {
    if (*n >= SL_LAUNCHER_CTX_ROWS_MAX) return;
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(g_launcher_info_lines[*n], sizeof(g_launcher_info_lines[0]), fmt, ap);
    va_end(ap);
    (*n)++;
}

// The lines of the Informazioni card, read from the app's manifest.
static uint32_t sl_launcher_info_build(size_t item_index, uint8_t details) {
    const sl_launcher_item_t *item = &g_launcher_items[item_index];
    sl_app_bundle_t app;
    peos_memset(&app, 0, sizeof(app));
    uint8_t have = 0u;
    char manifest[320];
    if (item->bundle_path[0] != '\0' &&
        sl_join_path(manifest, sizeof(manifest), item->bundle_path, SL_APP_MANIFEST)) {
        have = (uint8_t)(sl_load_app_bundle(manifest, item->bundle_path, &app) ||
                         sl_load_app_bundle(item->bundle_path, NULL, &app));
    }

    uint32_t n = 0u;
    app_instance_t *inst = sl_app_host_instance_peek_at(item->slot);
    const char *state = sl_tr((inst && sl_window_index_for_task(inst) != (size_t)-1) ? "apps.info.open" : "apps.info.closed");
    sl_launcher_info_append(&n, "%s", item->title);
    g_launcher_info_details_row = -1;
    if (!details) {
        // The summary (docs/system-apps-ux-plan.md, question 20): who made
        // it, what it is, and what it may do in plain words; the technical
        // data under "Details".
        if (have && app.version[0] && app.author[0]) {
            sl_launcher_info_append(&n, sl_tr("apps.info.version_by"), app.version, app.author);
        } else if (have && app.version[0]) {
            sl_launcher_info_append(&n, sl_tr("apps.info.version"), app.version);
        } else if (have && app.author[0]) {
            sl_launcher_info_append(&n, sl_tr("apps.info.by"), app.author);
        } else if (strncmp(item->bundle_path, "/system/apps/", 13) == 0) {
            sl_launcher_info_append(&n, "%s", sl_tr("apps.info.system_app"));
        }
        if (item->category != SL_CAT_ALL && item->category < SL_CAT_COUNT) {
            sl_launcher_info_append(&n, sl_tr("apps.info.category"), g_mgr_cat_labels(item->category));
        }
        sl_launcher_info_append(&n, sl_tr("apps.info.state"), state);
        if (have) {
            uint32_t ncaps = 0u;
            for (size_t i = 0u; i < sizeof(peos_cap_names) / sizeof(peos_cap_names[0]); i++) {
                if (app.caps_mask & peos_cap_names[i].mask) ncaps++;
            }
            if (ncaps == 0u) {
                sl_launcher_info_append(&n, "%s", sl_tr("apps.info.can_nothing"));
            } else {
                sl_launcher_info_append(&n, "%s", sl_tr("apps.info.can"));
                // One row each, as far as they fit with "Details" under them.
                uint32_t room = SL_LAUNCHER_CTX_ROWS_MAX > n + 1u ? SL_LAUNCHER_CTX_ROWS_MAX - n - 1u : 0u;
                uint32_t shown = 0u;
                for (size_t i = 0u; i < sizeof(peos_cap_names) / sizeof(peos_cap_names[0]); i++) {
                    if (!(app.caps_mask & peos_cap_names[i].mask)) continue;
                    if (shown + 1u == room && ncaps > room) {
                        sl_launcher_info_append(&n, sl_tr("apps.info.can_more"), (unsigned)(ncaps - shown));
                        break;
                    }
                    char key[32];
                    snprintf(key, sizeof(key), "apps.cap.%s", peos_cap_names[i].name);
                    sl_launcher_info_append(&n, "  \xC2\xB7 %s", sl_tr(key));
                    shown++;
                }
            }
        }
        if (n < SL_LAUNCHER_CTX_ROWS_MAX) {
            g_launcher_info_details_row = (int32_t)n;
            sl_launcher_info_append(&n, "%s", sl_tr("apps.info.details"));
        }
        return n;
    }
    if (item->bundle_path[0] != '\0') sl_launcher_info_append(&n, sl_tr("apps.info.bundle"), item->bundle_path);
    if (have) {
        sl_launcher_info_append(&n, sl_tr("apps.info.program"), app.exec);
        char list[112];
        size_t len = 0u;
        list[0] = '\0';
        for (size_t i = 0u; i < sizeof(peos_cap_names) / sizeof(peos_cap_names[0]); i++) {
            if (!(app.caps_mask & peos_cap_names[i].mask)) continue;
            int w = snprintf(list + len, sizeof(list) - len, "%s%s", len ? ", " : "", peos_cap_names[i].name);
            if (w < 0 || (size_t)w >= sizeof(list) - len) break;
            len += (size_t)w;
        }
        sl_launcher_info_append(&n, sl_tr("apps.info.rights"), len ? list : sl_tr("apps.info.none_rights"));
        len = 0u;
        list[0] = '\0';
        for (uint32_t i = 0u; i < app.path_count && i < PEOS_PATH_MAX_GRANTS; i++) {
            const peos_path_grant_t *g = &app.paths[i];
            const char *rights = (g->rights & PEOS_PATH_WRITE) ? ((g->rights & PEOS_PATH_READ) ? "rw" : "w") : "r";
            int w = snprintf(list + len, sizeof(list) - len, "%s%s:%s", len ? ", " : "", g->prefix, rights);
            if (w < 0 || (size_t)w >= sizeof(list) - len) break;
            len += (size_t)w;
        }
        sl_launcher_info_append(&n, sl_tr("apps.info.folders"), len ? list : sl_tr("apps.info.none_folders"));
        sl_launcher_info_append(&n, sl_tr("apps.info.window"), (unsigned)app.width, (unsigned)app.height);
    }
    sl_launcher_info_append(&n, sl_tr("apps.info.state"), state);
    return n;
}

static void sl_launcher_context_label_cb(uint32_t option_index, char *out, size_t out_cap, void *userdata) {
    (void)userdata;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (option_index >= g_launcher_ctx_rows) return;
    if (g_launcher_ctx_info) sl_copy_str(out, out_cap, g_launcher_info_lines[option_index]);
    else if (g_launcher_ctx_actions[option_index] == SL_CTX_FULL && out_cap > 1u) {
        out[0] = '~';   // drawn off: it says why, it does nothing (context_menu_renderer.c)
        sl_copy_str(out + 1, out_cap - 1u, sl_launcher_context_action_name(SL_CTX_FULL));
    } else sl_copy_str(out, out_cap, sl_launcher_context_action_name(g_launcher_ctx_actions[option_index]));
}

// Opens the popup (menu or card) whose rows are already in place, as wide as
// its longest row.
static void sl_launcher_popup_open(sl_ctx_t *ctx, size_t item_index, uint32_t anchor_x, uint32_t anchor_y) {
    uint32_t action_count = g_launcher_ctx_rows;
    uint32_t menu_w = SL_LAUNCHER_CTX_MIN_W;
    for (uint32_t r = 0u; r < action_count; r++) {
        char label[128];
        sl_launcher_context_label_cb(r, label, sizeof(label), NULL);
        uint32_t w = sl_ui_ctx_text_width(ctx, label) + 2u * SL_LAUNCHER_CTX_PAD_X;
        if (w > menu_w) menu_w = w;
    }
    uint32_t menu_h = action_count * SL_LAUNCHER_CTX_ROW_H + 2u;
    if (menu_w > ctx->fb.width) menu_w = ctx->fb.width;
    if (menu_h > ctx->fb.height) menu_h = ctx->fb.height;
    uint32_t menu_x = anchor_x;
    uint32_t menu_y = anchor_y;
    if (menu_x + menu_w > ctx->fb.width) menu_x = (ctx->fb.width > menu_w) ? (ctx->fb.width - menu_w) : 0u;
    if (menu_y + menu_h > ctx->fb.height) {
        menu_y = (anchor_y >= menu_h) ? (anchor_y - menu_h) : 0u;
    }
    if (menu_y + menu_h > ctx->fb.height && ctx->fb.height >= menu_h) {
        menu_y = ctx->fb.height - menu_h;
    }

    g_launcher_ctx_open = 1u;
    g_launcher_ctx_item_index = item_index;
    g_launcher_ctx_x = menu_x;
    g_launcher_ctx_y = menu_y;
    g_launcher_ctx_w = menu_w;
    g_launcher_ctx_h = menu_h;
    g_launcher_ctx_hover_row = -1;
    sl_dock_set_hover(ctx, (size_t)-1);
    sl_launcher_invalidate_overlay(ctx, g_launcher_ctx_x, g_launcher_ctx_y,
                                   g_launcher_ctx_w + 2u, g_launcher_ctx_h + 2u);
}

static void sl_launcher_context_open(sl_ctx_t *ctx, size_t item_index, uint32_t anchor_x, uint32_t anchor_y) {
    if (!ctx || item_index >= g_launcher_count) return;
    uint8_t actions[SL_LAUNCHER_CTX_ROWS_MAX];
    uint32_t count = sl_launcher_context_build(ctx, item_index, actions);
    if (count == 0u) return;
    sl_launcher_context_close(ctx);
    peos_memcpy(g_launcher_ctx_actions, actions, count);
    g_launcher_ctx_rows = count;
    g_launcher_ctx_info = 0u;
    sl_launcher_popup_open(ctx, item_index, anchor_x, anchor_y);
}

static void sl_launcher_info_open(sl_ctx_t *ctx, size_t item_index, uint32_t anchor_x, uint32_t anchor_y,
                                  uint8_t details) {
    if (!ctx || item_index >= g_launcher_count) return;
    sl_launcher_context_close(ctx);
    g_launcher_ctx_rows = sl_launcher_info_build(item_index, details);
    g_launcher_ctx_info = 1u;
    sl_launcher_popup_open(ctx, item_index, anchor_x, anchor_y);
}

static int sl_launcher_context_activate(sl_ctx_t *ctx, size_t item_index, uint8_t action,
                                        uint32_t anchor_x, uint32_t anchor_y) {
    if (!ctx || item_index >= g_launcher_count) return 0;
    sl_launcher_item_t *item = &g_launcher_items[item_index];
    if (action == SL_CTX_FULL) return 1;
    if (action == SL_CTX_PIN || action == SL_CTX_UNPIN) {
        sl_dock_pin_set(ctx, item_index, action == SL_CTX_PIN);
        return 1;
    }
    if (action == SL_CTX_INFO) {
        sl_launcher_info_open(ctx, item_index, anchor_x, anchor_y, 0u);
        return 1;
    }
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (!app) return 0;
    item->instance = app;

    size_t existing = sl_window_index_for_task(app);
    if (existing != (size_t)-1) {
        if (action == SL_CTX_SHOW || action == SL_CTX_OPEN) {
            sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
            g_windows[existing].minimized = 0u;   // "Show" brings a minimized window back too
            g_windows[existing].force_redraw = 1u;
            sl_window_rect_snapshot_t target = sl_window_snapshot_index(existing);
            sl_focus_window(existing);
            sl_windows_mark_focused_activity();
            sl_dirty_add_snapshot(ctx, &prev_focus);
            sl_dirty_add_snapshot(ctx, &target);
            return 1;
        }
        if (action == SL_CTX_CLOSE) {
            app_instance_t *t = g_windows[existing].task;
            if (t && t->on_close_request && t->on_close_request(t->userdata)) return 1;   // the app asks first
            if (!sl_window_is_closing(&g_windows[existing])) {
                sl_window_rect_snapshot_t closing = sl_window_snapshot_index(existing);
                sl_anim_start_close(g_windows[existing].task, (int32_t)g_windows[existing].y);
                sl_windows_mark_focused_activity();
                sl_dirty_add_snapshot(ctx, &closing);
            }
            return 1;
        }
        return 0;
    }

    if (action == SL_CTX_OPEN) return sl_windows_open_launcher_item(ctx, item_index);
    return 0;
}

// "Ordina finestre": the windows on screen in a staircase from the top left
// of the desktop, in the order they are stacked, each at its own size; the
// one in front stays in front. Maximized and minimized ones stay as they are.
static void sl_windows_arrange(sl_ctx_t *ctx) {
    if (!ctx || !g_windows) return;
    uint32_t left = sl_launcher_reserved_left(ctx) + 16u;
    uint32_t top = sl_windows_top(ctx);
    uint32_t step_x = 34u, step_y = 30u, k = 0u;
    for (size_t i = 0u; i < g_window_count; i++) {
        sl_window_t *w = &g_windows[i];
        if (w->minimized || w->maximized || sl_window_is_closing(w)) continue;
        uint32_t x = left + k * step_x, y = top + k * step_y;
        if (x + w->width > ctx->fb.width || y + w->height > ctx->fb.height) {
            k = 0u;   // off the screen: a new staircase from the corner
            x = left;
            y = top;
        }
        if (x + w->width > ctx->fb.width) x = ctx->fb.width > w->width ? ctx->fb.width - w->width : 0u;
        if (y + w->height > ctx->fb.height) y = ctx->fb.height > w->height ? ctx->fb.height - w->height : 0u;
        if (x < left - 16u) x = left - 16u;
        if (y < top) y = top;
        k++;
        if (x == w->x && y == w->y) continue;
        sl_window_dirty_all(ctx, w);   // where it was, shadow included
        w->x = x;
        w->y = y;
        w->force_redraw = 1u;
        sl_window_mark_activity(w, 0ull);
        sl_window_dirty_all(ctx, w);
        sl_windows_remember(w);
    }
}

static void sl_desk_menu_activate(sl_ctx_t *ctx, uint8_t action) {
    size_t settings = sl_launcher_find_id("impostazioni");
    if (action == SL_CTX_ARRANGE) {
        printf("starlight: desktop menu: arrange\n");
        sl_windows_arrange(ctx);
        return;
    }
    if (settings == (size_t)-1) return;
    if (action == SL_CTX_WALLPAPER) {
        // Impostazioni on its Theme tab, where the background is chosen.
        printf("starlight: desktop menu: wallpaper\n");
        (void)sl_windows_open_app_id_with(ctx, "impostazioni", "tab:theme");
        return;
    }
    if (action == SL_CTX_SETTINGS) {
        printf("starlight: desktop menu: settings\n");
        (void)sl_windows_open_launcher_item(ctx, settings);
    }
}

void sl_launcher_handle_context_hover(sl_ctx_t *ctx) {
    if (!ctx || !g_launcher_ctx_open || g_launcher_ctx_item_index >= g_launcher_count) return;
    if (g_launcher_ctx_w == 0u || g_launcher_ctx_h == 0u) return;
    if (g_launcher_ctx_info && g_launcher_info_details_row < 0) return;   // the card has nothing to pick
    uint32_t action_count = g_launcher_ctx_rows;
    if (action_count == 0u) return;

    int32_t next_row = -1;
    if (sl_point_in_rect(ctx->cursor_x, ctx->cursor_y, g_launcher_ctx_x, g_launcher_ctx_y,
                         g_launcher_ctx_w, g_launcher_ctx_h)) {
        if (ctx->cursor_y >= g_launcher_ctx_y + 1u) {
            uint32_t rel_y = ctx->cursor_y - (g_launcher_ctx_y + 1u);
            uint32_t row = rel_y / SL_LAUNCHER_CTX_ROW_H;
            if (row < action_count) next_row = (int32_t)row;
        }
    }
    if (g_launcher_ctx_info && next_row != g_launcher_info_details_row) next_row = -1;   // only Details
    if (next_row == g_launcher_ctx_hover_row) return;
    g_launcher_ctx_hover_row = next_row;
    sl_launcher_invalidate_overlay(ctx, g_launcher_ctx_x, g_launcher_ctx_y,
                                   g_launcher_ctx_w + 2u, g_launcher_ctx_h + 2u);
}

static size_t sl_window_index_for_task(const app_instance_t *task) {
    if (!task || !g_windows || g_window_count == 0u) return (size_t)-1;
    for (size_t i = 0u; i < g_window_count; i++) {
        if (g_windows[i].task == task) return i;
    }
    return (size_t)-1;
}

static int sl_window_is_closing(const sl_window_t *win) {
    int32_t idx;
    if (!win || !win->task) return 0;
    idx = sl_anim_find_slot(win->task);
    if (idx < 0) return 0;
    return g_window_anims[(size_t)idx].closing ? 1 : 0;
}

static int sl_window_contains_point(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win || win->minimized) return 0;
    if (x < win->x || y < win->y) return 0;
    if (x >= (win->x + win->width)) return 0;
    if (y >= (win->y + win->height)) return 0;
    return 1;
}

static void sl_window_close_index(size_t index) {
    if (!g_windows || index >= g_window_count) return;
    app_instance_t *task = g_windows[index].task;

    if (g_windows[index].task && g_windows[index].task->on_close) {
        g_windows[index].task->on_close(g_windows[index].task->userdata);
    }
    sl_window_shutdown(&g_windows[index]);
    sl_anim_clear_task(task);

    for (size_t i = index + 1u; i < g_window_count; i++) {
        g_windows[i - 1u] = g_windows[i];
    }

    g_window_count--;
    if (g_window_count == 0u) {
        free(g_windows);
        g_windows = NULL;
        g_focused_window = (size_t)-1;
        return;
    }

    sl_focus_top_visible();
}

static void sl_window_refresh_zorder(void) {
    uint64_t now = peos_time_ns();
    for (size_t i = 0; i < g_window_count; i++) {
        uint8_t was_focused = g_windows[i].focused;
        g_windows[i].z_index = (uint32_t)i;
        g_windows[i].focused = (i == g_focused_window) ? 1u : 0u;
        if (g_windows[i].focused != was_focused) {
            g_windows[i].force_redraw = 1u;
            g_windows[i].next_render_ns = 0ull;
            if (g_windows[i].focused) {
                sl_window_mark_activity(&g_windows[i], now);
            }
        }
    }
}

static void sl_focus_window(size_t index) {
    if (!g_windows || g_window_count == 0u || index >= g_window_count) return;
    if (index == g_window_count - 1u) {
        g_focused_window = index;
        sl_window_refresh_zorder();
        return;
    }

    sl_window_t win = g_windows[index];
    for (size_t i = index; i + 1u < g_window_count; i++) {
        g_windows[i] = g_windows[i + 1u];
    }
    g_windows[g_window_count - 1u] = win;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();
}

// Focus the topmost window still on screen (none if all are minimized).
static void sl_focus_top_visible(void) {
    g_focused_window = (size_t)-1;
    for (size_t i = g_window_count; i > 0u; i--) {
        if (!g_windows[i - 1u].minimized) {
            g_focused_window = i - 1u;
            break;
        }
    }
    sl_window_refresh_zorder();
}

static void sl_launcher_compact_label(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";

    size_t n = strlen(src);
    if (n < cap) {
        sl_copy_str(dst, cap, src);
        return;
    }

    if (cap <= 4u) {
        sl_copy_str(dst, cap, src);
        return;
    }

    size_t keep = cap - 4u;
    for (size_t i = 0u; i < keep; i++) {
        dst[i] = src[i];
    }
    dst[keep] = '.';
    dst[keep + 1u] = '.';
    dst[keep + 2u] = '.';
    dst[keep + 3u] = '\0';
}

static void sl_launcher_try_load_icon(sl_launcher_item_t *item) {
    if (!item) return;
    if (item->icon_path[0] == '\0') return;
    if (item->icon_loaded || item->icon_failed) return;
    if (sl_decode_image_file(item->icon_path, &item->icon_image)) {
        item->icon_loaded = 1u;
    } else {
        item->icon_failed = 1u;
    }
}

// ------------------------------------------------------- remembered rects

// Each app's window as it was last placed (moved or resized), by bundle:
// it opens there again. windows.cfg: "x y w h bundle" a line.
#define SL_GEOM_MAX 32u
static struct { char bundle[256]; uint32_t x, y, w, h; } g_geom[SL_GEOM_MAX];
static size_t g_geom_count;
static uint8_t g_geom_loaded;

static void sl_geom_load(void) {
    if (g_geom_loaded) return;
    g_geom_loaded = 1u;
    FILE *f = fopen(SL_RESOURCE_WINDOWS_FILE, "r");
    if (!f) return;
    char line[320];
    while (fgets(line, sizeof(line), f) && g_geom_count < SL_GEOM_MAX) {
        unsigned x, y, w, h;
        char bundle[256];
        if (line[0] == '#' || sscanf(line, "%u %u %u %u %255s", &x, &y, &w, &h, bundle) != 5) continue;
        sl_copy_str(g_geom[g_geom_count].bundle, sizeof(g_geom[0].bundle), bundle);
        g_geom[g_geom_count].x = x;
        g_geom[g_geom_count].y = y;
        g_geom[g_geom_count].w = w;
        g_geom[g_geom_count].h = h;
        g_geom_count++;
    }
    fclose(f);
}

static int sl_geom_find(const char *bundle) {
    sl_geom_load();
    for (size_t i = 0u; bundle && bundle[0] && i < g_geom_count; i++) {
        if (strcmp(g_geom[i].bundle, bundle) == 0) return (int)i;
    }
    return -1;
}

static sl_launcher_item_t *sl_launcher_item_for_task(const app_instance_t *task);

// Remember where a window is (after a move or a resize; not while
// maximized: then it is the rect to go back to).
void sl_windows_remember(sl_window_t *win) {
    if (win) win->fit_w = 0u;   // placed by the user: its size before a smaller screen no longer counts
    if (!win || win->maximized || !win->task) return;
    sl_launcher_item_t *item = sl_launcher_item_for_task(win->task);
    if (!item || !item->bundle_path[0] || strchr(item->bundle_path, ' ')) return;
    int i = sl_geom_find(item->bundle_path);
    if (i < 0) {
        if (g_geom_count >= SL_GEOM_MAX) {   // the oldest goes
            memmove(&g_geom[0], &g_geom[1], sizeof(g_geom[0]) * (SL_GEOM_MAX - 1u));
            g_geom_count--;
        }
        i = (int)g_geom_count++;
        sl_copy_str(g_geom[i].bundle, sizeof(g_geom[0].bundle), item->bundle_path);
    }
    if (g_geom[i].x == win->x && g_geom[i].y == win->y && g_geom[i].w == win->width && g_geom[i].h == win->height) return;
    g_geom[i].x = win->x;
    g_geom[i].y = win->y;
    g_geom[i].w = win->width;
    g_geom[i].h = win->height;
    FILE *f = fopen(SL_RESOURCE_WINDOWS_FILE, "w");   // a read-only disk (the live ISO): for this session only
    if (!f) return;
    fprintf(f, "# Starlight: where each app's window opens (x y width height bundle)\n");
    for (size_t k = 0u; k < g_geom_count; k++) {
        fprintf(f, "%u %u %u %u %s\n", (unsigned)g_geom[k].x, (unsigned)g_geom[k].y, (unsigned)g_geom[k].w,
                (unsigned)g_geom[k].h, g_geom[k].bundle);
    }
    fclose(f);
}

static sl_launcher_item_t *sl_launcher_item_for_task(const app_instance_t *task) {
    for (size_t i = 0u; task && i < g_launcher_count; i++) {
        if (g_launcher_items[i].instance == task) return &g_launcher_items[i];
    }
    return NULL;
}

static void sl_launcher_pick_window_rect(const sl_ctx_t *ctx, const sl_launcher_item_t *item,
                                         uint32_t *out_x, uint32_t *out_y,
                                         uint32_t *out_w, uint32_t *out_h) {
    if (!ctx || !out_x || !out_y || !out_w || !out_h) return;

    uint32_t reserved_left = sl_launcher_reserved_left(ctx);
    uint32_t top = sl_windows_top(ctx);
    uint32_t max_w = (ctx->fb.width > reserved_left + 8u)
                   ? (ctx->fb.width - reserved_left - 8u)
                   : ctx->fb.width;
    uint32_t max_h = (ctx->fb.height > top + 8u) ? (ctx->fb.height - top - 8u) : ctx->fb.height;

    uint32_t def_w = (ctx->fb.width > reserved_left + 560u)
                   ? 540u
                   : ((ctx->fb.width > reserved_left + 20u) ? (ctx->fb.width - reserved_left - 20u) : ctx->fb.width);
    uint32_t def_h = (ctx->fb.height > 380u) ? 360u : ctx->fb.height;

    // The app's size is its body as it always was; the soft UI frame goes
    // around it.
    uint32_t w = ((item && item->width > 0u) ? item->width : def_w) + SL_WIN_FRAME_EXTRA_W;
    uint32_t h = ((item && item->height > 0u) ? item->height : def_h) + SL_WIN_FRAME_EXTRA_H;

    if (w > max_w) w = max_w;
    if (h > max_h) h = max_h;

    if (max_w >= 320u && w < 320u) w = 320u;
    if (max_h >= 220u && h < 220u) h = 220u;

    uint32_t cascade = (uint32_t)(g_window_count % 6u);
    uint32_t x = reserved_left + 16u + cascade * 22u;
    uint32_t y = top + cascade * 18u;

    if (x + w > ctx->fb.width) {
        x = (ctx->fb.width > w) ? (ctx->fb.width - w) : 0u;
    }
    if (x < reserved_left) x = reserved_left;
    if (y + h > ctx->fb.height) {
        y = (ctx->fb.height > h) ? (ctx->fb.height - h) : 0u;
    }
    if (y < top) y = top;   // never under the top bar

    // Where it was last time, if that still fits the screen and its limits.
    int g = item ? sl_geom_find(item->bundle_path) : -1;
    if (g >= 0 && g_geom[g].x >= reserved_left && g_geom[g].x + g_geom[g].w <= ctx->fb.width && g_geom[g].y >= top &&
        g_geom[g].y + g_geom[g].h <= ctx->fb.height && (item->fixed_size ? (g_geom[g].w == w && g_geom[g].h == h)
                                                                           : (g_geom[g].w >= 120u && g_geom[g].h >= 120u))) {
        x = g_geom[g].x;
        y = g_geom[g].y;
        w = g_geom[g].w;
        h = g_geom[g].h;
    }

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

// Its app.ini read again just before the app starts: the catalog follows
// only the app directories (an app added, removed or rebuilt), so an
// app.ini edited in place (args=, paths=) took effect only after a
// restart. One small file per launch, nothing while the apps sit still.
// 0: the slot could not be set up again (the app does not start).
static int sl_launcher_reload_manifest(sl_launcher_item_t *item) {
    if (item->bundle_path[0] == '\0') return 1;
    char manifest_path[320];
    sl_app_bundle_t app;
    if (!sl_join_path(manifest_path, sizeof(manifest_path), item->bundle_path, SL_APP_MANIFEST) ||
        !sl_load_app_bundle(manifest_path, item->bundle_path, &app)) {
        return 1;   // unreadable now: as it was
    }
    // configure drops the slot's limits: the app starts only with all of
    // them back
    if (!sl_app_host_slot_configure(item->slot, app.title, app.exec, app.args) ||
        !sl_app_host_slot_restrict_caps(item->slot, app.caps_mask) ||
        !sl_app_host_slot_restrict_paths(item->slot, app.paths, app.path_count) ||
        !sl_app_host_slot_set_bundle(item->slot, item->bundle_path)) {
        return 0;
    }
    item->width = app.width;
    item->height = app.height;
    item->min_width = app.min_width;
    item->min_height = app.min_height;
    item->max_width = app.max_width;
    item->max_height = app.max_height;
    item->fixed_size = app.fixed_size;
    return 1;
}

// app_host's hook before a process starts (the dock, the app manager, Enter
// in a window whose app exited): the item of that slot, its app.ini again.
static int sl_launcher_prespawn(size_t slot) {
    for (size_t i = 0u; i < g_launcher_count; i++) {
        if (g_launcher_items[i].slot == slot) return sl_launcher_reload_manifest(&g_launcher_items[i]);
    }
    return 1;
}

static int sl_windows_open_launcher_item_try(sl_ctx_t *ctx, size_t launcher_index);

// Open (or bring up) an app; when it cannot, a notification says so (the
// click on the dock is not silent).
static int sl_windows_open_launcher_item(sl_ctx_t *ctx, size_t launcher_index) {
    if (!ctx || launcher_index >= g_launcher_count) return 0;
    if (sl_windows_open_launcher_item_try(ctx, launcher_index)) return 1;
    char msg[160];   // a notification's text (peos/notify.h)
    snprintf(msg, sizeof(msg), sl_tr("apps.cant_open"), g_launcher_items[launcher_index].title);
    sl_notify_post(ctx, sl_tr("notify.system"), msg);
    return 0;
}

static int sl_windows_open_launcher_item_try(sl_ctx_t *ctx, size_t launcher_index) {
    if (!ctx || launcher_index >= g_launcher_count) return 0;

    sl_launcher_item_t *item = &g_launcher_items[launcher_index];
    // Starting the process holds Starlight a moment: the wait cursor first
    // (the hardware one shows it at once).
    if (!sl_app_host_instance_peek_at(item->slot)) sl_cursor_set(ctx, SL_CUR_WAIT);
    app_instance_t *app = sl_app_host_instance_at(item->slot);
    if (!app) return 0;
    item->instance = app;

    size_t existing = sl_window_index_for_task(app);
    if (existing != (size_t)-1) {
        sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
        if (g_windows[existing].minimized) printf("starlight: window %s: back (dock)\n", g_windows[existing].title);
        g_windows[existing].minimized = 0u;   // the dock brings a minimized window back
        g_windows[existing].force_redraw = 1u;
        sl_window_rect_snapshot_t target = sl_window_snapshot_index(existing);
        sl_focus_window(existing);
        sl_windows_mark_focused_activity();
        sl_dirty_add_snapshot(ctx, &prev_focus);
        sl_dirty_add_snapshot(ctx, &target);
        return 1;
    }

    sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);

    uint32_t x = 0u, y = 0u, w = 0u, h = 0u;
    sl_launcher_pick_window_rect(ctx, item, &x, &y, &w, &h);

    sl_window_t win;
    if (!sl_window_init(&win, ctx, app, x, y, w, h)) {
        return 0;
    }
    sl_copy_str(win.title, sizeof(win.title), item->title);
    // The limits of a resize: app.ini's min_width/min_height (else its
    // width/height: the size its layout was made for), max_*, resizable.
    {
        uint32_t base_w = item->width ? item->width + SL_WIN_FRAME_EXTRA_W : w;
        uint32_t base_h = item->height ? item->height + SL_WIN_FRAME_EXTRA_H : h;
        win.min_w = item->min_width ? item->min_width + SL_WIN_FRAME_EXTRA_W : base_w;
        win.min_h = item->min_height ? item->min_height + SL_WIN_FRAME_EXTRA_H : base_h;
        if (win.min_w > w) win.min_w = w;   // the screen was smaller than that
        if (win.min_h > h) win.min_h = h;
        win.max_w = item->max_width ? item->max_width + SL_WIN_FRAME_EXTRA_W : 0u;
        win.max_h = item->max_height ? item->max_height + SL_WIN_FRAME_EXTRA_H : 0u;
        win.resizable = item->fixed_size ? 0u : 1u;
        if (win.resizable && win.width < win.min_w) win.width = win.min_w;
        if (win.resizable && win.height < win.min_h) win.height = win.min_h;
    }
    sl_window_set_fps_profile(&win, 30u, 8u);

    sl_window_t *new_windows =
        (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + 1u));
    if (!new_windows) {
        sl_window_shutdown(&win);
        return 0;
    }

    g_windows = new_windows;
    g_windows[g_window_count] = win;
    g_window_count++;
    g_focused_window = g_window_count - 1u;
    sl_window_refresh_zorder();
    sl_anim_start_open(&g_windows[g_window_count - 1u], g_windows[g_window_count - 1u].task,
                       (int32_t)g_windows[g_window_count - 1u].y);
    sl_dirty_add_snapshot(ctx, &prev_focus);
    {
        sl_window_rect_snapshot_t opened = sl_window_snapshot(&g_windows[g_window_count - 1u]);
        sl_dirty_add_snapshot(ctx, &opened);
    }
    return 1;
}

int sl_windows_open_app_bundle_path(sl_ctx_t *ctx, const char *bundle_path) {
    if (!ctx || !bundle_path || bundle_path[0] == '\0') return 0;
    for (size_t i = 0u; i < g_launcher_count; i++) {
        if (g_launcher_items[i].bundle_path[0] == '\0') continue;
        if (strcmp(g_launcher_items[i].bundle_path, bundle_path) != 0) continue;
        return sl_windows_open_launcher_item(ctx, i);
    }

    char manifest_path[320];
    if (!sl_join_path(manifest_path, sizeof(manifest_path), bundle_path,
                      SL_APP_MANIFEST)) {
        return 0;
    }
    sl_app_bundle_t app;
    if (!sl_load_app_bundle(manifest_path, bundle_path, &app)) return 0;

    size_t slot = sl_app_host_next_slot_index();
    if (!sl_app_host_slot_configure(slot, app.title, app.exec, app.args)) return 0;
    if (!sl_app_host_slot_restrict_caps(slot, app.caps_mask)) return 0;
    if (!sl_app_host_slot_restrict_paths(slot, app.paths, app.path_count)) return 0;
    if (!sl_app_host_slot_set_bundle(slot, bundle_path)) return 0;
    if (!sl_launcher_add_item(slot, &app, bundle_path)) return 0;
    sl_dirty_mark_full();
    return sl_windows_open_launcher_item(ctx, g_launcher_count - 1u);
}

static int sl_launcher_catalog_matches(const sl_scanned_app_t *entries, size_t count) {
    if (count != g_launcher_count) return 0;
    for (size_t i = 0u; i < count; i++) {
        if (entries[i].signature != g_launcher_items[i].signature ||
            strcmp(entries[i].bundle_path, g_launcher_items[i].bundle_path) != 0) {
            return 0;
        }
    }
    return 1;
}

static size_t sl_launcher_find_bundle(const char *bundle_path) {
    if (!bundle_path) return (size_t)-1;
    for (size_t i = 0u; i < g_launcher_count; i++) {
        if (strcmp(g_launcher_items[i].bundle_path, bundle_path) == 0) return i;
    }
    return (size_t)-1;
}

static void sl_launcher_release_items(sl_launcher_item_t *items, size_t count) {
    if (!items) return;
    for (size_t i = 0u; i < count; i++) {
        if (items[i].icon_image.data) sl_image_free(&items[i].icon_image);
        free(items[i].own_icon[0]);
        free(items[i].own_icon[1]);
    }
    free(items);
}

static int sl_launcher_reconcile_catalog(sl_ctx_t *ctx,
                                         sl_scanned_app_t *entries,
                                         size_t count,
                                         uint8_t force) {
    (void)ctx;
    if (!force && sl_launcher_catalog_matches(entries, count)) return 0;

    sl_launcher_item_t *next = NULL;
    if (count > 0u) {
        if (count > SIZE_MAX / sizeof(*next)) return 0;
        next = (sl_launcher_item_t *)calloc(count, sizeof(*next));
        if (!next) return 0;
    }

    size_t next_count = 0u;
    for (size_t i = 0u; i < count; i++) {
        sl_scanned_app_t *entry = &entries[i];
        size_t old_index = sl_launcher_find_bundle(entry->bundle_path);
        size_t slot = (old_index != (size_t)-1)
                          ? g_launcher_items[old_index].slot
                          : sl_app_host_next_slot_index();
        if (!sl_app_host_slot_configure(slot, entry->app.title,
                                        entry->app.exec, entry->app.args) ||
            !sl_app_host_slot_restrict_caps(slot, entry->app.caps_mask) ||
            !sl_app_host_slot_restrict_paths(slot, entry->app.paths, entry->app.path_count) ||
            !sl_app_host_slot_set_bundle(slot, entry->bundle_path)) {
            continue;
        }

        sl_launcher_item_t *item = &next[next_count++];
        if (old_index != (size_t)-1) {
            *item = g_launcher_items[old_index];
            g_launcher_items[old_index].icon_image.data = NULL;
            g_launcher_items[old_index].own_icon[0] = NULL;   // moved with the item
            g_launcher_items[old_index].own_icon[1] = NULL;
            item->tiles_ready = 0u;                            // the title may have changed
            // Its own icon stays as it is, scaled, unless its file changed
            // (it was lost at every new app: Orfeus and Abisso showed their initial).
            if (strcmp(item->icon_path, entry->app.icon) != 0) {
                free(item->own_icon[0]);
                free(item->own_icon[1]);
                item->own_icon[0] = item->own_icon[1] = NULL;
                if (item->icon_image.data) sl_image_free(&item->icon_image);
                peos_memset(&item->icon_image, 0, sizeof(item->icon_image));
                item->icon_loaded = 0u;
                item->icon_failed = 0u;
            }
        }

        item->slot = slot;
        item->width = entry->app.width;
        item->height = entry->app.height;
        item->category = entry->app.category;
        item->min_width = entry->app.min_width;
        item->min_height = entry->app.min_height;
        item->max_width = entry->app.max_width;
        item->max_height = entry->app.max_height;
        item->fixed_size = entry->app.fixed_size;
        item->in_dock = entry->app.in_dock;
        item->hidden = entry->app.hidden;
        item->signature = entry->signature;
        item->instance = sl_app_host_instance_peek_at(slot);
        sl_copy_str(item->title, sizeof(item->title), entry->app.title);
        sl_copy_str(item->bundle_path, sizeof(item->bundle_path), entry->bundle_path);
        sl_copy_str(item->icon_path, sizeof(item->icon_path), entry->app.icon);
        if (item->title[0] == '\0') sl_copy_str(item->title, sizeof(item->title), "App");
    }

    sl_launcher_release_items(g_launcher_items, g_launcher_count);
    sl_dock_move_reset();
    g_launcher_items = next;
    g_launcher_count = next_count;
    g_launcher_cap = count;
    // An open window follows its app's title (the language changed).
    uint8_t retitled = 0u;
    for (size_t i = 0u; i < g_launcher_count; i++) {
        if (!g_launcher_items[i].instance) continue;
        size_t w = sl_window_index_for_task(g_launcher_items[i].instance);
        if (w == (size_t)-1 || strcmp(g_windows[w].title, g_launcher_items[i].title) == 0) continue;
        sl_copy_str(g_windows[w].title, sizeof(g_windows[w].title), g_launcher_items[i].title);
        retitled = 1u;
    }
    if (retitled) sl_windows_theme_changed(ctx);   // the title bars are drawn again
    g_launcher_hover_index = (size_t)-1;
    g_launcher_ctx_open = 0u;
    g_launcher_ctx_item_index = (size_t)-1;

    g_dock_pressed = (size_t)-1;
    sl_dirty_mark_full();
    return 1;
}

static size_t sl_load_apps_from_system(sl_ctx_t *ctx) {
    sl_scanned_app_t *entries = NULL;
    size_t count = 0u;
    if (!sl_scan_apps_from_system(&entries, &count)) return 0u;
    sl_app_host_slots_clear();
    (void)sl_launcher_reconcile_catalog(ctx, entries, count, 1u);
    free(entries);
    return g_launcher_count;
}

// Records the app directory's stat and reports whether it changed since the
// last call. Installing, removing or rebuilding a bundle (starbuild recreates
// the bundle folder) adds or removes an entry, which moves the directory's
// mtime; reading every manifest instead stalls the frame on each tick.
static uint8_t sl_launcher_dir_changed(void) {
    uint8_t changed = 0u;
    for (uint32_t d = 0u; d < SL_APP_DIR_COUNT; d++) {
        peos_stat_t st;
        peos_memset(&st, 0, sizeof(st));
        uint8_t bit = (uint8_t)(1u << d);
        if ((int64_t)peos_stat(g_app_dirs[d], strlen(g_app_dirs[d]), &st, sizeof(st)) < 0) {
            // gone (or not made yet): a change only if it was there
            if (g_launcher_dir_known & bit) changed = 1u;
            g_launcher_dir_known &= (uint8_t)~bit;
            continue;
        }
        if (!(g_launcher_dir_known & bit) ||
            st.mtime_ns != g_launcher_dir_stat[d].mtime_ns ||
            st.ctime_ns != g_launcher_dir_stat[d].ctime_ns ||
            st.size != g_launcher_dir_stat[d].size) {
            changed = 1u;
        }
        g_launcher_dir_stat[d] = st;
        g_launcher_dir_known |= bit;
    }
    return changed;
}

void sl_windows_language_changed(sl_ctx_t *ctx) {
    (void)ctx;
    g_launcher_force_scan = 1u;
    g_launcher_next_scan_ns = 0ull;
}

static void sl_launcher_tick_catalog(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    static uint8_t was_open;
    uint8_t open = sl_launcher_manager_open();
    if (open && !was_open) g_launcher_next_scan_ns = 0ull;   // opened: what is there now
    was_open = open;
    if (now < g_launcher_next_scan_ns) return;
    g_launcher_next_scan_ns = now + (open ? SL_APP_CATALOG_SCAN_INTERVAL_NS : SL_APP_CATALOG_SCAN_IDLE_NS);
    uint8_t changed = sl_launcher_dir_changed();
    if (!changed && !g_launcher_force_scan) return;
    g_launcher_force_scan = 0u;

    sl_scanned_app_t *entries = NULL;
    size_t count = 0u;
    if (!sl_scan_apps_from_system(&entries, &count)) return;
    if (count == 0u && g_launcher_count == 1u &&
        g_launcher_items[0].bundle_path[0] == '\0') {
        free(entries);
        return;
    }
    size_t user = 0u;
    for (size_t i = 0u; i < count; i++) {
        user += strncmp(entries[i].bundle_path, SL_USER_APP_DIR "/", sizeof(SL_USER_APP_DIR)) == 0;
    }
    printf("starlight: %u apps (%u in " SL_USER_APP_DIR ")\n", (unsigned)count, (unsigned)user);
    (void)sl_launcher_reconcile_catalog(ctx, entries, count, 0u);
    free(entries);
}

#define SL_LAUNCH_WAIT_NS 5000000000ull   // the wait cursor at most this long

void sl_windows_wake(void) {
    sl_wake_at(g_launcher_next_scan_ns);
    uint64_t now = sl_frame_time_ns();
    for (size_t i = 0; g_windows && i < g_window_count; i++) {
        const sl_window_t *w = &g_windows[i];
        if (w->task && now - w->opened_ns < SL_LAUNCH_WAIT_NS && !sl_app_host_ready(w->task->userdata)) {
            sl_wake_at(w->opened_ns + SL_LAUNCH_WAIT_NS);   // the arrow again, if it never shows
        }
    }
}

uint8_t sl_windows_launching(void) {
    uint64_t now = peos_time_ns();
    for (size_t i = 0; g_windows && i < g_window_count; i++) {
        const sl_window_t *w = &g_windows[i];
        if (now - w->opened_ns >= SL_LAUNCH_WAIT_NS || !w->task) continue;
        if (!sl_app_host_ready(w->task->userdata)) return 1u;
    }
    return 0u;
}

// ----------------------------------------------------------------- resize

// The edges a press at x,y would move: a band a little outside and inside
// the frame, wider at the corners.
enum { SL_EDGE_L = 1, SL_EDGE_R = 2, SL_EDGE_T = 4, SL_EDGE_B = 8 };
#define SL_RESIZE_OUT 6
#define SL_RESIZE_IN 8
#define SL_RESIZE_CORNER 26

static uint32_t sl_window_resize_edges(const sl_window_t *w, uint32_t px, uint32_t py) {
    if (!w->resizable || w->maximized || w->minimized || sl_window_is_closing(w)) return 0u;
    int64_t x = px, y = py, x0 = w->x, y0 = w->y, x1 = x0 + w->width, y1 = y0 + w->height;
    if (x < x0 - SL_RESIZE_OUT || x >= x1 + SL_RESIZE_OUT || y < y0 - SL_RESIZE_OUT || y >= y1 + SL_RESIZE_OUT) return 0u;
    uint32_t e = 0u;
    if (x < x0 + SL_RESIZE_IN) e |= SL_EDGE_L;
    else if (x >= x1 - SL_RESIZE_IN) e |= SL_EDGE_R;
    if (y < y0 + SL_RESIZE_IN) e |= SL_EDGE_T;
    else if (y >= y1 - SL_RESIZE_IN) e |= SL_EDGE_B;
    if (e & (SL_EDGE_L | SL_EDGE_R)) {
        if (y < y0 + SL_RESIZE_CORNER) e |= SL_EDGE_T;
        else if (y >= y1 - SL_RESIZE_CORNER) e |= SL_EDGE_B;
    }
    if (e & (SL_EDGE_T | SL_EDGE_B)) {
        if (x < x0 + SL_RESIZE_CORNER) e |= SL_EDGE_L;
        else if (x >= x1 - SL_RESIZE_CORNER) e |= SL_EDGE_R;
    }
    return e;
}

static int sl_resize_cursor(uint32_t e) {
    if (e == (SL_EDGE_L | SL_EDGE_T) || e == (SL_EDGE_R | SL_EDGE_B)) return SL_CUR_RESIZE_NWSE;
    if (e == (SL_EDGE_R | SL_EDGE_T) || e == (SL_EDGE_L | SL_EDGE_B)) return SL_CUR_RESIZE_NESW;
    if (e & (SL_EDGE_L | SL_EDGE_R)) return SL_CUR_RESIZE_H;
    return SL_CUR_RESIZE_V;
}

// The window whose edge is at x,y (the top one: a window over another hides
// the lower one's edges), and those edges.
static size_t sl_windows_edge_at(uint32_t x, uint32_t y, uint32_t *edges) {
    for (size_t i = g_window_count; g_windows && i > 0u; i--) {
        const sl_window_t *w = &g_windows[i - 1u];
        if (w->minimized || sl_window_is_closing(w)) continue;
        uint32_t e = sl_window_resize_edges(w, x, y);
        if (e) {
            *edges = e;
            return i - 1u;
        }
        if (sl_window_contains_point(w, x, y)) return (size_t)-1;
    }
    return (size_t)-1;
}

static struct {
    uint8_t active;
    const app_instance_t *task;    // the window (the array moves; its task does not)
    uint32_t edges;
    int64_t mx, my;                // the mouse at the press
    int64_t x, y, w, h;            // the frame at the press
} g_resize;

uint8_t sl_windows_resizing(void) {
    return g_resize.active;
}

int sl_windows_resize_press(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    uint32_t edges = 0u;
    size_t idx = sl_windows_edge_at(x, y, &edges);
    if (!ctx || idx == (size_t)-1 || !g_windows[idx].task) return 0;
    if (idx != g_focused_window) {
        sl_window_rect_snapshot_t before = sl_window_snapshot_index(g_focused_window);
        sl_focus_window(idx);
        sl_dirty_add_snapshot(ctx, &before);
        idx = g_focused_window;
        sl_window_dirty_all(ctx, &g_windows[idx]);
    }
    const sl_window_t *w = &g_windows[idx];
    g_resize.active = 1u;
    g_resize.task = w->task;
    g_resize.edges = edges;
    g_resize.mx = x;
    g_resize.my = y;
    g_resize.x = w->x;
    g_resize.y = w->y;
    g_resize.w = w->width;
    g_resize.h = w->height;
    return 1;
}

// One axis: the moving edge follows the mouse, the other stays; the size
// within [lo, hi]; the window within [min_pos, max_end].
static void sl_resize_axis(int64_t start, int64_t size, int64_t d, uint8_t low_edge, uint8_t high_edge, int64_t lo,
                           int64_t hi, int64_t min_pos, int64_t max_end, int64_t *pos, int64_t *len) {
    int64_t p = start, n = size;
    if (low_edge) {
        int64_t end = start + size;
        p = start + d;
        if (p < min_pos) p = min_pos;
        n = end - p;
        if (n < lo) n = lo;
        if (n > hi) n = hi;
        p = end - n;
    } else if (high_edge) {
        n = size + d;
        if (n < lo) n = lo;
        if (n > hi) n = hi;
        if (p + n > max_end) n = max_end - p;
        if (n < lo) n = lo;
    }
    *pos = p;
    *len = n;
}

void sl_windows_resize_move(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_resize.active) return;
    size_t idx = sl_window_index_for_task(g_resize.task);
    if (idx == (size_t)-1) {
        g_resize.active = 0u;
        return;
    }
    sl_window_t *w = &g_windows[idx];
    int64_t left = sl_launcher_reserved_left(ctx);
    int64_t max_w = w->max_w ? w->max_w : (int64_t)ctx->fb.width - left;
    int64_t top = sl_windows_top(ctx);
    int64_t max_h = w->max_h ? w->max_h : (int64_t)ctx->fb.height - top;
    int64_t min_w = w->min_w ? w->min_w : 200, min_h = w->min_h ? w->min_h : 150;
    if (max_w < min_w) max_w = min_w;
    if (max_h < min_h) max_h = min_h;
    int64_t nx, ny, nw, nh;
    sl_resize_axis(g_resize.x, g_resize.w, (int64_t)x - g_resize.mx, (g_resize.edges & SL_EDGE_L) != 0u,
                   (g_resize.edges & SL_EDGE_R) != 0u, min_w, max_w, left, ctx->fb.width, &nx, &nw);
    sl_resize_axis(g_resize.y, g_resize.h, (int64_t)y - g_resize.my, (g_resize.edges & SL_EDGE_T) != 0u,
                   (g_resize.edges & SL_EDGE_B) != 0u, min_h, max_h, top, ctx->fb.height, &ny, &nh);
    if (nx == w->x && ny == w->y && nw == w->width && nh == w->height) return;
    sl_window_dirty_all(ctx, w);   // where it was, shadow included
    w->x = (uint32_t)nx;
    w->y = (uint32_t)ny;
    w->width = (uint32_t)nw;
    w->height = (uint32_t)nh;
    w->force_redraw = 1u;
    sl_window_mark_activity(w, 0ull);
    sl_window_dirty_all(ctx, w);
}

void sl_windows_resize_release(sl_ctx_t *ctx) {
    (void)ctx;
    if (!g_resize.active) return;
    g_resize.active = 0u;
    size_t idx = sl_window_index_for_task(g_resize.task);
    if (idx != (size_t)-1) sl_windows_remember(&g_windows[idx]);
}

int sl_windows_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    (void)ctx;
    if (g_resize.active) return sl_resize_cursor(g_resize.edges);
    uint32_t edges = 0u;
    if (sl_windows_edge_at(x, y, &edges) != (size_t)-1) return sl_resize_cursor(edges);
    for (size_t i = g_window_count; g_windows && i > 0u; i--) {
        const sl_window_t *w = &g_windows[i - 1u];
        if (w->minimized || sl_window_is_closing(w) || !sl_window_contains_point(w, x, y)) continue;
        if (sl_window_button_at(w, x, y) != SL_WIN_BTN_NONE) return SL_CUR_HAND;
        if (y < w->y + w->top_bar_height) return SL_CUR_ARROW;
        return w->task ? sl_app_host_cursor(w, w->task->userdata, x, y) : SL_CUR_ARROW;
    }
    return -1;
}

sl_window_t *sl_windows_top_at(uint32_t x, uint32_t y) {
    for (size_t i = g_window_count; g_windows && i > 0u; i--) {
        sl_window_t *w = &g_windows[i - 1u];
        if (w->minimized || sl_window_is_closing(w)) continue;
        if (sl_window_contains_point(w, x, y)) return w;
    }
    return NULL;
}

int sl_windows_any_at(uint32_t x, uint32_t y) {
    for (size_t i = 0; g_windows && i < g_window_count; i++) {
        if (g_windows[i].minimized || sl_window_is_closing(&g_windows[i])) continue;
        if (sl_window_contains_point(&g_windows[i], x, y)) return 1;
    }
    return 0;
}

// The launcher item of the bundle <id>.app, in /system/apps or /apps.
static size_t sl_launcher_find_id(const char *id) {
    if (!id) return (size_t)-1;
    size_t n = strlen(id);
    for (size_t i = 0; i < g_launcher_count; i++) {
        const char *base = strrchr(g_launcher_items[i].bundle_path, '/');
        base = base ? base + 1 : g_launcher_items[i].bundle_path;
        if (strncmp(base, id, n) == 0 && strcmp(base + n, ".app") == 0) return i;
    }
    return (size_t)-1;
}

int sl_windows_open_app_id(sl_ctx_t *ctx, const char *id) {
    if (!ctx) return 0;
    size_t i = sl_launcher_find_id(id);
    if (i == (size_t)-1) return 0;
    (void)sl_windows_open_launcher_item(ctx, i);
    return 1;
}

// The app <id>, given <file> once it listens (Impostazioni: "tab:wifi").
int sl_windows_open_app_id_with(sl_ctx_t *ctx, const char *id, const char *file) {
    if (!ctx) return 0;
    size_t i = sl_launcher_find_id(id);
    if (i == (size_t)-1) return 0;
    sl_app_host_open_with(g_launcher_items[i].bundle_path, file);
    return 1;
}

// The app whose title is <title> (a notification's source), any case.
int sl_windows_open_app_title(sl_ctx_t *ctx, const char *title) {
    if (!ctx || !title || !title[0]) return 0;
    for (size_t i = 0u; i < g_launcher_count; i++) {
        const char *a = g_launcher_items[i].title, *b = title;
        for (;; a++, b++) {
            char x = *a, y = *b;
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y) break;
            if (!x) {
                (void)sl_windows_open_launcher_item(ctx, i);
                return 1;
            }
        }
    }
    return 0;
}

const char *sl_windows_app_title(const char *id) {
    size_t i = sl_launcher_find_id(id);
    return i == (size_t)-1 ? NULL : g_launcher_items[i].title;
}

void sl_windows_focus_at_cursor(uint32_t x, uint32_t y) {
    if (!g_windows || g_window_count == 0u) return;
    for (size_t i = g_window_count; i > 0u; i--) {
        size_t idx = i - 1u;
        if (sl_window_is_closing(&g_windows[idx])) continue;
        if (sl_window_contains_point(&g_windows[idx], x, y)) {
            sl_focus_window(idx);
            return;
        }
    }
}

// The button sinks (or comes back up): the inside is redrawn once.
static void sl_window_set_pressed(sl_ctx_t *ctx, sl_window_t *win, int b) {
    if (win->pressed_btn == b) return;
    int shown = (b != SL_WIN_BTN_NONE) ? b : win->pressed_btn;
    win->pressed_btn = (int8_t)b;
    win->force_redraw = 1u;
    win->next_render_ns = 0ull;
    uint32_t bx, by;
    sl_window_button_rect(win, shown, &bx, &by);
    sl_dirty_add(ctx, bx, by, SL_WIN_BTN, SL_WIN_BTN);
}

// Title bar buttons of the focused window: close, minimize, maximize. The
// press sinks the button; the action comes on the release over it.
int sl_windows_close_at_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_windows || g_focused_window >= g_window_count) return 0;
    sl_window_t *win = &g_windows[g_focused_window];
    if (sl_window_is_closing(win)) return 0;
    int b = sl_window_button_at(win, x, y);
    if (b == SL_WIN_BTN_NONE) return 0;
    sl_window_set_pressed(ctx, win, b);
    g_title_press_active = 1u;
    g_title_press_btn = b;
    return 1;
}

// While the button is held: it shows sunk only with the pointer over it.
int sl_windows_title_button_held(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_windows || g_focused_window >= g_window_count) return 0;
    sl_window_t *win = &g_windows[g_focused_window];
    if (!g_title_press_active) return 0;
    int over = sl_window_button_at(win, x, y);
    sl_window_set_pressed(ctx, win, over == g_title_press_btn ? over : SL_WIN_BTN_NONE);
    return 1;
}

static int sl_windows_title_button_act(sl_ctx_t *ctx, size_t idx, int b);

// A double click on the title bar: maximize, or back.
void sl_windows_toggle_maximize(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_focused_window >= g_window_count) return;
    if (!g_windows[g_focused_window].resizable && !g_windows[g_focused_window].maximized) return;
    (void)sl_windows_title_button_act(ctx, g_focused_window, SL_WIN_BTN_MAX);
}

// The windows from the keyboard (docs/system-apps-ux-plan.md, phase 4: Caps
// with a key). Each acts as its mouse twin does.

// Caps+Tab: the window at the bottom of the stack comes up, a minimized one
// too; pressed again and again it goes through all of them.
void sl_windows_key_next(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_window_count == 0u) return;
    size_t pick = (size_t)-1;
    for (size_t i = 0u; i < g_window_count; i++) {   // the lowest one not closing
        if (!sl_window_is_closing(&g_windows[i])) { pick = i; break; }
    }
    if (pick == (size_t)-1) return;
    if (pick == g_focused_window && !g_windows[pick].minimized) return;   // the only one, already in front
    sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
    g_windows[pick].minimized = 0u;
    g_windows[pick].force_redraw = 1u;
    sl_focus_window(pick);
    printf("starlight: window %s: in front (Caps+Tab)\n", g_windows[g_focused_window].title);
    sl_windows_mark_focused_activity();
    sl_dirty_add_snapshot(ctx, &prev_focus);
    sl_window_rect_snapshot_t now = sl_window_snapshot_index(g_focused_window);
    sl_dirty_add_snapshot(ctx, &now);
}

// Caps+W: the window in front closes, as with its X (an app with work not
// saved asks first).
void sl_windows_key_close(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_focused_window >= g_window_count) return;
    if (g_windows[g_focused_window].minimized || sl_window_is_closing(&g_windows[g_focused_window])) return;
    printf("starlight: window %s: close (Caps+W)\n", g_windows[g_focused_window].title);
    (void)sl_windows_title_button_act(ctx, g_focused_window, SL_WIN_BTN_CLOSE);
}

// Caps+M: the window in front is minimized (the dock brings it back).
void sl_windows_key_minimize(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_focused_window >= g_window_count) return;
    if (g_windows[g_focused_window].minimized || sl_window_is_closing(&g_windows[g_focused_window])) return;
    printf("starlight: window %s: minimized (Caps+M)\n", g_windows[g_focused_window].title);
    (void)sl_windows_title_button_act(ctx, g_focused_window, SL_WIN_BTN_MIN);
}

// Caps+Space: the app manager opens (its search takes what is typed next), or closes.
void sl_launcher_key_toggle(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    sl_launcher_toggle(ctx);
    printf("starlight: app manager %s (Caps+Space)\n", g_launcher_expanded ? "open" : "closed");
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    sl_dock_dirty_panel(ctx, &L);
}

int sl_windows_title_button_release(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_title_press_active) return 0;
    int pressed = g_title_press_btn;
    g_title_press_active = 0u;
    g_title_press_btn = SL_WIN_BTN_NONE;
    if (!g_windows || g_focused_window >= g_window_count) return 1;
    sl_window_t *win = &g_windows[g_focused_window];
    sl_window_set_pressed(ctx, win, SL_WIN_BTN_NONE);
    if (sl_window_is_closing(win) || sl_window_button_at(win, x, y) != pressed) return 1;
    (void)sl_windows_title_button_act(ctx, g_focused_window, pressed);
    return 1;
}

static int sl_windows_title_button_act(sl_ctx_t *ctx, size_t idx, int b) {
    sl_window_t *win = &g_windows[idx];
    sl_window_rect_snapshot_t before = sl_window_snapshot_index(idx);
    sl_windows_mark_focused_activity();
    if (b == SL_WIN_BTN_CLOSE) {
        if (win->task && win->task->on_close_request && win->task->on_close_request(win->task->userdata))
            return 1;   // work not saved: the app asks first
        sl_anim_start_close(win->task, (int32_t)win->y);
        sl_dirty_add_snapshot(ctx, &before);
        return 1;
    }
    if (b == SL_WIN_BTN_MIN) {
        // Hidden at the bottom of the stack; focus goes to the next one.
        sl_window_t moved = *win;
        moved.minimized = 1u;
        for (size_t i = idx; i > 0u; i--) g_windows[i] = g_windows[i - 1u];
        g_windows[0] = moved;
        sl_focus_top_visible();
        sl_dirty_add_snapshot(ctx, &before);
        sl_window_rect_snapshot_t now_focused = sl_window_snapshot_index(g_focused_window);
        sl_dirty_add_snapshot(ctx, &now_focused);
        return 1;
    }
    // Maximize: the space right of the dock, with a margin for the shadow;
    // again to go back. Not a window of a fixed size (the double click too).
    if (!win->resizable && !win->maximized) return 1;
    if (!win->maximized) {
        uint32_t left = sl_launcher_reserved_left(ctx) + 24u;
        uint32_t m = 24u, top = sl_windows_top(ctx);
        if (ctx->fb.width <= left + m + 200u || ctx->fb.height <= top + m + 150u) return 1;
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_w = win->width;
        win->restore_h = win->height;
        win->x = left;
        win->y = top;
        win->width = ctx->fb.width - left - m;
        win->height = ctx->fb.height - top - m;
        win->maximized = 1u;
    } else {
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->width = win->restore_w;
        win->height = win->restore_h;
        win->maximized = 0u;
    }
    win->force_redraw = 1u;
    win->next_render_ns = 0ull;
    sl_dirty_add_snapshot(ctx, &before);
    sl_window_rect_snapshot_t after = sl_window_snapshot_index(idx);
    sl_dirty_add_snapshot(ctx, &after);
    return 1;
}

void sl_windows_create_test(sl_ctx_t *ctx, size_t count) {
    if (!ctx || count == 0u) return;

    for (size_t i = 0; i < count; i++) {
        sl_window_t w;
        sl_window_init(&w, ctx, NULL, 50u + (i * 30u), 50u + (i * 30u), 200u, 150u);

        sl_window_t *new_windows =
            (sl_window_t *)realloc(g_windows, sizeof(sl_window_t) * (g_window_count + 1u));
        if (!new_windows) continue;

        g_windows = new_windows;
        g_windows[g_window_count] = w;
        g_window_count++;
        g_focused_window = g_window_count - 1u;
        sl_window_refresh_zorder();
        sl_anim_start_open(&g_windows[g_window_count - 1u], g_windows[g_window_count - 1u].task,
                           (int32_t)g_windows[g_window_count - 1u].y);
    }
}

void sl_windows_create_default(sl_ctx_t *ctx) {
    if (!ctx) return;

    (void)sl_launcher_dir_changed();   // baseline for sl_launcher_tick_catalog
    sl_app_host_set_prespawn(sl_launcher_prespawn);
    sl_dock_pins_load();
    size_t loaded = sl_load_apps_from_system(ctx);
    if (loaded == 0u) {
        sl_app_host_slots_use_default();
        sl_launcher_items_reset();

        sl_app_bundle_t fallback;
        peos_memset(&fallback, 0, sizeof(fallback));
        sl_copy_str(fallback.title, sizeof(fallback.title), "Orbit");
        (void)sl_launcher_add_item(0u, &fallback, NULL);
    }

    g_launcher_next_scan_ns = peos_time_ns() + SL_APP_CATALOG_SCAN_INTERVAL_NS;
    sl_dirty_mark_full();
}

void sl_windows_mark_app_dirty(const sl_ctx_t *ctx, const void *userdata) {
    if (!ctx || !userdata || !g_windows || g_window_count == 0u) return;
    uint64_t now = peos_time_ns();
    for (size_t i = 0; i < g_window_count; i++) {
        if (!g_windows[i].task) continue;
        if (g_windows[i].task->userdata != userdata) continue;
        sl_window_mark_activity(&g_windows[i], now);
        sl_window_dirty_body(ctx, &g_windows[i]);
    }
}

void sl_windows_mark_app_pixels(const sl_ctx_t *ctx, const void *userdata) {
    if (!ctx || !userdata || !g_windows) return;
    uint64_t now = peos_time_ns();
    for (size_t i = 0; i < g_window_count; i++) {
        if (!g_windows[i].task || g_windows[i].task->userdata != userdata) continue;
        g_windows[i].pixels_pending = 1u;   // where: asked when it is drawn (sl_window_prepare_render)
        g_windows[i].last_activity_ns = now;
        g_windows[i].next_render_ns = 0ull;
    }
}

void sl_windows_mark_app_dirty_all(const sl_ctx_t *ctx, const void *userdata) {
    for (size_t i = 0; ctx && userdata && g_windows && i < g_window_count; i++) {
        if (!g_windows[i].task || g_windows[i].task->userdata != userdata) continue;
        g_windows[i].force_redraw = 1u;
        sl_window_mark_activity(&g_windows[i], peos_time_ns());
        sl_window_dirty_all(ctx, &g_windows[i]);
    }
}

void sl_windows_mark_focused_activity(void) {
    sl_window_t *focused = sl_window_get_focused();
    if (!focused) return;
    sl_window_mark_activity(focused, peos_time_ns());
}

void sl_windows_tick_animations(sl_ctx_t *ctx) {
    uint8_t any_active = 0u;
    if (!ctx) return;

    sl_launcher_tick_catalog(ctx);
    /* The launcher tween must keep repainting even when the desktop has no windows. */
    sl_launcher_track_animation_dirty(ctx);
    if (!g_windows || g_window_count == 0u) return;

    for (size_t i = 0u; i < g_window_count; ) {
        sl_window_t *win = &g_windows[i];
        int32_t anim_idx = (win->task) ? sl_anim_find_slot(win->task) : -1;
        win->anim_opacity = 255u;

        if (anim_idx < 0) {
            i++;
            continue;
        }

        sl_window_anim_t *anim = &g_window_anims[(size_t)anim_idx];
        sl_window_rect_snapshot_t before = sl_window_snapshot(win);

        int32_t y = (int32_t)(anim->y_value + 0.5f);
        if (y < 0) y = 0;
        win->y = (uint32_t)y;
        if (anim->opacity > 255.0f) anim->opacity = 255.0f;
        if (anim->opacity < 0.0f) anim->opacity = 0.0f;
        win->anim_opacity = (uint8_t)(anim->opacity + 0.5f);
        win->force_redraw = 1u;
        win->next_render_ns = 0ull;
        sl_window_mark_activity(win, peos_time_ns());

        sl_dirty_add_snapshot(ctx, &before);
        sl_window_dirty_all(ctx, win);

        if (anim->close_ready) {
            sl_window_rect_snapshot_t closing = sl_window_snapshot(win);
            sl_window_close_index(i);
            sl_dirty_add_snapshot(ctx, &closing);
            {
                sl_window_rect_snapshot_t focused_now = sl_window_snapshot_index(g_focused_window);
                sl_dirty_add_snapshot(ctx, &focused_now);
            }
            continue;
        }

        if (anim->closing &&
            !sl_tween_is_active_id(sl_anim_tween_id((size_t)anim_idx, 0u)) &&
            !sl_tween_is_active_id(sl_anim_tween_id((size_t)anim_idx, 1u))) {
            /* Handles runtime disable/reset of tween system while a close animation is in-flight. */
            sl_window_rect_snapshot_t closing = sl_window_snapshot(win);
            anim->close_ready = 1u;
            sl_window_close_index(i);
            sl_dirty_add_snapshot(ctx, &closing);
            {
                sl_window_rect_snapshot_t focused_now = sl_window_snapshot_index(g_focused_window);
                sl_dirty_add_snapshot(ctx, &focused_now);
            }
            continue;
        }

        if (!anim->closing &&
            !sl_tween_is_active_id(sl_anim_tween_id((size_t)anim_idx, 0u)) &&
            !sl_tween_is_active_id(sl_anim_tween_id((size_t)anim_idx, 1u)) &&
            anim->opacity >= 254.5f) {
            sl_anim_clear_slot((size_t)anim_idx);
        } else {
            any_active = 1u;
        }

        i++;
    }

    (void)any_active;
}

void sl_windows_theme_changed(sl_ctx_t *ctx) {
    (void)ctx;
    for (size_t i = 0u; g_windows && i < g_window_count; i++) {
        g_windows[i].force_redraw = 1u;
        g_windows[i].cache_valid = 0u;
        g_windows[i].next_render_ns = 0ull;
    }
}

// The screen changed size (the interface scale): maximized windows fill it
// again, the others shrink if they are resizable and move back inside.
// A window's rect inside the room a screen of sw x sh leaves (right of the
// dock, under the top bar): smaller if it is larger and may be resized
// (below its app.ini minimum too: the screen is the limit), then moved in.
// The top bar and the dock win over the far edges: a fixed window larger
// than the room starts at its top left and goes past the bottom or right.
static void sl_window_fit_rect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h, uint8_t resizable, uint32_t left,
                               uint32_t top, uint32_t sw, uint32_t sh) {
    uint32_t room_w = sw > left + 8u ? sw - left - 8u : sw;
    uint32_t room_h = sh > top + 8u ? sh - top - 8u : sh;
    if (resizable) {
        if (*w > room_w) *w = room_w;
        if (*h > room_h) *h = room_h;
    }
    if ((uint64_t)*x + *w > sw) *x = sw > *w ? sw - *w : 0u;
    if (*x < left) *x = left;
    if ((uint64_t)*y + *h > sh) *y = sh > *h ? sh - *h : 0u;
    if (*y < top) *y = top;
}

void sl_windows_screen_changed(sl_ctx_t *ctx) {
    if (!ctx || !g_windows) return;
    uint32_t left = sl_launcher_reserved_left(ctx);
    uint32_t top = sl_windows_top(ctx);
    uint32_t sw = ctx->fb.width, sh = ctx->fb.height;
    for (size_t i = 0u; i < g_window_count; i++) {
        sl_window_t *win = &g_windows[i];
        // Fitted to a smaller screen before: from where it was then.
        uint32_t x = win->x, y = win->y, w = win->width, h = win->height;
        if (!win->maximized && win->fit_w) {
            x = win->fit_x;
            y = win->fit_y;
            w = win->fit_w;
            h = win->fit_h;
        }
        uint32_t ox = x, oy = y, ow = w, oh = h;
        if (win->maximized) {
            uint32_t l = left + 24u, m = 24u;
            if (sw > l + m + 200u && sh > top + m + 150u) {
                win->x = l;
                win->y = top;
                win->width = sw - l - m;
                win->height = sh - top - m;
            } else {
                win->x = left;
                win->y = top;
                win->width = sw > left ? sw - left : sw;
                win->height = sh > top ? sh - top : sh;
            }
            // Restored later: to a rect that fits too.
            sl_window_fit_rect(&win->restore_x, &win->restore_y, &win->restore_w, &win->restore_h, win->resizable,
                               left, top, sw, sh);
        } else {
            sl_window_fit_rect(&x, &y, &w, &h, win->resizable, left, top, sw, sh);
            if (x != ox || y != oy || w != ow || h != oh) {
                if (!win->fit_w) {   // its own place, kept for a larger screen
                    win->fit_x = ox;
                    win->fit_y = oy;
                    win->fit_w = ow;
                    win->fit_h = oh;
                }
            } else {
                win->fit_w = 0u;   // all of it fits again
            }
            if (x != win->x || y != win->y || w != win->width || h != win->height) {
                printf("starlight: window %s %ux%u at %u,%u -> %ux%u at %u,%u (screen %ux%u)\n", win->title,
                       (unsigned)win->width, (unsigned)win->height, (unsigned)win->x, (unsigned)win->y, (unsigned)w,
                       (unsigned)h, (unsigned)x, (unsigned)y, (unsigned)sw, (unsigned)sh);
            }
            win->x = x;
            win->y = y;
            win->width = w;
            win->height = h;
        }
        win->force_redraw = 1u;
        win->cache_valid = 0u;
        win->next_render_ns = 0ull;
    }
    sl_dirty_mark_full();
}

int sl_windows_top_opaque_rect(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!ctx || !g_windows) return 0;
    for (size_t i = g_window_count; i > 0u; i--) {
        const sl_window_t *win = &g_windows[i - 1u];
        if (win->minimized) continue;
        // A fading window blends with what is under it: that must be there.
        if (win->anim_opacity < 255u || sl_window_is_closing(win)) return 0;
        if (win->width <= 2u * SL_WIN_OPAQUE_INSET || win->height <= 2u * SL_WIN_OPAQUE_INSET) return 0;
        uint64_t x1 = (uint64_t)win->x + win->width - SL_WIN_OPAQUE_INSET;
        uint64_t y1 = (uint64_t)win->y + win->height - SL_WIN_OPAQUE_INSET;
        if (x1 > ctx->fb.width) x1 = ctx->fb.width;
        if (y1 > ctx->fb.height) y1 = ctx->fb.height;
        uint32_t x0 = win->x + SL_WIN_OPAQUE_INSET, y0 = win->y + SL_WIN_OPAQUE_INSET;
        if (x1 <= x0 || y1 <= y0) return 0;
        *x = x0;
        *y = y0;
        *w = (uint32_t)(x1 - x0);
        *h = (uint32_t)(y1 - y0);
        return 1;
    }
    return 0;
}

int sl_windows_prepare_render(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_window_count == 0u) return 0;
    uint64_t now = peos_time_ns();
    int waiting = 0;
    for (size_t i = 0; i < g_window_count; i++) {
        sl_window_t *win = &g_windows[i];
        if (win->minimized) continue;
        int due = sl_window_prepare_render(win, now);
        if (due == 2) {   // in part: only there
            sl_dirty_add(ctx, win->part_x, win->part_y, win->part_w, win->part_h);
        } else if (due) {
            // Its inside is written whole: all that lies over it is composed again there.
            sl_window_dirty_inside(ctx, win);
        } else if (win->content_pending || win->pixels_pending) {
            waiting = 1;   // held back by its frame rate: a frame soon
        }
    }
    return waiting;
}

void sl_windows_render_all(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_window_count == 0u) return;
    for (size_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].minimized) continue;
        sl_window_rect_snapshot_t r = sl_window_snapshot(&g_windows[i]);
        if (!r.valid || !sl_dirty_intersects_rect(r.x, r.y, r.w, r.h)) continue;
        sl_window_render(&g_windows[i]);
    }
}

void sl_windows_destroy_all(void) {
    for (size_t i = 0u; i < SL_WIN_ANIM_MAX; i++) {
        if (g_window_anims[i].used) sl_anim_clear_slot(i);
    }
    if (g_windows) {
        for (size_t i = 0; i < g_window_count; i++) {
            if (g_windows[i].task && g_windows[i].task->on_close) {
                g_windows[i].task->on_close(g_windows[i].task->userdata);
            }
            sl_window_shutdown(&g_windows[i]);
        }
        free(g_windows);
    }

    g_windows = NULL;
    g_window_count = 0u;
    g_focused_window = (size_t)-1;
    sl_launcher_items_reset();
    g_launcher_hover_index = (size_t)-1;
    g_launcher_ctx_open = 0u;
    g_launcher_ctx_item_index = (size_t)-1;
    g_launcher_ctx_x = 0u;
    g_launcher_ctx_y = 0u;
    g_launcher_ctx_w = 0u;
    g_launcher_ctx_h = 0u;
    g_launcher_next_scan_ns = 0ull;
    g_launcher_dir_known = 0u;
    g_launcher_ctx_info = 0u;
    g_launcher_ctx_rows = 0u;
    g_dock_pin_count = 0u;
    g_dock_pins_set = 0u;
    sl_dock_move_reset();
}

void sl_window_move(sl_window_t *win, uint32_t new_x, uint32_t new_y) {
    if (!win) return;
    win->x = new_x;
    win->y = new_y;
}

sl_window_t *sl_windows_find_task(const app_instance_t *task) {
    size_t i = sl_window_index_for_task(task);
    return i == (size_t)-1 ? NULL : &g_windows[i];
}

sl_window_t* sl_window_get_focused(void) {
    if (!g_windows || g_window_count == 0u || g_focused_window >= g_window_count) return NULL;
    return &g_windows[g_focused_window];
}

uint32_t sl_launcher_reserved_left(const sl_ctx_t *ctx) {
    (void)ctx;
    if (g_launcher_count == 0u) return 0u;
    // The dock and its fin; the app manager covers windows, it does not push them.
    return SL_DOCK_X + SL_DOCK_W + SL_FIN_W + SL_LAUNCHER_RESERVED_GAP;
}

void sl_launcher_mark_dirty(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    sl_dock_dirty_panel(ctx, &L);
}

int sl_launcher_covers_point(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || g_launcher_extra_width == 0u) return 0;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    return sl_point_in_rect(x, y, L.cx, L.cy, L.cw + SL_FIN_W, L.ch);
}

// Where the app dragged at (x, y) goes among the pinned tiles (the others,
// not it): the place under the mouse in the dock laid out with an empty
// place for it. -1: not over the dock.
static int32_t sl_dock_drop_slot(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    sl_dock_layout_t P;
    sl_dock_layout_ex(ctx, &P, g_app_drag.item, 0);
    if (P.count == 0u || x >= P.px + P.pw + 24u) return -1;
    if (y + 24u < P.py || y >= P.py + P.ph + 24u) return -1;
    int64_t rel = (int64_t)y - (int64_t)P.cell0 + (int64_t)P.cell_h / 2 - (int64_t)SL_DOCK_TILE / 2;
    size_t slot = rel <= 0 || P.cell_h == 0u ? 0u : (size_t)(rel / (int64_t)P.cell_h);
    if (slot > P.pinned) slot = P.pinned;
    return (int32_t)slot;
}

// What the drag shows, to be drawn again: the tile under the mouse, its
// shadow included (a narrower rect left a trail of it behind).
static void sl_app_drag_dirty(sl_ctx_t *ctx) {
    if (!g_app_drag.active || g_app_drag.item >= g_launcher_count) return;
    int64_t x = (int64_t)g_app_drag.x - SL_DOCK_TILE / 2, y = (int64_t)g_app_drag.y - SL_DOCK_TILE / 2;
    const slsk_entry_t *e = g_launcher_items[g_app_drag.item].tiles[0];
    if (!e) e = g_dock.hi_pad_ref;
    sl_dirty_add_i64(ctx, x - 8, y - 8, SL_DOCK_TILE + 16, SL_DOCK_TILE + 16);
    sl_dirty_sprite(ctx, e, x, y, SL_DOCK_TILE, SL_DOCK_TILE);
}

static void sl_app_drag_arm(size_t item, uint8_t from_mgr, uint32_t x, uint32_t y) {
    peos_memset(&g_app_drag, 0, sizeof(g_app_drag));
    g_app_drag.armed = 1u;
    g_app_drag.from_mgr = from_mgr;
    g_app_drag.item = item;
    g_app_drag.movable = (item != sl_dock_settings_item() && g_launcher_items[item].bundle_path[0]) ? 1u : 0u;
    g_app_drag.sx = g_app_drag.x = x;
    g_app_drag.sy = g_app_drag.y = y;
    g_app_drag.slot = -1;
}

int sl_launcher_drag_held(void) {
    return g_app_drag.armed ? 1 : 0;
}

void sl_launcher_drag_move(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_app_drag.armed) return;
    if (!g_app_drag.active) {
        uint32_t dx = x > g_app_drag.sx ? x - g_app_drag.sx : g_app_drag.sx - x;
        uint32_t dy = y > g_app_drag.sy ? y - g_app_drag.sy : g_app_drag.sy - y;
        if (!g_app_drag.movable || dx + dy < SL_APP_DRAG_START) return;
        // Its tile's press and its name go first, while it is still in the
        // dock (once dragged it is out of the layout, and so out of their damage).
        if (g_dock_pressed != (size_t)-1) {   // no longer a press on its tile
            sl_dock_dirty_item(ctx, g_dock_pressed);
            g_dock_pressed = (size_t)-1;
        }
        sl_dock_set_hover_at(ctx, (size_t)-1, 0u);
        g_app_drag.active = 1u;
        printf("starlight: dock: dragging %s\n", g_launcher_items[g_app_drag.item].title);
    }
    sl_app_drag_dirty(ctx);   // where it was
    g_app_drag.x = x;
    g_app_drag.y = y;
    // The dock opens an empty place there (sl_dock_move_tick slides the tiles).
    g_app_drag.slot = sl_dock_drop_slot(ctx, x, y);
    sl_app_drag_dirty(ctx);   // where it is
}

// Pins <item_index> among the pinned tiles at <slot> (0: first; counted
// among the others, without it), or moves it there if it was pinned
// already. 0 when the dock has no room for it.
static int sl_dock_pin_place(sl_ctx_t *ctx, size_t item_index, size_t slot) {
    sl_dock_layout_t L, O;
    sl_dock_layout_ex(ctx, &L, (size_t)-1, -1);
    size_t settings = sl_dock_settings_item();
    const char *path = g_launcher_items[item_index].bundle_path;
    if (item_index == settings || path[0] == '\0') return 1;
    uint8_t pinned = sl_dock_is_pinned(&L, item_index);
    if (!pinned && L.pinned >= sl_dock_capacity(ctx, settings, SL_DOCK_PAD_TIGHT, SL_DOCK_GAP_TIGHT)) return 0;
    sl_dock_pins_start(ctx);
    // Where: before the other pinned tile at <slot>, or after the last one.
    sl_dock_layout_ex(ctx, &O, item_index, -1);
    char anchor[256];
    anchor[0] = '\0';
    uint8_t after = 0u;
    if (slot < O.pinned) {
        sl_copy_str(anchor, sizeof(anchor), g_launcher_items[O.item[slot]].bundle_path);
    } else if (O.pinned > 0u) {
        sl_copy_str(anchor, sizeof(anchor), g_launcher_items[O.item[O.pinned - 1u]].bundle_path);
        after = 1u;
    }
    size_t at = sl_dock_pin_find(path);
    if (at != (size_t)-1) {
        for (size_t i = at; i + 1u < g_dock_pin_count; i++) {
            peos_memcpy(g_dock_pins[i], g_dock_pins[i + 1u], sizeof(g_dock_pins[0]));
        }
        g_dock_pin_count--;
    } else if (g_dock_pin_count >= SL_DOCK_MAX) {
        return 0;
    }
    size_t pos = g_dock_pin_count;
    size_t a = anchor[0] ? sl_dock_pin_find(anchor) : (size_t)-1;
    if (a != (size_t)-1) pos = a + after;
    for (size_t i = g_dock_pin_count; i > pos; i--) {
        peos_memcpy(g_dock_pins[i], g_dock_pins[i - 1u], sizeof(g_dock_pins[0]));
    }
    sl_copy_str(g_dock_pins[pos], sizeof(g_dock_pins[0]), path);
    g_dock_pin_count++;
    sl_dock_pins_save();
    sl_dirty_mark_full();   // the dock may change height
    return 1;
}

// The click on a dock tile, on its release: the app in front goes down
// (minimized), as the dash of other desktops; otherwise it opens or comes up.
static void sl_dock_tile_click(sl_ctx_t *ctx, size_t index) {
    app_instance_t *app = sl_app_host_instance_peek_at(g_launcher_items[index].slot);
    size_t w = app ? sl_window_index_for_task(app) : (size_t)-1;
    if (w != (size_t)-1 && w == g_focused_window && !g_windows[w].minimized &&
        !sl_window_is_closing(&g_windows[w])) {
        printf("starlight: window %s: minimized (dock)\n", g_windows[w].title);
        (void)sl_windows_title_button_act(ctx, w, SL_WIN_BTN_MIN);
        return;
    }
    (void)sl_windows_open_launcher_item(ctx, index);
}

static void sl_app_drag_end(sl_ctx_t *ctx) {
    size_t item = g_app_drag.item;
    uint8_t active = g_app_drag.active, from_mgr = g_app_drag.from_mgr, from_dock = g_app_drag.from_dock;
    int32_t slot = g_app_drag.slot;
    sl_app_drag_dirty(ctx);
    peos_memset(&g_app_drag, 0, sizeof(g_app_drag));
    if (g_dock_pressed != (size_t)-1) {
        sl_dock_dirty_item(ctx, g_dock_pressed);
        g_dock_pressed = (size_t)-1;
    }
    if (item >= g_launcher_count) return;
    if (!active) {   // a click
        if (from_mgr) {
            sl_launcher_toggle(ctx);
            (void)sl_windows_open_launcher_item(ctx, item);
        } else {
            sl_dock_tile_click(ctx, item);
        }
        return;
    }
    if (slot < 0) {
        // Taken from the dock and let go away from it: out of the dock (the
        // context menu's "Tieni fuori"), as long as something stays in it.
        sl_dock_layout_t L;
        sl_dock_layout_ex(ctx, &L, (size_t)-1, -1);
        size_t settings = sl_dock_settings_item();
        if (from_dock && sl_dock_is_pinned(&L, item) && (settings != (size_t)-1 || L.pinned > 1u)) {
            sl_dock_pin_set(ctx, item, 0u);
            printf("starlight: dock: %s taken out (dragged off)\n", g_launcher_items[item].title);
            return;
        }
        printf("starlight: dock: %s let go off the dock\n", g_launcher_items[item].title);
        return;
    }
    if (!sl_dock_pin_place(ctx, item, (size_t)slot)) {
        printf("starlight: dock: full, %s not pinned\n", g_launcher_items[item].title);
        sl_notify_post(ctx, sl_tr("notify.system"), sl_tr("apps.ctx.dock_full"));
        return;
    }
    printf("starlight: dock: %s pinned at %d\n", g_launcher_items[item].title, (int)slot);
}

int sl_launcher_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;

    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count) {
        uint8_t inside_menu = sl_point_in_rect(x, y, g_launcher_ctx_x, g_launcher_ctx_y,
                                               g_launcher_ctx_w, g_launcher_ctx_h) ? 1u : 0u;
        if (inside_menu) {
            uint32_t rel_y = (y >= g_launcher_ctx_y + 1u) ? (y - (g_launcher_ctx_y + 1u)) : 0u;
            uint32_t row = rel_y / SL_LAUNCHER_CTX_ROW_H;
            uint8_t picked = (!g_launcher_ctx_info && row < g_launcher_ctx_rows) ? 1u : 0u;
            uint8_t action = picked ? g_launcher_ctx_actions[row] : 0u;
            size_t item_index = g_launcher_ctx_item_index;
            uint32_t ax = g_launcher_ctx_x, ay = g_launcher_ctx_y;
            if (g_launcher_ctx_info && g_launcher_info_details_row >= 0 &&
                row == (uint32_t)g_launcher_info_details_row) {
                printf("starlight: app card %s: details\n", g_launcher_items[item_index].title);
                sl_launcher_info_open(ctx, item_index, ax, ay, 1u);   // the card again, with the details
                return 1;
            }
            uint8_t desk = g_launcher_ctx_desk;
            sl_launcher_context_close(ctx);   // before acting: Informazioni opens its own card
            if (picked && desk) sl_desk_menu_activate(ctx, action);
            else if (picked) (void)sl_launcher_context_activate(ctx, item_index, action, ax, ay);
            return 1;
        }
        sl_launcher_context_close(ctx);
        if (x < sl_launcher_reserved_left(ctx)) return 1;
    }

    if (!sl_dock_ready(ctx)) return 0;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.count == 0u) return 0;
    uint32_t fx, fy;
    sl_fin_rect(&L, &fx, &fy);
    size_t index = 0u;

    if (g_launcher_extra_width != 0u) {
        // The app manager is open (or opening): it takes every click.
        if (sl_dock_expanded_fully()) {
            if (sl_point_in_rect(x, y, fx, fy, SL_FIN_W, SL_FIN_H)) {
                sl_launcher_toggle(ctx);
                return 1;
            }
            uint8_t on_dock = (uint8_t)sl_dock_hit_tile(&L, x, y, &index);
            if (on_dock || sl_mgr_hit_tile(&L, x, y, &index)) {
                // Opened on the release; dragged to the dock, or (a dock tile) off it.
                sl_app_drag_arm(index, 1u, x, y);
                g_app_drag.from_dock = on_dock;
                return 1;
            }
            sl_mgr_header_t H;
            sl_mgr_header(&L, &H);
            for (int c = 0; c < SL_CAT_COUNT; c++) {   // a category
                if (!sl_point_in_rect(x, y, H.cell_x[c], H.y + SL_MGR_SEG_PAD, H.cell_w[c], SL_MGR_HDR_H - 2u * SL_MGR_SEG_PAD)) continue;
                if (g_mgr_cat != (uint8_t)c) {
                    g_mgr_cat = (uint8_t)c;
                    g_mgr_scroll = 0u;
                    sl_dock_set_hover_at(ctx, (size_t)-1, 0u);
                    sl_dock_dirty_panel(ctx, &L);
                }
                return 1;
            }
            if (!sl_point_in_rect(x, y, L.cx, L.cy, L.cw, L.ch)) sl_launcher_toggle(ctx);   // outside: close
        }
        return 1;
    }

    if (g_dock_hot && sl_point_in_rect(x, y, fx, fy, SL_FIN_W, SL_FIN_H)) {
        sl_launcher_toggle(ctx);
        return 1;
    }
    if (sl_dock_hit_tile(&L, x, y, &index)) {
        g_dock_pressed = index;
        sl_dock_dirty_item(ctx, index);
        sl_app_drag_arm(index, 0u, x, y);   // the click on the release (sl_dock_tile_click), or a drag
        g_app_drag.from_dock = 1u;
        return 1;
    }
    return sl_point_in_rect(x, y, L.px, L.py, L.pw, L.ph) ? 1 : 0;
}

int sl_launcher_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || g_dock.ready != 1u) return -1;
    if (g_launcher_ctx_open && sl_point_in_rect(x, y, g_launcher_ctx_x, g_launcher_ctx_y, g_launcher_ctx_w, g_launcher_ctx_h)) {
        if (!g_launcher_ctx_info) return SL_CUR_HAND;
        uint32_t row = y >= g_launcher_ctx_y + 1u ? (y - g_launcher_ctx_y - 1u) / SL_LAUNCHER_CTX_ROW_H : 0u;
        return g_launcher_info_details_row >= 0 && row == (uint32_t)g_launcher_info_details_row ? SL_CUR_HAND
                                                                                               : SL_CUR_ARROW;
    }
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.count == 0u) return -1;
    size_t i = 0u;
    uint32_t fx, fy;
    sl_fin_rect(&L, &fx, &fy);
    uint8_t on_fin = sl_point_in_rect(x, y, fx, fy, SL_FIN_W, SL_FIN_H);
    if (g_launcher_extra_width != 0u) {   // the app manager
        if (!on_fin && !sl_point_in_rect(x, y, L.cx, L.cy, L.cw, L.ch)) return -1;
        if (!sl_dock_expanded_fully()) return SL_CUR_ARROW;
        if (on_fin || sl_dock_hit_tile(&L, x, y, &i) || sl_mgr_hit_tile(&L, x, y, &i)) return SL_CUR_HAND;
        sl_mgr_header_t H;
        sl_mgr_header(&L, &H);
        if (sl_point_in_rect(x, y, H.search_x, H.y, H.search_w, SL_MGR_HDR_H)) return SL_CUR_TEXT;
        if (sl_point_in_rect(x, y, H.seg_x, H.y, H.seg_w, SL_MGR_HDR_H)) return SL_CUR_HAND;
        return SL_CUR_ARROW;
    }
    if ((g_dock_hot && on_fin) || sl_dock_hit_tile(&L, x, y, &i)) return SL_CUR_HAND;
    if (sl_point_in_rect(x, y, L.px, L.py, L.pw, L.ph)) return SL_CUR_ARROW;
    return -1;
}

// The wheel over the open app manager scrolls its grid: 1 if it took it.
uint8_t sl_launcher_handle_wheel(sl_ctx_t *ctx, int32_t wheel) {
    if (!ctx || !sl_dock_ready(ctx) || !g_launcher_expanded || !sl_dock_expanded_fully() || wheel == 0) return 0u;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (!sl_point_in_rect(ctx->cursor_x, ctx->cursor_y, L.ex, L.ey, L.ew, L.eh)) return 0u;
    if (L.max_scroll == 0u) return 1u;   // over the manager: not for what is under it
    int64_t nv = (int64_t)g_mgr_scroll - (int64_t)wheel * (int64_t)(SL_MGR_ROW_H / 2u);
    if (nv < 0) nv = 0;
    if (nv > (int64_t)L.max_scroll) nv = (int64_t)L.max_scroll;
    if ((uint32_t)nv != g_mgr_scroll) {
        g_mgr_scroll = (uint32_t)nv;
        sl_dock_set_hover_at(ctx, (size_t)-1, 0u);
        sl_dock_dirty_panel(ctx, &L);
        sl_launcher_handle_hover(ctx);   // the tile now under the mouse
    }
    return 1u;
}

// A press on the grid's scroll bar, then the mouse held: the bar follows it.
static uint8_t g_mgr_dragging = 0u;
static uint32_t g_mgr_drag_grab = 0u;   // where on the thumb it was taken

uint8_t sl_launcher_scrollbar_press(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx) || !sl_dock_expanded_fully()) return 0u;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.max_scroll == 0u) return 0u;
    uint32_t bx, by, bh, bw, th, ty;
    sl_mgr_scrollbar(&L, &bx, &by, &bh, &bw, &th, &ty);
    if (!sl_point_in_rect(ctx->cursor_x, ctx->cursor_y, bx - 6u, by, bw + 12u, bh)) return 0u;
    g_mgr_dragging = 1u;
    g_mgr_drag_grab = (ctx->cursor_y >= ty && ctx->cursor_y < ty + th) ? ctx->cursor_y - ty : th / 2u;
    sl_launcher_scrollbar_drag(ctx);
    return 1u;
}

void sl_launcher_scrollbar_drag(sl_ctx_t *ctx) {
    if (!ctx || !g_mgr_dragging) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    uint32_t bx, by, bh, bw, th, ty;
    sl_mgr_scrollbar(&L, &bx, &by, &bh, &bw, &th, &ty);
    uint32_t travel = bh > th ? bh - th : 0u;
    int64_t pos = (int64_t)ctx->cursor_y - (int64_t)g_mgr_drag_grab - (int64_t)by;
    if (pos < 0) pos = 0;
    if (pos > (int64_t)travel) pos = travel;
    uint32_t nv = travel ? (uint32_t)(((uint64_t)pos * L.max_scroll) / travel) : 0u;
    if (nv != g_mgr_scroll) {
        g_mgr_scroll = nv;
        sl_dock_dirty_panel(ctx, &L);
    }
}

uint8_t sl_launcher_scrollbar_release(void) {
    uint8_t was = g_mgr_dragging;
    g_mgr_dragging = 0u;
    return was;
}

uint8_t sl_launcher_manager_open(void) {
    return g_launcher_expanded;
}

// Keys while the app manager is open: they type in its search field.
// Esc clears it (or closes the manager), Enter opens the first app shown.
void sl_launcher_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev, uint8_t caps_lock_on) {
    if (!ctx || !ev || !g_launcher_expanded) return;
    uint16_t code = ev->code;
    if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
        g_mgr_shift = ev->pressed ? 1u : 0u;
        return;
    }
    if (sl_keyboard_event_is_altgr(ev)) {
        g_mgr_altgr = ev->pressed ? 1u : 0u;
        return;
    }
    if (!ev->pressed) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    size_t len = strlen(g_mgr_query);
    uint8_t changed = 0u;
    if (code == PEOS_KEY_ESC) {
        if (len == 0u) {
            sl_launcher_toggle(ctx);
            return;
        }
        g_mgr_query[0] = '\0';
        changed = 1u;
    } else if (code == PEOS_KEY_ENTER) {
        if (L.mgr_count > 0u) {
            size_t first = L.mgr_item[0];
            sl_launcher_toggle(ctx);
            (void)sl_windows_open_launcher_item(ctx, first);
        }
        return;
    } else if (code == PEOS_KEY_BACKSPACE) {
        while (len > 0u && ((unsigned char)g_mgr_query[len - 1u] & 0xC0u) == 0x80u) len--;   // a whole UTF-8 character
        if (len > 0u) len--;
        g_mgr_query[len] = '\0';
        changed = 1u;
    } else {
        char in[8];
        size_t n = sl_keyboard_translate_event(ev, g_mgr_shift, caps_lock_on, g_mgr_altgr, in, sizeof(in));
        if (n > 0u && (unsigned char)in[0] >= 0x20u && len + n < sizeof(g_mgr_query)) {
            memcpy(g_mgr_query + len, in, n);
            g_mgr_query[len + n] = '\0';
            changed = 1u;
        }
    }
    if (changed) {
        g_mgr_scroll = 0u;   // a new search starts at the top
        sl_dock_set_hover_at(ctx, (size_t)-1, 0u);
        sl_dock_dirty_panel(ctx, &L);
    }
}

void sl_launcher_handle_release(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (g_app_drag.armed) {
        sl_app_drag_end(ctx);
        return;
    }
    if (g_dock_pressed == (size_t)-1) return;
    sl_dock_dirty_item(ctx, g_dock_pressed);
    g_dock_pressed = (size_t)-1;
}

int sl_launcher_handle_right_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    size_t index = 0u;
    if (sl_launcher_hit_test(ctx, x, y, &index)) {
        sl_launcher_context_open(ctx, index, x, y);
        return 1;
    }
    if (g_launcher_ctx_open) {
        sl_launcher_context_close(ctx);
        if (x < sl_launcher_reserved_left(ctx)) return 1;
    }
    // On the desktop itself (no window, not the dock, under the top bar):
    // its menu (design/starlight-soft-ui, Finestre).
    if (g_launcher_extra_width == 0u && g_launcher_count > 0u && x >= sl_launcher_reserved_left(ctx) &&
        y >= sl_windows_top(ctx) && !sl_windows_any_at(x, y)) {
        static const uint8_t desk_actions[] = { SL_CTX_WALLPAPER, SL_CTX_ARRANGE, SL_CTX_SETTINGS };
        peos_memcpy(g_launcher_ctx_actions, desk_actions, sizeof(desk_actions));
        g_launcher_ctx_rows = (uint32_t)sizeof(desk_actions);
        g_launcher_ctx_info = 0u;
        sl_launcher_popup_open(ctx, 0u, x, y);
        g_launcher_ctx_desk = 1u;
        printf("starlight: desktop menu open\n");
        return 1;
    }
    return 0;
}

void sl_launcher_handle_hover(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.count == 0u) return;

    // The fin shows while the mouse is on the dock or on the fin itself.
    uint32_t fx, fy;
    sl_fin_rect(&L, &fx, &fy);
    uint32_t x = ctx->cursor_x, y = ctx->cursor_y;
    uint8_t hot = (g_launcher_extra_width == 0u &&
                   (sl_point_in_rect(x, y, L.px, L.py, L.pw, L.ph) ||
                    (g_dock_hot && sl_point_in_rect(x, y, fx, fy, SL_FIN_W, SL_FIN_H)))) ? 1u : 0u;
    if (hot != g_dock_hot) {
        g_dock_hot = hot;
        sl_dock_dirty_fin(ctx, &L);
    }

    if (g_launcher_ctx_open) return;
    size_t next = (size_t)-1;
    uint8_t in_mgr = 0u;
    // While an app is dragged no tile is raised (the others slide under it).
    if (!g_app_drag.active && !sl_dock_hit_tile(&L, x, y, &next)) {
        in_mgr = (uint8_t)sl_mgr_hit_tile(&L, x, y, &next);
    }
    sl_dock_set_hover_at(ctx, next, in_mgr);
    if (g_dock_pressed != (size_t)-1 && next != g_dock_pressed) sl_launcher_handle_release(ctx);
}

// Draw one tile: its state's empty tile, the accent ring under the mouse,
// then the baked icon, or its own icon or initial if it has none.
// set: 0 dock, 1 dock-hi, 2 dock-down, 3 manager, 4 manager-hi.
static void sl_dock_draw_tile(sl_ctx_t *ctx, const sl_target_t *t, sl_launcher_item_t *item, int set,
                              int32_t x, int32_t y, uint32_t size) {
    sl_dock_resolve(ctx, item);
    const slsk_entry_t *e = item->tiles[set];
    if (e) sl_draw_skin(ctx, e, x, y, size, size);
    const slsk_entry_t *ring = set == 1 ? g_dock.ring : set == 4 ? g_dock.ring_big : NULL;
    if (ring && ring->width == size) {
        const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
        sl_blit_mask(t, sl_skin_data(&ctx->skin, ring), ring->width, ring->height, ring->width, x, y,
                     n->r, n->g, n->b, 89u);   // 35%, as the boards' inset ring
    }
    if (item->tile_custom) {
        const slsk_entry_t *ic = item->icons[set];
        if (ic) sl_draw_skin(ctx, ic, x + ((int32_t)size - ic->width) / 2, y + ((int32_t)size - ic->height) / 2,
                             ic->width, ic->height);
        return;
    }
    int big = set >= 3;
    if (item->own_icon[big]) {
        uint32_t is = big ? 66u : 44u;
        slsk_entry_t fake;
        peos_memset(&fake, 0, sizeof(fake));
        fake.kind = SLSK_KIND_PREMUL;
        fake.width = (uint16_t)is;
        fake.height = (uint16_t)is;
        sl_blit_sprite(t, &fake, item->own_icon[big], x + (int32_t)(size - is) / 2, y + (int32_t)(size - is) / 2, is, is);
        return;
    }
    const slsk_font_t *f = big ? g_dock.initial_big : g_dock.initial;
    uint32_t tw = sl_text_width(f, item->initial);
    int32_t base = y + (int32_t)size / 2 + (f->ascent - f->descent) / 2;
    sl_text_draw(t, f, x + ((int32_t)size - (int32_t)tw) / 2, base, item->initial, 0xEA, 0xF4, 0xFF);
}

static void sl_mgr_draw(sl_ctx_t *ctx, const sl_dock_layout_t *Lp, const sl_target_t *inp);

// Text and lines from the runtime theme (day or night).
#define SL_DOCK_RGB(tok) sl_ui_runtime_color(tok)->r, sl_ui_runtime_color(tok)->g, sl_ui_runtime_color(tok)->b
#define SL_DOCK_INK SL_DOCK_RGB(SL_UI_COLOR_TEXT_PRIMARY)
#define SL_DOCK_MUTED SL_DOCK_RGB(SL_UI_COLOR_TEXT_SECONDARY)
#define SL_DOCK_LINE SL_DOCK_RGB(SL_UI_COLOR_BORDER)

// The fin and its chevron, the chevron in the accent colour.
static void sl_dock_draw_fin(sl_ctx_t *ctx, const sl_target_t *t, uint32_t fx, uint32_t fy, const slsk_entry_t *chev) {
    sl_draw_skin(ctx, g_dock.fin, (int32_t)fx, (int32_t)fy, SL_FIN_W, SL_FIN_H);
    const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    sl_blit_mask(t, sl_skin_data(&ctx->skin, chev), chev->width, chev->height, chev->width,
                 (int32_t)fx + 6, (int32_t)fy + 65, n->r, n->g, n->b, 255u);
}

static void sl_dock_draw(sl_ctx_t *ctx) {
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.count == 0u) return;
    sl_target_t t = sl_target_from_ctx(ctx);
    uint32_t prog = g_launcher_extra_width;

    sl_draw_skin(ctx, g_dock.panel, (int32_t)L.cx, (int32_t)L.cy, L.cw, L.ch);
    if (L.has_sep) {
        sl_fill_rect(ctx, L.px + (SL_DOCK_W - SL_DOCK_SEP_W) / 2u, L.sep_y, SL_DOCK_SEP_W, 2u, SL_DOCK_LINE);
    }

    for (size_t k = 0u; k < L.count; k++) {
        size_t i = L.item[k];
        sl_launcher_item_t *item = &g_launcher_items[i];
        uint32_t tx, ty;
        sl_dock_tile_rect(&L, k, &tx, &ty);
        if (i == g_dock_pressed) {
            sl_dock_draw_tile(ctx, &t, item, 2, (int32_t)tx, (int32_t)ty, SL_DOCK_TILE);
        } else if (i == g_launcher_hover_index && !g_dock_hover_in_mgr) {
            sl_dock_draw_tile(ctx, &t, item, 1, (int32_t)tx - 3, (int32_t)ty - 3, SL_DOCK_HI);
        } else {
            sl_dock_draw_tile(ctx, &t, item, 0, (int32_t)tx, (int32_t)ty, SL_DOCK_TILE);
        }
        // Top right of the tile: the notifications not yet seen.
        uint32_t unseen = sl_notify_badge(item->title);
        if (unseen) {
            char num[4];
            if (unseen > 9u) snprintf(num, sizeof(num), "9+");
            else snprintf(num, sizeof(num), "%u", (unsigned)unseen);
            const slsk_font_t *f = g_dock.label;
            uint32_t nw = sl_text_width(f, num);
            // A circle, or a pill growing left over the tile for "9+".
            uint32_t bw = nw + 10u > 20u ? nw + 10u : 20u;
            uint32_t bx = tx + SL_DOCK_TILE + 6u - bw;
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, bx, ty - 6u, bw, 20u, 10u);
            sl_text_draw(&t, f, (int32_t)(bx + bw / 2u) - (int32_t)nw / 2, (int32_t)ty + 9, num, 0x11, 0x16, 0x26);
        }
        // Left of the tile: a dot if the app is open, a bar if it is focused.
        uint64_t bits = g_dock_indicators >> (2u * k);
        if (k < 32u && (bits & 1u)) {
            uint32_t ih = (bits & 2u) ? 24u : 6u;
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, tx - 14u, ty + (SL_DOCK_TILE - ih) / 2u, 6u, ih, 3u);
        }
    }

    if (prog == 0u) {
        if (g_dock_hot) {
            uint32_t fx, fy;
            sl_fin_rect(&L, &fx, &fy);
            sl_dock_draw_fin(ctx, &t, fx, fy, g_dock.chev_open);
        }
        return;
    }

    // The app manager, revealed by the widening panel.
    sl_target_t in = t;
    int64_t ix1 = (int64_t)L.cx + L.cw - 20, iy1 = (int64_t)L.cy + L.ch - 20;
    if ((int64_t)L.cx + 20 > in.clip_x0) in.clip_x0 = (int32_t)(L.cx + 20u);
    if ((int64_t)L.cy + 20 > in.clip_y0) in.clip_y0 = (int32_t)(L.cy + 20u);
    if (ix1 < in.clip_x1) in.clip_x1 = (int32_t)ix1;
    if (iy1 < in.clip_y1) in.clip_y1 = (int32_t)iy1;
    if (in.clip_x1 > in.clip_x0 && in.clip_y1 > in.clip_y0) sl_mgr_draw(ctx, &L, &in);

    if (sl_dock_expanded_fully()) {
        uint32_t fx, fy;
        sl_fin_rect(&L, &fx, &fy);
        sl_dock_draw_fin(ctx, &t, fx, fy, g_dock.chev_close);
    }
}

// The grid of every app, inside the clip `in` (the panel, less its edge).
static void sl_mgr_draw(sl_ctx_t *ctx, const sl_dock_layout_t *Lp, const sl_target_t *inp) {
    sl_dock_layout_t L = *Lp;
    sl_target_t in = *inp;
    uint8_t saved = ctx->clip_enabled;
    uint32_t scx = ctx->clip_x, scy = ctx->clip_y, scw = ctx->clip_w, sch = ctx->clip_h;
    sl_clip_set(ctx, (uint32_t)in.clip_x0, (uint32_t)in.clip_y0, (uint32_t)(in.clip_x1 - in.clip_x0),
                (uint32_t)(in.clip_y1 - in.clip_y0));
    if (L.ch > 76u) sl_fill_rect(ctx, L.px + SL_MGR_LINE_X, L.cy + 38u, 2u, L.ch - 76u, SL_DOCK_LINE);

    const slsk_font_t *title = g_dock.title;
    int32_t base = (int32_t)L.gy + title->ascent;
    int32_t end = sl_text_draw(&in, title, (int32_t)L.gx, base, sl_tr("apps.header"), SL_DOCK_INK);
    sl_mgr_header_t H;
    sl_mgr_header(&L, &H);
    if (H.show_count) {
        char count[64];
        size_t shown = 0u;   // the apps the manager can show (not hidden=yes)
        for (size_t i = 0u; i < g_launcher_count; i++) shown += g_launcher_items[i].hidden ? 0u : 1u;
        if (L.mgr_count == shown) snprintf(count, sizeof(count), sl_tr_n("apps.count_all", (uint32_t)shown), (unsigned)shown);
        else snprintf(count, sizeof(count), sl_tr("apps.count_some"), (unsigned)L.mgr_count, (unsigned)shown);
        sl_text_draw(&in, g_dock.label, end + 14, base, count, SL_DOCK_MUTED);
    }

    // The categories: a raised pill, the chosen one night with neon text.
    const sl_ui_color_t *neon = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    const slsk_font_t *lf = g_dock.label;
    uint32_t cell_h = SL_MGR_HDR_H - 2u * SL_MGR_SEG_PAD;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED, H.seg_x, H.y, H.seg_w, SL_MGR_HDR_H, SL_MGR_HDR_H / 2u);
    for (int c = 0; c < SL_CAT_COUNT; c++) {
        uint32_t lw = sl_text_width(lf, g_mgr_cat_labels(c));
        int32_t lx = (int32_t)(H.cell_x[c] + (H.cell_w[c] - lw) / 2u);
        int32_t lb = (int32_t)(H.y + SL_MGR_HDR_H / 2u) + (lf->ascent - lf->descent) / 2;
        if ((uint8_t)c == g_mgr_cat) {
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, H.cell_x[c], H.y + SL_MGR_SEG_PAD, H.cell_w[c], cell_h, cell_h / 2u);
            sl_text_draw(&in, lf, lx, lb, g_mgr_cat_labels(c), neon->r, neon->g, neon->b);
        } else {
            sl_text_draw(&in, lf, lx, lb, g_mgr_cat_labels(c), SL_DOCK_MUTED);
        }
    }

    // The search field: inset, a magnifier, the text typed (or the hint)
    // and the caret, since keys go here while the manager is open.
    sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, H.search_x, H.y, H.search_w, SL_MGR_HDR_H, SL_MGR_HDR_H / 2u);
    const slsk_entry_t *glass = g_dock.search_icon;
    sl_blit_mask(&in, sl_skin_data(&ctx->skin, glass), glass->width, glass->height, glass->width,
                 (int32_t)(H.search_x + 20u), (int32_t)(H.y + (SL_MGR_HDR_H - glass->height) / 2u), SL_DOCK_MUTED, 255u);
    {
        const slsk_font_t *sf = g_dock.search;
        int32_t tx = (int32_t)(H.search_x + 20u + glass->width + 12u);
        int32_t tb = (int32_t)(H.y + SL_MGR_HDR_H / 2u) + (sf->ascent - sf->descent) / 2;
        sl_target_t ft = in;
        int32_t fx1 = (int32_t)(H.search_x + H.search_w - 20u);
        if (fx1 < ft.clip_x1) ft.clip_x1 = fx1;
        int32_t caret = tx - 4;   // before the hint
        if (g_mgr_query[0]) caret = sl_text_draw(&ft, sf, tx, tb, g_mgr_query, SL_DOCK_INK);
        else sl_text_draw(&ft, sf, tx, tb, sl_tr("apps.search"), SL_DOCK_MUTED);
        if (sl_dock_expanded_fully() && caret < fx1) {
            sl_fill_rect(ctx, (uint32_t)caret + 1u, (uint32_t)(tb - sf->ascent), 2u, (uint32_t)(sf->ascent + sf->descent),
                         neon->r, neon->g, neon->b);
        }
    }

    // The tiles, cut at the grid's top and bottom (the header stays put).
    sl_target_t gin = in;
    if ((int32_t)L.grid_top - 24 > gin.clip_y0) gin.clip_y0 = (int32_t)L.grid_top - 24;   // room for a tile's glow
    if ((int32_t)L.grid_bottom < gin.clip_y1) gin.clip_y1 = (int32_t)L.grid_bottom;
    if (gin.clip_y1 > gin.clip_y0) {
        sl_clip_set(ctx, (uint32_t)gin.clip_x0, (uint32_t)gin.clip_y0, (uint32_t)(gin.clip_x1 - gin.clip_x0),
                    (uint32_t)(gin.clip_y1 - gin.clip_y0));
    }
    for (size_t i = 0u; i < L.mgr_count && gin.clip_y1 > gin.clip_y0; i++) {
        uint32_t tx, ty;
        if (!sl_mgr_tile_shown(&L, i)) continue;
        sl_mgr_tile_rect(&L, i, &tx, &ty);
        int64_t ty64 = sl_mgr_tile_y(&L, i);
        if (ty64 < (int64_t)L.grid_top - (int64_t)SL_MGR_ROW_H) continue;
        ty = (uint32_t)(ty64 < 0 ? 0 : ty64);
        size_t idx = L.mgr_item[i];
        sl_launcher_item_t *item = &g_launcher_items[idx];
        // Raised: the tile under the mouse, and with a search the first
        // one (the app Enter opens).
        uint8_t raised = (idx == g_launcher_hover_index && g_dock_hover_in_mgr) || (i == 0u && g_mgr_query[0]);
        if (raised && sl_dock_expanded_fully()) {
            sl_dock_draw_tile(ctx, &gin, item, 4, (int32_t)tx - 4, (int32_t)ty - 4, SL_MGR_HI);
        } else {
            sl_dock_draw_tile(ctx, &gin, item, 3, (int32_t)tx, (int32_t)ty, SL_MGR_TILE);
        }
        char label[26];
        sl_launcher_compact_label(item->title, label, sizeof(label));
        uint32_t lw = sl_text_width(g_dock.label, label);
        sl_text_draw(&gin, g_dock.label, (int32_t)(tx + SL_MGR_TILE / 2u) - (int32_t)lw / 2,
                     (int32_t)(ty + SL_MGR_TILE + 12u) + g_dock.label->ascent, label, SL_DOCK_INK);
    }
    sl_clip_set(ctx, (uint32_t)in.clip_x0, (uint32_t)in.clip_y0, (uint32_t)(in.clip_x1 - in.clip_x0),
                (uint32_t)(in.clip_y1 - in.clip_y0));
    // The scroll bar, at the grid's right, when there is more than fits.
    if (L.max_scroll > 0u && L.grid_bottom > L.grid_top + 40u) {
        uint32_t bx, by, bh, tw, th, ty0;
        sl_mgr_scrollbar(&L, &bx, &by, &bh, &tw, &th, &ty0);
        sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, bx, by, tw, bh, tw / 2u);
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT, bx, ty0, tw, th, tw / 2u);
    }
    if (L.mgr_count == 0u) {
        sl_text_draw(&in, g_dock.search, (int32_t)L.gx, (int32_t)L.grid_top + g_dock.search->ascent,
                     sl_tr(g_mgr_query[0] ? "apps.none_name" : "apps.none_cat"), SL_DOCK_MUTED);
    }
    {   // the hint at the bottom (design: App-manager-aperto)
        uint32_t hy = L.ey + L.eh - 34u;
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, L.gx, hy - 5u, 10u, 10u, 5u);
        sl_text_draw(&in, lf, (int32_t)L.gx + 20, (int32_t)hy + (lf->ascent - lf->descent) / 2,
                     sl_tr("apps.hint_drag"), SL_DOCK_MUTED);
    }
    if (saved) sl_clip_set(ctx, scx, scy, scw, sch);
    else sl_clip_clear(ctx);
}

static void sl_dock_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_dock_draw(ctx);
    if (g_app_drag.active && g_app_drag.item < g_launcher_count) {
        sl_target_t t = sl_target_from_ctx(ctx);
        sl_dock_draw_tile(ctx, &t, &g_launcher_items[g_app_drag.item], 0,
                          (int32_t)g_app_drag.x - (int32_t)SL_DOCK_TILE / 2, (int32_t)g_app_drag.y - (int32_t)SL_DOCK_TILE / 2,
                          SL_DOCK_TILE);
    }
    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count &&
        g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u) {
        uint32_t rows = g_launcher_ctx_rows;
        if (rows > 0u) {
            sl_context_menu_draw(ctx, g_launcher_ctx_x, g_launcher_ctx_y, g_launcher_ctx_w, g_launcher_ctx_h,
                                 SL_LAUNCHER_CTX_ROW_H, rows, 0u, rows,
                                 g_launcher_ctx_hover_row,
                                 0u, sl_launcher_context_label_cb, NULL);
        }
    }
    sl_clip_clear(ctx);
}

void sl_launcher_render(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    // The dock blends over what is under it: drawn in each dirty rect once,
    // never twice over the same pixels. Its whole reach: panel and shadow,
    // fin, pill, context menu.
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    if (L.count == 0u) return;
    uint32_t right = L.cx + L.cw + SL_FIN_W + 320u;
    if (g_launcher_ctx_open && g_launcher_ctx_x + g_launcher_ctx_w + 28u > right) right = g_launcher_ctx_x + g_launcher_ctx_w + 28u;
    if (g_app_drag.active) right = ctx->fb.width;   // the tile follows the mouse anywhere
    if (right > ctx->fb.width) right = ctx->fb.width;
    sl_dirty_for_each_intersection(0u, 0u, right, ctx->fb.height, sl_dock_draw_rect, ctx);
}

// The label of the hovered dock tile, a layer of its own (see sl_compose):
// over the app manager, the notifications and the other windows.
typedef struct {
    sl_ctx_t *ctx;
    uint32_t x, y, w;
    const char *label;
} sl_dock_label_t;

static void sl_dock_label_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    const sl_dock_label_t *d = (const sl_dock_label_t *)userdata;
    sl_ctx_t *ctx = d->ctx;
    sl_clip_set(ctx, x, y, w, h);
    sl_target_t t = sl_target_from_ctx(ctx);
    sl_draw_skin(ctx, g_dock.pill, (int32_t)d->x, (int32_t)d->y, d->w, SL_PILL_H);
    const slsk_font_t *f = g_dock.label;
    sl_text_draw(&t, f, (int32_t)(d->x + SL_PILL_PAD), (int32_t)(d->y + SL_PILL_H / 2u) + (f->ascent - f->descent) / 2,
                 d->label, SL_DOCK_INK);
    sl_clip_clear(ctx);
}

void sl_dock_label_render(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx) || g_launcher_hover_index == (size_t)-1 || g_dock_hover_in_mgr) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);
    char label[26];
    sl_dock_label_t d = { ctx, 0u, 0u, 0u, label };
    if (!sl_dock_pill_rect(&L, g_launcher_hover_index, &d.x, &d.y, &d.w)) return;
    sl_launcher_compact_label(g_launcher_items[g_launcher_hover_index].title, label, sizeof(label));
    const slsk_entry_t *e = g_dock.pill;
    uint32_t x0 = d.x > e->pad_l ? d.x - e->pad_l : 0u, y0 = d.y > e->pad_t ? d.y - e->pad_t : 0u;
    sl_dirty_for_each_intersection(x0, y0, d.x + d.w + e->pad_r - x0, d.y + SL_PILL_H + e->pad_b - y0,
                                   sl_dock_label_draw_rect, &d);
}

uint32_t sl_windows_top(const sl_ctx_t *ctx) {
    (void)ctx;
    return SL_DESK_BAR_HEIGHT - SL_WIN_OPAQUE_INSET;
}

void sl_windows_close_task(sl_ctx_t *ctx, const app_instance_t *task) {
    size_t idx = sl_window_index_for_task(task);
    if (!ctx || idx == (size_t)-1 || sl_window_is_closing(&g_windows[idx])) return;
    (void)sl_windows_title_button_act(ctx, idx, SL_WIN_BTN_CLOSE);
}
