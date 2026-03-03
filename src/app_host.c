#include "app_host.h"
#include "starlight_internal.h"
#include "ui_sdk.h"
#include "render/image_decode.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define ORBIT_MAX_LINES 256u
#define ORBIT_LINE_CHARS 160u
#define ORBIT_PENDING_LINE_CHARS 4096u
#define ORBIT_TITLE_CHARS 64u
#define ORBIT_EXEC_CHARS 160u
#define ORBIT_ARGS_CHARS 160u
#define ORBIT_KEY_BACKSPACE PEOS_KEY_BACKSPACE
#define ORBIT_APP_SLOTS 8u
#define ORBIT_MAX_POLL_READS 8u
#define ORBIT_POLL_INTERVAL_ACTIVE_NS 2000000ull
#define ORBIT_POLL_INTERVAL_BUSY_NS   4000000ull
#define ORBIT_POLL_INTERVAL_IDLE_NS  16000000ull
#define ORBIT_UI_CMD_SOFT_CHUNK 64u
#define ORBIT_UI_CMD_HARD_MAX 1024u
#define ORBIT_UI_TEXT_CHARS 4096u
#define ORBIT_SLUI_PROTO_VERSION 1u

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
    char text[ORBIT_UI_TEXT_CHARS];
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
    int32_t mouse_wheel_x;
    int32_t mouse_wheel_y;
    uint8_t key_event_count;
    uint8_t committed;
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
    uint8_t theme_override_enabled;
    sl_ui_theme_t theme_override;
} orbit_ui_state_t;

typedef struct {
    uint8_t enabled;
    char title[ORBIT_TITLE_CHARS];
    char exec_path[ORBIT_EXEC_CHARS];
    char exec_args[ORBIT_ARGS_CHARS];
} orbit_slot_cfg_t;

typedef struct {
    char lines[ORBIT_MAX_LINES][ORBIT_LINE_CHARS];
    size_t line_head;
    size_t line_count;
    char pending_line[ORBIT_PENDING_LINE_CHARS];
    size_t pending_len;
    uint8_t shift_down;
    uint8_t ctrl_down;
    uint8_t alt_down;
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
    orbit_ui_state_t ui;
    uint8_t esc_active;
    uint8_t csi_active;
    char csi_buf[16];
    size_t csi_len;
} orbit_shell_t;

static orbit_shell_t g_orbit[ORBIT_APP_SLOTS];
static app_instance_t g_orbit_app[ORBIT_APP_SLOTS];
static orbit_slot_cfg_t g_orbit_cfg[ORBIT_APP_SLOTS];
static uint8_t g_orbit_apps_bound = 0u;
static uint8_t g_orbit_cfg_init = 0u;
static uint8_t g_reload_display_requested = 0u;
static uint8_t g_reload_theme_requested = 0u;

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

static uint8_t orbit_u8_clamp(uint32_t v) {
    return (uint8_t)((v > 255u) ? 255u : v);
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
        "@SLUI HOST HELLO 1 CAPS LABEL BUTTON SLIDER IMAGE_FILE TOGGLE SELECT TEXT_INPUT TEXT_AREA SCROLL_V CLIP SEPARATOR LIST_ITEM DISABLED_STATE RECT RECT_ALPHA EVENT_BUTTON EVENT_SLIDER EVENT_TOGGLE EVENT_SELECT EVENT_TEXT_INPUT EVENT_TEXT_AREA EVENT_SCROLL_V EVENT_KEYBOARD THEME_TOKENS GRADIENT_2D PANEL_INPUT\n";
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, peos_strlen(msg));
    shell->ui.host_hello_sent = 1u;
}

static void orbit_ui_cmd_release(orbit_ui_cmd_t *cmd) {
    if (!cmd) return;
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

static void orbit_send_ui_event_text_input(orbit_shell_t *shell, uint32_t id, const char *text) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!text) text = "";

    char msg[320];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT TEXT_INPUT ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char num[16];
    size_t m = 0u;
    uint32_t v = id;
    if (v == 0u) num[m++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    for (size_t i = 0u; text[i] && n + 1u < sizeof(msg); i++) {
        char c = text[i];
        if (c == '\n' || c == '\r') c = ' ';
        msg[n++] = c;
    }
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
}

static void orbit_send_ui_event_text_area(orbit_shell_t *shell, uint32_t id, const char *text) {
    if (!shell || shell->child_stdin_fd < 0) return;
    if (!text) text = "";

    char msg[4608];
    size_t n = 0u;
    const char *prefix = "@SLUI EVENT TEXT_AREA ";
    while (prefix[n] && n + 1u < sizeof(msg)) msg[n] = prefix[n], n++;

    char num[16];
    size_t m = 0u;
    uint32_t v = id;
    if (v == 0u) num[m++] = '0';
    else {
        char rev[16]; size_t r = 0u;
        while (v > 0u && r < sizeof(rev)) { rev[r++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (r > 0u && m < sizeof(num)) num[m++] = rev[--r];
    }
    for (size_t i = 0u; i < m && n + 1u < sizeof(msg); i++) msg[n++] = num[i];
    if (n + 1u < sizeof(msg)) msg[n++] = ' ';

    for (size_t i = 0u; text[i] && n + 2u < sizeof(msg); i++) {
        char c = text[i];
        if (c == '\\') { msg[n++] = '\\'; msg[n++] = '\\'; }
        else if (c == '\n') { msg[n++] = '\\'; msg[n++] = 'n'; }
        else if (c == '\r') { msg[n++] = '\\'; msg[n++] = 'r'; }
        else if (c == '\t') { msg[n++] = '\\'; msg[n++] = 't'; }
        else msg[n++] = c;
    }
    if (n + 1u < sizeof(msg)) msg[n++] = '\n';
    (void)peos_fwrite((uint64_t)shell->child_stdin_fd, msg, n);
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
        orbit_ui_cmd_release_array(shell->ui.cmds, shell->ui.cmd_count);
        shell->ui.enabled = shell->ui.staged_enabled;
        if (!orbit_ui_cmd_reserve(&shell->ui.cmds, &shell->ui.cmd_cap, shell->ui.staged_count)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "command_buffer_alloc_failed");
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
        if (cmd->text[0] == '\0') {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SELECT, "select_missing_options");
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
        if (cmd->max_v >= ORBIT_UI_TEXT_CHARS) cmd->max_v = ORBIT_UI_TEXT_CHARS - 1u;
        p = orbit_skip_spaces(p);
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
        cmd->text[cmd->max_v] = '\0';
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
        if (cmd->max_v >= ORBIT_UI_TEXT_CHARS) cmd->max_v = ORBIT_UI_TEXT_CHARS - 1u;
        p = orbit_skip_spaces(p);
        orbit_copy_unescaped(cmd->text, sizeof(cmd->text), p);
        cmd->text[cmd->max_v] = '\0';
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
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
    shell->pending_len = 0u;
    shell->pending_line[0] = '\0';
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
    orbit_clear_text_buffer(shell);
    shell->esc_active = 0u;
    shell->csi_active = 0u;
    shell->csi_len = 0u;
    shell->csi_buf[0] = '\0';
    peos_memset(shell->ui.key_down, 0, sizeof(shell->ui.key_down));
    shell->ui.staged_enabled = 1u;
    shell->ui.active_panel_id = 0u;
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
        shell->pending_line[shell->pending_len] = '\0';
        if (!orbit_ui_handle_line(shell, shell->pending_line)) {
            orbit_push_line(shell, shell->pending_line);
        }
        shell->pending_len = 0u;
        shell->pending_line[0] = '\0';
        return;
    }

    if (c == '\b') {
        if (shell->pending_len > 0u) {
            shell->pending_len--;
            shell->pending_line[shell->pending_len] = '\0';
        }
        return;
    }

    if ((uint8_t)c < 32u) return;
    if (shell->pending_len + 1u >= sizeof(shell->pending_line)) {
        shell->pending_line[shell->pending_len] = '\0';
        orbit_push_line(shell, shell->pending_line);
        shell->pending_len = 0u;
    }
    shell->pending_line[shell->pending_len++] = c;
    shell->pending_line[shell->pending_len] = '\0';
}

static void orbit_feed_text(orbit_shell_t *shell, const char *text, size_t len) {
    if (!shell || !text || len == 0u) return;
    for (size_t i = 0u; i < len; i++) {
        orbit_feed_char(shell, text[i]);
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
    return 1;
}

static void orbit_stop_process(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_request_child_exit(shell);
    orbit_close_fds(shell);
    shell->child_pid = -1;
}

static void orbit_on_close(void *userdata) {
    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

    int64_t pid = shell->child_pid;
    orbit_request_child_exit(shell);
    orbit_close_fds(shell);

    if (pid > 0) {
        int status = 0;
        for (uint32_t i = 0u; i < 20u; i++) {
            uint64_t w = peos_waitpid_flags((uint64_t)pid, &status, PEOS_WAIT_NOHANG);
            if (w == (uint64_t)pid) break;
            peos_sleep(1u);
        }
    }

    shell->child_pid = -1;
}

static void orbit_mark_child_exited(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_close_fds(shell);
    shell->child_pid = -1;
    orbit_ui_storage_free(&shell->ui);
    peos_memset(&shell->ui, 0, sizeof(shell->ui));
    orbit_push_line(shell, "[app exited] Press Enter to restart.");
}

static uint8_t orbit_poll_output(orbit_shell_t *shell) {
    if (!shell || shell->child_stdout_fd < 0) return 0u;

    uint8_t changed = 0u;

    char buf[256];
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

static char orbit_key_to_char(uint16_t code, uint8_t shift) {
    switch (code) {
        case PEOS_KEY_A: return shift ? 'A' : 'a';
        case PEOS_KEY_B: return shift ? 'B' : 'b';
        case PEOS_KEY_C: return shift ? 'C' : 'c';
        case PEOS_KEY_D: return shift ? 'D' : 'd';
        case PEOS_KEY_E: return shift ? 'E' : 'e';
        case PEOS_KEY_F: return shift ? 'F' : 'f';
        case PEOS_KEY_G: return shift ? 'G' : 'g';
        case PEOS_KEY_H: return shift ? 'H' : 'h';
        case PEOS_KEY_I: return shift ? 'I' : 'i';
        case PEOS_KEY_J: return shift ? 'J' : 'j';
        case PEOS_KEY_K: return shift ? 'K' : 'k';
        case PEOS_KEY_L: return shift ? 'L' : 'l';
        case PEOS_KEY_M: return shift ? 'M' : 'm';
        case PEOS_KEY_N: return shift ? 'N' : 'n';
        case PEOS_KEY_O: return shift ? 'O' : 'o';
        case PEOS_KEY_P: return shift ? 'P' : 'p';
        case PEOS_KEY_Q: return shift ? 'Q' : 'q';
        case PEOS_KEY_R: return shift ? 'R' : 'r';
        case PEOS_KEY_S: return shift ? 'S' : 's';
        case PEOS_KEY_T: return shift ? 'T' : 't';
        case PEOS_KEY_U: return shift ? 'U' : 'u';
        case PEOS_KEY_V: return shift ? 'V' : 'v';
        case PEOS_KEY_W: return shift ? 'W' : 'w';
        case PEOS_KEY_X: return shift ? 'X' : 'x';
        case PEOS_KEY_Y: return shift ? 'Y' : 'y';
        case PEOS_KEY_Z: return shift ? 'Z' : 'z';
        case PEOS_KEY_SPACE: return ' ';
        case 0x0002u: return shift ? '!' : '1';
        case 0x0003u: return shift ? '@' : '2';
        case 0x0004u: return shift ? '#' : '3';
        case 0x0005u: return shift ? '$' : '4';
        case 0x0006u: return shift ? '%' : '5';
        case 0x0007u: return shift ? '^' : '6';
        case 0x0008u: return shift ? '&' : '7';
        case 0x0009u: return shift ? '*' : '8';
        case 0x000Au: return shift ? '(' : '9';
        case 0x000Bu: return shift ? ')' : '0';
        case 0x000Cu: return shift ? '_' : '-';
        case 0x000Du: return shift ? '+' : '=';
        case 0x001Au: return shift ? '{' : '[';
        case 0x001Bu: return shift ? '}' : ']';
        case 0x0027u: return shift ? ':' : ';';
        case 0x0028u: return shift ? '"' : '\'';
        case 0x0029u: return shift ? '~' : '`';
        case 0x002Bu: return shift ? '|' : '\\';
        case 0x0033u: return shift ? '<' : ',';
        case 0x0034u: return shift ? '>' : '.';
        case 0x0035u: return shift ? '?' : '/';
        default: return '\0';
    }
}

static uint16_t orbit_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) {
        return (uint16_t)(code & 0x00FFu);
    }
    return code;
}

static void orbit_on_input(sl_ctx_t *ctx, const sl_input_frame_t *frame,
                           const peos_input_event_t *event, void *userdata) {
    (void)ctx;

    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

    if (frame) {
        shell->ui.mouse_down = (frame->mouse_buttons & 0x01u) ? 1u : 0u;
        shell->ui.mouse_pressed = frame->mouse_left_pressed;
        shell->ui.mouse_released = frame->mouse_left_released;
        shell->ui.mouse_wheel_x = frame->mouse_wheel_x;
        shell->ui.mouse_wheel_y = frame->mouse_wheel_y;
    }

    if (!event) return;

    uint16_t code = orbit_norm_keycode(event->code);
    uint8_t key_ix = (uint8_t)(code & 0xFFu);

    if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
        shell->shift_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LCTRL || code == PEOS_KEY_RCTRL_NORM) {
        shell->ctrl_down = event->pressed ? 1u : 0u;
    } else if (code == PEOS_KEY_LALT || code == PEOS_KEY_RALT_NORM) {
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
        code == PEOS_KEY_LALT || code == PEOS_KEY_RALT_NORM ||
        code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
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

    char c = orbit_key_to_char(code, shell->shift_down);
    if (!c && code >= 32u && code <= 126u) {
        c = (char)code;
    }
    if (!c) return;

    orbit_send_input(shell, &c, 1u);
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

static void orbit_split_list_item_text(const char *in, char *title, size_t title_cap,
                                       char *subtitle, size_t subtitle_cap) {
    if (title && title_cap > 0u) title[0] = '\0';
    if (subtitle && subtitle_cap > 0u) subtitle[0] = '\0';
    if (!in) return;

    const char *sep = in;
    while (*sep && *sep != '|') sep++;

    if (title && title_cap > 0u) {
        size_t n = 0u;
        while (in[n] && (in + n) < sep && n + 1u < title_cap) {
            title[n] = in[n];
            n++;
        }
        title[n] = '\0';
    }

    if (*sep == '|' && subtitle && subtitle_cap > 0u) {
        orbit_copy_str(subtitle, subtitle_cap, sep + 1);
    }
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

static void orbit_on_render(sl_ctx_t *ctx, const sl_window_t *win, void *userdata) {
    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!ctx || !win || !shell) return;

    uint32_t body_x = win->x + 2u;
    uint32_t body_y = win->y + win->top_bar_height + 1u;
    uint32_t body_w = (win->width > 4u) ? (win->width - 4u) : 0u;
    uint32_t body_h = (win->height > (win->top_bar_height + 3u)) ? (win->height - win->top_bar_height - 3u) : 0u;

    if (body_w == 0u || body_h == 0u) return;

    sl_fill_rect(ctx, body_x, body_y, body_w, body_h, 12u, 12u, 12u);
    const char *title = shell->title[0] ? shell->title : "App";
    sl_draw_text(ctx, win->x + 6u, win->y + 6u, title, 20u, 20u, 20u);

    if (shell->ui.enabled && shell->ui.committed) {
        sl_ui_begin(&shell->ui.ui, ctx, win,
                    ctx->cursor_x, ctx->cursor_y,
                    shell->ui.mouse_down, shell->ui.mouse_pressed, shell->ui.mouse_released,
                    shell->ui.mouse_wheel_x, shell->ui.mouse_wheel_y);
        if (shell->ui.theme_override_enabled) {
            sl_ui_theme_apply(&shell->ui.ui, &shell->ui.theme_override);
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

        for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
            orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
            uint32_t x = body_x + cmd->x;
            uint32_t y = body_y + cmd->y;
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
            } else if (cmd->type == ORBIT_UI_CMD_LABEL) {
                sl_ui_label_rgb(&shell->ui.ui, x, y, cmd->text, cmd->r, cmd->g, cmd->b);
            } else if (cmd->type == ORBIT_UI_CMD_LABEL_THEME) {
                sl_ui_label(&shell->ui.ui, x, y, cmd->text, (sl_ui_color_token_t)cmd->value);
            } else if (cmd->type == ORBIT_UI_CMD_BUTTON) {
                if (sl_ui_button_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, cmd->text, cmd->enabled)) {
                    orbit_send_ui_event_button(shell, cmd->id);
                }
            } else if (cmd->type == ORBIT_UI_CMD_SLIDER) {
                uint32_t prev = cmd->value;
                if (sl_ui_slider_u32_state(&shell->ui.ui, cmd->id, x, y, cmd->w,
                                           cmd->min_v, cmd->max_v, &cmd->value, cmd->enabled)) {
                    if (cmd->value != prev) {
                        orbit_send_ui_event_slider(shell, cmd->id, cmd->value);
                    }
                }
            } else if (cmd->type == ORBIT_UI_CMD_TOGGLE) {
                uint8_t v = cmd->value ? 1u : 0u;
                if (sl_ui_toggle_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, &v, cmd->text, cmd->enabled)) {
                    cmd->value = v;
                    orbit_send_ui_event_toggle(shell, cmd->id, cmd->value);
                } else {
                    cmd->value = v;
                }
            } else if (cmd->type == ORBIT_UI_CMD_SELECT) {
                char current[ORBIT_UI_TEXT_CHARS];
                orbit_select_option_text(cmd->text, cmd->value, current, sizeof(current));
                if (sl_ui_select_cycle_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                             cmd->min_v, &cmd->value, current, cmd->enabled)) {
                    orbit_send_ui_event_select(shell, cmd->id, cmd->value);
                }
            } else if (cmd->type == ORBIT_UI_CMD_TEXT_INPUT) {
                uint32_t max_chars = cmd->max_v;
                if (max_chars < 1u) max_chars = 1u;
                if (max_chars >= ORBIT_UI_TEXT_CHARS) max_chars = ORBIT_UI_TEXT_CHARS - 1u;
                cmd->text[max_chars] = '\0';
                if (sl_ui_text_input_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                           cmd->text, max_chars + 1u,
                                           shell->ui.key_events, shell->ui.key_event_count,
                                           cmd->enabled)) {
                    orbit_send_ui_event_text_input(shell, cmd->id, cmd->text);
                }
            } else if (cmd->type == ORBIT_UI_CMD_TEXT_AREA) {
                uint32_t max_chars = cmd->max_v;
                if (max_chars < 1u) max_chars = 1u;
                if (max_chars >= ORBIT_UI_TEXT_CHARS) max_chars = ORBIT_UI_TEXT_CHARS - 1u;
                cmd->text[max_chars] = '\0';
                if (sl_ui_text_area_state(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                          cmd->text, max_chars + 1u,
                                          shell->ui.key_events, shell->ui.key_event_count,
                                          cmd->enabled)) {
                    orbit_send_ui_event_text_area(shell, cmd->id, cmd->text);
                }
            } else if (cmd->type == ORBIT_UI_CMD_SCROLL_V) {
                uint32_t prev = cmd->value;
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
                                         hit_x, hit_y, hit_w, hit_h, cmd->enabled)) {
                    if (cmd->value != prev) {
                        orbit_send_ui_event_scroll_v(shell, cmd->id, cmd->value);
                    }
                }
            } else if (cmd->type == ORBIT_UI_CMD_PANEL) {
                if (cmd->enabled && cmd->w > 0u && cmd->h > 0u) {
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
                char title[ORBIT_UI_TEXT_CHARS];
                char subtitle[ORBIT_UI_TEXT_CHARS];
                orbit_split_list_item_text(cmd->text, title, sizeof(title), subtitle, sizeof(subtitle));
                if (sl_ui_list_item(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h,
                                    title, subtitle, cmd->enabled)) {
                    orbit_send_ui_event_button(shell, cmd->id);
                }
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
            } else if (cmd->type == ORBIT_UI_CMD_IMAGE_FILE) {
                if (!cmd->image_loaded && !cmd->image_failed) {
                    if (sl_decode_image_file(cmd->text, &cmd->image)) {
                        cmd->image_loaded = 1u;
                    } else {
                        cmd->image_failed = 1u;
                        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_IMAGE_FILE, "image_file_decode_failed");
                    }
                }
                if (cmd->image_loaded && cmd->image.data) {
                    sl_ui_image_rgba(&shell->ui.ui, x, y, cmd->w, cmd->h,
                                     cmd->image.data,
                                     cmd->image.width,
                                     cmd->image.height,
                                     cmd->image.stride);
                }
            }
        }

        sl_clip_clear(ctx);
        if (!shell->ui.mouse_down) shell->ui.active_panel_id = 0u;
        shell->ui.mouse_pressed = 0u;
        shell->ui.mouse_released = 0u;
        shell->ui.mouse_wheel_x = 0;
        shell->ui.mouse_wheel_y = 0;
        shell->ui.key_event_count = 0u;
        return;
    }

    shell->ui.key_event_count = 0u;

    uint32_t text_x = body_x + 4u;
    uint32_t text_y = body_y + 4u;
    uint32_t text_w = (body_w > 8u) ? (body_w - 8u) : 0u;
    uint32_t text_h = (body_h > 8u) ? (body_h - 8u) : 0u;
    uint32_t cell_w = sl_text_cell_w();
    uint32_t cell_h = sl_text_cell_h();
    if (cell_w == 0u) cell_w = 8u;
    if (cell_h == 0u) cell_h = 8u;
    if (text_w < cell_w || text_h < cell_h) return;

    uint32_t cols = text_w / cell_w;
    uint32_t rows = text_h / cell_h;
    if (cols < 8u || rows < 1u) return;

    uint32_t history_rows = rows;
    size_t virtual_total = shell->line_count + ((shell->pending_len > 0u) ? 1u : 0u);
    size_t start = 0u;
    if (virtual_total > history_rows) {
        start = virtual_total - history_rows;
    }

    for (uint32_t r = 0u; r < history_rows; r++) {
        size_t idx = start + r;
        if (idx >= virtual_total) break;

        const char *src = NULL;
        char line_buf[ORBIT_LINE_CHARS];
        if (idx < shell->line_count) {
            src = orbit_get_line(shell, idx);
        } else {
            orbit_copy_str(line_buf, sizeof(line_buf), shell->pending_line);
            uint64_t blink = (peos_time_ns() / 500000000ull) & 1ull;
            size_t n = peos_strlen(line_buf);
            if (win->focused && blink && n + 1u < sizeof(line_buf)) {
                line_buf[n++] = '_';
                line_buf[n] = '\0';
            }
            src = line_buf;
        }

        size_t n = peos_strlen(src);
        if (n > cols) {
            const char *tail = src + (n - cols);
            char clipped[ORBIT_LINE_CHARS];
            orbit_copy_str(clipped, sizeof(clipped), tail);
            sl_draw_text(ctx, text_x, text_y + (r * cell_h), clipped, 200u, 230u, 200u);
        } else {
            sl_draw_text(ctx, text_x, text_y + (r * cell_h), src, 200u, 230u, 200u);
        }
    }

    if (shell->child_pid <= 0 && shell->pending_len == 0u) {
        const char *hint = "[Enter] start app";
        size_t n = peos_strlen(hint);
        if (n > cols) {
            hint += (n - cols);
        }
        sl_draw_text(ctx, text_x, text_y + ((history_rows - 1u) * cell_h), hint, 170u, 170u, 170u);
    } else {
        size_t idx = (size_t)(shell - &g_orbit[0]);
        if (idx < ORBIT_APP_SLOTS) {
            g_orbit_app[idx].pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
        }
    }
}

void sl_app_host_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (g_reload_display_requested) {
        g_reload_display_requested = 0u;
        sl_reload_display(ctx);
    }
    if (g_reload_theme_requested) {
        g_reload_theme_requested = 0u;
        sl_reload_theme(ctx);
    }
    uint64_t now = peos_time_ns();
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        orbit_shell_t *shell = &g_orbit[i];
        if (!shell->initialized) continue;
        if (shell->next_poll_ns > now) continue;
        uint8_t changed = orbit_poll_output(shell);
        shell->next_poll_ns = now + orbit_poll_interval_ns(shell, changed);

        if (changed) {
            sl_windows_mark_app_dirty(ctx, shell);
        }

        g_orbit_app[i].pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
    }
}

static void orbit_bind_apps(void) {
    if (g_orbit_apps_bound) return;
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        g_orbit_app[i].pid = 0u;
        g_orbit_app[i].tid = 0u;
        g_orbit_app[i].on_input = orbit_on_input;
        g_orbit_app[i].on_render = orbit_on_render;
        g_orbit_app[i].on_close = orbit_on_close;
        g_orbit_app[i].userdata = &g_orbit[i];
    }
    g_orbit_apps_bound = 1u;
}

static void orbit_cfg_apply_to_shell(size_t index) {
    if (index >= ORBIT_APP_SLOTS) return;
    g_orbit[index].enabled = g_orbit_cfg[index].enabled;
    orbit_copy_str(g_orbit[index].title, sizeof(g_orbit[index].title), g_orbit_cfg[index].title);
    orbit_copy_str(g_orbit[index].exec_path, sizeof(g_orbit[index].exec_path), g_orbit_cfg[index].exec_path);
    orbit_copy_str(g_orbit[index].exec_args, sizeof(g_orbit[index].exec_args), g_orbit_cfg[index].exec_args);
}

void sl_app_host_slots_clear(void) {
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        g_orbit_cfg[i].enabled = 0u;
        g_orbit_cfg[i].title[0] = '\0';
        g_orbit_cfg[i].exec_path[0] = '\0';
        g_orbit_cfg[i].exec_args[0] = '\0';
    }
    g_orbit_cfg_init = 1u;
}

int sl_app_host_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args) {
    if (index >= ORBIT_APP_SLOTS || !exec_path || exec_path[0] == '\0') return 0;
    if (!g_orbit_cfg_init) sl_app_host_slots_clear();

    g_orbit_cfg[index].enabled = 1u;
    orbit_copy_str(g_orbit_cfg[index].title, sizeof(g_orbit_cfg[index].title), title ? title : "App");
    orbit_copy_str(g_orbit_cfg[index].exec_path, sizeof(g_orbit_cfg[index].exec_path), exec_path);
    orbit_copy_str(g_orbit_cfg[index].exec_args, sizeof(g_orbit_cfg[index].exec_args), exec_args ? exec_args : "");
    return 1;
}

void sl_app_host_slots_use_default(void) {
    sl_app_host_slots_clear();
    (void)sl_app_host_slot_configure(0u, "Orbit", "/bin/orbit", NULL);
}

size_t sl_app_host_slot_count_enabled(void) {
    if (!g_orbit_cfg_init) sl_app_host_slots_use_default();
    size_t count = 0u;
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        if (g_orbit_cfg[i].enabled) count++;
    }
    return count;
}

void sl_app_host_reset(void) {
    if (!g_orbit_cfg_init) {
        sl_app_host_slots_use_default();
    }
    orbit_bind_apps();
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        orbit_stop_process(&g_orbit[i]);
        orbit_ui_storage_free(&g_orbit[i].ui);
        peos_memset(&g_orbit[i], 0, sizeof(g_orbit[i]));
        orbit_cfg_apply_to_shell(i);
        g_orbit[i].child_pid = -1;
        g_orbit[i].child_stdin_fd = -1;
        g_orbit[i].child_stdout_fd = -1;
        g_orbit[i].next_poll_ns = 0ull;
        orbit_reset_buffer(&g_orbit[i]);

        if (!g_orbit[i].enabled) {
            g_orbit[i].initialized = 0u;
            g_orbit_app[i].pid = 0u;
            continue;
        }

        if (!orbit_spawn_process(&g_orbit[i])) {
            orbit_push_line(&g_orbit[i], "failed to start app process");
        }

        g_orbit_app[i].pid = (uint32_t)((g_orbit[i].child_pid > 0) ? g_orbit[i].child_pid : 0u);
        g_orbit[i].initialized = 1u;
    }
}

app_instance_t *sl_app_host_instance(void) {
    return sl_app_host_instance_at(0u);
}

app_instance_t *sl_app_host_instance_at(size_t index) {
    orbit_bind_apps();
    if (!g_orbit_cfg_init) sl_app_host_slots_use_default();
    if (index >= ORBIT_APP_SLOTS) return NULL;
    if (!g_orbit_cfg[index].enabled) return NULL;
    if (!g_orbit[index].initialized) {
        orbit_ui_storage_free(&g_orbit[index].ui);
        peos_memset(&g_orbit[index], 0, sizeof(g_orbit[index]));
        orbit_cfg_apply_to_shell(index);
        g_orbit[index].child_pid = -1;
        g_orbit[index].child_stdin_fd = -1;
        g_orbit[index].child_stdout_fd = -1;
        g_orbit[index].next_poll_ns = 0ull;
        orbit_reset_buffer(&g_orbit[index]);

        if (!orbit_spawn_process(&g_orbit[index])) {
            orbit_push_line(&g_orbit[index], "failed to start app process");
        }

        g_orbit_app[index].pid = (uint32_t)((g_orbit[index].child_pid > 0) ? g_orbit[index].child_pid : 0u);
        g_orbit[index].initialized = 1u;
    } else {
        if (g_orbit[index].enabled && g_orbit[index].child_pid <= 0) {
            if (!orbit_spawn_process(&g_orbit[index])) {
                orbit_push_line(&g_orbit[index], "failed to start app process");
            }
        }
        g_orbit_app[index].pid = (uint32_t)((g_orbit[index].child_pid > 0) ? g_orbit[index].child_pid : 0u);
    }
    return &g_orbit_app[index];
}
