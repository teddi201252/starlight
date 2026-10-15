#include "starlight_internal.h"
#include "app_host.h"
#include "context_menu_renderer.h"
#include "render/pixels.h"
#include "window.h"
#include "peos/capability_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SL_APP_DIR "/system/apps"
#define SL_APP_MANIFEST "app.ini"
#define SL_APP_CATALOG_SCAN_INTERVAL_NS 1000000000ull
#define SL_APP_DIRECTORY_BUFFER_INITIAL 4096u
#define SL_APP_DIRECTORY_BUFFER_MAX (1024u * 1024u)
#define SL_LAUNCHER_VISIBLE_MAX 8u
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
} sl_app_bundle_t;

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
    uint64_t signature;
    app_instance_t *instance;
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

static sl_launcher_item_t *g_launcher_items = NULL;
static size_t g_launcher_count = 0u;
static size_t g_launcher_cap = 0u;
static sl_image_t g_launcher_placeholder_icon;
static uint8_t g_launcher_placeholder_loaded = 0u;
static uint8_t g_launcher_placeholder_failed = 0u;
static size_t g_launcher_hover_index = (size_t)-1;
static uint8_t g_launcher_ctx_open = 0u;
static size_t g_launcher_ctx_item_index = (size_t)-1;
static uint32_t g_launcher_ctx_x = 0u;
static uint32_t g_launcher_ctx_y = 0u;
static uint32_t g_launcher_ctx_w = 0u;
static uint32_t g_launcher_ctx_h = 0u;
static int32_t g_launcher_ctx_hover_row = -1;
static uint8_t g_launcher_expanded = 0u;
static uint32_t g_launcher_extra_width = 0u;
static sl_window_rect_snapshot_t g_launcher_last_rect;
static uint64_t g_launcher_next_scan_ns = 0ull;
static sl_window_anim_t g_window_anims[SL_WIN_ANIM_MAX];

static void sl_window_refresh_zorder(void);
static void sl_launcher_compact_label(const char *src, char *dst, size_t cap);
static size_t sl_window_index_for_task(const app_instance_t *task);
static void sl_focus_window(size_t index);
static int sl_window_is_closing(const sl_window_t *win);
static void sl_anim_start_close(app_instance_t *task, int32_t current_y);
static int sl_windows_open_launcher_item(sl_ctx_t *ctx, size_t launcher_index);

static sl_window_rect_snapshot_t sl_window_snapshot(const sl_window_t *win) {
    sl_window_rect_snapshot_t snap = {0u, 0u, 0u, 0u, 0u};
    if (!win || win->width == 0u || win->height == 0u) return snap;
    snap.x = win->x;
    snap.y = win->y;
    snap.w = win->width;
    snap.h = win->height;
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

#define SL_LAUNCHER_CTX_ROW_H 20u
#define SL_LAUNCHER_CTX_MIN_W 108u

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
    hash = sl_catalog_hash_text(hash, entry->app.args);
    hash = sl_catalog_hash_text(hash, entry->app.icon);
    hash = sl_catalog_hash_bytes(hash, &entry->app.width, sizeof(entry->app.width));
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

static size_t sl_launcher_visible_count(void) {
    return (g_launcher_count < SL_LAUNCHER_VISIBLE_MAX)
               ? g_launcher_count
               : SL_LAUNCHER_VISIBLE_MAX;
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

static uint32_t sl_launcher_content_width(void) {
    uint32_t icon_w = SL_LAUNCHER_ICON_SIZE;
    uint32_t header_w = 4u * 8u; // "APPS"
    return (icon_w > header_w) ? icon_w : header_w;
}

static size_t sl_launcher_column_count(void) {
    if (g_launcher_count == 0u) return 0u;
    return (g_launcher_count + SL_LAUNCHER_VISIBLE_MAX - 1u) /
           SL_LAUNCHER_VISIBLE_MAX;
}

static uint32_t sl_launcher_collapsed_width(void) {
    return SL_LAUNCHER_PAD_X * 2u + sl_launcher_content_width();
}

static uint32_t sl_launcher_desired_extra_width(void) {
    size_t columns = sl_launcher_column_count();
    if (columns <= 1u) return 0u;

    size_t extra_columns = columns - 1u;
    uint32_t stride = sl_launcher_content_width() + SL_LAUNCHER_COLUMN_GAP;
    if (extra_columns > UINT32_MAX / stride) return UINT32_MAX;
    return (uint32_t)extra_columns * stride;
}

static uint32_t sl_launcher_item_height(void) {
    uint32_t min_h = SL_LAUNCHER_ITEM_H;
    uint32_t icon_h = SL_LAUNCHER_ICON_SIZE + 2u;
    return (icon_h > min_h) ? icon_h : min_h;
}

static uint32_t sl_launcher_width(void) {
    if (g_launcher_count == 0u) return 0u;
    uint32_t base = sl_launcher_collapsed_width();
    if (g_launcher_extra_width > UINT32_MAX - base) return UINT32_MAX;
    return base + g_launcher_extra_width;
}

static uint32_t sl_launcher_panel_height(void) {
    if (g_launcher_count == 0u) return 0u;
    uint32_t count = (uint32_t)sl_launcher_visible_count();
    uint32_t item_h = sl_launcher_item_height();
    uint32_t items_h = count * item_h;
    if (count > 1u) {
        items_h += (count - 1u) * SL_LAUNCHER_ITEM_GAP;
    }
    return SL_LAUNCHER_PAD_Y + SL_LAUNCHER_HEADER_H + 8u + items_h + SL_LAUNCHER_PAD_Y;
}

static void sl_launcher_panel_rect(const sl_ctx_t *ctx,
                                   uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint32_t panel_w = sl_launcher_width();
    uint32_t panel_h = sl_launcher_panel_height();
    uint32_t panel_x = SL_LAUNCHER_MARGIN_LEFT;
    uint32_t panel_y = 0u;

    if (ctx) {
        uint32_t available_w = (ctx->fb.width > panel_x)
                                   ? (ctx->fb.width - panel_x)
                                   : ctx->fb.width;
        if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX) {
            uint32_t arrow_space = SL_LAUNCHER_ARROW_GAP + SL_LAUNCHER_ARROW_W;
            available_w = (available_w > arrow_space) ? (available_w - arrow_space) : 0u;
        }
        if (panel_x >= ctx->fb.width) panel_x = 0u;
        if (panel_w > available_w) panel_w = available_w;
        if (panel_h > ctx->fb.height) panel_h = ctx->fb.height;
        if (ctx->fb.height > panel_h) {
            panel_y = (ctx->fb.height - panel_h) / 2u;
        }
    }

    if (x) *x = panel_x;
    if (y) *y = panel_y;
    if (w) *w = panel_w;
    if (h) *h = panel_h;
}

static void sl_launcher_item_rect(const sl_ctx_t *ctx, size_t index,
                                  uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    (void)panel_h;

    uint32_t item_h = sl_launcher_item_height();
    uint32_t column = (uint32_t)(index / SL_LAUNCHER_VISIBLE_MAX);
    uint32_t row = (uint32_t)(index % SL_LAUNCHER_VISIBLE_MAX);
    uint32_t item_x = panel_x + SL_LAUNCHER_PAD_X +
                      column * (sl_launcher_content_width() + SL_LAUNCHER_COLUMN_GAP);
    uint32_t item_y = panel_y + SL_LAUNCHER_PAD_Y + SL_LAUNCHER_HEADER_H + 8u +
                      row * (item_h + SL_LAUNCHER_ITEM_GAP);
    uint32_t item_w = sl_launcher_content_width();

    if (x) *x = item_x;
    if (y) *y = item_y;
    if (w) *w = item_w;
    if (h) *h = item_h;
}

static void sl_launcher_arrow_rect_for_panel(uint32_t panel_x, uint32_t panel_y,
                                             uint32_t panel_w, uint32_t panel_h,
                                             uint32_t *x, uint32_t *y,
                                             uint32_t *w, uint32_t *h) {
    uint32_t arrow_w = SL_LAUNCHER_ARROW_W;
    uint32_t arrow_h = (panel_h < SL_LAUNCHER_ARROW_H) ? panel_h : SL_LAUNCHER_ARROW_H;
    uint32_t arrow_x = panel_x + panel_w + SL_LAUNCHER_ARROW_GAP;
    uint32_t arrow_y = panel_y + ((panel_h > arrow_h) ? ((panel_h - arrow_h) / 2u) : 0u);

    if (x) *x = arrow_x;
    if (y) *y = arrow_y;
    if (w) *w = arrow_w;
    if (h) *h = arrow_h;
}

static void sl_launcher_arrow_rect(const sl_ctx_t *ctx,
                                   uint32_t *x, uint32_t *y,
                                   uint32_t *w, uint32_t *h) {
    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    sl_launcher_arrow_rect_for_panel(panel_x, panel_y, panel_w, panel_h, x, y, w, h);
}

static int sl_rects_intersect(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                              uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    if (aw == 0u || ah == 0u || bw == 0u || bh == 0u) return 0;
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static int sl_point_in_rect(uint32_t px, uint32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

static void sl_launcher_hover_label_rect(const sl_ctx_t *ctx, size_t index,
                                         uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!ctx || index >= g_launcher_count) {
        if (x) *x = 0u;
        if (y) *y = 0u;
        if (w) *w = 0u;
        if (h) *h = 0u;
        return;
    }

    const sl_launcher_item_t *item = &g_launcher_items[index];
    char label[26];
    sl_launcher_compact_label(item->title, label, sizeof(label));

    uint32_t text_w = sl_text_measure_width(label);
    if (text_w == 0u && label[0] != '\0') text_w = (uint32_t)strlen(label) * 8u;
    uint32_t tip_w = text_w + SL_LAUNCHER_TOOLTIP_PAD_X * 2u;
    if (tip_w > SL_LAUNCHER_TOOLTIP_W_MAX) tip_w = SL_LAUNCHER_TOOLTIP_W_MAX;
    if (tip_w < 48u) tip_w = 48u;
    uint32_t tip_h = SL_LAUNCHER_TOOLTIP_H;

    uint32_t item_x = 0u, item_y = 0u, item_w = 0u, item_h = 0u;
    sl_launcher_item_rect(ctx, index, &item_x, &item_y, &item_w, &item_h);

    int64_t centered_x = (int64_t)item_x + ((int64_t)item_w - (int64_t)tip_w) / 2;
    uint32_t tip_x = (centered_x > 0) ? (uint32_t)centered_x : 0u;
    uint32_t tip_y = (item_y > tip_h + 4u) ? (item_y - tip_h - 4u) : 0u;

    if (tip_x + tip_w > ctx->fb.width) {
        tip_x = (ctx->fb.width > tip_w + 4u) ? (ctx->fb.width - tip_w - 4u) : 0u;
    }
    if (tip_y + tip_h > ctx->fb.height) {
        tip_y = (ctx->fb.height > tip_h) ? (ctx->fb.height - tip_h) : 0u;
    }

    if (x) *x = tip_x;
    if (y) *y = tip_y;
    if (w) *w = tip_w;
    if (h) *h = tip_h;
}

static void sl_launcher_draw_hover_label(sl_ctx_t *ctx, size_t index) {
    if (!ctx || index >= g_launcher_count) return;

    const sl_launcher_item_t *item = &g_launcher_items[index];
    char label[26];
    sl_launcher_compact_label(item->title, label, sizeof(label));

    uint32_t tip_x = 0u, tip_y = 0u, tip_w = 0u, tip_h = 0u;
    sl_launcher_hover_label_rect(ctx, index, &tip_x, &tip_y, &tip_w, &tip_h);
    if (tip_w == 0u || tip_h == 0u) return;

    sl_clip_set(ctx, tip_x, tip_y, tip_w, tip_h);
    sl_fill_rect(ctx, tip_x, tip_y, tip_w, tip_h, 20u, 25u, 34u);
    sl_fill_rect(ctx, tip_x, tip_y, tip_w, 1u, 90u, 110u, 132u);
    sl_fill_rect(ctx, tip_x, tip_y + tip_h - 1u, tip_w, 1u, 90u, 110u, 132u);
    sl_fill_rect(ctx, tip_x, tip_y, 1u, tip_h, 90u, 110u, 132u);
    sl_fill_rect(ctx, tip_x + tip_w - 1u, tip_y, 1u, tip_h, 90u, 110u, 132u);
    uint32_t text_y = tip_y + ((tip_h > 8u) ? ((tip_h - 8u) / 2u) : 0u);
    sl_draw_text(ctx, tip_x + SL_LAUNCHER_TOOLTIP_PAD_X, text_y, label, 242u, 244u, 247u);
    sl_clip_clear(ctx);
}

static void sl_launcher_invalidate_overlay(sl_ctx_t *ctx,
                                           uint32_t x, uint32_t y,
                                           uint32_t w, uint32_t h) {
    if (!ctx || w == 0u || h == 0u) return;

    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);

    int64_t x0 = (x < panel_x) ? x : panel_x;
    int64_t y0 = (y < panel_y) ? y : panel_y;
    int64_t x1 = (int64_t)x + w;
    int64_t y1 = (int64_t)y + h;
    int64_t panel_x1 = (int64_t)panel_x + panel_w;
    int64_t panel_y1 = (int64_t)panel_y + panel_h;
    if (panel_x1 > x1) x1 = panel_x1;
    if (panel_y1 > y1) y1 = panel_y1;

    x0 -= 2;
    y0 -= 2;
    x1 += 2;
    y1 += 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)ctx->fb.width) x1 = ctx->fb.width;
    if (y1 > (int64_t)ctx->fb.height) y1 = ctx->fb.height;
    if (x1 <= x0 || y1 <= y0) return;
    sl_dirty_add(ctx, (uint32_t)x0, (uint32_t)y0,
                 (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
}

static int sl_launcher_hit_test(const sl_ctx_t *ctx, uint32_t x, uint32_t y, size_t *out_index) {
    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    if (panel_w == 0u || panel_h == 0u) return 0;
    if (!sl_point_in_rect(x, y, panel_x, panel_y, panel_w, panel_h)) return 0;

    uint32_t content_right = panel_x + panel_w;

    for (size_t i = 0u; i < g_launcher_count; i++) {
        uint32_t ix, iy, iw, ih;
        sl_launcher_item_rect(ctx, i, &ix, &iy, &iw, &ih);
        if (ix + iw > content_right) continue;
        if (!sl_point_in_rect(x, y, ix, iy, iw, ih)) continue;
        if (out_index) *out_index = i;
        return 1;
    }

    return 0;
}

static uint32_t sl_launcher_expand_target(const sl_ctx_t *ctx) {
    uint32_t desired = sl_launcher_desired_extra_width();
    uint32_t base = sl_launcher_collapsed_width();
    if (!ctx || desired == 0u) return desired;

    uint32_t available = (ctx->fb.width > SL_LAUNCHER_MARGIN_LEFT)
                             ? (ctx->fb.width - SL_LAUNCHER_MARGIN_LEFT)
                             : ctx->fb.width;
    uint32_t arrow_space = SL_LAUNCHER_ARROW_GAP + SL_LAUNCHER_ARROW_W;
    available = (available > arrow_space) ? (available - arrow_space) : 0u;
    if (available <= base) return 0u;
    uint32_t maximum = available - base;
    return (desired < maximum) ? desired : maximum;
}

static void sl_launcher_toggle(sl_ctx_t *ctx) {
    if (!ctx || g_launcher_count <= SL_LAUNCHER_VISIBLE_MAX) return;

    if (g_launcher_hover_index != (size_t)-1) {
        uint32_t tx = 0u, ty = 0u, tw = 0u, th = 0u;
        sl_launcher_hover_label_rect(ctx, g_launcher_hover_index, &tx, &ty, &tw, &th);
        if (tw > 0u && th > 0u) sl_launcher_invalidate_overlay(ctx, tx, ty, tw, th);
        g_launcher_hover_index = (size_t)-1;
    }

    g_launcher_expanded = g_launcher_expanded ? 0u : 1u;
    uint32_t target = g_launcher_expanded ? sl_launcher_expand_target(ctx) : 0u;
    if (target == 0u) g_launcher_expanded = 0u;
    if (!sl_tween_to_u32(SL_LAUNCHER_TWEEN_ID, &g_launcher_extra_width, target,
                         SL_LAUNCHER_ANIM_MS, SL_TWEEN_EASE_IN_OUT_CUBIC,
                         NULL, NULL)) {
        sl_tween_set_u32_immediate(&g_launcher_extra_width, target);
    }
}

static void sl_launcher_track_animation_dirty(sl_ctx_t *ctx) {
    if (!ctx) return;

    sl_window_rect_snapshot_t current = {0u, 0u, 0u, 0u, 0u};
    sl_launcher_panel_rect(ctx, &current.x, &current.y, &current.w, &current.h);
    current.valid = (current.w > 0u && current.h > 0u) ? 1u : 0u;

    uint8_t changed = (current.valid != g_launcher_last_rect.valid) ? 1u : 0u;
    if (!changed && current.valid) {
        changed = (current.x != g_launcher_last_rect.x ||
                   current.y != g_launcher_last_rect.y ||
                   current.w != g_launcher_last_rect.w ||
                   current.h != g_launcher_last_rect.h) ? 1u : 0u;
    }
    if (!changed) return;

    sl_dirty_add_snapshot(ctx, &g_launcher_last_rect);
    sl_dirty_add_snapshot(ctx, &current);
    if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX) {
        uint32_t ax = 0u, ay = 0u, aw = 0u, ah = 0u;
        if (g_launcher_last_rect.valid) {
            sl_launcher_arrow_rect_for_panel(g_launcher_last_rect.x, g_launcher_last_rect.y,
                                             g_launcher_last_rect.w, g_launcher_last_rect.h,
                                             &ax, &ay, &aw, &ah);
            sl_dirty_add(ctx, ax, ay, aw, ah);
        }
        if (current.valid) {
            sl_launcher_arrow_rect_for_panel(current.x, current.y, current.w, current.h,
                                             &ax, &ay, &aw, &ah);
            sl_dirty_add(ctx, ax, ay, aw, ah);
        }
    }
    g_launcher_last_rect = current;
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
}

static uint32_t sl_launcher_context_action_count(const sl_launcher_item_t *item) {
    if (!item) return 0u;
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (!app) return 0u;
    return (sl_window_index_for_task(app) != (size_t)-1) ? 2u : 1u;
}

static void sl_launcher_context_action_text(const sl_launcher_item_t *item, uint32_t action,
                                            char *out, size_t out_cap) {
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!item) return;
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (app && sl_window_index_for_task(app) != (size_t)-1) {
        if (action == 0u) sl_copy_str(out, out_cap, "Focus");
        else if (action == 1u) sl_copy_str(out, out_cap, "Close");
        else sl_copy_str(out, out_cap, "(invalid)");
    } else {
        if (action == 0u) sl_copy_str(out, out_cap, "Open");
        else sl_copy_str(out, out_cap, "(invalid)");
    }
}

static void sl_launcher_context_label_cb(uint32_t option_index, char *out, size_t out_cap, void *userdata) {
    const sl_launcher_item_t *item = (const sl_launcher_item_t *)userdata;
    sl_launcher_context_action_text(item, option_index, out, out_cap);
}

static void sl_launcher_context_open(sl_ctx_t *ctx, size_t item_index, uint32_t anchor_x, uint32_t anchor_y) {
    if (!ctx || item_index >= g_launcher_count) return;
    if (g_launcher_ctx_open && g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u) {
        sl_launcher_invalidate_overlay(ctx, g_launcher_ctx_x, g_launcher_ctx_y,
                                       g_launcher_ctx_w + 2u, g_launcher_ctx_h + 2u);
    }
    const sl_launcher_item_t *item = &g_launcher_items[item_index];
    uint32_t action_count = sl_launcher_context_action_count(item);
    if (action_count == 0u) return;

    uint32_t menu_w = SL_LAUNCHER_CTX_MIN_W;
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
    if (g_launcher_hover_index != (size_t)-1) {
        uint32_t tx = 0u, ty = 0u, tw = 0u, th = 0u;
        sl_launcher_hover_label_rect(ctx, g_launcher_hover_index, &tx, &ty, &tw, &th);
        if (tw > 0u && th > 0u) sl_launcher_invalidate_overlay(ctx, tx, ty, tw, th);
        g_launcher_hover_index = (size_t)-1;
    }
    sl_launcher_invalidate_overlay(ctx, g_launcher_ctx_x, g_launcher_ctx_y,
                                   g_launcher_ctx_w + 2u, g_launcher_ctx_h + 2u);
}

static int sl_launcher_context_activate(sl_ctx_t *ctx, size_t item_index, uint32_t action) {
    if (!ctx || item_index >= g_launcher_count) return 0;
    sl_launcher_item_t *item = &g_launcher_items[item_index];
    app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
    if (!app) return 0;
    item->instance = app;

    size_t existing = sl_window_index_for_task(app);
    if (existing != (size_t)-1) {
        if (action == 0u) {
            sl_window_rect_snapshot_t prev_focus = sl_window_snapshot_index(g_focused_window);
            sl_window_rect_snapshot_t target = sl_window_snapshot_index(existing);
            sl_focus_window(existing);
            sl_windows_mark_focused_activity();
            sl_dirty_add_snapshot(ctx, &prev_focus);
            sl_dirty_add_snapshot(ctx, &target);
            return 1;
        }
        if (action == 1u) {
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

    if (action == 0u) return sl_windows_open_launcher_item(ctx, item_index);
    return 0;
}

void sl_launcher_handle_context_hover(sl_ctx_t *ctx) {
    if (!ctx || !g_launcher_ctx_open || g_launcher_ctx_item_index >= g_launcher_count) return;
    if (g_launcher_ctx_w == 0u || g_launcher_ctx_h == 0u) return;
    const sl_launcher_item_t *item = &g_launcher_items[g_launcher_ctx_item_index];
    uint32_t action_count = sl_launcher_context_action_count(item);
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
    if (!win) return 0;
    if (x < win->x || y < win->y) return 0;
    if (x >= (win->x + win->width)) return 0;
    if (y >= (win->y + win->height)) return 0;
    return 1;
}

static int sl_window_close_btn_hit(const sl_window_t *win, uint32_t x, uint32_t y) {
    if (!win) return 0;
    if (!sl_window_contains_point(win, x, y)) return 0;
    if (y >= win->y + win->top_bar_height) return 0;
    if (win->width <= (SL_WIN_CLOSE_BTN_W + SL_WIN_CLOSE_BTN_MARGIN + 2u)) return 0;
    if (win->top_bar_height <= (SL_WIN_CLOSE_BTN_H + 2u)) return 0;

    uint32_t cb_x = win->x + win->width - SL_WIN_CLOSE_BTN_MARGIN - SL_WIN_CLOSE_BTN_W - 1u;
    uint32_t cb_y = win->y + 1u + ((win->top_bar_height - 2u - SL_WIN_CLOSE_BTN_H) / 2u);
    if (x < cb_x || y < cb_y) return 0;
    if (x >= cb_x + SL_WIN_CLOSE_BTN_W) return 0;
    if (y >= cb_y + SL_WIN_CLOSE_BTN_H) return 0;
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

    if (g_focused_window >= g_window_count) {
        g_focused_window = g_window_count - 1u;
    }
    sl_window_refresh_zorder();
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

static void sl_launcher_try_load_placeholder_icon(void) {
    if (g_launcher_placeholder_loaded || g_launcher_placeholder_failed) return;
    if (sl_decode_image_file(SL_RESOURCE_APP_PLACEHOLDER, &g_launcher_placeholder_icon)) {
        g_launcher_placeholder_loaded = 1u;
    } else {
        g_launcher_placeholder_failed = 1u;
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

    uint32_t w = (item && item->width > 0u) ? item->width : def_w;
    uint32_t h = (item && item->height > 0u) ? item->height : def_h;

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
    }
    free(items);
}

static int sl_launcher_reconcile_catalog(sl_ctx_t *ctx,
                                         sl_scanned_app_t *entries,
                                         size_t count,
                                         uint8_t force) {
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
            !sl_app_host_slot_restrict_caps(slot, entry->app.caps_mask)) {
            continue;
        }

        sl_launcher_item_t *item = &next[next_count++];
        if (old_index != (size_t)-1) {
            *item = g_launcher_items[old_index];
            g_launcher_items[old_index].icon_image.data = NULL;
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

    uint32_t target = (g_launcher_expanded && g_launcher_count > SL_LAUNCHER_VISIBLE_MAX)
                          ? sl_launcher_expand_target(ctx)
                          : 0u;
    if (target == 0u) g_launcher_expanded = 0u;
    if (!sl_tween_to_u32(SL_LAUNCHER_TWEEN_ID, &g_launcher_extra_width, target,
                         SL_LAUNCHER_ANIM_MS, SL_TWEEN_EASE_IN_OUT_CUBIC,
                         NULL, NULL)) {
        sl_tween_set_u32_immediate(&g_launcher_extra_width, target);
    }
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

static void sl_launcher_tick_catalog(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    if (now < g_launcher_next_scan_ns) return;
    g_launcher_next_scan_ns = now + SL_APP_CATALOG_SCAN_INTERVAL_NS;

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

int sl_windows_close_at_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx || !g_windows || g_window_count == 0u) return 0;
    for (size_t i = g_window_count; i > 0u; i--) {
        size_t idx = i - 1u;
        if (sl_window_is_closing(&g_windows[idx])) continue;
        if (!sl_window_close_btn_hit(&g_windows[idx], x, y)) continue;
        sl_window_rect_snapshot_t closing = sl_window_snapshot_index(idx);
        sl_anim_start_close(g_windows[idx].task, (int32_t)g_windows[idx].y);
        sl_windows_mark_focused_activity();
        sl_dirty_add_snapshot(ctx, &closing);
        return 1;
    }
    return 0;
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
        sl_dirty_add(ctx, g_windows[i].x, g_windows[i].y, g_windows[i].width, g_windows[i].height);
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
        uint32_t old_x = win->x;
        uint32_t old_y = win->y;
        uint32_t old_w = win->width;
        uint32_t old_h = win->height;

        int32_t y = (int32_t)(anim->y_value + 0.5f);
        if (y < 0) y = 0;
        win->y = (uint32_t)y;
        if (anim->opacity > 255.0f) anim->opacity = 255.0f;
        if (anim->opacity < 0.0f) anim->opacity = 0.0f;
        win->anim_opacity = (uint8_t)(anim->opacity + 0.5f);
        win->force_redraw = 1u;
        win->next_render_ns = 0ull;
        sl_window_mark_activity(win, peos_time_ns());

        sl_dirty_add(ctx, old_x, old_y, old_w, old_h);
        sl_dirty_add(ctx, win->x, win->y, win->width, win->height);

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

void sl_windows_render_all(sl_ctx_t *ctx) {
    if (!ctx || !g_windows || g_window_count == 0u) return;
    for (size_t i = 0; i < g_window_count; i++) {
        if (!sl_dirty_intersects_rect(g_windows[i].x, g_windows[i].y,
                                      g_windows[i].width, g_windows[i].height)) {
            continue;
        }
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
    if (g_launcher_placeholder_icon.data) {
        sl_image_free(&g_launcher_placeholder_icon);
    }
    g_launcher_placeholder_loaded = 0u;
    g_launcher_placeholder_failed = 0u;
    g_launcher_hover_index = (size_t)-1;
    g_launcher_ctx_open = 0u;
    g_launcher_ctx_item_index = (size_t)-1;
    g_launcher_ctx_x = 0u;
    g_launcher_ctx_y = 0u;
    g_launcher_ctx_w = 0u;
    g_launcher_ctx_h = 0u;
    g_launcher_next_scan_ns = 0ull;
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
    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    (void)panel_y;
    (void)panel_h;
    if (panel_w == 0u) return 0u;
    uint32_t right = panel_x + panel_w;
    if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX) {
        right += SL_LAUNCHER_ARROW_GAP + SL_LAUNCHER_ARROW_W;
    }
    return right + SL_LAUNCHER_RESERVED_GAP;
}

int sl_launcher_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;

    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count) {
        uint8_t inside_menu = sl_point_in_rect(x, y, g_launcher_ctx_x, g_launcher_ctx_y,
                                               g_launcher_ctx_w, g_launcher_ctx_h) ? 1u : 0u;
        if (inside_menu) {
            uint32_t rel_y = (y >= g_launcher_ctx_y + 1u) ? (y - (g_launcher_ctx_y + 1u)) : 0u;
            uint32_t row = rel_y / SL_LAUNCHER_CTX_ROW_H;
            uint32_t action_count = sl_launcher_context_action_count(&g_launcher_items[g_launcher_ctx_item_index]);
            if (row < action_count) {
                (void)sl_launcher_context_activate(ctx, g_launcher_ctx_item_index, row);
            }
            sl_launcher_context_close(ctx);
            return 1;
        }
        sl_launcher_context_close(ctx);
        if (x < sl_launcher_reserved_left(ctx)) return 1;
    }

    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    if (panel_w == 0u || panel_h == 0u) return 0;

    if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX) {
        uint32_t arrow_x = 0u, arrow_y = 0u, arrow_w = 0u, arrow_h = 0u;
        sl_launcher_arrow_rect(ctx, &arrow_x, &arrow_y, &arrow_w, &arrow_h);
        if (sl_point_in_rect(x, y, arrow_x, arrow_y, arrow_w, arrow_h)) {
            sl_dirty_add(ctx, arrow_x, arrow_y, arrow_w, arrow_h);
            sl_launcher_toggle(ctx);
            sl_dirty_add(ctx, panel_x, panel_y, panel_w, panel_h);
            sl_launcher_arrow_rect(ctx, &arrow_x, &arrow_y, &arrow_w, &arrow_h);
            sl_dirty_add(ctx, arrow_x, arrow_y, arrow_w, arrow_h);
            return 1;
        }
    }

    if (x < panel_x || y < panel_y) return 0;
    if (x >= panel_x + panel_w || y >= panel_y + panel_h) return 0;

    size_t index = 0u;
    if (sl_launcher_hit_test(ctx, x, y, &index)) {
        (void)sl_windows_open_launcher_item(ctx, index);
    }

    sl_dirty_add(ctx, panel_x, panel_y, panel_w, panel_h);
    return 1;
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
    if (!ctx) return;
    if (g_launcher_ctx_open) return;

    size_t next_hover = (size_t)-1;
    (void)sl_launcher_hit_test(ctx, ctx->cursor_x, ctx->cursor_y, &next_hover);
    if (next_hover == g_launcher_hover_index) return;

    if (g_launcher_hover_index != (size_t)-1) {
        uint32_t tx = 0u, ty = 0u, tw = 0u, th = 0u;
        sl_launcher_hover_label_rect(ctx, g_launcher_hover_index, &tx, &ty, &tw, &th);
        if (tw > 0u && th > 0u) sl_launcher_invalidate_overlay(ctx, tx, ty, tw, th);
    }
    if (next_hover != (size_t)-1) {
        uint32_t tx = 0u, ty = 0u, tw = 0u, th = 0u;
        sl_launcher_hover_label_rect(ctx, next_hover, &tx, &ty, &tw, &th);
        if (tw > 0u && th > 0u) sl_launcher_invalidate_overlay(ctx, tx, ty, tw, th);
    }

    g_launcher_hover_index = next_hover;
}

void sl_launcher_render(sl_ctx_t *ctx) {
    if (!ctx) return;

    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    sl_launcher_panel_rect(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    if (panel_w == 0u || panel_h == 0u) return;

    uint8_t needs_panel = sl_dirty_intersects_rect(panel_x, panel_y, panel_w, panel_h) ? 1u : 0u;
    uint8_t needs_tip = 0u;
    uint8_t needs_ctx = 0u;
    uint8_t needs_arrow = 0u;
    uint32_t arrow_x = 0u, arrow_y = 0u, arrow_w = 0u, arrow_h = 0u;
    if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX) {
        sl_launcher_arrow_rect(ctx, &arrow_x, &arrow_y, &arrow_w, &arrow_h);
        needs_arrow = sl_dirty_intersects_rect(arrow_x, arrow_y, arrow_w, arrow_h) ? 1u : 0u;
    }
    if (g_launcher_hover_index != (size_t)-1) {
        uint32_t tx = 0u, ty = 0u, tw = 0u, th = 0u;
        sl_launcher_hover_label_rect(ctx, g_launcher_hover_index, &tx, &ty, &tw, &th);
        if (tw > 0u && th > 0u && sl_dirty_intersects_rect(tx, ty, tw, th)) {
            needs_tip = 1u;
        }
    }
    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count &&
        g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u &&
        sl_dirty_intersects_rect(g_launcher_ctx_x, g_launcher_ctx_y, g_launcher_ctx_w, g_launcher_ctx_h)) {
        needs_ctx = 1u;
    }
    if (!needs_panel && !needs_tip && !needs_ctx && !needs_arrow) return;

    sl_fill_rect(ctx, panel_x, panel_y, panel_w, panel_h, 16u, 20u, 26u);
    sl_fill_rect(ctx, panel_x + panel_w - 1u, panel_y, 1u, panel_h, 54u, 65u, 78u);
    sl_draw_text(ctx, panel_x + SL_LAUNCHER_PAD_X, panel_y + SL_LAUNCHER_PAD_Y, "APPS", 228u, 233u, 238u);

    uint32_t content_right = panel_x + panel_w;

    uint32_t content_w = (content_right > panel_x) ? (content_right - panel_x) : 0u;
    sl_clip_set(ctx, panel_x, panel_y, content_w, panel_h);
    for (size_t i = 0u; i < g_launcher_count; i++) {
        sl_launcher_item_t *item = &g_launcher_items[i];
        uint32_t ix, iy, iw, ih;
        sl_launcher_item_rect(ctx, i, &ix, &iy, &iw, &ih);
        if (!sl_rects_intersect(ix, iy, iw, ih,
                                panel_x, panel_y, content_w, panel_h)) {
            continue;
        }
        sl_launcher_try_load_icon(item);

        app_instance_t *app = sl_app_host_instance_peek_at(item->slot);
        item->instance = app;
        size_t win_idx = app ? sl_window_index_for_task(app) : (size_t)-1;
        uint8_t running = (win_idx != (size_t)-1) ? 1u : 0u;
        uint8_t focused = (running && win_idx == g_focused_window) ? 1u : 0u;

        const sl_image_t *icon_img = NULL;
        if (item->icon_loaded && item->icon_image.data) {
            icon_img = &item->icon_image;
        } else {
            sl_launcher_try_load_placeholder_icon();
            if (g_launcher_placeholder_loaded && g_launcher_placeholder_icon.data) {
                icon_img = &g_launcher_placeholder_icon;
            }
        }

        uint8_t drew_icon = 0u;
        uint32_t icon_size = SL_LAUNCHER_ICON_SIZE;
        uint32_t icon_x = ix + ((iw > icon_size) ? ((iw - icon_size) / 2u) : 0u);
        uint32_t icon_y = iy + ((ih > icon_size) ? ((ih - icon_size) / 2u) : 0u);

        if (icon_img && icon_img->data) {
            sl_blit_image_stretch(ctx, icon_x, icon_y, icon_size, icon_size, icon_img);
            drew_icon = 1u;
        }

        if (running) {
            uint32_t bar_w = focused ? 4u : 3u;
            uint32_t bar_x = icon_x;
            uint32_t bar_h = icon_size;
            uint8_t gr = focused ? 188u : 173u;
            uint8_t gg = focused ? 242u : 228u;
            uint8_t gb = focused ? 130u : 109u;
            sl_fill_rect(ctx, bar_x, icon_y, bar_w, bar_h, gr, gg, gb);
        }

        if (!drew_icon) {
            char short_label[13];
            sl_launcher_compact_label(item->title, short_label, sizeof(short_label));
            sl_draw_text(ctx, ix + 12u, iy + 15u, short_label, 242u, 244u, 247u);
        }
    }
    sl_clip_clear(ctx);

    if (g_launcher_count > SL_LAUNCHER_VISIBLE_MAX && arrow_w > 0u && arrow_h > 0u) {
        sl_fill_rect(ctx, arrow_x, arrow_y, arrow_w, arrow_h, 29u, 36u, 46u);
        sl_fill_rect(ctx, arrow_x, arrow_y, arrow_w, 1u, 71u, 85u, 101u);
        sl_fill_rect(ctx, arrow_x, arrow_y + arrow_h - 1u, arrow_w, 1u, 71u, 85u, 101u);

        uint32_t cx = arrow_x + arrow_w / 2u;
        uint32_t cy = arrow_y + arrow_h / 2u;
        if (g_launcher_expanded) {
            sl_fill_rect(ctx, cx + 2u, cy - 6u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx, cy - 3u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx - 2u, cy, 2u, 2u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx, cy + 2u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx + 2u, cy + 5u, 2u, 3u, 228u, 233u, 238u);
        } else {
            sl_fill_rect(ctx, cx - 3u, cy - 6u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx - 1u, cy - 3u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx + 1u, cy, 2u, 2u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx - 1u, cy + 2u, 2u, 3u, 228u, 233u, 238u);
            sl_fill_rect(ctx, cx - 3u, cy + 5u, 2u, 3u, 228u, 233u, 238u);
        }
    }

    if (g_launcher_hover_index != (size_t)-1) {
        sl_launcher_draw_hover_label(ctx, g_launcher_hover_index);
    }

    if (g_launcher_ctx_open && g_launcher_ctx_item_index < g_launcher_count &&
        g_launcher_ctx_w > 0u && g_launcher_ctx_h > 0u) {
        const sl_launcher_item_t *item = &g_launcher_items[g_launcher_ctx_item_index];
        uint32_t action_count = sl_launcher_context_action_count(item);
        if (action_count > 0u) {
            sl_context_menu_draw(ctx,
                                 g_launcher_ctx_x, g_launcher_ctx_y, g_launcher_ctx_w, g_launcher_ctx_h,
                                 SL_LAUNCHER_CTX_ROW_H,
                                 action_count,
                                 0u,
                                 action_count,
                                 g_launcher_ctx_hover_row,
                                 0u,
                                 sl_launcher_context_label_cb,
                                 (void *)item);
        }
    }
}
