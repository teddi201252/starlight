#include "ui_sdk.h"
#include "notify.h"
#include "starlight_internal.h"
#include "app_host.h"
#include "context_menu_renderer.h"
#include "render/font.h"
#include "render/pixels.h"
#include "window.h"
#include "peos/capability_parse.h"
#include "peos/path_scope_parse.h"
#include "keyboard_layout.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SL_APP_DIR "/system/apps"
#define SL_APP_MANIFEST "app.ini"
#define SL_APP_CATALOG_SCAN_INTERVAL_NS 1000000000ull
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
#define SL_LAUNCHER_CTX_ROWS_MAX 8u
static uint8_t g_launcher_ctx_info = 0u;
static uint32_t g_launcher_ctx_rows = 0u;
static uint8_t g_launcher_ctx_actions[SL_LAUNCHER_CTX_ROWS_MAX];
static char g_launcher_info_lines[SL_LAUNCHER_CTX_ROWS_MAX][128];
static uint8_t g_launcher_expanded = 0u;
static uint32_t g_launcher_extra_width = 0u;
static sl_window_rect_snapshot_t g_launcher_last_rect;
static uint64_t g_launcher_next_scan_ns = 0ull;
static peos_stat_t g_launcher_dir_stat;      // SL_APP_DIR as of the last catalog scan
static uint8_t g_launcher_dir_known = 0u;
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
        } else if (strcmp(key, "width") == 0) {
            (void)sl_parse_u32(val, &out->width);
        } else if (strcmp(key, "height") == 0) {
            (void)sl_parse_u32(val, &out->height);
        }
    }

    fclose(f);
    if (raw_exec[0] == '\0') return 0;
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

static char *sl_read_app_directory(size_t *out_len) {
    size_t cap = SL_APP_DIRECTORY_BUFFER_INITIAL;
    if (out_len) *out_len = 0u;

    while (cap <= SL_APP_DIRECTORY_BUFFER_MAX) {
        char *buf = (char *)malloc(cap);
        if (!buf) return NULL;
        uint64_t result = peos_readdir(SL_APP_DIR, sizeof(SL_APP_DIR) - 1u,
                                       buf, cap - 1u);
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

static int sl_scan_apps_from_system(sl_scanned_app_t **out_entries, size_t *out_count) {
    if (!out_entries || !out_count) return 0;
    *out_entries = NULL;
    *out_count = 0u;

    size_t list_len = 0u;
    char *list = sl_read_app_directory(&list_len);
    if (!list) return 0;

    sl_scanned_app_t *entries = NULL;
    size_t count = 0u;
    size_t cap = 0u;
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

        sl_scanned_app_t entry;
        peos_memset(&entry, 0, sizeof(entry));
        if (!sl_join_path(entry.bundle_path, sizeof(entry.bundle_path), SL_APP_DIR, name)) continue;

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
        if (!sl_scan_reserve(&entries, &cap, count + 1u)) {
            free(entries);
            free(list);
            return 0;
        }
        entries[count++] = entry;
    }

    free(list);
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
    sl_dirty_add(ctx, x > 2u ? x - 2u : 0u, y > 2u ? y - 2u : 0u, w + 4u, h + 4u);
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
static uint64_t g_dock_indicators = 0u;    // running/focused bits last drawn
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

// The baked tile set of an app: by its title, as the icons are named.
static const char *sl_dock_skin_key(const char *title) {
    static const struct { const char *title, *key; } map[] = {
        { "orbit", "orbit" }, { "voyager", "voyager" }, { "calcolatrice", "calcolatrice" },
        { "curiosity", "curiosity" }, { "impostazioni", "impostazioni" }, { "installa peos", "installer" },
        { "installer", "installer" }, { "musica", "musica" }, { "rete", "rete" },
        { "monitor", "monitor" }, { "sistema", "sistema" },
    };
    char low[64];
    size_t i = 0u;
    for (; title && title[i] && i + 1u < sizeof(low); i++) {
        char c = title[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    low[i] = '\0';
    for (size_t k = 0u; k < sizeof(map) / sizeof(map[0]); k++) {
        if (strcmp(low, map[k].title) == 0) return map[k].key;
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
    const char *key = sl_dock_skin_key(item->title);
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
    // No baked tile: its own icon if it has one, else its initial.
    sl_launcher_try_load_icon(item);
    if (item->icon_loaded && item->icon_image.data) {
        item->own_icon[0] = sl_dock_scale_icon(&item->icon_image, 44u);
        item->own_icon[1] = sl_dock_scale_icon(&item->icon_image, 66u);
        sl_image_free(&item->icon_image);   // the scaled copies are all that is drawn
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
        const char *key = sl_dock_skin_key(g_launcher_items[i].title);
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
    size_t item[SL_DOCK_MAX];       // launcher item of each
    uint32_t tile_y[SL_DOCK_MAX];
    uint8_t has_sep;
    uint32_t sep_y;
    uint32_t px, py, pw, ph;        // the dock
    uint32_t ex, ey, ew, eh;        // the app manager
    uint32_t cx, cy, cw, ch;        // now (while it widens or narrows)
    uint32_t gx, gy, gw, cols, cell_w, grid_top;
    size_t mgr_count;               // the app manager's tiles: the apps that pass the filters
    size_t mgr_item[SL_MGR_ITEMS_MAX];
} sl_dock_layout_t;

// The app manager's filters: the search text (typed while it is open) and
// the category.
static char g_mgr_query[40];
static uint8_t g_mgr_cat = SL_CAT_ALL;
static uint8_t g_mgr_shift = 0u, g_mgr_altgr = 0u;
static const char *const g_mgr_cat_labels[SL_CAT_COUNT] = { "Tutte", "Sistema", "Strumenti", "Svago" };

// ASCII case-insensitive "needle in hay" (accented letters must match).
static int sl_mgr_matches(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    for (size_t i = 0u; hay[i]; i++) {
        size_t k = 0u;
        for (; needle[k] && hay[i + k]; k++) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (!needle[k]) return 1;
    }
    return 0;
}

static void sl_mgr_filter(sl_dock_layout_t *L) {
    L->mgr_count = 0u;
    for (size_t i = 0u; i < g_launcher_count && L->mgr_count < SL_MGR_ITEMS_MAX; i++) {
        const sl_launcher_item_t *item = &g_launcher_items[i];
        if (g_mgr_cat != SL_CAT_ALL && item->category != g_mgr_cat) continue;
        if (!sl_mgr_matches(item->title, g_mgr_query)) continue;
        L->mgr_item[L->mgr_count++] = i;
    }
}

static uint32_t sl_lerp_u32(uint32_t a, uint32_t b, uint32_t t) {
    int64_t v = (int64_t)a + ((int64_t)b - (int64_t)a) * (int64_t)t / (int64_t)SL_DOCK_ANIM_FULL;
    return (uint32_t)(v < 0 ? 0 : v);
}

static void sl_dock_layout(const sl_ctx_t *ctx, sl_dock_layout_t *L) {
    peos_memset(L, 0, sizeof(*L));
    if (!ctx || g_launcher_count == 0u) return;
    uint32_t fh = ctx->fb.height, fw = ctx->fb.width;

    // The pinned apps (else the first ones) as far as they fit; Impostazioni
    // always last, after a separator.
    size_t settings = sl_dock_settings_item();
    // As many as fit packed tight; the design's spacing if they fit with it.
    size_t take = sl_dock_capacity(ctx, settings, SL_DOCK_PAD_TIGHT, SL_DOCK_GAP_TIGHT);
    if (g_dock_pins_set) {
        for (size_t p = 0u; p < g_dock_pin_count && L->count < take; p++) {
            size_t i = sl_launcher_find_bundle(g_dock_pins[p]);
            if (i != (size_t)-1 && i != settings) L->item[L->count++] = i;
        }
    }
    // No pins yet, or none of them left and nothing else to show.
    if (!g_dock_pins_set || (L->count == 0u && settings == (size_t)-1)) {
        for (size_t i = 0u; i < g_launcher_count && L->count < take; i++) {
            if (i != settings) L->item[L->count++] = i;
        }
    }
    uint8_t tight = L->count > sl_dock_capacity(ctx, settings, SL_DOCK_PAD, SL_DOCK_GAP);
    uint32_t pad = tight ? SL_DOCK_PAD_TIGHT : SL_DOCK_PAD, gap = tight ? SL_DOCK_GAP_TIGHT : SL_DOCK_GAP;
    L->has_sep = (settings != (size_t)-1 && L->count > 0u) ? 1u : 0u;
    if (settings != (size_t)-1) L->item[L->count++] = settings;
    if (L->count == 0u) return;

    uint32_t h = 2u * pad + (uint32_t)L->count * SL_DOCK_TILE + (uint32_t)(L->count - 1u) * gap;
    if (L->has_sep) h += 2u + gap;
    L->px = SL_DOCK_X;
    L->pw = SL_DOCK_W;
    L->ph = h;
    L->py = fh > h ? (fh - h) / 2u : 0u;
    uint32_t y = L->py + pad;
    for (size_t k = 0u; k < L->count; k++) {
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
        H->cell_w[c] = sl_text_width(g_dock.label, g_mgr_cat_labels[c]) + 2u * SL_MGR_SEG_CELL_PAD;
        seg_w += H->cell_w[c];
    }
    uint32_t right = L->gx + L->gw;
    uint32_t title_w = sl_text_width(g_dock.title, "App");
    uint32_t count_w = 14u + sl_text_width(g_dock.label, "000 nella cartella");
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
static void sl_mgr_tile_rect(const sl_dock_layout_t *L, size_t i, uint32_t *x, uint32_t *y) {
    uint32_t col = (uint32_t)(i % L->cols), row = (uint32_t)(i / L->cols);
    *x = L->gx + col * L->cell_w + (L->cell_w > SL_MGR_TILE ? (L->cell_w - SL_MGR_TILE) / 2u : 0u);
    *y = L->grid_top + row * SL_MGR_ROW_H;
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

static int sl_mgr_hit_tile(const sl_dock_layout_t *L, uint32_t x, uint32_t y, size_t *out) {
    if (!sl_dock_expanded_fully()) return 0;
    for (size_t i = 0u; i < L->mgr_count; i++) {
        uint32_t tx, ty;
        sl_mgr_tile_rect(L, i, &tx, &ty);
        if (ty + SL_MGR_TILE > L->ey + L->eh) break;
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
    if (item >= g_launcher_count || g_launcher_extra_width != 0u || g_dock.ready != 1u) return 0;
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
            sl_dirty_add_i64(ctx, (int64_t)tx - 40, (int64_t)ty - 40, SL_MGR_HI + 80, SL_MGR_HI + 80);
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
    if (!g_launcher_expanded) g_mgr_query[0] = '\0';   // opened again, it shows every app
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
    for (size_t k = 0u; k < L->count && k < 32u; k++) {
        app_instance_t *app = sl_app_host_instance_peek_at(g_launcher_items[L->item[k]].slot);
        size_t w = app ? sl_window_index_for_task(app) : (size_t)-1;
        if (w == (size_t)-1) continue;
        bits |= 1ull << (2u * k);
        if (w == g_focused_window && !g_windows[w].minimized) bits |= 2ull << (2u * k);
    }
    return bits;
}

static void sl_launcher_track_animation_dirty(sl_ctx_t *ctx) {
    if (!ctx || !sl_dock_ready(ctx)) return;
    sl_dock_layout_t L;
    sl_dock_layout(ctx, &L);

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
    g_launcher_ctx_rows = 0u;
}

enum { SL_CTX_OPEN, SL_CTX_SHOW, SL_CTX_CLOSE, SL_CTX_PIN, SL_CTX_UNPIN, SL_CTX_INFO };

static const char *sl_launcher_context_action_name(uint8_t action) {
    switch (action) {
        case SL_CTX_OPEN: return "Apri";
        case SL_CTX_SHOW: return "Mostra";
        case SL_CTX_CLOSE: return "Chiudi";
        case SL_CTX_PIN: return "Fissa nel dock";
        case SL_CTX_UNPIN: return "Togli dal dock";
        default: return "Informazioni";
    }
}

// Pinned: listed in dock.cfg or, until it exists, shown in the dock.
static uint8_t sl_dock_is_pinned(const sl_dock_layout_t *L, size_t item_index) {
    if (g_dock_pins_set) return sl_dock_pin_find(g_launcher_items[item_index].bundle_path) != (size_t)-1;
    for (size_t k = 0u; k < L->count; k++) {
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
        size_t shown = L.count - (settings != (size_t)-1 && L.count > 0u ? 1u : 0u);
        if (sl_dock_is_pinned(&L, item_index)) {
            // Without Impostazioni the last tile stays: the dock never empties.
            if (settings != (size_t)-1 || shown > 1u) out[n++] = SL_CTX_UNPIN;
        } else if (shown < sl_dock_capacity(ctx, settings, SL_DOCK_PAD_TIGHT, SL_DOCK_GAP_TIGHT)) {
            out[n++] = SL_CTX_PIN;
        }
    }
    out[n++] = SL_CTX_INFO;
    return n;
}

static void sl_dock_pin_set(sl_ctx_t *ctx, size_t item_index, uint8_t pin) {
    const char *path = g_launcher_items[item_index].bundle_path;
    if (!g_dock_pins_set) {
        // The first change starts from what the dock shows.
        sl_dock_layout_t L;
        sl_dock_layout(ctx, &L);
        size_t settings = sl_dock_settings_item();
        g_dock_pin_count = 0u;
        for (size_t k = 0u; k < L.count; k++) {
            const sl_launcher_item_t *shown = &g_launcher_items[L.item[k]];
            if (L.item[k] == settings || shown->bundle_path[0] == '\0') continue;
            sl_copy_str(g_dock_pins[g_dock_pin_count++], sizeof(g_dock_pins[0]), shown->bundle_path);
        }
        g_dock_pins_set = 1u;
    }
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
static uint32_t sl_launcher_info_build(size_t item_index) {
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
    sl_launcher_info_append(&n, "%s", item->title);
    if (item->bundle_path[0] != '\0') sl_launcher_info_append(&n, "Bundle: %s", item->bundle_path);
    if (have) {
        sl_launcher_info_append(&n, "Programma: %s", app.exec);
        char list[112];
        size_t len = 0u;
        list[0] = '\0';
        for (size_t i = 0u; i < sizeof(peos_cap_names) / sizeof(peos_cap_names[0]); i++) {
            if (!(app.caps_mask & peos_cap_names[i].mask)) continue;
            int w = snprintf(list + len, sizeof(list) - len, "%s%s", len ? ", " : "", peos_cap_names[i].name);
            if (w < 0 || (size_t)w >= sizeof(list) - len) break;
            len += (size_t)w;
        }
        sl_launcher_info_append(&n, "Permessi: %s", len ? list : "nessuno");
        len = 0u;
        list[0] = '\0';
        for (uint32_t i = 0u; i < app.path_count && i < PEOS_PATH_MAX_GRANTS; i++) {
            const peos_path_grant_t *g = &app.paths[i];
            const char *rights = (g->rights & PEOS_PATH_WRITE) ? ((g->rights & PEOS_PATH_READ) ? "rw" : "w") : "r";
            int w = snprintf(list + len, sizeof(list) - len, "%s%s:%s", len ? ", " : "", g->prefix, rights);
            if (w < 0 || (size_t)w >= sizeof(list) - len) break;
            len += (size_t)w;
        }
        sl_launcher_info_append(&n, "Cartelle: %s", len ? list : "nessuna");
        sl_launcher_info_append(&n, "Finestra: %ux%u", (unsigned)app.width, (unsigned)app.height);
    }
    app_instance_t *inst = sl_app_host_instance_peek_at(item->slot);
    sl_launcher_info_append(&n, "Stato: %s",
                            (inst && sl_window_index_for_task(inst) != (size_t)-1) ? "aperta" : "chiusa");
    return n;
}

static void sl_launcher_context_label_cb(uint32_t option_index, char *out, size_t out_cap, void *userdata) {
    (void)userdata;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (option_index >= g_launcher_ctx_rows) return;
    if (g_launcher_ctx_info) sl_copy_str(out, out_cap, g_launcher_info_lines[option_index]);
    else sl_copy_str(out, out_cap, sl_launcher_context_action_name(g_launcher_ctx_actions[option_index]));
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

static void sl_launcher_info_open(sl_ctx_t *ctx, size_t item_index, uint32_t anchor_x, uint32_t anchor_y) {
    if (!ctx || item_index >= g_launcher_count) return;
    sl_launcher_context_close(ctx);
    g_launcher_ctx_rows = sl_launcher_info_build(item_index);
    g_launcher_ctx_info = 1u;
    sl_launcher_popup_open(ctx, item_index, anchor_x, anchor_y);
}

static int sl_launcher_context_activate(sl_ctx_t *ctx, size_t item_index, uint8_t action,
                                        uint32_t anchor_x, uint32_t anchor_y) {
    if (!ctx || item_index >= g_launcher_count) return 0;
    sl_launcher_item_t *item = &g_launcher_items[item_index];
    if (action == SL_CTX_PIN || action == SL_CTX_UNPIN) {
        sl_dock_pin_set(ctx, item_index, action == SL_CTX_PIN);
        return 1;
    }
    if (action == SL_CTX_INFO) {
        sl_launcher_info_open(ctx, item_index, anchor_x, anchor_y);
        return 1;
    }
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (!app) return 0;
    item->instance = app;

    size_t existing = sl_window_index_for_task(app);
    if (existing != (size_t)-1) {
        if (action == SL_CTX_SHOW || action == SL_CTX_OPEN) {
            sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
            sl_window_rect_snapshot_t target = sl_window_snapshot_index(existing);
            sl_focus_window(existing);
            sl_windows_mark_focused_activity();
            sl_dirty_add_snapshot(ctx, &prev_focus);
            sl_dirty_add_snapshot(ctx, &target);
            return 1;
        }
        if (action == SL_CTX_CLOSE) {
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

void sl_launcher_handle_context_hover(sl_ctx_t *ctx) {
    if (!ctx || !g_launcher_ctx_open || g_launcher_ctx_item_index >= g_launcher_count) return;
    if (g_launcher_ctx_w == 0u || g_launcher_ctx_h == 0u) return;
    if (g_launcher_ctx_info) return;   // the card has nothing to pick
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

static void sl_launcher_pick_window_rect(const sl_ctx_t *ctx, const sl_launcher_item_t *item,
                                         uint32_t *out_x, uint32_t *out_y,
                                         uint32_t *out_w, uint32_t *out_h) {
    if (!ctx || !out_x || !out_y || !out_w || !out_h) return;

    uint32_t reserved_left = sl_launcher_reserved_left(ctx);
    uint32_t max_w = (ctx->fb.width > reserved_left + 8u)
                   ? (ctx->fb.width - reserved_left - 8u)
                   : ctx->fb.width;
    uint32_t max_h = (ctx->fb.height > 8u) ? (ctx->fb.height - 8u) : ctx->fb.height;

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
    uint32_t y = 24u + cascade * 18u;

    if (x + w > ctx->fb.width) {
        x = (ctx->fb.width > w) ? (ctx->fb.width - w) : 0u;
    }
    if (x < reserved_left) x = reserved_left;
    if (y + h > ctx->fb.height) {
        y = (ctx->fb.height > h) ? (ctx->fb.height - h) : 0u;
    }

    *out_x = x;
    *out_y = y;
    *out_w = w;
    *out_h = h;
}

static int sl_windows_open_launcher_item(sl_ctx_t *ctx, size_t launcher_index) {
    if (!ctx || launcher_index >= g_launcher_count) return 0;

    sl_launcher_item_t *item = &g_launcher_items[launcher_index];
    app_instance_t *app = sl_app_host_instance_at(item->slot);
    if (!app) return 0;
    item->instance = app;

    size_t existing = sl_window_index_for_task(app);
    if (existing != (size_t)-1) {
        sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
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
            !sl_app_host_slot_restrict_paths(slot, entry->app.paths, entry->app.path_count)) {
            continue;
        }

        sl_launcher_item_t *item = &next[next_count++];
        if (old_index != (size_t)-1) {
            *item = g_launcher_items[old_index];
            g_launcher_items[old_index].icon_image.data = NULL;
            g_launcher_items[old_index].own_icon[0] = NULL;   // moved with the item
            g_launcher_items[old_index].own_icon[1] = NULL;
            item->tiles_ready = 0u;                            // the title may have changed
            free(item->own_icon[0]);
            free(item->own_icon[1]);
            item->own_icon[0] = item->own_icon[1] = NULL;
            if (strcmp(item->icon_path, entry->app.icon) != 0) {
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
        item->signature = entry->signature;
        item->instance = sl_app_host_instance_peek_at(slot);
        sl_copy_str(item->title, sizeof(item->title), entry->app.title);
        sl_copy_str(item->bundle_path, sizeof(item->bundle_path), entry->bundle_path);
        sl_copy_str(item->icon_path, sizeof(item->icon_path), entry->app.icon);
        if (item->title[0] == '\0') sl_copy_str(item->title, sizeof(item->title), "App");
    }

    sl_launcher_release_items(g_launcher_items, g_launcher_count);
    g_launcher_items = next;
    g_launcher_count = next_count;
    g_launcher_cap = count;
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
    peos_stat_t st;
    peos_memset(&st, 0, sizeof(st));
    if ((int64_t)peos_stat(SL_APP_DIR, sizeof(SL_APP_DIR) - 1u, &st, sizeof(st)) < 0) {
        g_launcher_dir_known = 0u;
        return 0u;
    }
    uint8_t changed = !g_launcher_dir_known ||
                      st.mtime_ns != g_launcher_dir_stat.mtime_ns ||
                      st.ctime_ns != g_launcher_dir_stat.ctime_ns ||
                      st.size != g_launcher_dir_stat.size;
    g_launcher_dir_stat = st;
    g_launcher_dir_known = 1u;
    return changed;
}

static void sl_launcher_tick_catalog(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    if (now < g_launcher_next_scan_ns) return;
    g_launcher_next_scan_ns = now + SL_APP_CATALOG_SCAN_INTERVAL_NS;
    if (!sl_launcher_dir_changed()) return;

    sl_scanned_app_t *entries = NULL;
    size_t count = 0u;
    if (!sl_scan_apps_from_system(&entries, &count)) return;
    if (count == 0u && g_launcher_count == 1u &&
        g_launcher_items[0].bundle_path[0] == '\0') {
        free(entries);
        return;
    }
    (void)sl_launcher_reconcile_catalog(ctx, entries, count, 0u);
    free(entries);
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
    // again to go back.
    if (!win->maximized) {
        uint32_t left = sl_launcher_reserved_left(ctx) + 24u;
        uint32_t m = 24u;
        if (ctx->fb.width <= left + m + 200u || ctx->fb.height <= 2u * m + 150u) return 1;
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_w = win->width;
        win->restore_h = win->height;
        win->x = left;
        win->y = m;
        win->width = ctx->fb.width - left - m;
        win->height = ctx->fb.height - 2u * m;
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
}

void sl_window_move(sl_window_t *win, uint32_t new_x, uint32_t new_y) {
    if (!win) return;
    win->x = new_x;
    win->y = new_y;
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
            sl_launcher_context_close(ctx);   // before acting: Informazioni opens its own card
            if (picked) (void)sl_launcher_context_activate(ctx, item_index, action, ax, ay);
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
            if (sl_dock_hit_tile(&L, x, y, &index) || sl_mgr_hit_tile(&L, x, y, &index)) {
                sl_launcher_toggle(ctx);
                (void)sl_windows_open_launcher_item(ctx, index);
                return 1;
            }
            sl_mgr_header_t H;
            sl_mgr_header(&L, &H);
            for (int c = 0; c < SL_CAT_COUNT; c++) {   // a category
                if (!sl_point_in_rect(x, y, H.cell_x[c], H.y + SL_MGR_SEG_PAD, H.cell_w[c], SL_MGR_HDR_H - 2u * SL_MGR_SEG_PAD)) continue;
                if (g_mgr_cat != (uint8_t)c) {
                    g_mgr_cat = (uint8_t)c;
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
        (void)sl_windows_open_launcher_item(ctx, index);
        return 1;
    }
    return sl_point_in_rect(x, y, L.px, L.py, L.pw, L.ph) ? 1 : 0;
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
        sl_dock_set_hover_at(ctx, (size_t)-1, 0u);
        sl_dock_dirty_panel(ctx, &L);
    }
}

void sl_launcher_handle_release(sl_ctx_t *ctx) {
    if (!ctx || g_dock_pressed == (size_t)-1) return;
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
    if (!sl_dock_hit_tile(&L, x, y, &next)) in_mgr = (uint8_t)sl_mgr_hit_tile(&L, x, y, &next);
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
            snprintf(num, sizeof(num), "%u", (unsigned)(unseen > 9u ? 9u : unseen));
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, tx + SL_DOCK_TILE - 14u, ty - 6u, 20u, 20u, 10u);
            const slsk_font_t *f = g_dock.label;
            uint32_t nw = sl_text_width(f, num);
            sl_text_draw(&t, f, (int32_t)(tx + SL_DOCK_TILE - 4u) - (int32_t)nw / 2, (int32_t)ty + 9, num, 0x11, 0x16, 0x26);
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
        uint32_t px, py, pw;
        if (g_launcher_hover_index != (size_t)-1 && sl_dock_pill_rect(&L, g_launcher_hover_index, &px, &py, &pw)) {
            char label[26];
            sl_launcher_compact_label(g_launcher_items[g_launcher_hover_index].title, label, sizeof(label));
            sl_draw_skin(ctx, g_dock.pill, (int32_t)px, (int32_t)py, pw, SL_PILL_H);
            const slsk_font_t *f = g_dock.label;
            sl_text_draw(&t, f, (int32_t)(px + SL_PILL_PAD), (int32_t)(py + SL_PILL_H / 2u) + (f->ascent - f->descent) / 2,
                         label, SL_DOCK_INK);
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
    int32_t end = sl_text_draw(&in, title, (int32_t)L.gx, base, "App", SL_DOCK_INK);
    sl_mgr_header_t H;
    sl_mgr_header(&L, &H);
    if (H.show_count) {
        char count[40];
        if (L.mgr_count == g_launcher_count) snprintf(count, sizeof(count), "%u nella cartella", (unsigned)g_launcher_count);
        else snprintf(count, sizeof(count), "%u di %u", (unsigned)L.mgr_count, (unsigned)g_launcher_count);
        sl_text_draw(&in, g_dock.label, end + 14, base, count, SL_DOCK_MUTED);
    }

    // The categories: a raised pill, the chosen one night with neon text.
    const sl_ui_color_t *neon = sl_ui_runtime_color(SL_UI_COLOR_NEON);
    const slsk_font_t *lf = g_dock.label;
    uint32_t cell_h = SL_MGR_HDR_H - 2u * SL_MGR_SEG_PAD;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED, H.seg_x, H.y, H.seg_w, SL_MGR_HDR_H, SL_MGR_HDR_H / 2u);
    for (int c = 0; c < SL_CAT_COUNT; c++) {
        uint32_t lw = sl_text_width(lf, g_mgr_cat_labels[c]);
        int32_t lx = (int32_t)(H.cell_x[c] + (H.cell_w[c] - lw) / 2u);
        int32_t lb = (int32_t)(H.y + SL_MGR_HDR_H / 2u) + (lf->ascent - lf->descent) / 2;
        if ((uint8_t)c == g_mgr_cat) {
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, H.cell_x[c], H.y + SL_MGR_SEG_PAD, H.cell_w[c], cell_h, cell_h / 2u);
            sl_text_draw(&in, lf, lx, lb, g_mgr_cat_labels[c], neon->r, neon->g, neon->b);
        } else {
            sl_text_draw(&in, lf, lx, lb, g_mgr_cat_labels[c], SL_DOCK_MUTED);
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
        else sl_text_draw(&ft, sf, tx, tb, "Cerca un'app", SL_DOCK_MUTED);
        if (sl_dock_expanded_fully() && caret < fx1) {
            sl_fill_rect(ctx, (uint32_t)caret + 1u, (uint32_t)(tb - sf->ascent), 2u, (uint32_t)(sf->ascent + sf->descent),
                         neon->r, neon->g, neon->b);
        }
    }

    for (size_t i = 0u; i < L.mgr_count; i++) {
        uint32_t tx, ty;
        sl_mgr_tile_rect(&L, i, &tx, &ty);
        if (ty + SL_MGR_TILE > L.ey + L.eh) break;
        size_t idx = L.mgr_item[i];
        sl_launcher_item_t *item = &g_launcher_items[idx];
        if (idx == g_launcher_hover_index && g_dock_hover_in_mgr && sl_dock_expanded_fully()) {
            sl_dock_draw_tile(ctx, &in, item, 4, (int32_t)tx - 4, (int32_t)ty - 4, SL_MGR_HI);
        } else {
            sl_dock_draw_tile(ctx, &in, item, 3, (int32_t)tx, (int32_t)ty, SL_MGR_TILE);
        }
        char label[26];
        sl_launcher_compact_label(item->title, label, sizeof(label));
        uint32_t lw = sl_text_width(g_dock.label, label);
        sl_text_draw(&in, g_dock.label, (int32_t)(tx + SL_MGR_TILE / 2u) - (int32_t)lw / 2,
                     (int32_t)(ty + SL_MGR_TILE + 12u) + g_dock.label->ascent, label, SL_DOCK_INK);
    }
    if (L.mgr_count == 0u) {
        sl_text_draw(&in, g_dock.search, (int32_t)L.gx, (int32_t)L.grid_top + g_dock.search->ascent,
                     g_mgr_query[0] ? "Nessuna app con questo nome" : "Nessuna app in questa categoria", SL_DOCK_MUTED);
    }
    if (saved) sl_clip_set(ctx, scx, scy, scw, sch);
    else sl_clip_clear(ctx);
}

static void sl_dock_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void *userdata) {
    sl_ctx_t *ctx = (sl_ctx_t *)userdata;
    sl_clip_set(ctx, x, y, w, h);
    sl_dock_draw(ctx);
    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count &&
        g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u) {
        uint32_t rows = g_launcher_ctx_rows;
        if (rows > 0u) {
            sl_context_menu_draw(ctx, g_launcher_ctx_x, g_launcher_ctx_y, g_launcher_ctx_w, g_launcher_ctx_h,
                                 SL_LAUNCHER_CTX_ROW_H, rows, 0u, rows,
                                 g_launcher_ctx_info ? -1 : g_launcher_ctx_hover_row,
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
    if (g_launcher_ctx_open && g_launcher_ctx_x + g_launcher_ctx_w + 4u > right) right = g_launcher_ctx_x + g_launcher_ctx_w + 4u;
    if (right > ctx->fb.width) right = ctx->fb.width;
    sl_dirty_for_each_intersection(0u, 0u, right, ctx->fb.height, sl_dock_draw_rect, ctx);
}
