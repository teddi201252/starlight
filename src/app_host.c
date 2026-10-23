#include "app_host.h"
#include "starlight_internal.h"
#include "context_menu_renderer.h"
#include "keyboard_layout.h"
#include "orbit_text_layout.h"
#include "ui_sdk.h"
#include "render/image_decode.h"
#include "peos/orbitfb.h"
#include "peos/capability.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORBIT_MAX_LINES 256u
#define ORBIT_LINE_CHARS 160u
#define ORBIT_DRAW_ROW_CHARS 512u
#define ORBIT_PENDING_LINE_INITIAL_CHARS 256u
#define ORBIT_TITLE_CHARS 64u
#define ORBIT_EXEC_CHARS 160u
#define ORBIT_ARGS_CHARS 160u
#define ORBIT_KEY_BACKSPACE PEOS_KEY_BACKSPACE
#define ORBIT_APP_INITIAL_CAP 8u
#define ORBIT_MAX_POLL_READS 32u
#define ORBIT_POLL_INTERVAL_ACTIVE_NS 2000000ull
#define ORBIT_POLL_INTERVAL_BUSY_NS   4000000ull
#define ORBIT_POLL_INTERVAL_IDLE_NS  16000000ull
#define ORBIT_UI_CMD_SOFT_CHUNK 64u
#define ORBIT_UI_CMD_HARD_MAX 1024u
#define ORBIT_SLUI_PROTO_VERSION 1u
#define ORBIT_FB_MAX_WIDTH 4096u
#define ORBIT_FB_MAX_HEIGHT 4096u
#define ORBIT_FB_MAX_BYTES (32u * 1024u * 1024u)
#define ORBIT_FB_SHM_BYTES (4u * 1024u * 1024u)
#define ORBIT_FB_FORMAT_XRGB8888 PEOS_ORBITFB_FORMAT_XRGB8888
#define ORBIT_FB_FORMAT_RGB565 PEOS_ORBITFB_FORMAT_RGB565

static uint32_t g_orbit_rgb565_lut[65536];
static uint8_t g_orbit_rgb565_lut_ready = 0u;
static uint32_t *g_orbit_xmap = NULL;
static size_t g_orbit_xmap_cap = 0u;

#define ORBIT_SLUI_CAP_LABEL        (1u << 0)
#define ORBIT_SLUI_CAP_BUTTON       (1u << 1)
#define ORBIT_SLUI_CAP_SLIDER       (1u << 2)
#define ORBIT_SLUI_CAP_EVENT_BUTTON (1u << 3)
#define ORBIT_SLUI_CAP_EVENT_SLIDER (1u << 4)
#define ORBIT_SLUI_CAP_IMAGE_FILE   (1u << 5)
#define ORBIT_SLUI_CAP_TOGGLE       (1u << 6)
#define ORBIT_SLUI_CAP_SELECT       (1u << 7)
#define ORBIT_SLUI_CAP_TEXT_INPUT   (1u << 8)
#define ORBIT_SLUI_CAP_SCROLL_V     (1u << 9)
#define ORBIT_SLUI_CAP_EVENT_TOGGLE     (1u << 10)
#define ORBIT_SLUI_CAP_EVENT_SELECT     (1u << 11)
#define ORBIT_SLUI_CAP_EVENT_TEXT_INPUT (1u << 12)
#define ORBIT_SLUI_CAP_EVENT_SCROLL_V   (1u << 13)
#define ORBIT_SLUI_CAP_CLIP             (1u << 14)
#define ORBIT_SLUI_CAP_SEPARATOR        (1u << 15)
#define ORBIT_SLUI_CAP_LIST_ITEM        (1u << 16)
#define ORBIT_SLUI_CAP_DISABLED_STATE   (1u << 17)
#define ORBIT_SLUI_CAP_RECT             (1u << 18)
#define ORBIT_SLUI_CAP_RECT_ALPHA       (1u << 19)
#define ORBIT_SLUI_CAP_TEXT_AREA        (1u << 20)
#define ORBIT_SLUI_CAP_EVENT_TEXT_AREA  (1u << 21)
#define ORBIT_SLUI_CAP_EVENT_KEYBOARD   (1u << 22)
#define ORBIT_SLUI_CAP_THEME_TOKENS     (1u << 23)
#define ORBIT_SLUI_CAP_GRADIENT_2D      (1u << 24)
#define ORBIT_SLUI_CAP_PANEL_INPUT      (1u << 25)
#define ORBIT_SLUI_CAP_FONT_SIZE        (1u << 26)
#define ORBIT_SLUI_CAP_EVENT_DOUBLE_CLICK (1u << 27)
#define ORBIT_SLUI_CAP_CONTEXT_MENU       (1u << 28)
#define ORBIT_SLUI_CAP_EVENT_CONTEXT_MENU (1u << 29)
#define ORBIT_SLUI_CAP_FOCUS              (1u << 30)

#define ORBIT_UI_DOUBLE_CLICK_MS_DEFAULT 300u
#define ORBIT_UI_DOUBLE_CLICK_MS_MIN      80u
#define ORBIT_UI_DOUBLE_CLICK_MS_MAX      1000u

#define ORBIT_SLUI_ERR_MALFORMED   100u
#define ORBIT_SLUI_ERR_HELLO       101u
#define ORBIT_SLUI_ERR_LABEL       110u
#define ORBIT_SLUI_ERR_BUTTON      111u
#define ORBIT_SLUI_ERR_SLIDER      112u
#define ORBIT_SLUI_ERR_IMAGE_FILE  113u
#define ORBIT_SLUI_ERR_TOGGLE      114u
#define ORBIT_SLUI_ERR_SELECT      115u
#define ORBIT_SLUI_ERR_TEXT_INPUT  116u
#define ORBIT_SLUI_ERR_SCROLL_V    117u
#define ORBIT_SLUI_ERR_CLIP        118u
#define ORBIT_SLUI_ERR_SEPARATOR   119u
#define ORBIT_SLUI_ERR_LIST_ITEM   120u
#define ORBIT_SLUI_ERR_RECT        121u
#define ORBIT_SLUI_ERR_RECTA       122u
#define ORBIT_SLUI_ERR_TEXT_AREA   123u
#define ORBIT_SLUI_ERR_GRAD2D      124u
#define ORBIT_SLUI_ERR_PANEL       125u
#define ORBIT_SLUI_ERR_FONT        126u
#define ORBIT_SLUI_ERR_CONTEXT     127u
#define ORBIT_SLUI_ERR_UNKNOWN_CMD 199u
#define ORBIT_SLUI_ERR_CMD_LIMIT   200u

typedef enum {
    ORBIT_UI_CMD_NONE = 0,
    ORBIT_UI_CMD_LABEL = 1,
    ORBIT_UI_CMD_BUTTON = 2,
    ORBIT_UI_CMD_SLIDER = 3,
    ORBIT_UI_CMD_IMAGE_FILE = 4,
    ORBIT_UI_CMD_TOGGLE = 5,
    ORBIT_UI_CMD_SELECT = 6,
    ORBIT_UI_CMD_TEXT_INPUT = 7,
    ORBIT_UI_CMD_SCROLL_V = 8,
    ORBIT_UI_CMD_CLIP_PUSH = 9,
    ORBIT_UI_CMD_CLIP_POP = 10,
    ORBIT_UI_CMD_SEPARATOR = 11,
    ORBIT_UI_CMD_LIST_ITEM = 12,
    ORBIT_UI_CMD_RECT = 13,
    ORBIT_UI_CMD_RECTA = 14,
    ORBIT_UI_CMD_TEXT_AREA = 15,
    ORBIT_UI_CMD_LABEL_THEME = 16,
    ORBIT_UI_CMD_RECT_THEME = 17,
    ORBIT_UI_CMD_RECTA_THEME = 18,
    ORBIT_UI_CMD_GRAD2D = 19,
    ORBIT_UI_CMD_PANEL = 20,
    ORBIT_UI_CMD_FONT_SIZE = 21,
    ORBIT_UI_CMD_CONTEXT_MENU = 22,
} orbit_ui_cmd_type_t;

typedef struct {
    uint8_t type;
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t min_v;
    uint32_t max_v;
    uint32_t value;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    uint8_t grad[4][4];
    char *text;
    size_t text_cap;
    sl_image_t image;
    uint8_t image_loaded;
    uint8_t image_failed;
    uint8_t enabled;
} orbit_ui_cmd_t;

typedef struct {
    uint8_t enabled;
    uint8_t mouse_down;
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    uint8_t mouse_r_down;
    uint8_t mouse_r_pressed;
    uint8_t mouse_r_released;
    int32_t mouse_wheel_x;
    int32_t mouse_wheel_y;
    uint32_t clipboard_cmds;
    uint8_t key_event_count;
    uint8_t committed;
    // The last text of the field being edited that Starlight sent to the app
    // and the app has not yet given back (hash, 0 = none), and since when.
    uint32_t pending_text_hash;
    uint64_t pending_text_ns;
    sl_ui_ctx_t ui;
    peos_input_event_t key_events[16];
    orbit_ui_cmd_t *cmds;
    size_t cmd_count;
    size_t cmd_cap;
    orbit_ui_cmd_t *staged_cmds;
    size_t staged_count;
    size_t staged_cap;
    uint8_t staged_enabled;
    uint8_t cmd_limit_reached;
    uint8_t hello_seen;
    uint8_t host_hello_sent;
    uint8_t client_proto_ver;
    uint32_t client_caps_mask;
    uint8_t key_down[256];
    uint32_t active_panel_id;
    uint32_t hover_cmd_id;
    uint32_t hover_cmd_x;
    uint32_t hover_cmd_y;
    uint32_t hover_cmd_w;
    uint32_t hover_cmd_h;
    uint32_t nav_focus_id;
    uint8_t keyboard_nav_active;
    uint32_t double_click_ms;
    uint32_t last_click_id;
    uint64_t last_click_ns;
    uint8_t theme_override_enabled;
    sl_ui_theme_t theme_override;
    uint32_t context_open_id;
    uint32_t context_open_anchor_x;
    uint32_t context_open_anchor_y;
    uint32_t context_popup_x;
    uint32_t context_popup_y;
    uint32_t context_popup_w;
    uint32_t context_popup_h;
    uint32_t context_popup_row_h;
    uint32_t context_scroll_idx;
} orbit_ui_state_t;

typedef struct {
    uint8_t enabled;
    char title[ORBIT_TITLE_CHARS];
    char exec_path[ORBIT_EXEC_CHARS];
    char exec_args[ORBIT_ARGS_CHARS];
    uint8_t caps_restrict;
    uint64_t caps_mask;
    uint8_t paths_restrict;
    uint32_t path_count;
    peos_path_grant_t paths[PEOS_PATH_MAX_GRANTS];
} orbit_slot_cfg_t;

typedef struct {
    uint8_t active;
    uint8_t receiving;
    uint8_t discard;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t format;
    uint8_t pixels_owned;
    uint8_t *pixels;
    size_t pixels_len;
    size_t pixels_cap;
    uint64_t shared_id;
    uint8_t *shared_base;
    size_t shared_len;
    peos_orbitfb_shm_t *shared_header;
    uint32_t pending_width;
    uint32_t pending_height;
    uint32_t pending_pitch;
    uint8_t pending_format;
    size_t pending_len;
    size_t pending_off;
} orbit_surface_t;

typedef struct {
    char lines[ORBIT_MAX_LINES][ORBIT_LINE_CHARS];
    size_t line_head;
    size_t line_count;
    char *pending_line;
    size_t pending_len;
    size_t pending_cap;
    uint8_t shift_down;
    uint8_t caps_lock_on;
    uint8_t ctrl_down;
    uint8_t alt_down;
    uint8_t altgr_down;
    uint8_t super_down;
    uint8_t initialized;
    int64_t child_pid;
    int64_t child_stdin_fd;
    int64_t child_stdout_fd;
    uint64_t next_poll_ns;
    uint8_t enabled;
    char title[ORBIT_TITLE_CHARS];
    char exec_path[ORBIT_EXEC_CHARS];
    char exec_args[ORBIT_ARGS_CHARS];
    size_t slot_index;
    orbit_ui_state_t ui;
    orbit_surface_t surface;
    uint8_t esc_active;
    uint8_t csi_active;
    char csi_buf[16];
    size_t csi_len;
} orbit_shell_t;

static orbit_shell_t **g_orbit = NULL;
static app_instance_t **g_orbit_app = NULL;
static orbit_slot_cfg_t *g_orbit_cfg = NULL;
static size_t g_orbit_slot_count = 0u;
static size_t g_orbit_slot_cap = 0u;
static uint8_t g_orbit_cfg_init = 0u;
static uint32_t g_orbit_double_click_ms = ORBIT_UI_DOUBLE_CLICK_MS_DEFAULT;
static uint8_t g_reload_display_requested = 0u;
static uint8_t g_reload_theme_requested = 0u;
static uint8_t g_open_app_requested = 0u;
static char g_open_app_bundle_path[256];

static int orbit_pending_line_reserve(orbit_shell_t *shell, size_t need_cap) {
    if (!shell) return 0;
    if (need_cap == 0u) need_cap = 1u;
    if (shell->pending_cap >= need_cap && shell->pending_line) return 1;

    size_t cap = shell->pending_cap;
    if (cap < ORBIT_PENDING_LINE_INITIAL_CHARS) cap = ORBIT_PENDING_LINE_INITIAL_CHARS;
    while (cap < need_cap) {
        if (cap > (SIZE_MAX / 2u)) {
            cap = need_cap;
            break;
        }
        cap *= 2u;
    }

    char *buf = (char *)realloc(shell->pending_line, cap);
    if (!buf) return 0;
    shell->pending_line = buf;
    shell->pending_cap = cap;
    if (shell->pending_len >= shell->pending_cap) shell->pending_len = shell->pending_cap - 1u;
    shell->pending_line[shell->pending_len] = '\0';
    return 1;
}

static void orbit_pending_line_reset(orbit_shell_t *shell) {
    if (!shell) return;
    shell->pending_len = 0u;
    if (shell->pending_line && shell->pending_cap > 0u) {
        shell->pending_line[0] = '\0';
    }
}

static void orbit_pending_line_free(orbit_shell_t *shell) {
    if (!shell) return;
    if (shell->pending_line) {
        free(shell->pending_line);
        shell->pending_line = NULL;
    }
    shell->pending_cap = 0u;
    shell->pending_len = 0u;
}

static void orbit_surface_reset(orbit_shell_t *shell) {
    if (!shell) return;
    if (!shell->surface.pixels_owned) {
        shell->surface.pixels = NULL;
        shell->surface.pixels_cap = 0u;
    }
    shell->surface.active = 0u;
    shell->surface.receiving = 0u;
    shell->surface.discard = 0u;
    shell->surface.width = 0u;
    shell->surface.height = 0u;
    shell->surface.pitch = 0u;
    shell->surface.format = 0u;
    shell->surface.pixels_len = 0u;
    shell->surface.pending_width = 0u;
    shell->surface.pending_height = 0u;
    shell->surface.pending_pitch = 0u;
    shell->surface.pending_format = 0u;
    shell->surface.pending_len = 0u;
    shell->surface.pending_off = 0u;
}

static void orbit_surface_free(orbit_shell_t *shell) {
    if (!shell) return;
    if (shell->surface.pixels_owned && shell->surface.pixels) {
        free(shell->surface.pixels);
    }
    if (shell->surface.shared_base && shell->surface.shared_len) {
        (void)peos_munmap((uint64_t)(uintptr_t)shell->surface.shared_base,
                           (uint64_t)shell->surface.shared_len);
    }
    shell->surface.pixels = NULL;
    shell->surface.pixels_cap = 0u;
    shell->surface.pixels_owned = 0u;
    shell->surface.shared_base = NULL;
    shell->surface.shared_len = 0u;
    shell->surface.shared_header = NULL;
    orbit_surface_reset(shell);
}

static void orbit_surface_destroy_shared_object(orbit_shell_t *shell) {
    if (!shell) return;
    uint64_t id = shell->surface.shared_id;
    orbit_surface_free(shell);
    if (id != 0u) {
        (void)peos_shm_destroy(id);
    }
    shell->surface.shared_id = 0u;
}

static void orbit_copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    size_t i = 0u;
    while (i + 1u < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int orbit_cmd_text_reserve(orbit_ui_cmd_t *cmd, size_t need_cap) {
    if (!cmd) return 0;
    if (need_cap == 0u) need_cap = 1u;
    if (cmd->text && cmd->text_cap >= need_cap) return 1;
    size_t cap = cmd->text_cap;
    if (cap < 32u) cap = 32u;
    while (cap < need_cap) {
        if (cap > (SIZE_MAX / 2u)) {
            cap = need_cap;
            break;
        }
        cap *= 2u;
    }
    char *grown = (char *)realloc(cmd->text, cap);
    if (!grown) return 0;
    cmd->text = grown;
    cmd->text_cap = cap;
    return 1;
}

static void orbit_cmd_text_free(orbit_ui_cmd_t *cmd) {
    if (!cmd) return;
    if (cmd->text) {
        free(cmd->text);
        cmd->text = NULL;
    }
    cmd->text_cap = 0u;
}

static int orbit_cmd_set_text_raw(orbit_ui_cmd_t *cmd, const char *src) {
    if (!cmd) return 0;
    if (!src) src = "";
    size_t n = peos_strlen(src);
    if (!orbit_cmd_text_reserve(cmd, n + 1u)) return 0;
    if (n > 0u) peos_memcpy(cmd->text, src, n);
    cmd->text[n] = '\0';
    return 1;
}

static int orbit_cmd_set_text_unescaped(orbit_ui_cmd_t *cmd, const char *src) {
    if (!cmd) return 0;
    if (!src) src = "";
    size_t si = 0u;
    size_t out_n = 0u;
    while (src[si]) {
        char c = src[si++];
        if (c == '\\' && src[si]) si++;
        out_n++;
    }
    if (!orbit_cmd_text_reserve(cmd, out_n + 1u)) return 0;
    si = 0u;
    size_t di = 0u;
    while (src[si]) {
        char c = src[si++];
        if (c == '\\' && src[si]) {
            char n = src[si++];
            if (n == 'n') c = '\n';
            else if (n == 'r') c = '\r';
            else if (n == 't') c = '\t';
            else if (n == '\\') c = '\\';
            else c = n;
        }
        cmd->text[di++] = c;
    }
    cmd->text[di] = '\0';
    return 1;
}

static uint8_t orbit_u8_clamp(uint32_t v) {
    return (uint8_t)((v > 255u) ? 255u : v);
}

static uint32_t orbit_double_click_ms_clamp(uint32_t v) {
    if (v < ORBIT_UI_DOUBLE_CLICK_MS_MIN) return ORBIT_UI_DOUBLE_CLICK_MS_MIN;
    if (v > ORBIT_UI_DOUBLE_CLICK_MS_MAX) return ORBIT_UI_DOUBLE_CLICK_MS_MAX;
    return v;
}

static void orbit_theme_override_set_accent(orbit_shell_t *shell, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!shell) return;
    sl_ui_theme_runtime_get(&shell->ui.theme_override);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT].r = r;
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT].g = g;
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT].b = b;
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT].a = a;
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_HOVER].r = orbit_u8_clamp((uint32_t)r + 28u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_HOVER].g = orbit_u8_clamp((uint32_t)g + 28u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_HOVER].b = orbit_u8_clamp((uint32_t)b + 28u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_HOVER].a = a;
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_ACTIVE].r = (uint8_t)((r > 24u) ? (r - 24u) : 0u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_ACTIVE].g = (uint8_t)((g > 24u) ? (g - 24u) : 0u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_ACTIVE].b = (uint8_t)((b > 24u) ? (b - 24u) : 0u);
    shell->ui.theme_override.colors[SL_UI_COLOR_ACCENT_ACTIVE].a = a;
    shell->ui.theme_override_enabled = 1u;
    shell->ui.ui.theme_initialized = 0u;
}

static const char *orbit_skip_spaces(const char *s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s;
}

static int orbit_parse_u32(const char **ps, uint32_t *out) {
    if (!ps || !*ps || !out) return 0;
    const char *s = orbit_skip_spaces(*ps);
    if (!s || *s < '0' || *s > '9') return 0;
    uint64_t v = 0u;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFu) return 0;
        s++;
    }
    *out = (uint32_t)v;
    *ps = s;
    return 1;
}

static int orbit_parse_size(const char **ps, size_t *out) {
    if (!ps || !*ps || !out) return 0;
    const char *s = orbit_skip_spaces(*ps);
    if (!s || *s < '0' || *s > '9') return 0;
    size_t v = 0u;
    while (*s >= '0' && *s <= '9') {
        size_t digit = (size_t)(*s - '0');
        if (v > ((SIZE_MAX - digit) / 10u)) return 0;
        v = (v * 10u) + digit;
        s++;
    }
    *out = v;
    *ps = s;
    return 1;
}

static int orbit_token_is(const char *tok, size_t tok_len, const char *lit) {
    if (!tok || !lit) return 0;
    size_t i = 0u;
    while (i < tok_len && lit[i]) {
        if (tok[i] != lit[i]) return 0;
        i++;
    }
    return (i == tok_len && lit[i] == '\0') ? 1 : 0;
}

static int orbit_next_token(const char **ps, const char **tok, size_t *tok_len) {
    if (!ps || !*ps || !tok || !tok_len) return 0;
    const char *s = orbit_skip_spaces(*ps);
    if (!s || *s == '\0') return 0;
    const char *start = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    *tok = start;
    *tok_len = (size_t)(s - start);
    *ps = s;
    return (*tok_len > 0u) ? 1 : 0;
}

static uint32_t orbit_parse_cap_token(const char *tok, size_t tok_len) {
    if (orbit_token_is(tok, tok_len, "LABEL")) return ORBIT_SLUI_CAP_LABEL;
    if (orbit_token_is(tok, tok_len, "BUTTON")) return ORBIT_SLUI_CAP_BUTTON;
    if (orbit_token_is(tok, tok_len, "SLIDER")) return ORBIT_SLUI_CAP_SLIDER;
    if (orbit_token_is(tok, tok_len, "EVENT_BUTTON")) return ORBIT_SLUI_CAP_EVENT_BUTTON;
    if (orbit_token_is(tok, tok_len, "EVENT_SLIDER")) return ORBIT_SLUI_CAP_EVENT_SLIDER;
    if (orbit_token_is(tok, tok_len, "IMAGE_FILE")) return ORBIT_SLUI_CAP_IMAGE_FILE;
    if (orbit_token_is(tok, tok_len, "TOGGLE")) return ORBIT_SLUI_CAP_TOGGLE;
    if (orbit_token_is(tok, tok_len, "SELECT")) return ORBIT_SLUI_CAP_SELECT;
    if (orbit_token_is(tok, tok_len, "TEXT_INPUT")) return ORBIT_SLUI_CAP_TEXT_INPUT;
    if (orbit_token_is(tok, tok_len, "SCROLL_V")) return ORBIT_SLUI_CAP_SCROLL_V;
    if (orbit_token_is(tok, tok_len, "EVENT_TOGGLE")) return ORBIT_SLUI_CAP_EVENT_TOGGLE;
    if (orbit_token_is(tok, tok_len, "EVENT_SELECT")) return ORBIT_SLUI_CAP_EVENT_SELECT;
    if (orbit_token_is(tok, tok_len, "EVENT_TEXT_INPUT")) return ORBIT_SLUI_CAP_EVENT_TEXT_INPUT;
    if (orbit_token_is(tok, tok_len, "EVENT_SCROLL_V")) return ORBIT_SLUI_CAP_EVENT_SCROLL_V;
    if (orbit_token_is(tok, tok_len, "CLIP")) return ORBIT_SLUI_CAP_CLIP;
    if (orbit_token_is(tok, tok_len, "SEPARATOR")) return ORBIT_SLUI_CAP_SEPARATOR;
    if (orbit_token_is(tok, tok_len, "LIST_ITEM")) return ORBIT_SLUI_CAP_LIST_ITEM;
    if (orbit_token_is(tok, tok_len, "DISABLED_STATE")) return ORBIT_SLUI_CAP_DISABLED_STATE;
    if (orbit_token_is(tok, tok_len, "RECT")) return ORBIT_SLUI_CAP_RECT;
    if (orbit_token_is(tok, tok_len, "RECT_ALPHA")) return ORBIT_SLUI_CAP_RECT_ALPHA;
    if (orbit_token_is(tok, tok_len, "TEXT_AREA")) return ORBIT_SLUI_CAP_TEXT_AREA;
    if (orbit_token_is(tok, tok_len, "EVENT_TEXT_AREA")) return ORBIT_SLUI_CAP_EVENT_TEXT_AREA;
    if (orbit_token_is(tok, tok_len, "EVENT_KEYBOARD")) return ORBIT_SLUI_CAP_EVENT_KEYBOARD;
    if (orbit_token_is(tok, tok_len, "THEME_TOKENS")) return ORBIT_SLUI_CAP_THEME_TOKENS;
    if (orbit_token_is(tok, tok_len, "GRADIENT_2D")) return ORBIT_SLUI_CAP_GRADIENT_2D;
    if (orbit_token_is(tok, tok_len, "PANEL_INPUT")) return ORBIT_SLUI_CAP_PANEL_INPUT;
    if (orbit_token_is(tok, tok_len, "FONT_SIZE")) return ORBIT_SLUI_CAP_FONT_SIZE;
    if (orbit_token_is(tok, tok_len, "EVENT_DOUBLE_CLICK")) return ORBIT_SLUI_CAP_EVENT_DOUBLE_CLICK;
    if (orbit_token_is(tok, tok_len, "CONTEXT_MENU")) return ORBIT_SLUI_CAP_CONTEXT_MENU;
    if (orbit_token_is(tok, tok_len, "EVENT_CONTEXT_MENU")) return ORBIT_SLUI_CAP_EVENT_CONTEXT_MENU;
    if (orbit_token_is(tok, tok_len, "FOCUS")) return ORBIT_SLUI_CAP_FOCUS;
    return 0u;
}

static void orbit_send_ui_error(orbit_shell_t *shell, uint32_t code, const char *msg) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!msg) msg = "protocol_error";

    char out[192];
    size_t n = 0u;
    const char *prefix = "@SLUI ERROR ";
    while (prefix[n] && n + 1u < sizeof(out)) {
        out[n] = prefix[n];
        n++;
    }

    char num[16];
    size_t m = 0u;
    if (code == 0u) {
        num[m++] = '0';
    } else {
        uint32_t v = code;
        char rev[16];
        size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }

    for (size_t i = 0u; i < m && n + 1u < sizeof(out); i++) out[n++] = num[i];
    if (n + 1u < sizeof(out)) out[n++] = ' ';
    for (size_t i = 0u; msg[i] && n + 1u < sizeof(out); i++) out[n++] = msg[i];
    if (n + 1u < sizeof(out)) out[n++] = '\n';

    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, out, n);
}

static void orbit_send_host_hello(orbit_shell_t *shell) {
    if (!shell || shell->child_stdin_fd < 0) return;
    const char *msg =
        "@SLUI HOST HELLO 1 CAPS LABEL BUTTON SLIDER IMAGE_FILE TOGGLE SELECT TEXT_INPUT TEXT_AREA SCROLL_V CLIP SEPARATOR LIST_ITEM DISABLED_STATE RECT RECT_ALPHA EVENT_BUTTON EVENT_SLIDER EVENT_TOGGLE EVENT_SELECT EVENT_TEXT_INPUT EVENT_TEXT_AREA EVENT_SCROLL_V EVENT_KEYBOARD THEME_TOKENS GRADIENT_2D PANEL_INPUT FONT_SIZE EVENT_DOUBLE_CLICK CONTEXT_MENU EVENT_CONTEXT_MENU FOCUS\n";
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, peos_strlen(msg));
    shell->ui.host_hello_sent = 1u;
}

static void orbit_send_fb_host_windowed(orbit_shell_t *shell) {
    if (!shell || shell->child_stdin_fd < 0) return;
    static const char msg[] = "@ORBITFB HOST WINDOWED\n";
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, sizeof(msg) - 1u);
}

static void orbit_send_fb_key(orbit_shell_t *shell, uint16_t code, uint8_t pressed) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "@ORBITFB KEY %u %u\n",
                     (unsigned)code, pressed ? 1u : 0u);
    if (n <= 0 || (size_t)n >= sizeof(msg)) return;
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, (size_t)n);
}

static int orbit_surface_setup_shared(orbit_shell_t *shell) {
    if (!shell) return 0;
    if (shell->surface.shared_base && shell->surface.shared_header) return 1;

    peos_shm_map_t map;
    if (shell->surface.shared_id != 0u) {
        if (peos_shm_map(shell->surface.shared_id, &map) == 0 && map.addr != 0u &&
            map.len >= PEOS_ORBITFB_SHM_HEADER_BYTES + 2u * 4096u) {
            shell->surface.shared_base = (uint8_t *)(uintptr_t)map.addr;
            shell->surface.shared_len = (size_t)map.len;
            shell->surface.shared_header = (peos_orbitfb_shm_t *)(void *)shell->surface.shared_base;
            return 1;
        }
        shell->surface.shared_id = 0u;
    }

    int64_t id = peos_shm_create(ORBIT_FB_SHM_BYTES);
    if (id <= 0) return 0;

    if (peos_shm_map((uint64_t)id, &map) != 0 || map.addr == 0u ||
        map.len < PEOS_ORBITFB_SHM_HEADER_BYTES + 2u * 4096u) {
        (void)peos_shm_destroy((uint64_t)id);
        return 0;
    }

    shell->surface.shared_id = (uint64_t)id;
    shell->surface.shared_base = (uint8_t *)(uintptr_t)map.addr;
    shell->surface.shared_len = (size_t)map.len;
    shell->surface.shared_header = (peos_orbitfb_shm_t *)(void *)shell->surface.shared_base;

    peos_memset(shell->surface.shared_base, 0, PEOS_ORBITFB_SHM_HEADER_BYTES);
    shell->surface.shared_header->magic = PEOS_ORBITFB_SHM_MAGIC;
    shell->surface.shared_header->version = PEOS_ORBITFB_SHM_VERSION;
    shell->surface.shared_header->slot_count = PEOS_ORBITFB_SHM_SLOTS;
    shell->surface.shared_header->slot_size =
        (uint32_t)((shell->surface.shared_len - PEOS_ORBITFB_SHM_HEADER_BYTES) /
                   PEOS_ORBITFB_SHM_SLOTS);
    return 1;
}

static void orbit_send_fb_host_shared(orbit_shell_t *shell) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!orbit_surface_setup_shared(shell)) {
        orbit_send_fb_host_windowed(shell);
        return;
    }

    // Only the child running in this Orbit window (and what it spawns) may map it.
    if (shell->child_pid <= 0 ||
        peos_shm_allow(shell->surface.shared_id, (uint64_t)shell->child_pid) != 0) {
        orbit_send_fb_host_windowed(shell);
        return;
    }

    char msg[96];
    int n = snprintf(msg, sizeof(msg), "@ORBITFB HOST SHM %llu %llu\n",
                     (unsigned long long)shell->surface.shared_id,
                     (unsigned long long)shell->surface.shared_len);
    if (n <= 0 || (size_t)n >= sizeof(msg)) {
        orbit_send_fb_host_windowed(shell);
        return;
    }
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, (size_t)n);
}

static int orbit_surface_reserve(orbit_shell_t *shell, size_t need) {
    if (!shell) return 0;
    if (need == 0u) return 0;
    if (need > ORBIT_FB_MAX_BYTES) return 0;
    if (shell->surface.pixels && !shell->surface.pixels_owned) {
        shell->surface.pixels = NULL;
        shell->surface.pixels_cap = 0u;
    }
    if (shell->surface.pixels && shell->surface.pixels_cap >= need) return 1;
    uint8_t *buf = (uint8_t *)realloc(shell->surface.pixels, need);
    if (!buf) return 0;
    shell->surface.pixels = buf;
    shell->surface.pixels_cap = need;
    shell->surface.pixels_owned = 1u;
    return 1;
}

static int orbit_fb_handle_line(orbit_shell_t *shell, const char *line) {
    if (!shell || !line) return 0;
    const char *p = line;
    const char *token = "@ORBITFB";
    size_t pos = 0u;
    uint8_t found = 0u;
    while (p[pos]) {
        size_t i = 0u;
        while (token[i] && p[pos + i] == token[i]) i++;
        if (token[i] == '\0') {
            p = p + pos + i;
            found = 1u;
            break;
        }
        pos++;
    }
    if (!found) return 0;

    if (*p == ':') p++;
    p = orbit_skip_spaces(p);

    const char *cmd_tok = NULL;
    size_t cmd_len = 0u;
    if (!orbit_next_token(&p, &cmd_tok, &cmd_len)) return 1;

    if (orbit_token_is(cmd_tok, cmd_len, "HELLO")) {
        orbit_send_fb_host_shared(shell);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "PRESENT")) {
        uint32_t seq = 0u;
        (void)orbit_parse_u32(&p, &seq);
        (void)seq;

        peos_orbitfb_shm_t *shm = shell->surface.shared_header;
        if (!shm || !shell->surface.shared_base ||
            shm->magic != PEOS_ORBITFB_SHM_MAGIC ||
            shm->version != PEOS_ORBITFB_SHM_VERSION ||
            shm->slot_count == 0u ||
            shm->slot_count > PEOS_ORBITFB_SHM_SLOTS) {
            orbit_surface_reset(shell);
            return 1;
        }

        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t slot = shm->ready_slot;
        uint32_t slot_size = shm->slot_size;
        uint32_t w = shm->width;
        uint32_t h = shm->height;
        uint32_t pitch = shm->pitch;
        uint32_t format = shm->format;
        uint32_t bytespp = (format == ORBIT_FB_FORMAT_RGB565) ? 2u : 4u;
        size_t len = (size_t)pitch * (size_t)h;
        size_t offset = PEOS_ORBITFB_SHM_HEADER_BYTES + (size_t)slot * (size_t)slot_size;

        if (format != ORBIT_FB_FORMAT_RGB565 && format != ORBIT_FB_FORMAT_XRGB8888) {
            orbit_surface_reset(shell);
            return 1;
        }
        if (slot >= shm->slot_count ||
            w == 0u || h == 0u ||
            w > ORBIT_FB_MAX_WIDTH || h > ORBIT_FB_MAX_HEIGHT ||
            pitch < w * bytespp ||
            len == 0u || len > slot_size ||
            offset > shell->surface.shared_len ||
            len > shell->surface.shared_len - offset) {
            orbit_surface_reset(shell);
            return 1;
        }

        if (shell->surface.pixels_owned && shell->surface.pixels) {
            free(shell->surface.pixels);
        }
        shell->surface.pixels_owned = 0u;
        shell->surface.pixels = shell->surface.shared_base + offset;
        shell->surface.pixels_cap = 0u;
        shell->surface.pixels_len = len;
        shell->surface.width = w;
        shell->surface.height = h;
        shell->surface.pitch = pitch;
        shell->surface.format = (uint8_t)format;
        shell->surface.receiving = 0u;
        shell->surface.discard = 0u;
        shell->surface.active = 1u;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "FRAME")) {
        const char *fmt_tok = NULL;
        size_t fmt_len = 0u;
        uint32_t w = 0u, h = 0u, pitch = 0u;
        uint8_t format = 0u;
        uint32_t bytespp = 0u;
        size_t len = 0u;
        if (!orbit_next_token(&p, &fmt_tok, &fmt_len) ||
            !orbit_parse_u32(&p, &w) ||
            !orbit_parse_u32(&p, &h) ||
            !orbit_parse_u32(&p, &pitch) ||
            !orbit_parse_size(&p, &len)) {
            orbit_surface_reset(shell);
            return 1;
        }
        if (orbit_token_is(fmt_tok, fmt_len, "XRGB8888")) {
            format = ORBIT_FB_FORMAT_XRGB8888;
            bytespp = 4u;
        } else if (orbit_token_is(fmt_tok, fmt_len, "RGB565")) {
            format = ORBIT_FB_FORMAT_RGB565;
            bytespp = 2u;
        } else {
            orbit_surface_reset(shell);
            return 1;
        }

        if (w == 0u || h == 0u ||
            w > ORBIT_FB_MAX_WIDTH || h > ORBIT_FB_MAX_HEIGHT ||
            pitch < (w * bytespp) || len == 0u ||
            len > ORBIT_FB_MAX_BYTES ||
            len != ((size_t)pitch * (size_t)h)) {
            orbit_surface_reset(shell);
            shell->surface.discard = 1u;
            shell->surface.pending_len = len;
            shell->surface.pending_off = 0u;
            return 1;
        }

        if (!orbit_surface_reserve(shell, len)) {
            orbit_surface_reset(shell);
            shell->surface.discard = 1u;
            shell->surface.pending_len = len;
            shell->surface.pending_off = 0u;
            return 1;
        }

        shell->surface.receiving = 1u;
        shell->surface.discard = 0u;
        shell->surface.pending_width = w;
        shell->surface.pending_height = h;
        shell->surface.pending_pitch = pitch;
        shell->surface.pending_format = format;
        shell->surface.pending_len = len;
        shell->surface.pending_off = 0u;
        return 1;
    }

    return 1;
}

static void orbit_ui_cmd_release(orbit_ui_cmd_t *cmd) {
    if (!cmd) return;
    orbit_cmd_text_free(cmd);
    if (cmd->image.data) {
        sl_image_free(&cmd->image);
    }
    cmd->image_loaded = 0u;
    cmd->image_failed = 0u;
}

static void orbit_ui_cmd_release_array(orbit_ui_cmd_t *cmds, size_t count) {
    if (!cmds) return;
    for (size_t i = 0u; i < count; i++) {
        orbit_ui_cmd_release(&cmds[i]);
    }
}

static int orbit_ui_cmd_reserve(orbit_ui_cmd_t **arr, size_t *cap, size_t need) {
    if (!arr || !cap) return 0;
    if (need <= *cap) return 1;
    if (need > ORBIT_UI_CMD_HARD_MAX) return 0;

    size_t new_cap = *cap;
    if (new_cap == 0u) new_cap = ORBIT_UI_CMD_SOFT_CHUNK;
    while (new_cap < need && new_cap < ORBIT_UI_CMD_HARD_MAX) {
        if (new_cap > ORBIT_UI_CMD_HARD_MAX - ORBIT_UI_CMD_SOFT_CHUNK) {
            new_cap = ORBIT_UI_CMD_HARD_MAX;
        } else {
            new_cap += ORBIT_UI_CMD_SOFT_CHUNK;
        }
    }
    if (new_cap < need) return 0;

    orbit_ui_cmd_t *grown = (orbit_ui_cmd_t *)realloc(*arr, new_cap * sizeof(orbit_ui_cmd_t));
    if (!grown) return 0;
    *arr = grown;
    *cap = new_cap;
    return 1;
}

static void orbit_ui_storage_free(orbit_ui_state_t *ui) {
    if (!ui) return;
    orbit_ui_cmd_release_array(ui->cmds, ui->cmd_count);
    orbit_ui_cmd_release_array(ui->staged_cmds, ui->staged_count);
    if (ui->cmds) free(ui->cmds);
    if (ui->staged_cmds) free(ui->staged_cmds);
    ui->cmds = NULL;
    ui->cmd_count = 0u;
    ui->cmd_cap = 0u;
    ui->staged_cmds = NULL;
    ui->staged_count = 0u;
    ui->staged_cap = 0u;
    ui->cmd_limit_reached = 0u;
}

static void orbit_ui_clear(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_ui_cmd_release_array(shell->ui.staged_cmds, shell->ui.staged_count);
    shell->ui.staged_enabled = 1u;
    shell->ui.staged_count = 0u;
    shell->ui.cmd_limit_reached = 0u;
}

static orbit_ui_cmd_t *orbit_ui_next_cmd(orbit_shell_t *shell, uint8_t type) {
    if (!shell) return NULL;
    shell->ui.staged_enabled = 1u;
    if (shell->ui.staged_count >= ORBIT_UI_CMD_HARD_MAX) {
        if (!shell->ui.cmd_limit_reached) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "command_limit_reached");
            shell->ui.cmd_limit_reached = 1u;
        }
        return NULL;
    }
    if (!orbit_ui_cmd_reserve(&shell->ui.staged_cmds, &shell->ui.staged_cap, shell->ui.staged_count + 1u)) {
        if (!shell->ui.cmd_limit_reached) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "command_buffer_alloc_failed");
            shell->ui.cmd_limit_reached = 1u;
        }
        return NULL;
    }
    orbit_ui_cmd_t *cmd = &shell->ui.staged_cmds[shell->ui.staged_count++];
    peos_memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
    cmd->enabled = 1u;
    return cmd;
}

static void orbit_send_ui_event_button(orbit_shell_t *shell, uint32_t id) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[48];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT BUTTON ";
    while (prefix[n] && n + 1u < sizeof(msg)) {
        msg[n] = prefix[n];
        n++;
    }
    char num[16];
    size_t m = 0u;
    uint32_t v = id;
    if (v == 0u) {
        num[m++] = '0';
    } else {
        char rev[16];
        size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_double_click(orbit_shell_t *shell, uint32_t id) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT DOUBLE_CLICK ";
    while (prefix[n] && n + 1u < sizeof(msg)) {
        msg[n] = prefix[n];
        n++;
    }
    char num[16];
    size_t m = 0u;
    uint32_t v = id;
    if (v == 0u) {
        num[m++] = '0';
    } else {
        char rev[16];
        size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_emit_click_events(orbit_shell_t *shell, uint32_t id, uint8_t is_pointer_click) {
    if (!shell) return;
    orbit_send_ui_event_button(shell, id);
    if (!is_pointer_click) return;

    uint64_t now_ns = peos_time_ns();
    uint64_t delta_ns = now_ns - shell->ui.last_click_ns;
    uint64_t threshold_ns = (uint64_t)shell->ui.double_click_ms * 1000000ull;
    if (shell->ui.last_click_id == id && shell->ui.last_click_ns != 0ull && delta_ns <= threshold_ns) {
        orbit_send_ui_event_double_click(shell, id);
        shell->ui.last_click_id = 0u;
        shell->ui.last_click_ns = 0ull;
    } else {
        shell->ui.last_click_id = id;
        shell->ui.last_click_ns = now_ns;
    }
}

static void orbit_send_ui_event_slider(orbit_shell_t *shell, uint32_t id, uint32_t value) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT SLIDER ";
    while (prefix[n] && n + 1u < sizeof(msg)) {
        msg[n] = prefix[n];
        n++;
    }

    char a[16];
    size_t an = 0u;
    uint32_t va = id;
    if (va == 0u) a[an++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (va > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (va % 10u)); va /= 10u; }
        while (r > 0u && an < sizeof(a)) a[an++] = rev[--r];
    }
    for (size_t i = 0u; i < an && n + 1u < sizeof(msg); i++) msg[n++] = a[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char b[16];
    size_t bn = 0u;
    uint32_t vb = value;
    if (vb == 0u) b[bn++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (vb > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (vb % 10u)); vb /= 10u; }
        while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
    }
    for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';

    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_toggle(orbit_shell_t *shell, uint32_t id, uint32_t value) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT TOGGLE ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char a[16];
    size_t an = 0u;
    uint32_t va = id;
    if (va == 0u) a[an++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (va > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (va % 10u)); va /= 10u; }
        while (r > 0u && an < sizeof(a)) a[an++] = rev[--r];
    }
    for (size_t i = 0u; i < an && n + 1u < sizeof(msg); i++) msg[n++] = a[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char b[16];
    size_t bn = 0u;
    uint32_t vb = value;
    if (vb == 0u) b[bn++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (vb > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (vb % 10u)); vb /= 10u; }
        while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
    }
    for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_select(orbit_shell_t *shell, uint32_t id, uint32_t value) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT SELECT ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char a[16];
    size_t an = 0u;
    uint32_t va = id;
    if (va == 0u) a[an++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (va > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (va % 10u)); va /= 10u; }
        while (r > 0u && an < sizeof(a)) a[an++] = rev[--r];
    }
    for (size_t i = 0u; i < an && n + 1u < sizeof(msg); i++) msg[n++] = a[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char b[16];
    size_t bn = 0u;
    uint32_t vb = value;
    if (vb == 0u) b[bn++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (vb > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (vb % 10u)); vb /= 10u; }
        while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
    }
    for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_context_menu(orbit_shell_t *shell, uint32_t id, uint32_t value) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT CONTEXT_MENU ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char a[16];
    size_t an = 0u;
    uint32_t va = id;
    if (va == 0u) a[an++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (va > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (va % 10u)); va /= 10u; }
        while (r > 0u && an < sizeof(a)) a[an++] = rev[--r];
    }
    for (size_t i = 0u; i < an && n + 1u < sizeof(msg); i++) msg[n++] = a[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char b[16];
    size_t bn = 0u;
    uint32_t vb = value;
    if (vb == 0u) b[bn++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (vb > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (vb % 10u)); vb /= 10u; }
        while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
    }
    for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_select_option_text(const char *options_pipe, uint32_t selected,
                                     char *out, size_t out_cap);

typedef struct {
    const orbit_ui_cmd_t *zone;
} orbit_context_label_source_t;

static void orbit_context_label_cb(uint32_t option_index, char *out, size_t out_cap, void *userdata) {
    const orbit_context_label_source_t *src = (const orbit_context_label_source_t *)userdata;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!src || !src->zone) return;
    orbit_select_option_text(src->zone->text, option_index, out, out_cap);
}

static void orbit_send_ui_event_scroll_v(orbit_shell_t *shell, uint32_t id, uint32_t value) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[64];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT SCROLL_V ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char a[16];
    size_t an = 0u;
    uint32_t va = id;
    if (va == 0u) a[an++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (va > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (va % 10u)); va /= 10u; }
        while (r > 0u && an < sizeof(a)) a[an++] = rev[--r];
    }
    for (size_t i = 0u; i < an && n + 1u < sizeof(msg); i++) msg[n++] = a[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char b[16];
    size_t bn = 0u;
    uint32_t vb = value;
    if (vb == 0u) b[bn++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (vb > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (vb % 10u)); vb /= 10u; }
        while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
    }
    for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_panel(orbit_shell_t *shell, uint32_t id, uint32_t vx, uint32_t vy) {
    if (!shell || shell->child_stdin_fd < 0) return;
    char msg[80];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT PANEL ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    uint32_t vals[3] = { id, vx, vy };
    for (uint32_t k = 0u; k < 3u; k++) {
        char b[16];
        size_t bn = 0u;
        uint32_t v = vals[k];
        if (v == 0u) b[bn++] = '0';
        else {
            char rev[16]; size_t r = 0u;
            while (v > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (v % 10u)); v /= 10u; }
            while (r > 0u && bn < sizeof(b)) b[bn++] = rev[--r];
        }
        for (size_t i = 0u; i < bn && n + 1u < sizeof(msg); i++) msg[n++] = b[i];
        if (k != 2u && n + 1u < sizeof(msg)) msg[n++] = ' ';
    }
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_write_fd_u32(int64_t fd, uint32_t v) {
    char num[16];
    size_t n = 0u;
    if (v == 0u) {
        num[n++] = '0';
    } else {
        char rev[16];
        size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (r > 0u && n < sizeof(num)) num[n++] = rev[--r];
    }
    (void)peos_fwrite((uint64_t)fd, num, n);
}

static uint32_t orbit_text_hash(const char *text) {
    uint32_t h = 2166136261u;   // FNV-1a
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) h = (h ^ *p) * 16777619u;
    return h;
}

static void orbit_ui_note_sent_text(orbit_shell_t *shell, const char *text) {
    shell->ui.pending_text_hash = orbit_text_hash(text) | 1u;   // never 0
    shell->ui.pending_text_ns = peos_time_ns();
}

// The app's answer to an edit can take a while: an edit pending longer than
// this lets the app's text win again (an app may rewrite what was typed).
#define ORBIT_UI_ECHO_WAIT_NS 500000000ull

// The user types into a field of Starlight's, which sends each new text to
// the app; until the app's frames carry that text back, they still carry an
// older one. Committing it would step the field back under the caret (a
// letter typed then shows up further on, or not at all). So while an edit
// of the focused field is pending, the local text stays; once the app gives
// it back, or after ORBIT_UI_ECHO_WAIT_NS, the app's text is taken again.
static void orbit_ui_keep_local_text(orbit_shell_t *shell) {
    uint32_t id = shell->ui.ui.focused_id;
    if (!shell->ui.pending_text_hash) return;
    if (!id || !shell->ui.staged_cmds ||
        peos_time_ns() - shell->ui.pending_text_ns > ORBIT_UI_ECHO_WAIT_NS) {
        shell->ui.pending_text_hash = 0u;
        return;
    }
    for (size_t i = 0u; i < shell->ui.staged_count; i++) {
        orbit_ui_cmd_t *in = &shell->ui.staged_cmds[i];
        if (in->id != id || (in->type != ORBIT_UI_CMD_TEXT_INPUT && in->type != ORBIT_UI_CMD_TEXT_AREA)) continue;
        const char *incoming = in->text ? in->text : "";
        if ((orbit_text_hash(incoming) | 1u) == shell->ui.pending_text_hash) {
            shell->ui.pending_text_hash = 0u;   // caught up
            return;
        }
        for (size_t j = 0u; j < shell->ui.cmd_count; j++) {
            const orbit_ui_cmd_t *cur = &shell->ui.cmds[j];
            if (cur->id != id || cur->type != in->type || !cur->text) continue;
            size_t len = strlen(cur->text);
            if (orbit_cmd_text_reserve(in, len + 1u)) memcpy(in->text, cur->text, len + 1u);
            return;
        }
        return;
    }
}

static void orbit_send_ui_event_text_input(orbit_shell_t *shell, uint32_t id, const char *text) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!text) text = "";
    orbit_ui_note_sent_text(shell, text);
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "@SLUI EVENT TEXT_INPUT ", peos_strlen("@SLUI EVENT TEXT_INPUT "));
    orbit_write_fd_u32(shell->child_stdin_fd, id);
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, " ", 1u);
    for (size_t i = 0u; text[i]; i++) {
        char c = text[i];
        if (c == '\n' || c == '\r') c = ' ';
        (void)peos_fwrite((uint64_t)shell->child_stdin_fd, &c, 1u);
    }
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\n", 1u);
}

static void orbit_send_ui_event_text_area(orbit_shell_t *shell, uint32_t id, const char *text) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!text) text = "";
    orbit_ui_note_sent_text(shell, text);
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "@SLUI EVENT TEXT_AREA ", peos_strlen("@SLUI EVENT TEXT_AREA "));
    orbit_write_fd_u32(shell->child_stdin_fd, id);
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, " ", 1u);
    for (size_t i = 0u; text[i]; i++) {
        char c = text[i];
        if (c == '\\') (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\\\\", 2u);
        else if (c == '\n') (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\\n", 2u);
        else if (c == '\r') (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\\r", 2u);
        else if (c == '\t') (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\\t", 2u);
        else (void)peos_fwrite((uint64_t)shell->child_stdin_fd, &c, 1u);
    }
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "\n", 1u);
}

static uint32_t orbit_mod_mask(const orbit_shell_t *shell) {
    uint32_t mods = 0u;
    if (!shell) return 0u;
    if (shell->shift_down) mods |= 1u;       // SLUI_MOD_SHIFT
    if (shell->ctrl_down) mods |= (1u << 1); // SLUI_MOD_CTRL
    if (shell->alt_down) mods |= (1u << 2);  // SLUI_MOD_ALT
    if (shell->super_down) mods |= (1u << 3);// SLUI_MOD_SUPER
    return mods;
}

static void orbit_send_ui_event_key(orbit_shell_t *shell, const char *kind, uint16_t code, uint32_t mods) {
    if (!shell || shell->child_stdin_fd < 0 || !kind || !kind[0]) return;
    char msg[96];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;
    for (size_t i = 0u; kind[i] && n + 1u < sizeof(msg); i++) msg[n++] = kind[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    char num[16];
    size_t m = 0u;
    uint32_t v = (uint32_t)code;
    if (v == 0u) num[m++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    m = 0u;
    v = mods;
    if (v == 0u) num[m++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_copy_unescaped(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    size_t di = 0u;
    size_t si = 0u;
    while (src[si] && di + 1u < cap) {
        char c = src[si++];
        if (c == '\\' && src[si]) {
            char n = src[si++];
            if (n == 'n') c = '\n';
            else if (n == 'r') c = '\r';
            else if (n == 't') c = '\t';
            else if (n == '\\') c = '\\';
            else c = n;
        }
        dst[di++] = c;
    }
    dst[di] = '\0';
}

static int orbit_ui_handle_line(orbit_shell_t *shell, const char *line) {
    if (!shell || !line) return 0;
    const char *p = line;
    const char *token = "@SLUI";
    size_t pos = 0u;
    uint8_t found = 0u;
    while (p[pos]) {
        size_t i = 0u;
        while (token[i] && p[pos + i] == token[i]) i++;
        if (token[i] == '\0') {
            p = p + pos + i;
            found = 1u;
            break;
        }
        pos++;
    }
    if (!found) return 0;

    if (*p == ':') p++;
    p = orbit_skip_spaces(p);

    const char *cmd_tok = NULL;
    size_t cmd_len = 0u;
    if (!orbit_next_token(&p, &cmd_tok, &cmd_len)) {
        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "empty_command");
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "HELLO")) {
        uint32_t version = 0u;
        if (!orbit_parse_u32(&p, &version)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_HELLO, "hello_missing_version");
            return 1;
        }
        shell->ui.hello_seen = 1u;
        shell->ui.client_proto_ver = (uint8_t)(version & 0xFFu);
        shell->ui.client_caps_mask = 0u;

        const char *next_tok = NULL;
        size_t next_len = 0u;
        if (orbit_next_token(&p, &next_tok, &next_len) && orbit_token_is(next_tok, next_len, "CAPS")) {
            const char *cap_tok = NULL;
            size_t cap_len = 0u;
            while (orbit_next_token(&p, &cap_tok, &cap_len)) {
                shell->ui.client_caps_mask |= orbit_parse_cap_token(cap_tok, cap_len);
            }
        }

        if (version > ORBIT_SLUI_PROTO_VERSION) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_HELLO, "unsupported_protocol_version");
        }
        orbit_send_host_hello(shell);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "CLEAR")) {
        orbit_ui_clear(shell);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "COMMIT")) {
        orbit_ui_keep_local_text(shell);
        orbit_ui_cmd_release_array(shell->ui.cmds, shell->ui.cmd_count);
        shell->ui.enabled = shell->ui.staged_enabled;
    if (!orbit_ui_cmd_reserve(&shell->ui.cmds, &shell->ui.cmd_cap, shell->ui.staged_count)) {
        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "command_buffer_alloc_failed");
        orbit_ui_cmd_release_array(shell->ui.staged_cmds, shell->ui.staged_count);
        shell->ui.cmd_count = 0u;
        shell->ui.staged_count = 0u;
        shell->ui.cmd_limit_reached = 0u;
            shell->ui.committed = 1u;
            return 1;
        }
        shell->ui.cmd_count = shell->ui.staged_count;
        if (shell->ui.cmd_count > 0u && shell->ui.cmds && shell->ui.staged_cmds) {
            peos_memcpy(shell->ui.cmds, shell->ui.staged_cmds,
                        shell->ui.cmd_count * sizeof(shell->ui.cmds[0]));
        }
        shell->ui.staged_count = 0u;
        shell->ui.cmd_limit_reached = 0u;
        shell->ui.committed = 1u;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "FOCUS")) {
        uint32_t id = 0u;
        if (!orbit_parse_u32(&p, &id)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "focus_missing_id");
            return 1;
        }
        shell->ui.nav_focus_id = id;
        shell->ui.ui.focused_id = id;
        shell->ui.keyboard_nav_active = 1u;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "HOST")) {
        const char *host_tok = NULL;
        size_t host_len = 0u;
        if (!orbit_next_token(&p, &host_tok, &host_len)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "host_missing_command");
            return 1;
        }
        if (orbit_token_is(host_tok, host_len, "RELOAD_DISPLAY")) {
            g_reload_display_requested = 1u;
            return 1;
        }
        if (orbit_token_is(host_tok, host_len, "RELOAD_THEME")) {
            g_reload_theme_requested = 1u;
            return 1;
        }
        if (orbit_token_is(host_tok, host_len, "OPEN_APP")) {
            char tmp[sizeof(g_open_app_bundle_path)];
            p = orbit_skip_spaces(p);
            if (!p || p[0] == '\0') {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "open_app_missing_path");
                return 1;
            }
            orbit_copy_unescaped(tmp, sizeof(tmp), p);
            if (tmp[0] == '\0') {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "open_app_empty_path");
                return 1;
            }
            orbit_copy_str(g_open_app_bundle_path, sizeof(g_open_app_bundle_path), tmp);
            g_open_app_requested = 1u;
            return 1;
        }
        if (orbit_token_is(host_tok, host_len, "THEME_ACCENTA")) {
            uint32_t rv = 0u, gv = 0u, bv = 0u, av = 0u;
            if (!orbit_parse_u32(&p, &rv) || !orbit_parse_u32(&p, &gv) || !orbit_parse_u32(&p, &bv) || !orbit_parse_u32(&p, &av)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_MALFORMED, "theme_accenta_invalid_args");
                return 1;
            }
            orbit_theme_override_set_accent(shell, orbit_u8_clamp(rv), orbit_u8_clamp(gv), orbit_u8_clamp(bv), orbit_u8_clamp(av));
            return 1;
        }
        if (orbit_token_is(host_tok, host_len, "THEME_RESET")) {
            shell->ui.theme_override_enabled = 0u;
            shell->ui.ui.theme_initialized = 0u;
            return 1;
        }
        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_UNKNOWN_CMD, "unknown_host_command");
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "LABEL")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_LABEL);
        if (!cmd) return 1;
        uint32_t rv = 0u, gv = 0u, bv = 0u;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &rv) || !orbit_parse_u32(&p, &gv) || !orbit_parse_u32(&p, &bv)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_LABEL, "label_invalid_args");
            return 1;
        }
        cmd->r = (uint8_t)(rv & 0xFFu);
        cmd->g = (uint8_t)(gv & 0xFFu);
        cmd->b = (uint8_t)(bv & 0xFFu);
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "label_text_alloc_failed");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "FONT_SIZE")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_FONT_SIZE);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_FONT, "font_size_invalid_args");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "LABELT")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_LABEL_THEME);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_LABEL, "labelt_invalid_args");
            return 1;
        }
        if (cmd->value >= SL_UI_COLOR_COUNT) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_LABEL, "labelt_invalid_token");
            return 1;
        }
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "labelt_text_alloc_failed");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "BUTTON") || orbit_token_is(cmd_tok, cmd_len, "BUTTONX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_BUTTON);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "BUTTONX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_BUTTON, "button_invalid_args");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_BUTTON, "button_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "button_text_alloc_failed");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "SLIDER") || orbit_token_is(cmd_tok, cmd_len, "SLIDERX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_SLIDER);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "SLIDERX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->min_v) || !orbit_parse_u32(&p, &cmd->max_v) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SLIDER, "slider_invalid_args");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SLIDER, "slider_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "TOGGLE") || orbit_token_is(cmd_tok, cmd_len, "TOGGLEX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_TOGGLE);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "TOGGLEX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TOGGLE, "toggle_invalid_args");
            return 1;
        }
        cmd->value = cmd->value ? 1u : 0u;
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TOGGLE, "toggle_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "toggle_text_alloc_failed");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "SELECT") || orbit_token_is(cmd_tok, cmd_len, "SELECTX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_SELECT);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "SELECTX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->min_v) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SELECT, "select_invalid_args");
            return 1;
        }
        if (cmd->min_v == 0u) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SELECT, "select_empty_options");
            return 1;
        }
        if (cmd->value >= cmd->min_v) cmd->value = 0u;
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SELECT, "select_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "select_text_alloc_failed");
            return 1;
        }
        if (!cmd->text || cmd->text[0] == '\0') {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SELECT, "select_missing_options");
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "CONTEXT_MENU") || orbit_token_is(cmd_tok, cmd_len, "CONTEXT_MENUX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_CONTEXT_MENU);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "CONTEXT_MENUX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->min_v)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CONTEXT, "context_menu_invalid_args");
            return 1;
        }
        if (cmd->min_v == 0u) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CONTEXT, "context_menu_empty_options");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CONTEXT, "context_menu_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "context_menu_text_alloc_failed");
            return 1;
        }
        if (!cmd->text || cmd->text[0] == '\0') {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CONTEXT, "context_menu_missing_options");
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "TEXT_INPUT") || orbit_token_is(cmd_tok, cmd_len, "TEXT_INPUTX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_TEXT_INPUT);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "TEXT_INPUTX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->max_v)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TEXT_INPUT, "text_input_invalid_args");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TEXT_INPUT, "text_input_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        if (cmd->max_v < 1u) cmd->max_v = 1u;
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "text_input_alloc_failed");
            return 1;
        }
        if (cmd->text && cmd->max_v < 0xFFFFFFFFu) {
            size_t max_idx = (size_t)cmd->max_v;
            if (max_idx + 1u < cmd->text_cap) {
                cmd->text[max_idx] = '\0';
            }
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "TEXT_AREA") || orbit_token_is(cmd_tok, cmd_len, "TEXT_AREAX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_TEXT_AREA);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "TEXT_AREAX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->max_v)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TEXT_AREA, "text_area_invalid_args");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_TEXT_AREA, "text_area_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        if (cmd->max_v < 1u) cmd->max_v = 1u;
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_unescaped(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "text_area_alloc_failed");
            return 1;
        }
        if (cmd->text && cmd->max_v < 0xFFFFFFFFu) {
            size_t max_idx = (size_t)cmd->max_v;
            if (max_idx + 1u < cmd->text_cap) {
                cmd->text[max_idx] = '\0';
            }
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "SCROLL_V") || orbit_token_is(cmd_tok, cmd_len, "SCROLL_VX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_SCROLL_V);
        if (!cmd) return 1;
        uint8_t with_state = orbit_token_is(cmd_tok, cmd_len, "SCROLL_VX") ? 1u : 0u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &cmd->max_v) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SCROLL_V, "scroll_v_invalid_args");
            return 1;
        }
        if (with_state) {
            uint32_t enabled = 1u;
            if (!orbit_parse_u32(&p, &enabled)) {
                orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SCROLL_V, "scroll_v_invalid_enabled");
                return 1;
            }
            cmd->enabled = enabled ? 1u : 0u;
        }
        if (cmd->max_v < cmd->h) cmd->value = 0u;
        else if (cmd->value > (cmd->max_v - cmd->h)) cmd->value = cmd->max_v - cmd->h;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "PANELX")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_PANEL);
        if (!cmd) return 1;
        uint32_t enabled = 1u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &enabled)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_PANEL, "panel_invalid_args");
            return 1;
        }
        cmd->enabled = enabled ? 1u : 0u;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "SEPARATOR")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_SEPARATOR);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SEPARATOR, "separator_invalid_args");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "LIST_ITEM")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_LIST_ITEM);
        if (!cmd) return 1;
        uint32_t enabled = 1u;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h) || !orbit_parse_u32(&p, &enabled)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_LIST_ITEM, "list_item_invalid_args");
            return 1;
        }
        cmd->enabled = enabled ? 1u : 0u;
        p = orbit_skip_spaces(p);
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "list_item_text_alloc_failed");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "RECT")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_RECT);
        if (!cmd) return 1;
        uint32_t rv = 0u, gv = 0u, bv = 0u;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h) ||
            !orbit_parse_u32(&p, &rv) || !orbit_parse_u32(&p, &gv) || !orbit_parse_u32(&p, &bv)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECT, "rect_invalid_args");
            return 1;
        }
        cmd->r = (uint8_t)(rv & 0xFFu);
        cmd->g = (uint8_t)(gv & 0xFFu);
        cmd->b = (uint8_t)(bv & 0xFFu);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "RECTT")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_RECT_THEME);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECT, "rectt_invalid_args");
            return 1;
        }
        if (cmd->value >= SL_UI_COLOR_COUNT) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECT, "rectt_invalid_token");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "RECTA")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_RECTA);
        if (!cmd) return 1;
        uint32_t rv = 0u, gv = 0u, bv = 0u, av = 0u;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h) ||
            !orbit_parse_u32(&p, &rv) || !orbit_parse_u32(&p, &gv) ||
            !orbit_parse_u32(&p, &bv) || !orbit_parse_u32(&p, &av)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECTA, "recta_invalid_args");
            return 1;
        }
        cmd->r = (uint8_t)(rv & 0xFFu);
        cmd->g = (uint8_t)(gv & 0xFFu);
        cmd->b = (uint8_t)(bv & 0xFFu);
        cmd->a = (uint8_t)(av & 0xFFu);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "RECTAT")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_RECTA_THEME);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h) ||
            !orbit_parse_u32(&p, &cmd->value) || !orbit_parse_u32(&p, &cmd->min_v)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECTA, "rectat_invalid_args");
            return 1;
        }
        if (cmd->value >= SL_UI_COLOR_COUNT) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_RECTA, "rectat_invalid_token");
            return 1;
        }
        cmd->a = (uint8_t)(cmd->min_v & 0xFFu);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "GRAD2D")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_GRAD2D);
        if (!cmd) return 1;
        uint32_t c[16];
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h) ||
            !orbit_parse_u32(&p, &c[0]) || !orbit_parse_u32(&p, &c[1]) ||
            !orbit_parse_u32(&p, &c[2]) || !orbit_parse_u32(&p, &c[3]) ||
            !orbit_parse_u32(&p, &c[4]) || !orbit_parse_u32(&p, &c[5]) ||
            !orbit_parse_u32(&p, &c[6]) || !orbit_parse_u32(&p, &c[7]) ||
            !orbit_parse_u32(&p, &c[8]) || !orbit_parse_u32(&p, &c[9]) ||
            !orbit_parse_u32(&p, &c[10]) || !orbit_parse_u32(&p, &c[11]) ||
            !orbit_parse_u32(&p, &c[12]) || !orbit_parse_u32(&p, &c[13]) ||
            !orbit_parse_u32(&p, &c[14]) || !orbit_parse_u32(&p, &c[15])) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_GRAD2D, "grad2d_invalid_args");
            return 1;
        }
        for (uint32_t i = 0u; i < 4u; i++) {
            cmd->grad[i][0] = (uint8_t)(c[i * 4u + 0u] & 0xFFu);
            cmd->grad[i][1] = (uint8_t)(c[i * 4u + 1u] & 0xFFu);
            cmd->grad[i][2] = (uint8_t)(c[i * 4u + 2u] & 0xFFu);
            cmd->grad[i][3] = (uint8_t)(c[i * 4u + 3u] & 0xFFu);
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "CLIP_PUSH")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_CLIP_PUSH);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CLIP, "clip_push_invalid_args");
            return 1;
        }
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "CLIP_POP")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_CLIP_POP);
        if (!cmd) return 1;
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "IMAGE_FILE")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_IMAGE_FILE);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->x) || !orbit_parse_u32(&p, &cmd->y) ||
            !orbit_parse_u32(&p, &cmd->w) || !orbit_parse_u32(&p, &cmd->h)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_IMAGE_FILE, "image_file_invalid_args");
            return 1;
        }
        p = orbit_skip_spaces(p);
        if (!p || p[0] == '\0') {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_IMAGE_FILE, "image_file_missing_path");
            return 1;
        }
        if (!orbit_cmd_set_text_raw(cmd, p)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "image_file_path_alloc_failed");
            return 1;
        }
        return 1;
    }

    orbit_send_ui_error(shell, ORBIT_SLUI_ERR_UNKNOWN_CMD, "unknown_command");
    return 1;
}

static void orbit_push_line(orbit_shell_t *shell, const char *line) {
    if (!shell || !line) return;
    size_t slot = (shell->line_head + shell->line_count) % ORBIT_MAX_LINES;
    orbit_copy_str(shell->lines[slot], ORBIT_LINE_CHARS, line);

    if (shell->line_count < ORBIT_MAX_LINES) {
        shell->line_count++;
    } else {
        shell->line_head = (shell->line_head + 1u) % ORBIT_MAX_LINES;
    }
}

static void orbit_clear_text_buffer(orbit_shell_t *shell) {
    if (!shell) return;
    shell->line_head = 0u;
    shell->line_count = 0u;
    orbit_pending_line_reset(shell);
}

static uint32_t orbit_parse_csi_first_param(const char *buf, size_t len) {
    if (!buf || len == 0u) return 0u;
    uint32_t v = 0u;
    size_t i = 0u;
    uint8_t seen_digit = 0u;
    while (i < len && buf[i] != ';') {
        char c = buf[i++];
        if (c < '0' || c > '9') return 0u;
        seen_digit = 1u;
        v = (v * 10u) + (uint32_t)(c - '0');
        if (v > 999u) break;
    }
    return seen_digit ? v : 0u;
}

static void orbit_ansi_finish(orbit_shell_t *shell, char final) {
    if (!shell) return;
    if (final == 'J') {
        uint32_t mode = orbit_parse_csi_first_param(shell->csi_buf, shell->csi_len);
        if (mode == 0u || mode == 2u || mode == 3u) {
            orbit_clear_text_buffer(shell);
        }
    }
}

static int orbit_feed_ansi(orbit_shell_t *shell, char c) {
    if (!shell) return 0;

    if (!shell->esc_active) {
        if ((uint8_t)c == 0x1Bu) {
            shell->esc_active = 1u;
            shell->csi_active = 0u;
            shell->csi_len = 0u;
            return 1;
        }
        return 0;
    }

    if (!shell->csi_active) {
        if (c == '[') {
            shell->csi_active = 1u;
            shell->csi_len = 0u;
            return 1;
        }
        if (c == 'c') {
            orbit_clear_text_buffer(shell);
            shell->esc_active = 0u;
            return 1;
        }
        shell->esc_active = 0u;
        return 1;
    }

    if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
        if (shell->csi_len + 1u < sizeof(shell->csi_buf)) {
            shell->csi_buf[shell->csi_len++] = c;
            shell->csi_buf[shell->csi_len] = '\0';
        }
        return 1;
    }

    if ((uint8_t)c >= 0x40u && (uint8_t)c <= 0x7Eu) {
        orbit_ansi_finish(shell, c);
        shell->esc_active = 0u;
        shell->csi_active = 0u;
        shell->csi_len = 0u;
        return 1;
    }

    shell->esc_active = 0u;
    shell->csi_active = 0u;
    shell->csi_len = 0u;
    return 1;
}

static void orbit_reset_buffer(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_ui_storage_free(&shell->ui);
    orbit_surface_reset(shell);
    (void)orbit_pending_line_reserve(shell, ORBIT_PENDING_LINE_INITIAL_CHARS);
    orbit_clear_text_buffer(shell);
    shell->esc_active = 0u;
    shell->csi_active = 0u;
    shell->csi_len = 0u;
    shell->csi_buf[0] = '\0';
    peos_memset(shell->ui.key_down, 0, sizeof(shell->ui.key_down));
    shell->ui.staged_enabled = 1u;
    shell->ui.active_panel_id = 0u;
    shell->ui.nav_focus_id = 0u;
    shell->ui.context_open_id = 0u;
    shell->ui.context_popup_w = 0u;
    shell->ui.context_popup_h = 0u;
    shell->ui.mouse_r_down = 0u;
    shell->ui.mouse_r_pressed = 0u;
    shell->ui.mouse_r_released = 0u;
    shell->ui.double_click_ms = g_orbit_double_click_ms;
    shell->ui.last_click_id = 0u;
    shell->ui.last_click_ns = 0ull;
    shell->ui.theme_override_enabled = 0u;
    shell->ui.ui.theme_initialized = 0u;
}

static void orbit_feed_char(orbit_shell_t *shell, char c) {
    if (!shell) return;
    if (orbit_feed_ansi(shell, c)) return;
    if (c == '\r') return;

    if (c == '\f') {
        orbit_clear_text_buffer(shell);
        return;
    }

    if (c == '\n') {
        const char *line = (shell->pending_line) ? shell->pending_line : "";
        if (shell->pending_line) shell->pending_line[shell->pending_len] = '\0';
        if (!orbit_fb_handle_line(shell, line)) {
            shell->surface.active = 0u;
            if (!orbit_ui_handle_line(shell, line)) {
                orbit_push_line(shell, line);
            }
        }
        orbit_pending_line_reset(shell);
        return;
    }

    if (c == '\b') {
        if (shell->pending_len > 0u) {
            shell->pending_len--;
            if (shell->pending_line) shell->pending_line[shell->pending_len] = '\0';
        }
        return;
    }

    if ((uint8_t)c < 32u) return;
    if (shell->surface.active && shell->pending_len == 0u && c != '@') {
        shell->surface.active = 0u;
    }
    if (!orbit_pending_line_reserve(shell, shell->pending_len + 2u)) {
        if (shell->pending_line && shell->pending_len > 0u) {
            shell->pending_line[shell->pending_len] = '\0';
            orbit_push_line(shell, shell->pending_line);
        }
        orbit_pending_line_reset(shell);
        return;
    }
    shell->pending_line[shell->pending_len++] = c;
    shell->pending_line[shell->pending_len] = '\0';
}

static void orbit_feed_text(orbit_shell_t *shell, const char *text, size_t len) {
    if (!shell || !text || len == 0u) return;
    size_t i = 0u;
    while (i < len) {
        if (shell->surface.receiving || shell->surface.discard) {
            size_t remaining = shell->surface.pending_len - shell->surface.pending_off;
            size_t avail = len - i;
            size_t take = (avail < remaining) ? avail : remaining;

            if (take > 0u && shell->surface.receiving && shell->surface.pixels) {
                peos_memcpy(shell->surface.pixels + shell->surface.pending_off, text + i, take);
            }
            shell->surface.pending_off += take;
            i += take;

            if (shell->surface.pending_off >= shell->surface.pending_len) {
                if (shell->surface.receiving) {
                    shell->surface.width = shell->surface.pending_width;
                    shell->surface.height = shell->surface.pending_height;
                    shell->surface.pitch = shell->surface.pending_pitch;
                    shell->surface.format = shell->surface.pending_format;
                    shell->surface.pixels_len = shell->surface.pending_len;
                    shell->surface.active = 1u;
                }
                shell->surface.receiving = 0u;
                shell->surface.discard = 0u;
                shell->surface.pending_len = 0u;
                shell->surface.pending_off = 0u;
            }
            continue;
        }
        orbit_feed_char(shell, text[i]);
        i++;
    }
}

static void orbit_close_fds(orbit_shell_t *shell) {
    if (!shell) return;
    if (shell->child_stdin_fd >= 0) {
        (void)peos_fclose((uint64_t)shell->child_stdin_fd);
        shell->child_stdin_fd = -1;
    }
    if (shell->child_stdout_fd >= 0) {
        (void)peos_fclose((uint64_t)shell->child_stdout_fd);
        shell->child_stdout_fd = -1;
    }
}

static void orbit_request_child_exit(orbit_shell_t *shell) {
    static const char quit_line[] = "@SLUI HOST QUIT\n";
    if (!shell || shell->child_stdin_fd < 0) return;
    // Legacy text apps may handle this.
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "exit\n", 5u);
    // SLUI apps handle this explicit host shutdown message.
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, quit_line, sizeof(quit_line) - 1u);
}

static int orbit_spawn_process(orbit_shell_t *shell) {
    if (!shell) return 0;
    if (!shell->enabled || shell->exec_path[0] == '\0') return 0;

    uint64_t stdin_pipe[2] = {0, 0};
    uint64_t stdout_pipe[2] = {0, 0};
    if (peos_pipe(stdin_pipe) != 0) return 0;
    if (peos_pipe(stdout_pipe) != 0) {
        (void)peos_fclose(stdin_pipe[0]);
        (void)peos_fclose(stdin_pipe[1]);
        return 0;
    }

    int64_t child_stdin_rd = (int64_t)stdin_pipe[0];
    int64_t parent_stdin_wr = (int64_t)stdin_pipe[1];
    int64_t parent_stdout_rd = (int64_t)stdout_pipe[0];
    int64_t child_stdout_wr = (int64_t)stdout_pipe[1];

    const char *path = shell->exec_path;
    const char *arg = (shell->exec_args[0] != '\0') ? shell->exec_args : NULL;
    // Least privilege: the app only gets the rights its manifest declares.
    if (shell->slot_index < g_orbit_slot_count && g_orbit_cfg[shell->slot_index].caps_restrict) {
        if (peos_cap_set_spawn_mask((peos_cap_mask_t)g_orbit_cfg[shell->slot_index].caps_mask) != 0) {
            // Never fall back to inheriting every right when restriction fails.
            (void)peos_fclose((uint64_t)child_stdin_rd);
            (void)peos_fclose((uint64_t)child_stdout_wr);
            (void)peos_fclose((uint64_t)parent_stdin_wr);
            (void)peos_fclose((uint64_t)parent_stdout_rd);
            return 0;
        }
    }
    if (shell->slot_index < g_orbit_slot_count && g_orbit_cfg[shell->slot_index].paths_restrict) {
        const orbit_slot_cfg_t *cfg = &g_orbit_cfg[shell->slot_index];
        if (peos_path_set_spawn_scope(cfg->paths, cfg->path_count) != 0) {
            // Never fall back to the full filesystem when the scope cannot be applied.
            (void)peos_fclose((uint64_t)child_stdin_rd);
            (void)peos_fclose((uint64_t)child_stdout_wr);
            (void)peos_fclose((uint64_t)parent_stdin_wr);
            (void)peos_fclose((uint64_t)parent_stdout_rd);
            return 0;
        }
    }
    int64_t pid = peos_spawnv(path, peos_strlen(path), arg,
                              child_stdin_rd, child_stdout_wr, child_stdout_wr);

    (void)peos_fclose((uint64_t)child_stdin_rd);
    (void)peos_fclose((uint64_t)child_stdout_wr);

    if (pid <= 0) {
        (void)peos_fclose((uint64_t)parent_stdin_wr);
        (void)peos_fclose((uint64_t)parent_stdout_rd);
        return 0;
    }

    shell->child_pid = pid;
    shell->child_stdin_fd = parent_stdin_wr;
    shell->child_stdout_fd = parent_stdout_rd;
    shell->ui.host_hello_sent = 0u;
    shell->ui.hello_seen = 0u;
    shell->ui.client_proto_ver = 0u;
    shell->ui.client_caps_mask = 0u;
    shell->ui.double_click_ms = g_orbit_double_click_ms;
    return 1;
}

static void orbit_stop_process(orbit_shell_t *shell) {
    if (!shell) return;
    int64_t pid = shell->child_pid;
    uint8_t exited = 0u;
    orbit_request_child_exit(shell);
    orbit_close_fds(shell);
    if (pid > 0) {
        int status = 0;
        for (uint32_t i = 0u; i < 20u; i++) {
            uint64_t w = peos_waitpid_flags((uint64_t)pid, &status, PEOS_WAIT_NOHANG);
            if (w == (uint64_t)pid) {
                exited = 1u;
                break;
            }
            peos_sleep(1u);
        }
    }
    shell->child_pid = -1;
    if (exited) orbit_surface_destroy_shared_object(shell);
    else orbit_surface_free(shell);
}

static void orbit_on_close(void *userdata) {
    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

    int64_t pid = shell->child_pid;
    uint8_t exited = 0u;
    orbit_request_child_exit(shell);
    orbit_close_fds(shell);

    if (pid > 0) {
        int status = 0;
        for (uint32_t i = 0u; i < 20u; i++) {
            uint64_t w = peos_waitpid_flags((uint64_t)pid, &status, PEOS_WAIT_NOHANG);
            if (w == (uint64_t)pid) {
                exited = 1u;
                break;
            }
            peos_sleep(1u);
        }
    }

    shell->child_pid = -1;
    if (exited) orbit_surface_destroy_shared_object(shell);
    else orbit_surface_free(shell);
}

static void orbit_mark_child_exited(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_close_fds(shell);
    shell->child_pid = -1;
    orbit_ui_storage_free(&shell->ui);
    orbit_surface_destroy_shared_object(shell);
    peos_memset(&shell->ui, 0, sizeof(shell->ui));
    orbit_push_line(shell, "[app exited] Press Enter to restart.");
}

static uint8_t orbit_poll_output(orbit_shell_t *shell) {
    if (!shell || shell->child_stdout_fd < 0) return 0u;

    uint8_t changed = 0u;

    char buf[4096];
    for (uint32_t i = 0u; i < ORBIT_MAX_POLL_READS; i++) {
        uint64_t n = peos_fread_nb((uint64_t)shell->child_stdout_fd, buf, sizeof(buf));
        if ((int64_t)n < 0) {
            break;
        }
        if (n == 0u) {
            break;
        }
        changed = 1u;
        orbit_feed_text(shell, buf, (size_t)n);
        if (n < sizeof(buf)) {
            break;
        }
    }

    if (shell->child_pid > 0) {
        int status = 0;
        uint64_t w = peos_waitpid_flags((uint64_t)shell->child_pid, &status, PEOS_WAIT_NOHANG);
        if (w == (uint64_t)shell->child_pid) {
            orbit_mark_child_exited(shell);
            changed = 1u;
        }
    }

    return changed;
}

static uint64_t orbit_poll_interval_ns(const orbit_shell_t *shell, uint8_t changed) {
    if (!shell) return ORBIT_POLL_INTERVAL_IDLE_NS;
    if (changed) return ORBIT_POLL_INTERVAL_ACTIVE_NS;
    if (shell->child_pid > 0) return ORBIT_POLL_INTERVAL_BUSY_NS;
    return ORBIT_POLL_INTERVAL_IDLE_NS;
}

static void orbit_send_input(orbit_shell_t *shell, const char *buf, size_t len) {
    if (!shell || !buf || len == 0u) return;
    if (shell->child_pid <= 0) {
        if (!orbit_spawn_process(shell)) {
            orbit_push_line(shell, "failed to start app process");
            return;
        }
    }
    if (shell->child_stdin_fd >= 0) {
        (void)peos_fwrite((uint64_t)shell->child_stdin_fd, buf, len);
    }
}

static uint16_t orbit_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) {
        return (uint16_t)(code & 0x00FFu);
    }
    return code;
}

static uint8_t orbit_clip_intersect(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                                    uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                                    uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh);

static void orbit_on_input(sl_ctx_t *ctx, const sl_input_frame_t *frame,
                           const peos_input_event_t *event, void *userdata) {
    (void)ctx;

    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

    if (frame) {
        shell->ui.mouse_down = (frame->mouse_buttons & 0x01u) ? 1u : 0u;
        shell->ui.mouse_pressed = frame->mouse_left_pressed;
        shell->ui.mouse_released = frame->mouse_left_released;
        shell->ui.mouse_r_down = (frame->mouse_buttons & 0x02u) ? 1u : 0u;
        shell->ui.mouse_r_pressed = frame->mouse_right_pressed;
        shell->ui.mouse_r_released = frame->mouse_right_released;
        shell->ui.mouse_wheel_x = frame->mouse_wheel_x;
        shell->ui.mouse_wheel_y = frame->mouse_wheel_y;
        shell->ui.clipboard_cmds = frame->clipboard_cmds;
        shell->caps_lock_on = frame->caps_lock_on;
    }

    if (!event) return;

    uint16_t code = orbit_norm_keycode(event->code);
    uint8_t key_ix = (uint8_t)(code & 0xFFu);

    if (sl_keyboard_event_is_altgr(event)) {
        shell->altgr_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
        shell->shift_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LCTRL || code == PEOS_KEY_RCTRL_NORM) {
        shell->ctrl_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LALT) {
        shell->alt_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
        shell->super_down = event->pressed ? 1u : 0u;
    }

    if (shell->ui.enabled && shell->ui.committed) {
        uint8_t was_down = shell->ui.key_down[key_ix];
        uint32_t mods = orbit_mod_mask(shell);
        if (event->pressed) {
            if (was_down) orbit_send_ui_event_key(shell, "KEY_REPEAT", code, mods);
            else orbit_send_ui_event_key(shell, "KEY_DOWN", code, mods);
            shell->ui.key_down[key_ix] = 1u;
        } else {
            orbit_send_ui_event_key(shell, "KEY_UP", code, mods);
            shell->ui.key_down[key_ix] = 0u;
        }
        if (shell->ui.key_event_count < 16u) {
            shell->ui.key_events[shell->ui.key_event_count++] = *event;
        }
        return;
    }

    if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT ||
        code == PEOS_KEY_LCTRL || code == PEOS_KEY_RCTRL_NORM ||
        code == PEOS_KEY_LALT || sl_keyboard_event_is_altgr(event) ||
        code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
        if (shell->surface.active || shell->surface.receiving) {
            orbit_send_fb_key(shell, code, event->pressed ? 1u : 0u);
        }
        return;
    }

    if (shell->surface.active || shell->surface.receiving) {
        orbit_send_fb_key(shell, code, event->pressed ? 1u : 0u);
        return;
    }

    if (!event->pressed) return;

    if (code == PEOS_KEY_ENTER) {
        if (shell->child_pid <= 0) {
            if (!orbit_spawn_process(shell)) {
                orbit_push_line(shell, "failed to start /bin/orbit");
            }
        } else {
            orbit_send_input(shell, "\n", 1u);
        }
        return;
    }

    if (code == ORBIT_KEY_BACKSPACE) {
        orbit_send_input(shell, "\b", 1u);
        return;
    }

    char input[8];
    size_t input_len = sl_keyboard_translate_event(event, shell->shift_down,
                                                   shell->caps_lock_on,
                                                   shell->altgr_down,
                                                   input, sizeof(input));
    if (input_len == 0u) return;

    orbit_send_input(shell, input, input_len);
}

static void orbit_select_option_text(const char *options_pipe, uint32_t selected,
                                     char *out, size_t out_cap) {
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!options_pipe || options_pipe[0] == '\0') return;

    uint32_t idx = 0u;
    const char *start = options_pipe;
    const char *p = options_pipe;
    while (1) {
        if (*p == '|' || *p == '\0') {
            if (idx == selected) {
                size_t n = 0u;
                while (start[n] && start + n < p && n + 1u < out_cap) {
                    out[n] = start[n];
                    n++;
                }
                out[n] = '\0';
                return;
            }
            if (*p == '\0') break;
            idx++;
            start = p + 1;
        }
        if (*p == '\0') break;
        p++;
    }

    orbit_copy_str(out, out_cap, options_pipe);
}

static int orbit_context_find_zone_hovered(const orbit_shell_t *shell,
                                           uint32_t cursor_x, uint32_t cursor_y,
                                           uint32_t body_x, uint32_t body_y,
                                           uint32_t body_w, uint32_t body_h,
                                           size_t *out_idx) {
    if (!shell || !shell->ui.cmds || !out_idx) return 0;
    for (size_t i = shell->ui.cmd_count; i > 0u; i--) {
        size_t idx = i - 1u;
        const orbit_ui_cmd_t *cmd = &shell->ui.cmds[idx];
        if (cmd->type != ORBIT_UI_CMD_CONTEXT_MENU || !cmd->enabled || cmd->id == 0u) continue;
        uint32_t x = body_x + cmd->x;
        uint32_t y = body_y + cmd->y;
        uint32_t w = cmd->w;
        uint32_t h = cmd->h;
        if (w == 0u || h == 0u) continue;
        if (!orbit_clip_intersect(body_x, body_y, body_w, body_h, x, y, w, h, &x, &y, &w, &h)) continue;
        if (cursor_x >= x && cursor_y >= y && cursor_x < (x + w) && cursor_y < (y + h)) {
            *out_idx = idx;
            return 1;
        }
    }
    return 0;
}

static int orbit_context_find_zone_by_id(const orbit_shell_t *shell, uint32_t id, size_t *out_idx) {
    if (!shell || !shell->ui.cmds || !out_idx || id == 0u) return 0;
    for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
        const orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
        if (cmd->type != ORBIT_UI_CMD_CONTEXT_MENU) continue;
        if (cmd->id != id) continue;
        if (!cmd->enabled || cmd->min_v == 0u || !cmd->text || cmd->text[0] == '\0') return 0;
        *out_idx = i;
        return 1;
    }
    return 0;
}

static void orbit_context_compute_popup(const orbit_shell_t *shell, const orbit_ui_cmd_t *zone,
                                        uint32_t body_x, uint32_t body_y, uint32_t body_w, uint32_t body_h,
                                        uint32_t anchor_x, uint32_t anchor_y,
                                        uint32_t *out_x, uint32_t *out_y, uint32_t *out_w, uint32_t *out_h,
                                        uint32_t *out_row_h, uint32_t *out_visible, uint32_t *out_max_start) {
    if (!zone || !out_x || !out_y || !out_w || !out_h || !out_row_h || !out_visible || !out_max_start) return;
    uint32_t option_count = zone->min_v;
    uint32_t row_h = 20u;
    uint32_t desired_items = (option_count < 10u) ? option_count : 10u;
    if (desired_items == 0u) desired_items = 1u;

    uint32_t cell_w = sl_text_cell_w();
    if (cell_w == 0u) cell_w = 8u;
    uint32_t max_label_chars = 0u;
    for (uint32_t i = 0u; i < option_count; i++) {
        char opt_text[128];
        uint32_t n = 0u;
        orbit_select_option_text(zone->text, i, opt_text, sizeof(opt_text));
        while (opt_text[n]) n++;
        if (n > max_label_chars) max_label_chars = n;
    }
    uint32_t popup_w = 16u + (max_label_chars * cell_w) + 16u;
    if (option_count > desired_items) popup_w += 8u; // room for scrollbar when needed
    if (popup_w < 120u) popup_w = 120u;
    if (popup_w > 420u) popup_w = 420u;
    if (popup_w > body_w) popup_w = body_w;

    uint32_t fit_below = (body_y + body_h > anchor_y) ? ((body_y + body_h - anchor_y) / row_h) : 0u;
    uint32_t fit_above = (anchor_y > body_y) ? ((anchor_y - body_y) / row_h) : 0u;
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
    uint32_t px = anchor_x;
    uint32_t py = open_above ? ((anchor_y >= popup_h) ? (anchor_y - popup_h) : body_y) : anchor_y;
    if (px + popup_w > body_x + body_w) px = body_x + body_w - popup_w;
    if (px < body_x) px = body_x;
    if (py + popup_h > body_y + body_h) py = (anchor_y >= popup_h) ? (anchor_y - popup_h) : body_y;
    if (py < body_y) py = body_y;
    if (py + popup_h > body_y + body_h && body_h >= popup_h) py = body_y + body_h - popup_h;

    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    (void)shell;
    *out_x = px;
    *out_y = py;
    *out_w = popup_w;
    *out_h = popup_h;
    *out_row_h = row_h;
    *out_visible = visible_items;
    *out_max_start = max_start;
}

static uint8_t orbit_clip_intersect(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                                    uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                                    uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    if (!ox || !oy || !ow || !oh || aw == 0u || ah == 0u || bw == 0u || bh == 0u) return 0u;
    uint32_t ax1 = ax + aw;
    uint32_t ay1 = ay + ah;
    uint32_t bx1 = bx + bw;
    uint32_t by1 = by + bh;
    if (ax1 < ax) ax1 = 0xFFFFFFFFu;
    if (ay1 < ay) ay1 = 0xFFFFFFFFu;
    if (bx1 < bx) bx1 = 0xFFFFFFFFu;
    if (by1 < by) by1 = 0xFFFFFFFFu;

    uint32_t x0 = (ax > bx) ? ax : bx;
    uint32_t y0 = (ay > by) ? ay : by;
    uint32_t x1 = (ax1 < bx1) ? ax1 : bx1;
    uint32_t y1 = (ay1 < by1) ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) return 0u;

    *ox = x0;
    *oy = y0;
    *ow = x1 - x0;
    *oh = y1 - y0;
    return 1u;
}

static uint8_t orbit_ui_body_rect(const sl_window_t *win,
                                  uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!win || !x || !y || !w || !h) return 0u;
    uint32_t bx, by, bw, bh;
    sl_window_body_rect(win, &bx, &by, &bw, &bh);
    if (bw == 0u || bh == 0u) return 0u;
    *x = bx; *y = by; *w = bw; *h = bh;
    return 1u;
}

static int orbit_ui_cmd_is_interactive(const orbit_ui_cmd_t *cmd) {
    if (!cmd || !cmd->enabled) return 0;
    switch (cmd->type) {
        case ORBIT_UI_CMD_BUTTON:
        case ORBIT_UI_CMD_SLIDER:
        case ORBIT_UI_CMD_TOGGLE:
        case ORBIT_UI_CMD_SELECT:
        case ORBIT_UI_CMD_TEXT_INPUT:
        case ORBIT_UI_CMD_TEXT_AREA:
        case ORBIT_UI_CMD_SCROLL_V:
        case ORBIT_UI_CMD_LIST_ITEM:
        case ORBIT_UI_CMD_PANEL:
            return 1;
        default:
            return 0;
    }
}

static int orbit_ui_cmd_is_focusable(const orbit_ui_cmd_t *cmd) {
    if (!cmd || !cmd->enabled) return 0;
    switch (cmd->type) {
        case ORBIT_UI_CMD_BUTTON:
        case ORBIT_UI_CMD_SLIDER:
        case ORBIT_UI_CMD_TOGGLE:
        case ORBIT_UI_CMD_SELECT:
        case ORBIT_UI_CMD_TEXT_INPUT:
        case ORBIT_UI_CMD_TEXT_AREA:
        case ORBIT_UI_CMD_SCROLL_V:
        case ORBIT_UI_CMD_LIST_ITEM:
            return 1;
        default:
            return 0;
    }
}

static uint8_t orbit_ui_cmd_enabled_for_modal(const orbit_ui_cmd_t *cmd, uint32_t modal_select_id) {
    if (!cmd || !cmd->enabled) return 0u;
    if (modal_select_id != 0u && cmd->id != modal_select_id) return 0u;
    return 1u;
}

static int orbit_ui_find_focusable_index(const orbit_shell_t *shell, uint32_t modal_select_id,
                                         uint32_t id, size_t *out_idx) {
    if (!shell || !shell->ui.cmds || shell->ui.cmd_count == 0u || !out_idx || id == 0u) return 0;
    for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
        const orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
        if (cmd->id != id) continue;
        if (!orbit_ui_cmd_is_focusable(cmd)) return 0;
        if (!orbit_ui_cmd_enabled_for_modal(cmd, modal_select_id)) return 0;
        *out_idx = i;
        return 1;
    }
    return 0;
}

static int orbit_ui_find_next_focusable(const orbit_shell_t *shell, uint32_t modal_select_id,
                                        uint32_t current_id, int dir, uint32_t *out_id) {
    if (!shell || !shell->ui.cmds || shell->ui.cmd_count == 0u || !out_id) return 0;
    size_t start = 0u;
    int has_current = orbit_ui_find_focusable_index(shell, modal_select_id, current_id, &start);
    size_t count = shell->ui.cmd_count;
    if (count == 0u) return 0;
    if (dir >= 0) {
        size_t i = has_current ? ((start + 1u) % count) : 0u;
        for (size_t step = 0u; step < count; step++) {
            const orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
            if (orbit_ui_cmd_is_focusable(cmd) && orbit_ui_cmd_enabled_for_modal(cmd, modal_select_id)) {
                *out_id = cmd->id;
                return 1;
            }
            i = (i + 1u) % count;
        }
    } else {
        size_t i = has_current ? ((start + count - 1u) % count) : (count - 1u);
        for (size_t step = 0u; step < count; step++) {
            const orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
            if (orbit_ui_cmd_is_focusable(cmd) && orbit_ui_cmd_enabled_for_modal(cmd, modal_select_id)) {
                *out_id = cmd->id;
                return 1;
            }
            i = (i + count - 1u) % count;
        }
    }
    return 0;
}

static void orbit_ui_draw_nav_focus_ring(sl_ui_ctx_t *ui, const orbit_ui_cmd_t *cmd,
                                         uint32_t x, uint32_t y) {
    if (!ui || !cmd || cmd->w < 2u || cmd->h < 2u) return;
    sl_ui_rect(ui, x, y, cmd->w, 1u, SL_UI_COLOR_BORDER_FOCUS);
    sl_ui_rect(ui, x, y + cmd->h - 1u, cmd->w, 1u, SL_UI_COLOR_BORDER_FOCUS);
    sl_ui_rect(ui, x, y, 1u, cmd->h, SL_UI_COLOR_BORDER_FOCUS);
    sl_ui_rect(ui, x + cmd->w - 1u, y, 1u, cmd->h, SL_UI_COLOR_BORDER_FOCUS);
}

static size_t orbit_ui_find_enclosing_scroll_index(const orbit_shell_t *shell, size_t cmd_idx) {
    if (!shell || !shell->ui.cmds || cmd_idx >= shell->ui.cmd_count) return (size_t)-1;
    uint32_t depth = 0u;
    for (size_t i = cmd_idx; i > 0u; i--) {
        const orbit_ui_cmd_t *c = &shell->ui.cmds[i - 1u];
        if (c->type == ORBIT_UI_CMD_CLIP_POP) {
            depth++;
            continue;
        }
        if (c->type == ORBIT_UI_CMD_CLIP_PUSH) {
            if (depth == 0u) {
                if ((i - 1u) > 0u) {
                    size_t prev = i - 2u;
                    if (shell->ui.cmds[prev].type == ORBIT_UI_CMD_SCROLL_V) return prev;
                }
                return (size_t)-1;
            }
            depth--;
        }
    }
    return (size_t)-1;
}

static uint8_t orbit_ui_scroll_step(orbit_shell_t *shell, size_t scroll_idx, int dir, uint32_t preferred_step) {
    if (!shell || !shell->ui.cmds || scroll_idx >= shell->ui.cmd_count) return 0u;
    orbit_ui_cmd_t *scroll = &shell->ui.cmds[scroll_idx];
    if (scroll->type != ORBIT_UI_CMD_SCROLL_V) return 0u;
    if (scroll->h == 0u) return 0u;

    uint32_t max_off = (scroll->max_v > scroll->h) ? (scroll->max_v - scroll->h) : 0u;
    if (scroll->value > max_off) scroll->value = max_off;

    uint32_t step = preferred_step;
    if (step < 8u) step = 8u;
    if (step > scroll->h) step = scroll->h;
    if (step == 0u) step = 8u;

    uint32_t prev = scroll->value;
    if (dir < 0) {
        scroll->value = (scroll->value > step) ? (scroll->value - step) : 0u;
    } else {
        uint64_t nv = (uint64_t)scroll->value + (uint64_t)step;
        scroll->value = (nv < (uint64_t)max_off) ? (uint32_t)nv : max_off;
    }

    if (scroll->value != prev) {
        orbit_send_ui_event_scroll_v(shell, scroll->id, scroll->value);
        return 1u;
    }
    return 0u;
}

static void orbit_ui_scroll_focus_into_view(orbit_shell_t *shell, size_t focus_idx) {
    if (!shell || !shell->ui.cmds || focus_idx >= shell->ui.cmd_count) return;
    orbit_ui_cmd_t *focused = &shell->ui.cmds[focus_idx];

    /* Find the nearest enclosing CLIP_PUSH for this control. */
    size_t clip_idx = shell->ui.cmd_count;
    uint32_t depth = 0u;
    for (size_t i = focus_idx; i > 0u; i--) {
        const orbit_ui_cmd_t *c = &shell->ui.cmds[i - 1u];
        if (c->type == ORBIT_UI_CMD_CLIP_POP) {
            depth++;
            continue;
        }
        if (c->type == ORBIT_UI_CMD_CLIP_PUSH) {
            if (depth == 0u) {
                clip_idx = i - 1u;
                break;
            }
            depth--;
        }
    }
    if (clip_idx >= shell->ui.cmd_count) return;
    if (clip_idx == 0u) return;

    orbit_ui_cmd_t *clip = &shell->ui.cmds[clip_idx];
    orbit_ui_cmd_t *scroll = &shell->ui.cmds[clip_idx - 1u];
    if (scroll->type != ORBIT_UI_CMD_SCROLL_V) return;
    if (scroll->h == 0u) return;

    uint32_t max_off = (scroll->max_v > scroll->h) ? (scroll->max_v - scroll->h) : 0u;
    if (scroll->value > max_off) scroll->value = max_off;

    uint32_t clip_top = clip->y;
    uint32_t clip_bottom = clip->y + clip->h;
    uint32_t focus_top = focused->y;
    uint32_t focus_bottom = focused->y + focused->h;

    uint32_t new_off = scroll->value;
    if (focus_top < clip_top) {
        uint32_t delta = clip_top - focus_top;
        new_off = (new_off > delta) ? (new_off - delta) : 0u;
    } else if (focus_bottom > clip_bottom) {
        uint32_t delta = focus_bottom - clip_bottom;
        uint64_t nv = (uint64_t)new_off + (uint64_t)delta;
        new_off = (nv < (uint64_t)max_off) ? (uint32_t)nv : max_off;
    }

    if (new_off != scroll->value) {
        scroll->value = new_off;
        orbit_send_ui_event_scroll_v(shell, scroll->id, scroll->value);
    }
}

static void orbit_ui_apply_keyboard_navigation(orbit_shell_t *shell) {
    if (!shell || !shell->ui.enabled || !shell->ui.committed || !shell->ui.cmds || shell->ui.cmd_count == 0u) return;
    uint32_t modal_select_id = shell->ui.ui.select_open_id;

    size_t focus_idx = 0u;
    if (!orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &focus_idx)) {
        uint32_t first_id = 0u;
        if (orbit_ui_find_next_focusable(shell, modal_select_id, 0u, +1, &first_id)) {
            shell->ui.nav_focus_id = first_id;
            (void)orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &focus_idx);
        } else {
            shell->ui.nav_focus_id = 0u;
            shell->ui.ui.focused_id = 0u;
            return;
        }
    }

    for (uint8_t i = 0u; i < shell->ui.key_event_count; i++) {
        peos_input_event_t *ev = &shell->ui.key_events[i];
        uint16_t code = orbit_norm_keycode(ev->code);
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) continue;
        if (!ev->pressed) continue;

        if (code == PEOS_KEY_TAB) {
            if (!shell->shift_down) {
                size_t cur_idx = 0u;
                if (orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &cur_idx)) {
                    orbit_ui_cmd_t *cur = &shell->ui.cmds[cur_idx];
                    if (cur->type == ORBIT_UI_CMD_TEXT_INPUT || cur->type == ORBIT_UI_CMD_TEXT_AREA) {
                        /* Keep Tab inside text controls; only Shift+Tab leaves field focus. */
                        continue;
                    }
                }
            }
            uint32_t next_id = 0u;
            int dir = shell->shift_down ? -1 : +1;
            size_t current_scroll = orbit_ui_find_enclosing_scroll_index(shell, focus_idx);
            if (orbit_ui_find_next_focusable(shell, modal_select_id, shell->ui.nav_focus_id, dir, &next_id)) {
                size_t next_idx = 0u;
                size_t next_scroll = (size_t)-1;
                if (orbit_ui_find_focusable_index(shell, modal_select_id, next_id, &next_idx)) {
                    next_scroll = orbit_ui_find_enclosing_scroll_index(shell, next_idx);
                }
                if (current_scroll != (size_t)-1 && next_scroll != current_scroll) {
                    uint32_t step = shell->ui.cmds[focus_idx].h + 6u;
                    if (orbit_ui_scroll_step(shell, current_scroll, dir, step)) {
                        shell->ui.keyboard_nav_active = 1u;
                        continue;
                    }
                }
                shell->ui.keyboard_nav_active = 1u;
                shell->ui.nav_focus_id = next_id;
                (void)orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &focus_idx);
                const orbit_ui_cmd_t *f = &shell->ui.cmds[focus_idx];
                if (f->type == ORBIT_UI_CMD_TEXT_INPUT || f->type == ORBIT_UI_CMD_TEXT_AREA) {
                    shell->ui.ui.focused_id = f->id;
                } else {
                    shell->ui.ui.focused_id = 0u;
                }
                orbit_ui_scroll_focus_into_view(shell, focus_idx);
            } else if (current_scroll != (size_t)-1) {
                uint32_t step = shell->ui.cmds[focus_idx].h + 6u;
                if (orbit_ui_scroll_step(shell, current_scroll, dir, step)) {
                    shell->ui.keyboard_nav_active = 1u;
                    continue;
                }
            }
            continue;
        }

        if (!orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &focus_idx)) continue;
        orbit_ui_cmd_t *cmd = &shell->ui.cmds[focus_idx];

        if (code == PEOS_KEY_LEFT_NORM || code == PEOS_KEY_UP_NORM ||
            code == PEOS_KEY_RIGHT_NORM || code == PEOS_KEY_DOWN_NORM) {
            if (cmd->type != ORBIT_UI_CMD_SLIDER &&
                cmd->type != ORBIT_UI_CMD_SCROLL_V &&
                cmd->type != ORBIT_UI_CMD_SELECT &&
                cmd->type != ORBIT_UI_CMD_TEXT_INPUT &&
                cmd->type != ORBIT_UI_CMD_TEXT_AREA) {
                uint32_t next_id = 0u;
                int dir = (code == PEOS_KEY_LEFT_NORM || code == PEOS_KEY_UP_NORM) ? -1 : +1;
                if (orbit_ui_find_next_focusable(shell, modal_select_id, shell->ui.nav_focus_id, dir, &next_id)) {
                    size_t current_scroll = orbit_ui_find_enclosing_scroll_index(shell, focus_idx);
                    size_t next_idx = 0u;
                    size_t next_scroll = (size_t)-1;
                    if (orbit_ui_find_focusable_index(shell, modal_select_id, next_id, &next_idx)) {
                        next_scroll = orbit_ui_find_enclosing_scroll_index(shell, next_idx);
                    }
                    if (current_scroll != (size_t)-1 && next_scroll != current_scroll) {
                        uint32_t step = cmd->h + 6u;
                        if (orbit_ui_scroll_step(shell, current_scroll, dir, step)) {
                            shell->ui.keyboard_nav_active = 1u;
                            continue;
                        }
                    }
                    shell->ui.keyboard_nav_active = 1u;
                    shell->ui.nav_focus_id = next_id;
                    (void)orbit_ui_find_focusable_index(shell, modal_select_id, shell->ui.nav_focus_id, &focus_idx);
                    const orbit_ui_cmd_t *f = &shell->ui.cmds[focus_idx];
                    if (f->type == ORBIT_UI_CMD_TEXT_INPUT || f->type == ORBIT_UI_CMD_TEXT_AREA) {
                        shell->ui.ui.focused_id = f->id;
                    } else {
                        shell->ui.ui.focused_id = 0u;
                    }
                    orbit_ui_scroll_focus_into_view(shell, focus_idx);
                } else {
                    size_t current_scroll = orbit_ui_find_enclosing_scroll_index(shell, focus_idx);
                    if (current_scroll != (size_t)-1) {
                        uint32_t step = cmd->h + 6u;
                        if (orbit_ui_scroll_step(shell, current_scroll, dir, step)) {
                            shell->ui.keyboard_nav_active = 1u;
                            continue;
                        }
                    }
                }
                continue;
            }
        }

        if (code == PEOS_KEY_ESC) {
            if (cmd->type == ORBIT_UI_CMD_SELECT && shell->ui.ui.select_open_id == cmd->id) {
                shell->ui.ui.select_open_id = 0u;
            }
            continue;
        }

        if (cmd->type == ORBIT_UI_CMD_TEXT_INPUT || cmd->type == ORBIT_UI_CMD_TEXT_AREA) {
            continue;
        }

        if (code == PEOS_KEY_ENTER || code == PEOS_KEY_SPACE) {
            shell->ui.keyboard_nav_active = 1u;
            if (cmd->type == ORBIT_UI_CMD_BUTTON || cmd->type == ORBIT_UI_CMD_LIST_ITEM) {
                orbit_send_ui_event_button(shell, cmd->id);
            } else if (cmd->type == ORBIT_UI_CMD_TOGGLE) {
                cmd->value = cmd->value ? 0u : 1u;
                orbit_send_ui_event_toggle(shell, cmd->id, cmd->value);
            } else if (cmd->type == ORBIT_UI_CMD_SELECT) {
                if (shell->ui.ui.select_open_id == cmd->id) shell->ui.ui.select_open_id = 0u;
                else shell->ui.ui.select_open_id = cmd->id;
            }
            continue;
        }

        if (cmd->type == ORBIT_UI_CMD_SLIDER) {
            shell->ui.keyboard_nav_active = 1u;
            uint32_t prev = cmd->value;
            uint32_t range = (cmd->max_v > cmd->min_v) ? (cmd->max_v - cmd->min_v) : 0u;
            uint32_t step = (range >= 100u) ? (range / 100u) : 1u;
            if (step == 0u) step = 1u;
            if (code == PEOS_KEY_LEFT_NORM || code == PEOS_KEY_DOWN_NORM) {
                if (cmd->value > cmd->min_v) {
                    cmd->value = (cmd->value > cmd->min_v + step) ? (cmd->value - step) : cmd->min_v;
                }
            } else if (code == PEOS_KEY_RIGHT_NORM || code == PEOS_KEY_UP_NORM) {
                if (cmd->value < cmd->max_v) {
                    uint64_t nv = (uint64_t)cmd->value + (uint64_t)step;
                    cmd->value = (nv < cmd->max_v) ? (uint32_t)nv : cmd->max_v;
                }
            }
            if (cmd->value != prev) orbit_send_ui_event_slider(shell, cmd->id, cmd->value);
            continue;
        }

        if (cmd->type == ORBIT_UI_CMD_SCROLL_V) {
            shell->ui.keyboard_nav_active = 1u;
            uint32_t prev = cmd->value;
            uint32_t max_off = (cmd->max_v > cmd->h) ? (cmd->max_v - cmd->h) : 0u;
            uint32_t step = (cmd->h / 8u);
            if (step < 8u) step = 8u;
            if (code == PEOS_KEY_UP_NORM || code == PEOS_KEY_LEFT_NORM) {
                cmd->value = (cmd->value > step) ? (cmd->value - step) : 0u;
            } else if (code == PEOS_KEY_DOWN_NORM || code == PEOS_KEY_RIGHT_NORM) {
                uint64_t nv = (uint64_t)cmd->value + (uint64_t)step;
                cmd->value = (nv < max_off) ? (uint32_t)nv : max_off;
            }
            if (cmd->value != prev) orbit_send_ui_event_scroll_v(shell, cmd->id, cmd->value);
            continue;
        }

        if (cmd->type == ORBIT_UI_CMD_SELECT) {
            shell->ui.keyboard_nav_active = 1u;
            uint32_t prev = cmd->value;
            if (code == PEOS_KEY_UP_NORM || code == PEOS_KEY_LEFT_NORM) {
                if (cmd->value > 0u) cmd->value--;
            } else if (code == PEOS_KEY_DOWN_NORM || code == PEOS_KEY_RIGHT_NORM) {
                if (cmd->value + 1u < cmd->min_v) cmd->value++;
            }
            if (cmd->value != prev) orbit_send_ui_event_select(shell, cmd->id, cmd->value);
            continue;
        }
    }
}

static uint8_t orbit_ui_dropdown_popup_rect(const orbit_shell_t *shell,
                                            uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!shell || !x || !y || !w || !h) return 0u;
    if (!shell->ui.ui.select_open_id) return 0u;
    if (shell->ui.ui.select_overlay_visible_items == 0u ||
        shell->ui.ui.select_overlay_row_h == 0u ||
        shell->ui.ui.select_overlay_w == 0u) return 0u;
    *x = shell->ui.ui.select_overlay_x;
    *y = shell->ui.ui.select_overlay_y;
    *w = shell->ui.ui.select_overlay_w;
    *h = shell->ui.ui.select_overlay_visible_items * shell->ui.ui.select_overlay_row_h + 2u;
    return (*w > 0u && *h > 0u) ? 1u : 0u;
}

static uint8_t orbit_ui_context_popup_rect(const orbit_shell_t *shell,
                                           uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!shell || !x || !y || !w || !h) return 0u;
    if (!shell->ui.context_open_id) return 0u;
    if (shell->ui.context_popup_w == 0u || shell->ui.context_popup_h == 0u) return 0u;
    *x = shell->ui.context_popup_x;
    *y = shell->ui.context_popup_y;
    *w = shell->ui.context_popup_w;
    *h = shell->ui.context_popup_h;
    return 1u;
}

static uint8_t orbit_ui_cmd_rect(const sl_window_t *win, const orbit_ui_cmd_t *cmd,
                                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!win || !cmd || !x || !y || !w || !h) return 0u;
    uint32_t body_x = 0u, body_y = 0u, body_w = 0u, body_h = 0u;
    if (!orbit_ui_body_rect(win, &body_x, &body_y, &body_w, &body_h)) return 0u;

    uint32_t rx = body_x + cmd->x;
    uint32_t ry = body_y + cmd->y;
    uint32_t rw = cmd->w;
    uint32_t rh = cmd->h;
    if (!orbit_clip_intersect(body_x, body_y, body_w, body_h, rx, ry, rw, rh, &rx, &ry, &rw, &rh)) return 0u;
    *x = rx; *y = ry; *w = rw; *h = rh;
    return 1u;
}

static void orbit_rect_union_accum(uint8_t *has_rect,
                                   uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h,
                                   uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    if (!has_rect || !x || !y || !w || !h || rw == 0u || rh == 0u) return;
    if (!*has_rect) {
        *x = rx; *y = ry; *w = rw; *h = rh;
        *has_rect = 1u;
        return;
    }
    uint32_t x0 = (*x < rx) ? *x : rx;
    uint32_t y0 = (*y < ry) ? *y : ry;
    uint64_t x1a = (uint64_t)(*x) + (uint64_t)(*w);
    uint64_t y1a = (uint64_t)(*y) + (uint64_t)(*h);
    uint64_t x1b = (uint64_t)rx + (uint64_t)rw;
    uint64_t y1b = (uint64_t)ry + (uint64_t)rh;
    uint64_t x1 = (x1a > x1b) ? x1a : x1b;
    uint64_t y1 = (y1a > y1b) ? y1a : y1b;
    *x = x0;
    *y = y0;
    *w = (uint32_t)(x1 - x0);
    *h = (uint32_t)(y1 - y0);
}

static int orbit_ui_cmd_find_hovered(const sl_window_t *win, const orbit_shell_t *shell,
                                     uint32_t modal_select_id,
                                     uint32_t cursor_x, uint32_t cursor_y,
                                     uint32_t *out_id, uint32_t *out_x, uint32_t *out_y,
                                     uint32_t *out_w, uint32_t *out_h, size_t *out_idx) {
    if (!win || !shell || !out_id || !out_x || !out_y || !out_w || !out_h) return 0;
    if (!shell->ui.enabled || !shell->ui.committed || !shell->ui.cmds || shell->ui.cmd_count == 0u) return 0;

    for (size_t i = shell->ui.cmd_count; i > 0u; i--) {
        size_t idx = i - 1u;
        const orbit_ui_cmd_t *cmd = &shell->ui.cmds[idx];
        if (!orbit_ui_cmd_is_interactive(cmd)) continue;
        if (!orbit_ui_cmd_enabled_for_modal(cmd, modal_select_id)) continue;
        uint32_t x = 0u, y = 0u, w = 0u, h = 0u;
        if (!orbit_ui_cmd_rect(win, cmd, &x, &y, &w, &h)) continue;
        if (cursor_x >= x && cursor_y >= y && cursor_x < (x + w) && cursor_y < (y + h)) {
            *out_id = cmd->id;
            *out_x = x;
            *out_y = y;
            *out_w = w;
            *out_h = h;
            if (out_idx) *out_idx = idx;
            return 1;
        }
    }
    return 0;
}

static int orbit_ui_cmd_find_by_id(const sl_window_t *win, const orbit_shell_t *shell, uint32_t id,
                                   uint32_t *out_x, uint32_t *out_y,
                                   uint32_t *out_w, uint32_t *out_h, size_t *out_idx) {
    if (!win || !shell || !out_x || !out_y || !out_w || !out_h || id == 0u) return 0;
    if (!shell->ui.cmds || shell->ui.cmd_count == 0u) return 0;
    for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
        const orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
        if (cmd->id != id) continue;
        if (!orbit_ui_cmd_rect(win, cmd, out_x, out_y, out_w, out_h)) return 0;
        if (out_idx) *out_idx = i;
        return 1;
    }
    return 0;
}

int sl_app_host_input_dirty_rect(const sl_window_t *win, const sl_input_frame_t *frame,
                                 const void *userdata,
                                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!win || !frame || !userdata || !x || !y || !w || !h) return 0;

    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell->initialized) return 0;
    if (!shell->ui.enabled || !shell->ui.committed || shell->ui.cmd_count == 0u) return 0;

    uint8_t has_rect = 0u;
    uint32_t rx = 0u, ry = 0u, rw = 0u, rh = 0u;
    uint32_t modal_select_id = shell->ui.ui.select_open_id;
    uint32_t dd_x = 0u, dd_y = 0u, dd_w = 0u, dd_h = 0u;
    uint32_t cm_x = 0u, cm_y = 0u, cm_w = 0u, cm_h = 0u;
    uint8_t dropdown_open = orbit_ui_dropdown_popup_rect(shell, &dd_x, &dd_y, &dd_w, &dd_h);
    uint8_t context_open = orbit_ui_context_popup_rect(shell, &cm_x, &cm_y, &cm_w, &cm_h);

    uint32_t cursor_x = win->ctx ? win->ctx->cursor_x : 0u;
    uint32_t cursor_y = win->ctx ? win->ctx->cursor_y : 0u;

    uint32_t hover_id = 0u, hover_x = 0u, hover_y = 0u, hover_w = 0u, hover_h = 0u;
    size_t hover_idx = 0u;
    uint8_t hover_now = orbit_ui_cmd_find_hovered(win, shell, modal_select_id,
                                                  cursor_x, cursor_y,
                                                  &hover_id, &hover_x, &hover_y, &hover_w, &hover_h, &hover_idx) ? 1u : 0u;
    if (dropdown_open &&
        cursor_x >= dd_x && cursor_y >= dd_y &&
        cursor_x < (dd_x + dd_w) && cursor_y < (dd_y + dd_h)) {
        hover_now = 1u;
        hover_id = modal_select_id;
        hover_x = dd_x;
        hover_y = dd_y;
        hover_w = dd_w;
        hover_h = dd_h;
    }
    if (context_open &&
        cursor_x >= cm_x && cursor_y >= cm_y &&
        cursor_x < (cm_x + cm_w) && cursor_y < (cm_y + cm_h)) {
        hover_now = 1u;
        hover_id = shell->ui.context_open_id;
        hover_x = cm_x;
        hover_y = cm_y;
        hover_w = cm_w;
        hover_h = cm_h;
    }
    uint8_t hover_prev = (shell->ui.hover_cmd_id != 0u) ? 1u : 0u;
    uint8_t hover_changed = 0u;
    if (hover_now != hover_prev) {
        hover_changed = 1u;
    } else if (hover_now) {
        if (shell->ui.hover_cmd_id != hover_id ||
            shell->ui.hover_cmd_x != hover_x || shell->ui.hover_cmd_y != hover_y ||
            shell->ui.hover_cmd_w != hover_w || shell->ui.hover_cmd_h != hover_h) {
            hover_changed = 1u;
        }
    }

    uint8_t has_direct_interaction =
        (frame->mouse_left_pressed || frame->mouse_left_released ||
         frame->mouse_right_pressed || frame->mouse_right_released ||
         frame->mouse_wheel_x != 0 || frame->mouse_wheel_y != 0 ||
         ((frame->mouse_buttons & 0x01u) && frame->mouse_moved)) ? 1u : 0u;

    if (hover_changed && shell->ui.hover_cmd_id != 0u) {
        orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh,
                               shell->ui.hover_cmd_x, shell->ui.hover_cmd_y,
                               shell->ui.hover_cmd_w, shell->ui.hover_cmd_h);
    }
    if (hover_changed && hover_now) {
        orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, hover_x, hover_y, hover_w, hover_h);
    }

    if (has_direct_interaction && (frame->mouse_buttons & 0x01u) && shell->ui.ui.active_id != 0u) {
        uint32_t ax = 0u, ay = 0u, aw = 0u, ah = 0u;
        size_t active_idx = 0u;
        if (orbit_ui_cmd_find_by_id(win, shell, shell->ui.ui.active_id, &ax, &ay, &aw, &ah, &active_idx)) {
            orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, ax, ay, aw, ah);

            const orbit_ui_cmd_t *acmd = &shell->ui.cmds[active_idx];
            if (acmd->type == ORBIT_UI_CMD_SCROLL_V && (active_idx + 1u) < shell->ui.cmd_count) {
                const orbit_ui_cmd_t *next = &shell->ui.cmds[active_idx + 1u];
                if (next->type == ORBIT_UI_CMD_CLIP_PUSH && next->y == acmd->y && next->h == acmd->h) {
                    uint32_t cx = 0u, cy = 0u, cw = 0u, ch = 0u;
                    if (orbit_ui_cmd_rect(win, next, &cx, &cy, &cw, &ch)) {
                        orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, cx, cy, cw, ch);
                    }
                }
            }
        }
    }

    if ((frame->mouse_wheel_x != 0 || frame->mouse_wheel_y != 0) && hover_now) {
        const orbit_ui_cmd_t *hcmd = &shell->ui.cmds[hover_idx];
        if (hcmd->type == ORBIT_UI_CMD_SCROLL_V && (hover_idx + 1u) < shell->ui.cmd_count) {
            const orbit_ui_cmd_t *next = &shell->ui.cmds[hover_idx + 1u];
            if (next->type == ORBIT_UI_CMD_CLIP_PUSH && next->y == hcmd->y && next->h == hcmd->h) {
                uint32_t cx = 0u, cy = 0u, cw = 0u, ch = 0u;
                if (orbit_ui_cmd_rect(win, next, &cx, &cy, &cw, &ch)) {
                    orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, cx, cy, cw, ch);
                }
            }
        }
    }

    if (dropdown_open &&
        (frame->mouse_moved || frame->mouse_left_pressed || frame->mouse_left_released ||
         frame->mouse_wheel_x != 0 || frame->mouse_wheel_y != 0)) {
        orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, dd_x, dd_y, dd_w, dd_h);
    }
    if (context_open &&
        (frame->mouse_moved ||
         frame->mouse_left_pressed || frame->mouse_left_released ||
         frame->mouse_right_pressed || frame->mouse_right_released ||
         frame->mouse_wheel_x != 0 || frame->mouse_wheel_y != 0)) {
        orbit_rect_union_accum(&has_rect, &rx, &ry, &rw, &rh, cm_x, cm_y, cm_w, cm_h);
    }

    shell->ui.hover_cmd_id = hover_now ? hover_id : 0u;
    shell->ui.hover_cmd_x = hover_now ? hover_x : 0u;
    shell->ui.hover_cmd_y = hover_now ? hover_y : 0u;
    shell->ui.hover_cmd_w = hover_now ? hover_w : 0u;
    shell->ui.hover_cmd_h = hover_now ? hover_h : 0u;

    if (!has_rect) return 0;
    *x = rx; *y = ry; *w = rw; *h = rh;
    return 1;
}

int sl_app_host_cache_update_rect(const sl_window_t *win, const void *userdata,
                                  uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (!win || !userdata || !x || !y || !w || !h) return 0;

    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell->initialized) return 0;
    if (!shell->surface.active) return 0;
    return orbit_ui_body_rect(win, x, y, w, h) ? 1 : 0;
}

static const char *orbit_get_line(const orbit_shell_t *shell, size_t logical_index) {
    if (!shell || logical_index >= shell->line_count) return "";
    size_t slot = (shell->line_head + logical_index) % ORBIT_MAX_LINES;
    return shell->lines[slot];
}

static inline uint8_t orbit_lerp_u8(uint8_t a, uint8_t b, uint32_t t255) {
    uint32_t av = (uint32_t)a;
    uint32_t bv = (uint32_t)b;
    return (uint8_t)((av * (255u - t255) + bv * t255 + 127u) / 255u);
}

static inline uint8_t orbit_bilerp_u8(uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11,
                                      uint32_t tx255, uint32_t ty255) {
    uint8_t top = orbit_lerp_u8(c00, c10, tx255);
    uint8_t bot = orbit_lerp_u8(c01, c11, tx255);
    return orbit_lerp_u8(top, bot, ty255);
}

static void orbit_draw_grad2d(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              const uint8_t grad[4][4]) {
    if (!ui || !ui->ctx || !grad || w == 0u || h == 0u) return;
    sl_ctx_t *ctx = ui->ctx;
    for (uint32_t yy = 0u; yy < h; yy++) {
        uint32_t ty = (h > 1u) ? ((yy * 255u) / (h - 1u)) : 0u;
        for (uint32_t xx = 0u; xx < w; xx++) {
            uint32_t tx = (w > 1u) ? ((xx * 255u) / (w - 1u)) : 0u;
            uint8_t r = orbit_bilerp_u8(grad[0][0], grad[1][0], grad[2][0], grad[3][0], tx, ty);
            uint8_t g = orbit_bilerp_u8(grad[0][1], grad[1][1], grad[2][1], grad[3][1], tx, ty);
            uint8_t b = orbit_bilerp_u8(grad[0][2], grad[1][2], grad[2][2], grad[3][2], tx, ty);
            uint8_t a = orbit_bilerp_u8(grad[0][3], grad[1][3], grad[2][3], grad[3][3], tx, ty);
            if (a == 255u) sl_put_pixel(ctx, x + xx, y + yy, r, g, b);
            else sl_fill_rect_a(ctx, x + xx, y + yy, 1u, 1u, r, g, b, a);
        }
    }
}

static uint32_t orbit_read_xrgb8888(const uint8_t *p) {
    if (!p) return 0u;
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t orbit_read_rgb565(const uint8_t *p) {
    if (!p) return 0u;
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    uint32_t r = (v >> 11) & 0x1Fu;
    uint32_t g = (v >> 5) & 0x3Fu;
    uint32_t b = v & 0x1Fu;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static void orbit_rgb565_lut_init(void) {
    if (g_orbit_rgb565_lut_ready) return;
    for (uint32_t v = 0u; v <= 0xFFFFu; v++) {
        uint32_t r = (v >> 11) & 0x1Fu;
        uint32_t g = (v >> 5) & 0x3Fu;
        uint32_t b = v & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        g_orbit_rgb565_lut[v] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    g_orbit_rgb565_lut_ready = 1u;
}

static int orbit_xmap_ensure(uint32_t width) {
    if (width == 0u) return 0;
    if (g_orbit_xmap && g_orbit_xmap_cap >= (size_t)width) return 1;
    uint32_t *map = (uint32_t *)realloc(g_orbit_xmap, (size_t)width * sizeof(uint32_t));
    if (!map) return 0;
    g_orbit_xmap = map;
    g_orbit_xmap_cap = (size_t)width;
    return 1;
}

static uint32_t orbit_surface_read_rgb(const orbit_surface_t *surface, const uint8_t *src_row, uint32_t sx) {
    if (!surface || !src_row) return 0u;
    if (surface->format == ORBIT_FB_FORMAT_RGB565) {
        return orbit_read_rgb565(src_row + (size_t)sx * 2u);
    }
    return orbit_read_xrgb8888(src_row + (size_t)sx * 4u);
}

static void orbit_draw_surface(sl_ctx_t *ctx, const orbit_shell_t *shell,
                               uint32_t body_x, uint32_t body_y,
                               uint32_t body_w, uint32_t body_h) {
    if (!ctx || !shell || !shell->surface.active || !shell->surface.pixels) return;
    uint32_t src_bytespp = (shell->surface.format == ORBIT_FB_FORMAT_RGB565) ? 2u : 4u;
    if (shell->surface.format != ORBIT_FB_FORMAT_XRGB8888 &&
        shell->surface.format != ORBIT_FB_FORMAT_RGB565) {
        return;
    }
    if (shell->surface.width == 0u || shell->surface.height == 0u ||
        shell->surface.pitch < shell->surface.width * src_bytespp ||
        shell->surface.pixels_len < (size_t)shell->surface.pitch * (size_t)shell->surface.height) {
        return;
    }
    if (body_w == 0u || body_h == 0u) return;

    uint32_t dst_w = body_w;
    uint32_t dst_h = (uint32_t)(((uint64_t)dst_w * (uint64_t)shell->surface.height) /
                                (uint64_t)shell->surface.width);
    if (dst_h == 0u) dst_h = 1u;
    if (dst_h > body_h) {
        dst_h = body_h;
        dst_w = (uint32_t)(((uint64_t)dst_h * (uint64_t)shell->surface.width) /
                           (uint64_t)shell->surface.height);
        if (dst_w == 0u) dst_w = 1u;
    }

    uint32_t dst_x = body_x + ((body_w > dst_w) ? ((body_w - dst_w) / 2u) : 0u);
    uint32_t dst_y = body_y + ((body_h > dst_h) ? ((body_h - dst_h) / 2u) : 0u);

    if (!ctx->frame || (ctx->bytespp != 3u && ctx->bytespp != 4u)) return;

    uint32_t draw_x0 = dst_x;
    uint32_t draw_y0 = dst_y;
    uint32_t draw_x1 = dst_x + dst_w;
    uint32_t draw_y1 = dst_y + dst_h;
    uint32_t body_x1 = body_x + body_w;
    uint32_t body_y1 = body_y + body_h;
    if (draw_x0 < body_x) draw_x0 = body_x;
    if (draw_y0 < body_y) draw_y0 = body_y;
    if (draw_x1 > body_x1) draw_x1 = body_x1;
    if (draw_y1 > body_y1) draw_y1 = body_y1;
    if (draw_x1 > ctx->fb.width) draw_x1 = ctx->fb.width;
    if (draw_y1 > ctx->fb.height) draw_y1 = ctx->fb.height;
    if (draw_x1 <= draw_x0 || draw_y1 <= draw_y0) return;

    {
        uint32_t draw_w = draw_x1 - draw_x0;
        uint32_t draw_h = draw_y1 - draw_y0;
        uint32_t src_x0 = draw_x0 - dst_x;
        uint32_t src_y0 = draw_y0 - dst_y;

        if (draw_w > 0u && draw_h > 0u &&
            dst_w == shell->surface.width &&
            dst_h == shell->surface.height &&
            ctx->bytespp == 4u &&
            shell->surface.format == ORBIT_FB_FORMAT_XRGB8888) {
            for (uint32_t row = 0u; row < draw_h; row++) {
                const uint8_t *src = shell->surface.pixels +
                                     ((size_t)(src_y0 + row) * (size_t)shell->surface.pitch) +
                                     ((size_t)src_x0 * 4u);
                uint8_t *dst = ctx->frame +
                               ((size_t)(draw_y0 + row) * ctx->row_stride) +
                               ((size_t)draw_x0 * 4u);
                peos_memcpy(dst, src, (size_t)draw_w * 4u);
            }
            return;
        }

        if (ctx->bytespp == 4u && shell->surface.format == ORBIT_FB_FORMAT_RGB565) {
            orbit_rgb565_lut_init();
            if (!orbit_xmap_ensure(draw_w)) return;

            uint32_t x_step = (uint32_t)(((uint64_t)shell->surface.width << 16) / (uint64_t)dst_w);
            uint32_t y_step = (uint32_t)(((uint64_t)shell->surface.height << 16) / (uint64_t)dst_h);
            if (x_step == 0u) x_step = 1u;
            if (y_step == 0u) y_step = 1u;

            uint32_t x_fp = src_x0 * x_step;
            for (uint32_t i = 0u; i < draw_w; i++) {
                uint32_t sx = x_fp >> 16;
                if (sx >= shell->surface.width) sx = shell->surface.width - 1u;
                g_orbit_xmap[i] = sx;
                x_fp += x_step;
            }

            uint32_t y_fp = src_y0 * y_step;
            for (uint32_t y = 0u; y < draw_h; y++) {
                uint32_t sy = y_fp >> 16;
                if (sy >= shell->surface.height) sy = shell->surface.height - 1u;
                const uint16_t *src16 = (const uint16_t *)(const void *)
                    (shell->surface.pixels + (size_t)sy * (size_t)shell->surface.pitch);
                uint32_t *dst32 = (uint32_t *)(void *)
                    (ctx->frame + ((size_t)(draw_y0 + y) * ctx->row_stride) +
                     ((size_t)draw_x0 * 4u));
                for (uint32_t x = 0u; x < draw_w; x++) {
                    dst32[x] = g_orbit_rgb565_lut[src16[g_orbit_xmap[x]]];
                }
                y_fp += y_step;
            }
            return;
        }
    }

    uint32_t x_step = (uint32_t)(((uint64_t)shell->surface.width << 16) / (uint64_t)dst_w);
    uint32_t y_step = (uint32_t)(((uint64_t)shell->surface.height << 16) / (uint64_t)dst_h);
    if (x_step == 0u) x_step = 1u;
    if (y_step == 0u) y_step = 1u;

    uint32_t y_fp = (draw_y0 - dst_y) * y_step;
    for (uint32_t y = draw_y0; y < draw_y1; y++) {
        uint32_t sy = y_fp >> 16;
        if (sy >= shell->surface.height) sy = shell->surface.height - 1u;
        const uint8_t *src_row = shell->surface.pixels + (size_t)sy * (size_t)shell->surface.pitch;
        uint8_t *dst_row = ctx->frame + (size_t)y * ctx->row_stride +
                           (size_t)draw_x0 * (size_t)ctx->bytespp;
        uint32_t x_fp = (draw_x0 - dst_x) * x_step;

        if (ctx->bytespp == 4u) {
            uint32_t *dst32 = (uint32_t *)(void *)dst_row;
            for (uint32_t x = draw_x0; x < draw_x1; x++) {
                uint32_t sx = x_fp >> 16;
                if (sx >= shell->surface.width) sx = shell->surface.width - 1u;
                uint32_t px = orbit_surface_read_rgb(&shell->surface, src_row, sx);
                dst32[x - draw_x0] = 0xFF000000u | (px & 0x00FFFFFFu);
                x_fp += x_step;
            }
        } else {
            uint8_t *dst = dst_row;
            for (uint32_t x = draw_x0; x < draw_x1; x++) {
                uint32_t sx = x_fp >> 16;
                if (sx >= shell->surface.width) sx = shell->surface.width - 1u;
                uint32_t px = orbit_surface_read_rgb(&shell->surface, src_row, sx);
                dst[0] = (uint8_t)(px & 0xFFu);
                dst[1] = (uint8_t)((px >> 8) & 0xFFu);
                dst[2] = (uint8_t)((px >> 16) & 0xFFu);
                dst += 3u;
                x_fp += x_step;
            }
        }
        y_fp += y_step;
    }
}

static void orbit_on_render(sl_ctx_t *ctx, const sl_window_t *win, void *userdata) {
    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!ctx || !win || !shell) return;

    uint32_t body_x, body_y, body_w, body_h;
    sl_window_body_rect(win, &body_x, &body_y, &body_w, &body_h);
    if (body_w == 0u || body_h == 0u) return;

    // The title is the window frame's: the app draws its body only. The
    // terminal is always night (Soft UI plan, phase 7); SLUI apps paint
    // their own canvas below.
    {
        uint32_t cw = 0u, ch = 0u;
        if (sl_ui_ctx_mono_cell(ctx, &cw, &ch)) {
            const sl_ui_color_t *ground = sl_ui_runtime_color(SL_UI_COLOR_BG_CANVAS);
            sl_fill_rect(ctx, body_x, body_y, body_w, body_h, ground->r, ground->g, ground->b);
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT, body_x, body_y, body_w, body_h, 18u);
        } else {
            sl_fill_rect(ctx, body_x, body_y, body_w, body_h, 12u, 12u, 12u);
        }
    }

    if (shell->surface.active) {
        orbit_draw_surface(ctx, shell, body_x, body_y, body_w, body_h);
        return;
    }

    if (shell->ui.enabled && shell->ui.committed) {
        uint32_t select_open_before = shell->ui.ui.select_open_id;
        uint32_t context_open_before = shell->ui.context_open_id;
        sl_ui_begin(&shell->ui.ui, ctx, win,
                    ctx->cursor_x, ctx->cursor_y,
                    shell->ui.mouse_down, shell->ui.mouse_pressed, shell->ui.mouse_released,
                    shell->ui.mouse_wheel_x, shell->ui.mouse_wheel_y,
                    shell->ui.clipboard_cmds, shell->caps_lock_on);
        if (shell->ui.mouse_pressed) shell->ui.keyboard_nav_active = 0u;
        sl_ui_set_font_px(&shell->ui.ui, 0u);
        if (shell->ui.theme_override_enabled) {
            sl_ui_theme_apply(&shell->ui.ui, &shell->ui.theme_override);
        }
        orbit_ui_apply_keyboard_navigation(shell);

        if (shell->ui.context_open_id != 0u) {
            size_t open_idx = 0u;
            if (!orbit_context_find_zone_by_id(shell, shell->ui.context_open_id, &open_idx)) {
                shell->ui.context_open_id = 0u;
                shell->ui.context_popup_w = 0u;
                shell->ui.context_popup_h = 0u;
            }
        }

        if (shell->ui.mouse_r_pressed) {
            size_t zone_idx = 0u;
            if (orbit_context_find_zone_hovered(shell,
                                                shell->ui.ui.cursor_x, shell->ui.ui.cursor_y,
                                                body_x, body_y, body_w, body_h,
                                                &zone_idx)) {
                orbit_ui_cmd_t *zone = &shell->ui.cmds[zone_idx];
                shell->ui.context_open_id = zone->id;
                shell->ui.context_open_anchor_x = shell->ui.ui.cursor_x;
                shell->ui.context_open_anchor_y = shell->ui.ui.cursor_y;
                shell->ui.context_scroll_idx = 0u;
                shell->ui.ui.select_open_id = 0u;
            } else {
                shell->ui.context_open_id = 0u;
                shell->ui.context_popup_w = 0u;
                shell->ui.context_popup_h = 0u;
            }
        }

        if (shell->ui.context_open_id != 0u) {
            size_t zone_idx = 0u;
            if (orbit_context_find_zone_by_id(shell, shell->ui.context_open_id, &zone_idx)) {
                orbit_ui_cmd_t *zone = &shell->ui.cmds[zone_idx];
                uint32_t max_start = 0u;
                uint32_t visible_items = 0u;
                orbit_context_compute_popup(shell, zone,
                                            body_x, body_y, body_w, body_h,
                                            shell->ui.context_open_anchor_x,
                                            shell->ui.context_open_anchor_y,
                                            &shell->ui.context_popup_x,
                                            &shell->ui.context_popup_y,
                                            &shell->ui.context_popup_w,
                                            &shell->ui.context_popup_h,
                                            &shell->ui.context_popup_row_h,
                                            &visible_items,
                                            &max_start);
                if (shell->ui.context_scroll_idx > max_start) shell->ui.context_scroll_idx = max_start;

                uint8_t popup_hovered = 0u;
                uint32_t px = shell->ui.context_popup_x;
                uint32_t py = shell->ui.context_popup_y;
                uint32_t pw = shell->ui.context_popup_w;
                uint32_t ph = shell->ui.context_popup_h;
                if (pw > 0u && ph > 0u) {
                    uint32_t cx = shell->ui.ui.cursor_x;
                    uint32_t cy = shell->ui.ui.cursor_y;
                    popup_hovered = (cx >= px && cy >= py && cx < (px + pw) && cy < (py + ph)) ? 1u : 0u;
                }

                if (popup_hovered && !shell->ui.ui.wheel_consumed && max_start > 0u) {
                    int32_t wheel = (shell->ui.ui.wheel_y != 0) ? shell->ui.ui.wheel_y : shell->ui.ui.wheel_x;
                    if (wheel != 0) {
                        int64_t nv = (int64_t)shell->ui.context_scroll_idx - (int64_t)wheel;
                        if (nv < 0) nv = 0;
                        if (nv > (int64_t)max_start) nv = (int64_t)max_start;
                        shell->ui.context_scroll_idx = (uint32_t)nv;
                        shell->ui.ui.wheel_consumed = 1u;
                    }
                }

                for (uint8_t k = 0u; k < shell->ui.key_event_count; k++) {
                    peos_input_event_t *ev = &shell->ui.key_events[k];
                    if (!ev->pressed) continue;
                    if (orbit_norm_keycode(ev->code) == PEOS_KEY_ESC) {
                        shell->ui.context_open_id = 0u;
                        shell->ui.context_popup_w = 0u;
                        shell->ui.context_popup_h = 0u;
                        break;
                    }
                }

                if (shell->ui.context_open_id != 0u && shell->ui.mouse_pressed) {
                    uint32_t cx = shell->ui.ui.cursor_x;
                    uint32_t cy = shell->ui.ui.cursor_y;
                    uint8_t inside = (cx >= px && cy >= py && cx < (px + pw) && cy < (py + ph)) ? 1u : 0u;
                    if (!inside) {
                        shell->ui.context_open_id = 0u;
                        shell->ui.context_popup_w = 0u;
                        shell->ui.context_popup_h = 0u;
                    }
                }
                if (shell->ui.context_open_id != 0u && shell->ui.mouse_released) {
                    uint32_t cx = shell->ui.ui.cursor_x;
                    uint32_t cy = shell->ui.ui.cursor_y;
                    uint8_t has_scrollbar = (zone->min_v > visible_items && shell->ui.context_popup_w > 20u) ? 1u : 0u;
                    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
                    uint32_t list_w = (shell->ui.context_popup_w > scrollbar_w) ? (shell->ui.context_popup_w - scrollbar_w) : shell->ui.context_popup_w;
                    uint8_t in_list = (cx >= px + 1u && cy >= py + 1u &&
                                       cx < (px + list_w - 1u) && cy < (py + shell->ui.context_popup_h - 1u)) ? 1u : 0u;
                    if (in_list) {
                        uint32_t rel = cy - (py + 1u);
                        uint32_t item = (shell->ui.context_popup_row_h > 0u) ? (rel / shell->ui.context_popup_row_h) : 0u;
                        uint32_t picked = shell->ui.context_scroll_idx + item;
                        if (item < visible_items && picked < zone->min_v) {
                            orbit_send_ui_event_context_menu(shell, zone->id, picked);
                        }
                    }
                    shell->ui.context_open_id = 0u;
                    shell->ui.context_popup_w = 0u;
                    shell->ui.context_popup_h = 0u;
                }
            }
        }

        {
            const sl_ui_color_t *canvas = sl_ui_theme_color(&shell->ui.ui, SL_UI_COLOR_BG_CANVAS);
            if (canvas && canvas->a < 255u) {
                sl_fill_rect_a(ctx, body_x, body_y, body_w, body_h,
                               canvas->r, canvas->g, canvas->b, canvas->a);
            } else if (canvas) {
                sl_fill_rect(ctx, body_x, body_y, body_w, body_h,
                             canvas->r, canvas->g, canvas->b);
            }
        }

        uint32_t clip_x[8];
        uint32_t clip_y[8];
        uint32_t clip_w[8];
        uint32_t clip_h[8];
        uint8_t clip_depth = 0u;
        sl_clip_set(ctx, body_x, body_y, body_w, body_h);

        uint32_t modal_select_id = shell->ui.ui.select_open_id;
        uint8_t context_modal_open = (shell->ui.context_open_id != 0u) ? 1u : 0u;
        for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
            orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
            uint32_t x = body_x + cmd->x;
            uint32_t y = body_y + cmd->y;
            uint8_t cmd_enabled = orbit_ui_cmd_enabled_for_modal(cmd, modal_select_id);
            if (context_modal_open) cmd_enabled = 0u;
            uint8_t draw_nav_focus = (shell->ui.nav_focus_id != 0u &&
                                      shell->ui.keyboard_nav_active &&
                                      cmd->id == shell->ui.nav_focus_id &&
                                      orbit_ui_cmd_is_focusable(cmd) &&
                                      cmd_enabled) ? 1u : 0u;
            if (cmd_enabled && orbit_ui_cmd_is_focusable(cmd) && shell->ui.mouse_pressed) {
                uint32_t cx = shell->ui.ui.cursor_x;
                uint32_t cy = shell->ui.ui.cursor_y;
                if (cx >= x && cy >= y && cx < (x + cmd->w) && cy < (y + cmd->h)) {
                    shell->ui.nav_focus_id = cmd->id;
                    if (cmd->type == ORBIT_UI_CMD_TEXT_INPUT || cmd->type == ORBIT_UI_CMD_TEXT_AREA) {
                        shell->ui.ui.focused_id = cmd->id;
                    } else {
                        shell->ui.ui.focused_id = 0u;
                    }
                }
            }
            if (cmd->type == ORBIT_UI_CMD_CLIP_PUSH) {
                if (clip_depth < 8u) {
                    uint32_t nx = x, ny = y, nw = cmd->w, nh = cmd->h;
                    uint32_t bx = body_x, by = body_y, bw = body_w, bh = body_h;
                    if (clip_depth > 0u) {
                        bx = clip_x[clip_depth - 1u];
                        by = clip_y[clip_depth - 1u];
                        bw = clip_w[clip_depth - 1u];
                        bh = clip_h[clip_depth - 1u];
                    }
                    if (!orbit_clip_intersect(bx, by, bw, bh, nx, ny, nw, nh, &nx, &ny, &nw, &nh)) {
                        nw = 0u;
                        nh = 0u;
                    }
                    clip_x[clip_depth] = nx;
                    clip_y[clip_depth] = ny;
                    clip_w[clip_depth] = nw;
                    clip_h[clip_depth] = nh;
                    clip_depth++;
                    sl_clip_set(ctx, nx, ny, nw, nh);
                }
                continue;
            } else if (cmd->type == ORBIT_UI_CMD_CLIP_POP) {
                if (clip_depth > 0u) clip_depth--;
                if (clip_depth > 0u) {
                    sl_clip_set(ctx, clip_x[clip_depth - 1u], clip_y[clip_depth - 1u],
                                clip_w[clip_depth - 1u], clip_h[clip_depth - 1u]);
                } else {
                    sl_clip_set(ctx, body_x, body_y, body_w, body_h);
                }
                continue;
            } else if (cmd->type == ORBIT_UI_CMD_FONT_SIZE) {
                sl_ui_set_font_px(&shell->ui.ui, cmd->value);
            } else if (cmd->type == ORBIT_UI_CMD_LABEL) {
                sl_ui_label_rgb(&shell->ui.ui, x, y, cmd->text, cmd->r, cmd->g, cmd->b);
            } else if (cmd->type == ORBIT_UI_CMD_LABEL_THEME) {
                sl_ui_label(&shell->ui.ui, x, y, cmd->text, (sl_ui_color_token_t)cmd->value);
            } else if (cmd->type == ORBIT_UI_CMD_BUTTON) {
                uint8_t en = cmd_enabled;
                if (sl_ui_button_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, cmd->text, en)) {
                    orbit_emit_click_events(shell, cmd->id, 1u);
                }
            } else if (cmd->type == ORBIT_UI_CMD_SLIDER) {
                uint32_t prev = cmd->value;
                uint8_t en = cmd_enabled;
                if (sl_ui_slider_u32_state(&shell->ui.ui, cmd->id, x, y, cmd->w,
                                           cmd->min_v, cmd->max_v, &cmd->value, en)) {
                    if (cmd->value != prev) {
                        orbit_send_ui_event_slider(shell, cmd->id, cmd->value);
                    }
                }
            } else if (cmd->type == ORBIT_UI_CMD_TOGGLE) {
                uint8_t v = cmd->value ? 1u : 0u;
                uint8_t en = cmd_enabled;
                if (sl_ui_toggle_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, &v, cmd->text, en)) {
                    cmd->value = v;
                    orbit_send_ui_event_toggle(shell, cmd->id, cmd->value);
                } else {
                    cmd->value = v;
                }
            } else if (cmd->type == ORBIT_UI_CMD_SELECT) {
                char current[256];
                orbit_select_option_text(cmd->text ? cmd->text : "", cmd->value, current, sizeof(current));
                uint8_t en = cmd_enabled;
                if (sl_ui_select_cycle_state_ex(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                                cmd->min_v, &cmd->value, current, cmd->text ? cmd->text : "",
                                                shell->ui.key_events, shell->ui.key_event_count, en)) {
                    orbit_send_ui_event_select(shell, cmd->id, cmd->value);
                }
            } else if (cmd->type == ORBIT_UI_CMD_TEXT_INPUT) {
                uint32_t max_chars = cmd->max_v;
                if (max_chars < 1u) max_chars = 1u;
                if (!orbit_cmd_text_reserve(cmd, 2u)) {
                    orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "text_input_alloc_failed");
                    continue;
                }
                if ((size_t)max_chars + 1u > cmd->text_cap) {
                    (void)orbit_cmd_text_reserve(cmd, (size_t)max_chars + 1u);
                }
                if (!cmd->text) continue;
                size_t cap_sz = cmd->text_cap;
                size_t max_cap = (size_t)max_chars + 1u;
                if (cap_sz > max_cap) cap_sz = max_cap;
                if (cap_sz < 2u) cap_sz = 2u;
                if (cap_sz > UINT32_MAX) cap_sz = UINT32_MAX;
                cmd->text[cap_sz - 1u] = '\0';
                uint8_t en = cmd_enabled;
                if (sl_ui_text_input_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                           cmd->text, (uint32_t)cap_sz,
                                           shell->ui.key_events, shell->ui.key_event_count,
                                           en)) {
                    orbit_send_ui_event_text_input(shell, cmd->id, cmd->text);
                }
            } else if (cmd->type == ORBIT_UI_CMD_TEXT_AREA) {
                uint32_t max_chars = cmd->max_v;
                if (max_chars < 1u) max_chars = 1u;
                if (!orbit_cmd_text_reserve(cmd, 2u)) {
                    orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "text_area_alloc_failed");
                    continue;
                }
                if ((size_t)max_chars + 1u > cmd->text_cap) {
                    (void)orbit_cmd_text_reserve(cmd, (size_t)max_chars + 1u);
                }
                if (!cmd->text) continue;
                size_t cap_sz = cmd->text_cap;
                size_t max_cap = (size_t)max_chars + 1u;
                if (cap_sz > max_cap) cap_sz = max_cap;
                if (cap_sz < 2u) cap_sz = 2u;
                if (cap_sz > UINT32_MAX) cap_sz = UINT32_MAX;
                cmd->text[cap_sz - 1u] = '\0';
                uint8_t en = cmd_enabled;
                if (sl_ui_text_area_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                          cmd->text, (uint32_t)cap_sz,
                                          shell->ui.key_events, shell->ui.key_event_count,
                                          en)) {
                    orbit_send_ui_event_text_area(shell, cmd->id, cmd->text);
                }
            } else if (cmd->type == ORBIT_UI_CMD_SCROLL_V) {
                uint32_t prev = cmd->value;
                uint8_t en = cmd_enabled;
                uint32_t hit_x = x;
                uint32_t hit_y = y;
                uint32_t hit_w = cmd->w;
                uint32_t hit_h = cmd->h;

                // If SCROLL_V is followed by CLIP_PUSH generated by slui_scroll_view_begin,
                // use the content clip as wheel hit area (drag stays on scrollbar only).
                if ((i + 1u) < shell->ui.cmd_count) {
                    orbit_ui_cmd_t *next = &shell->ui.cmds[i + 1u];
                    if (next->type == ORBIT_UI_CMD_CLIP_PUSH &&
                        next->y == cmd->y &&
                        next->h == cmd->h) {
                        uint32_t cx = body_x + next->x;
                        uint32_t cy = body_y + next->y;
                        uint32_t cw = next->w;
                        uint32_t ch = next->h;
                        if (cw > 0u && ch > 0u) {
                            hit_x = cx;
                            hit_y = cy;
                            hit_w = cw;
                            hit_h = ch;
                        }
                    }
                }

                if (sl_ui_scroll_v_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, cmd->max_v, &cmd->value,
                                         hit_x, hit_y, hit_w, hit_h, en)) {
                    if (cmd->value != prev) {
                        orbit_send_ui_event_scroll_v(shell, cmd->id, cmd->value);
                    }
                }
            } else if (cmd->type == ORBIT_UI_CMD_PANEL) {
                uint8_t en = cmd_enabled;
                if (en && cmd->w > 0u && cmd->h > 0u) {
                    uint8_t hovered = 0u;
                    uint32_t cx = shell->ui.ui.cursor_x;
                    uint32_t cy = shell->ui.ui.cursor_y;
                    if (cx >= x && cy >= y && cx < (x + cmd->w) && cy < (y + cmd->h)) hovered = 1u;

                    if (hovered && shell->ui.mouse_pressed) {
                        shell->ui.active_panel_id = cmd->id;
                    }
                    if (shell->ui.mouse_released && shell->ui.active_panel_id == cmd->id) {
                        shell->ui.active_panel_id = 0u;
                    }

                    if (shell->ui.mouse_down && shell->ui.active_panel_id == cmd->id) {
                        uint32_t px = cx;
                        uint32_t py = cy;
                        if (px < x) px = x;
                        if (py < y) py = y;
                        if (px >= x + cmd->w) px = x + cmd->w - 1u;
                        if (py >= y + cmd->h) py = y + cmd->h - 1u;
                        uint32_t relx = px - x;
                        uint32_t rely = py - y;
                        uint32_t vx = (cmd->w > 1u) ? ((relx * 255u) / (cmd->w - 1u)) : 0u;
                        uint32_t vy = (cmd->h > 1u) ? ((rely * 255u) / (cmd->h - 1u)) : 0u;
                        if (shell->ui.mouse_pressed || vx != cmd->min_v || vy != cmd->max_v) {
                            cmd->min_v = vx;
                            cmd->max_v = vy;
                            orbit_send_ui_event_panel(shell, cmd->id, vx, vy);
                        }
                    }
                }
            } else if (cmd->type == ORBIT_UI_CMD_SEPARATOR) {
                sl_ui_separator(&shell->ui.ui, x, y, cmd->w);
            } else if (cmd->type == ORBIT_UI_CMD_LIST_ITEM) {
                const char *src = cmd->text ? cmd->text : "";
                const char *sep = src;
                char *title = NULL;
                char *subtitle = NULL;
                while (*sep && *sep != '|') sep++;
                {
                    size_t title_len = (size_t)(sep - src);
                    title = (char *)malloc(title_len + 1u);
                    if (title) {
                        if (title_len > 0u) peos_memcpy(title, src, title_len);
                        title[title_len] = '\0';
                    }
                    if (*sep == '|') {
                        size_t subtitle_len = peos_strlen(sep + 1u);
                        subtitle = (char *)malloc(subtitle_len + 1u);
                        if (subtitle) {
                            if (subtitle_len > 0u) peos_memcpy(subtitle, sep + 1u, subtitle_len);
                            subtitle[subtitle_len] = '\0';
                        }
                    }
                }
                uint8_t en = cmd_enabled;
                if (sl_ui_list_item(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                    title ? title : "", subtitle ? subtitle : "", en)) {
                    orbit_emit_click_events(shell, cmd->id, 1u);
                }
                if (title) free(title);
                if (subtitle) free(subtitle);
            } else if (cmd->type == ORBIT_UI_CMD_RECT) {
                sl_ui_rect_rgb(&shell->ui.ui, x, y, cmd->w, cmd->h, cmd->r, cmd->g, cmd->b);
            } else if (cmd->type == ORBIT_UI_CMD_RECTA) {
                sl_ui_rect_a_rgb(&shell->ui.ui, x, y, cmd->w, cmd->h, cmd->r, cmd->g, cmd->b, cmd->a);
            } else if (cmd->type == ORBIT_UI_CMD_RECT_THEME) {
                sl_ui_rect(&shell->ui.ui, x, y, cmd->w, cmd->h, (sl_ui_color_token_t)cmd->value);
            } else if (cmd->type == ORBIT_UI_CMD_RECTA_THEME) {
                sl_ui_rect_a(&shell->ui.ui, x, y, cmd->w, cmd->h, (sl_ui_color_token_t)cmd->value, cmd->a);
            } else if (cmd->type == ORBIT_UI_CMD_GRAD2D) {
                orbit_draw_grad2d(&shell->ui.ui, x, y, cmd->w, cmd->h, cmd->grad);
            } else if (cmd->type == ORBIT_UI_CMD_CONTEXT_MENU) {
                // Registration-only control: rendered by host overlay on right click.
            } else if (cmd->type == ORBIT_UI_CMD_IMAGE_FILE) {
                if (!cmd->image_loaded && !cmd->image_failed) {
                    if (cmd->text && sl_decode_image_file(cmd->text, &cmd->image)) {
                        cmd->image_loaded = 1u;
                    } else {
                        cmd->image_failed = 1u;
                        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_IMAGE_FILE, "image_file_decode_failed");
                    }
                }
                if (cmd->image_loaded && cmd->image.data) {
                    sl_ui_image(&shell->ui.ui, x, y, cmd->w, cmd->h, &cmd->image);
                }
            }

            if (draw_nav_focus &&
                cmd->type != ORBIT_UI_CMD_TEXT_INPUT &&
                cmd->type != ORBIT_UI_CMD_TEXT_AREA) {
                orbit_ui_draw_nav_focus_ring(&shell->ui.ui, cmd, x, y);
            }
        }

        sl_clip_set(ctx, body_x, body_y, body_w, body_h);
        sl_ui_draw_overlays(&shell->ui.ui);
        if (shell->ui.context_open_id != 0u && shell->ui.context_popup_w > 0u && shell->ui.context_popup_h > 0u) {
            size_t zone_idx = 0u;
            if (orbit_context_find_zone_by_id(shell, shell->ui.context_open_id, &zone_idx)) {
                const orbit_ui_cmd_t *zone = &shell->ui.cmds[zone_idx];
                uint32_t px = shell->ui.context_popup_x;
                uint32_t py = shell->ui.context_popup_y;
                uint32_t pw = shell->ui.context_popup_w;
                uint32_t ph = shell->ui.context_popup_h;
                uint32_t row_h = shell->ui.context_popup_row_h;
                uint32_t visible_items = (ph > 2u && row_h > 0u) ? ((ph - 2u) / row_h) : 0u;
                uint32_t option_count = zone->min_v;
                if (visible_items == 0u) visible_items = 1u;
                if (visible_items > option_count) visible_items = option_count;

                uint8_t has_scrollbar = (option_count > visible_items && pw > 20u) ? 1u : 0u;
                uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
                uint32_t list_w = (pw > scrollbar_w) ? (pw - scrollbar_w) : pw;
                uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
                uint32_t start_idx = shell->ui.context_scroll_idx;
                if (start_idx > max_start) start_idx = max_start;
                int32_t hover_row = -1;
                {
                    uint32_t cx = shell->ui.ui.cursor_x;
                    uint32_t cy = shell->ui.ui.cursor_y;
                    uint8_t in_list = (cx >= px + 1u && cy >= py + 1u &&
                                       cx < (px + list_w - 1u) && cy < (py + ph - 1u)) ? 1u : 0u;
                    if (in_list) {
                        uint32_t rel = cy - (py + 1u);
                        uint32_t row = (row_h > 0u) ? (rel / row_h) : 0u;
                        if (row < visible_items) hover_row = (int32_t)row;
                    }
                }
                orbit_context_label_source_t label_src;
                label_src.zone = zone;
                sl_context_menu_draw(ctx,
                                     px, py, pw, ph,
                                     row_h,
                                     option_count,
                                     start_idx,
                                     visible_items,
                                     hover_row,
                                     has_scrollbar,
                                     orbit_context_label_cb,
                                     &label_src);
            }
        }
        sl_clip_clear(ctx);

        if (select_open_before != shell->ui.ui.select_open_id ||
            context_open_before != shell->ui.context_open_id) {
            // Ensure all controls refresh visual state immediately when dropdown opens/closes.
            sl_window_dirty_body(ctx, win);
        }

        if (!shell->ui.mouse_down) shell->ui.active_panel_id = 0u;
        shell->ui.mouse_pressed = 0u;
        shell->ui.mouse_released = 0u;
        shell->ui.mouse_r_pressed = 0u;
        shell->ui.mouse_r_released = 0u;
        shell->ui.mouse_wheel_x = 0;
        shell->ui.mouse_wheel_y = 0;
        shell->ui.clipboard_cmds = 0u;
        shell->ui.key_event_count = 0u;
        return;
    }

    shell->ui.clipboard_cmds = 0u;
    shell->ui.key_event_count = 0u;

    uint32_t cell_w = sl_text_cell_w();
    uint32_t cell_h = sl_text_cell_h();
    uint8_t mono = (uint8_t)sl_ui_ctx_mono_cell(ctx, &cell_w, &cell_h);
    uint32_t pad = mono ? 16u : 4u;   // inside the rounded night card
    uint32_t text_x = body_x + pad;
    uint32_t text_y = body_y + (mono ? 12u : 4u);
    uint32_t text_w = (body_w > 2u * pad) ? (body_w - 2u * pad) : 0u;
    uint32_t text_h = (body_h > 2u * pad) ? (body_h - 2u * pad) : 0u;
    if (cell_w == 0u) cell_w = 8u;
    if (cell_h == 0u) cell_h = 8u;
    if (text_w < cell_w || text_h < cell_h) return;

    uint32_t cols = text_w / cell_w;
    uint32_t rows = text_h / cell_h;
    if (cols < 8u || rows < 1u) return;

    uint32_t history_rows = rows;
    if (cols >= ORBIT_DRAW_ROW_CHARS) cols = ORBIT_DRAW_ROW_CHARS - 1u;

    size_t virtual_total = 0u;
    for (size_t i = 0u; i < shell->line_count; i++) {
        const char *line = orbit_get_line(shell, i);
        virtual_total += orbit_text_row_count(peos_strlen(line), cols, 0u);
    }
    uint8_t has_pending = (shell->pending_len > 0u) ? 1u : 0u;
    if (has_pending) {
        virtual_total += orbit_text_row_count(shell->pending_len, cols, 1u);
    }

    size_t start = (virtual_total > history_rows) ? (virtual_total - history_rows) : 0u;
    size_t virtual_row = 0u;
    uint32_t draw_row = 0u;
    uint64_t blink = (peos_time_ns() / 500000000ull) & 1ull;

    size_t logical_total = shell->line_count + (has_pending ? 1u : 0u);
    for (size_t i = 0u; i < logical_total && draw_row < history_rows; i++) {
        uint8_t pending = (i == shell->line_count) ? 1u : 0u;
        const char *line = pending
                               ? (shell->pending_line ? shell->pending_line : "")
                               : orbit_get_line(shell, i);
        size_t line_len = pending ? shell->pending_len : peos_strlen(line);
        size_t line_rows = orbit_text_row_count(line_len, cols, pending);
        size_t cursor_row = pending ? (line_len / cols) : SIZE_MAX;

        for (size_t segment = 0u; segment < line_rows; segment++, virtual_row++) {
            if (virtual_row < start) continue;
            char row_buf[ORBIT_DRAW_ROW_CHARS];
            uint8_t show_cursor = (pending && segment == cursor_row &&
                                   win->focused && blink) ? 1u : 0u;
            (void)orbit_text_copy_row(line, line_len, cols, segment,
                                      show_cursor, row_buf, sizeof(row_buf));
            if (mono) {
                // Night terminal: pale blue text, the prompt in the accent.
                const sl_ui_color_t *n = sl_ui_runtime_color(SL_UI_COLOR_NEON);
                uint32_t ry = text_y + (draw_row * cell_h);
                size_t plen = (segment == 0u && strncmp(row_buf, "orbit>", 6) == 0) ? 6u : 0u;
                if (plen) {
                    char head[8];
                    memcpy(head, row_buf, plen);
                    head[plen] = '\0';
                    sl_ui_ctx_mono_text(ctx, text_x, ry, head, n->r, n->g, n->b);
                }
                sl_ui_ctx_mono_text(ctx, text_x + (uint32_t)plen * cell_w, ry, row_buf + plen, 0xC9u, 0xE8u, 0xFFu);
            } else {
                sl_draw_text(ctx, text_x, text_y + (draw_row * cell_h),
                             row_buf, 200u, 230u, 200u);
            }
            draw_row++;
            if (draw_row >= history_rows) break;
        }
    }

    if (shell->child_pid <= 0 && shell->pending_len == 0u) {
        const char *hint = "[Enter] start app";
        size_t n = peos_strlen(hint);
        if (n > cols) {
            hint += (n - cols);
        }
        if (mono) sl_ui_ctx_mono_text(ctx, text_x, text_y + ((history_rows - 1u) * cell_h), hint, 0x9Fu, 0xB4u, 0xD8u);
        else sl_draw_text(ctx, text_x, text_y + ((history_rows - 1u) * cell_h), hint, 170u, 170u, 170u);
    } else {
        size_t idx = shell->slot_index;
        if (idx < g_orbit_slot_count && g_orbit_app[idx]) {
            g_orbit_app[idx]->pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
        }
    }
}

void sl_app_host_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    g_orbit_double_click_ms = orbit_double_click_ms_clamp(ctx->double_click_ms);
    if (g_reload_display_requested) {
        g_reload_display_requested = 0u;
        sl_reload_display(ctx);
    }
    if (g_reload_theme_requested) {
        g_reload_theme_requested = 0u;
        sl_reload_theme(ctx);
    }
    if (g_open_app_requested) {
        g_open_app_requested = 0u;
        (void)sl_windows_open_app_bundle_path(ctx, g_open_app_bundle_path);
    }
    uint64_t now = peos_time_ns();
    for (size_t i = 0u; i < g_orbit_slot_count; i++) {
        orbit_shell_t *shell = g_orbit[i];
        if (!shell) continue;
        if (!shell->initialized) continue;
        shell->ui.double_click_ms = g_orbit_double_click_ms;
        if (shell->next_poll_ns > now) continue;
        uint8_t changed = orbit_poll_output(shell);
        shell->next_poll_ns = now + orbit_poll_interval_ns(shell, changed);

        if (changed) {
            sl_windows_mark_app_dirty(ctx, shell);
        }

        g_orbit_app[i]->pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
    }
}

static int orbit_slots_reserve(size_t needed) {
    if (needed <= g_orbit_slot_cap) return 1;
    size_t new_cap = g_orbit_slot_cap ? g_orbit_slot_cap : ORBIT_APP_INITIAL_CAP;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2u) return 0;
        new_cap *= 2u;
    }

    orbit_shell_t **new_shells = (orbit_shell_t **)calloc(new_cap, sizeof(*new_shells));
    app_instance_t **new_apps = (app_instance_t **)calloc(new_cap, sizeof(*new_apps));
    orbit_slot_cfg_t *new_cfg = (orbit_slot_cfg_t *)calloc(new_cap, sizeof(*new_cfg));
    if (!new_shells || !new_apps || !new_cfg) {
        free(new_shells);
        free(new_apps);
        free(new_cfg);
        return 0;
    }
    for (size_t i = 0u; i < g_orbit_slot_count; i++) {
        new_shells[i] = g_orbit[i];
        new_apps[i] = g_orbit_app[i];
        new_cfg[i] = g_orbit_cfg[i];
    }
    free(g_orbit);
    free(g_orbit_app);
    free(g_orbit_cfg);
    g_orbit = new_shells;
    g_orbit_app = new_apps;
    g_orbit_cfg = new_cfg;
    g_orbit_slot_cap = new_cap;
    return 1;
}

static int orbit_slots_ensure(size_t needed) {
    if (!orbit_slots_reserve(needed)) return 0;
    while (g_orbit_slot_count < needed) {
        size_t index = g_orbit_slot_count;
        orbit_shell_t *shell = (orbit_shell_t *)calloc(1u, sizeof(*shell));
        app_instance_t *app = (app_instance_t *)calloc(1u, sizeof(*app));
        if (!shell || !app) {
            free(shell);
            free(app);
            return 0;
        }
        shell->slot_index = index;
        shell->child_pid = -1;
        shell->child_stdin_fd = -1;
        shell->child_stdout_fd = -1;
        app->on_input = orbit_on_input;
        app->on_render = orbit_on_render;
        app->on_close = orbit_on_close;
        app->userdata = shell;
        g_orbit[index] = shell;
        g_orbit_app[index] = app;
        g_orbit_slot_count++;
    }
    return 1;
}

static void orbit_cfg_apply_to_shell(size_t index) {
    if (index >= g_orbit_slot_count || !g_orbit[index]) return;
    orbit_shell_t *shell = g_orbit[index];
    shell->enabled = g_orbit_cfg[index].enabled;
    orbit_copy_str(shell->title, sizeof(shell->title), g_orbit_cfg[index].title);
    orbit_copy_str(shell->exec_path, sizeof(shell->exec_path), g_orbit_cfg[index].exec_path);
    orbit_copy_str(shell->exec_args, sizeof(shell->exec_args), g_orbit_cfg[index].exec_args);
}

void sl_app_host_slots_clear(void) {
    for (size_t i = 0u; i < g_orbit_slot_count; i++) {
        g_orbit_cfg[i].enabled = 0u;
        g_orbit_cfg[i].title[0] = '\0';
        g_orbit_cfg[i].exec_path[0] = '\0';
        g_orbit_cfg[i].exec_args[0] = '\0';
        g_orbit_cfg[i].caps_restrict = 0u;
        g_orbit_cfg[i].caps_mask = 0u;
        g_orbit_cfg[i].paths_restrict = 0u;
        g_orbit_cfg[i].path_count = 0u;
    }
    g_orbit_cfg_init = 1u;
}

int sl_app_host_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args) {
    if (!exec_path || exec_path[0] == '\0' || index == SIZE_MAX) return 0;
    if (!orbit_slots_ensure(index + 1u)) return 0;
    if (!g_orbit_cfg_init) sl_app_host_slots_clear();

    g_orbit_cfg[index].enabled = 1u;
    orbit_copy_str(g_orbit_cfg[index].title, sizeof(g_orbit_cfg[index].title), title ? title : "App");
    orbit_copy_str(g_orbit_cfg[index].exec_path, sizeof(g_orbit_cfg[index].exec_path), exec_path);
    orbit_copy_str(g_orbit_cfg[index].exec_args, sizeof(g_orbit_cfg[index].exec_args), exec_args ? exec_args : "");
    g_orbit_cfg[index].caps_restrict = 0u;
    g_orbit_cfg[index].caps_mask = 0u;
    g_orbit_cfg[index].paths_restrict = 0u;
    g_orbit_cfg[index].path_count = 0u;
    if (g_orbit[index] && !g_orbit[index]->initialized) {
        orbit_cfg_apply_to_shell(index);
    }
    return 1;
}

int sl_app_host_slot_restrict_paths(size_t index, const peos_path_grant_t *grants, size_t count) {
    if (index >= g_orbit_slot_count || !g_orbit_cfg_init) return 0;
    if (count > PEOS_PATH_MAX_GRANTS || (count != 0u && !grants)) return 0;
    for (size_t i = 0; i < count; i++) g_orbit_cfg[index].paths[i] = grants[i];
    g_orbit_cfg[index].path_count = (uint32_t)count;
    g_orbit_cfg[index].paths_restrict = 1u;
    return 1;
}

int sl_app_host_slot_restrict_caps(size_t index, uint64_t caps_mask) {
    if (index >= g_orbit_slot_count || !g_orbit_cfg_init) return 0;
    g_orbit_cfg[index].caps_restrict = 1u;
    g_orbit_cfg[index].caps_mask = caps_mask;
    return 1;
}

size_t sl_app_host_next_slot_index(void) {
    return g_orbit_slot_count;
}

void sl_app_host_slots_use_default(void) {
    sl_app_host_slots_clear();
    (void)sl_app_host_slot_configure(0u, "Orbit", "/bin/orbit", NULL);
}

size_t sl_app_host_slot_count_enabled(void) {
    if (!g_orbit_cfg_init) sl_app_host_slots_use_default();
    size_t count = 0u;
    for (size_t i = 0u; i < g_orbit_slot_count; i++) {
        if (g_orbit_cfg[i].enabled) count++;
    }
    return count;
}

void sl_app_host_reset(void) {
    if (!g_orbit_cfg_init) {
        sl_app_host_slots_use_default();
    }
    for (size_t i = 0u; i < g_orbit_slot_count; i++) {
        orbit_shell_t *shell = g_orbit[i];
        if (!shell) continue;
        orbit_stop_process(shell);
        orbit_ui_storage_free(&shell->ui);
        orbit_pending_line_free(shell);
        orbit_surface_free(shell);
        peos_memset(shell, 0, sizeof(*shell));
        shell->slot_index = i;
        orbit_cfg_apply_to_shell(i);
        shell->child_pid = -1;
        shell->child_stdin_fd = -1;
        shell->child_stdout_fd = -1;
        orbit_reset_buffer(shell);
        g_orbit_app[i]->pid = 0u;
        g_orbit_app[i]->userdata = shell;
    }
}

app_instance_t *sl_app_host_instance(void) {
    return sl_app_host_instance_at(0u);
}

app_instance_t *sl_app_host_instance_peek_at(size_t index) {
    if (!g_orbit_cfg_init) sl_app_host_slots_use_default();
    if (index >= g_orbit_slot_count || !g_orbit_cfg[index].enabled) return NULL;
    return g_orbit_app[index];
}

app_instance_t *sl_app_host_instance_at(size_t index) {
    if (!g_orbit_cfg_init) sl_app_host_slots_use_default();
    if (index >= g_orbit_slot_count) return NULL;
    if (!g_orbit_cfg[index].enabled) return NULL;
    orbit_shell_t *shell = g_orbit[index];
    app_instance_t *app = g_orbit_app[index];
    if (!shell || !app) return NULL;
    if (!shell->initialized) {
        orbit_ui_storage_free(&shell->ui);
        orbit_pending_line_free(shell);
        orbit_surface_free(shell);
        peos_memset(shell, 0, sizeof(*shell));
        shell->slot_index = index;
        orbit_cfg_apply_to_shell(index);
        shell->child_pid = -1;
        shell->child_stdin_fd = -1;
        shell->child_stdout_fd = -1;
        orbit_reset_buffer(shell);

        if (!orbit_spawn_process(shell)) {
            orbit_push_line(shell, "failed to start app process");
        }

        app->pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
        shell->initialized = 1u;
    } else {
        if (shell->enabled && shell->child_pid <= 0) {
            orbit_cfg_apply_to_shell(index);
            if (!orbit_spawn_process(shell)) {
                orbit_push_line(shell, "failed to start app process");
            }
        }
        app->pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
    }
    return app;
}
