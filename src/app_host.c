#include "app_host.h"
#include "starlight_internal.h"
#include "ui_sdk.h"
#include "render/image_decode.h"

#include <stddef.h>
#include <stdint.h>

#define ORBIT_MAX_LINES 256u
#define ORBIT_LINE_CHARS 160u
#define ORBIT_TITLE_CHARS 64u
#define ORBIT_EXEC_CHARS 160u
#define ORBIT_ARGS_CHARS 160u
#define ORBIT_KEY_BACKSPACE 0x000Eu
#define ORBIT_APP_SLOTS 8u
#define ORBIT_MAX_POLL_READS 8u
#define ORBIT_POLL_INTERVAL_NS 10000000ull
#define ORBIT_UI_MAX_CMDS 64u
#define ORBIT_UI_TEXT_CHARS 96u
#define ORBIT_SLUI_PROTO_VERSION 1u

#define ORBIT_SLUI_CAP_LABEL        (1u << 0)
#define ORBIT_SLUI_CAP_BUTTON       (1u << 1)
#define ORBIT_SLUI_CAP_SLIDER       (1u << 2)
#define ORBIT_SLUI_CAP_EVENT_BUTTON (1u << 3)
#define ORBIT_SLUI_CAP_EVENT_SLIDER (1u << 4)
#define ORBIT_SLUI_CAP_IMAGE_FILE   (1u << 5)

#define ORBIT_SLUI_ERR_MALFORMED   100u
#define ORBIT_SLUI_ERR_HELLO       101u
#define ORBIT_SLUI_ERR_LABEL       110u
#define ORBIT_SLUI_ERR_BUTTON      111u
#define ORBIT_SLUI_ERR_SLIDER      112u
#define ORBIT_SLUI_ERR_IMAGE_FILE  113u
#define ORBIT_SLUI_ERR_UNKNOWN_CMD 199u
#define ORBIT_SLUI_ERR_CMD_LIMIT   200u

typedef enum {
    ORBIT_UI_CMD_NONE = 0,
    ORBIT_UI_CMD_LABEL = 1,
    ORBIT_UI_CMD_BUTTON = 2,
    ORBIT_UI_CMD_SLIDER = 3,
    ORBIT_UI_CMD_IMAGE_FILE = 4,
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
    char text[ORBIT_UI_TEXT_CHARS];
    sl_image_t image;
    uint8_t image_loaded;
    uint8_t image_failed;
} orbit_ui_cmd_t;

typedef struct {
    uint8_t enabled;
    uint8_t mouse_down;
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    uint8_t committed;
    sl_ui_ctx_t ui;
    orbit_ui_cmd_t cmds[ORBIT_UI_MAX_CMDS];
    size_t cmd_count;
    orbit_ui_cmd_t staged_cmds[ORBIT_UI_MAX_CMDS];
    size_t staged_count;
    uint8_t staged_enabled;
    uint8_t hello_seen;
    uint8_t host_hello_sent;
    uint8_t client_proto_ver;
    uint32_t client_caps_mask;
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
    char pending_line[ORBIT_LINE_CHARS];
    size_t pending_len;
    uint8_t shift_down;
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
        "@SLUI HOST HELLO 1 CAPS LABEL BUTTON SLIDER IMAGE_FILE EVENT_BUTTON EVENT_SLIDER\n";
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

static void orbit_ui_clear(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_ui_cmd_release_array(shell->ui.staged_cmds, shell->ui.staged_count);
    shell->ui.staged_enabled = 1u;
    shell->ui.staged_count = 0u;
}

static orbit_ui_cmd_t *orbit_ui_next_cmd(orbit_shell_t *shell, uint8_t type) {
    if (!shell) return NULL;
    shell->ui.staged_enabled = 1u;
    if (shell->ui.staged_count >= ORBIT_UI_MAX_CMDS) {
        orbit_send_ui_error(shell, ORBIT_SLUI_ERR_CMD_LIMIT, "command_limit_reached");
        return NULL;
    }
    orbit_ui_cmd_t *cmd = &shell->ui.staged_cmds[shell->ui.staged_count++];
    peos_memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
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
        shell->ui.cmd_count = shell->ui.staged_count;
        if (shell->ui.cmd_count > 0u) {
            peos_memcpy(shell->ui.cmds, shell->ui.staged_cmds,
                        shell->ui.cmd_count * sizeof(shell->ui.cmds[0]));
        }
        shell->ui.committed = 1u;
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

    if (orbit_token_is(cmd_tok, cmd_len, "BUTTON")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_BUTTON);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->h)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_BUTTON, "button_invalid_args");
            return 1;
        }
        p = orbit_skip_spaces(p);
        orbit_copy_str(cmd->text, sizeof(cmd->text), p);
        return 1;
    }

    if (orbit_token_is(cmd_tok, cmd_len, "SLIDER")) {
        orbit_ui_cmd_t *cmd = orbit_ui_next_cmd(shell, ORBIT_UI_CMD_SLIDER);
        if (!cmd) return 1;
        if (!orbit_parse_u32(&p, &cmd->id) || !orbit_parse_u32(&p, &cmd->x) ||
            !orbit_parse_u32(&p, &cmd->y) || !orbit_parse_u32(&p, &cmd->w) ||
            !orbit_parse_u32(&p, &cmd->min_v) || !orbit_parse_u32(&p, &cmd->max_v) ||
            !orbit_parse_u32(&p, &cmd->value)) {
            orbit_send_ui_error(shell, ORBIT_SLUI_ERR_SLIDER, "slider_invalid_args");
            return 1;
        }
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
    orbit_ui_cmd_release_array(shell->ui.cmds, shell->ui.cmd_count);
    orbit_ui_cmd_release_array(shell->ui.staged_cmds, shell->ui.staged_count);
    orbit_clear_text_buffer(shell);
    shell->esc_active = 0u;
    shell->csi_active = 0u;
    shell->csi_len = 0u;
    shell->csi_buf[0] = '\0';
    peos_memset(&shell->ui, 0, sizeof(shell->ui));
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
    if (shell->child_stdin_fd >= 0) {
        (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "exit\n", 5u);
    }
    orbit_close_fds(shell);
    shell->child_pid = -1;
}

static void orbit_on_close(void *userdata) {
    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

    int64_t pid = shell->child_pid;
    if (shell->child_stdin_fd >= 0) {
        (void)peos_fwrite((uint64_t)shell->child_stdin_fd, "exit\n", 5u);
    }
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
    orbit_ui_cmd_release_array(shell->ui.cmds, shell->ui.cmd_count);
    orbit_ui_cmd_release_array(shell->ui.staged_cmds, shell->ui.staged_count);
    peos_memset(&shell->ui, 0, sizeof(shell->ui));
    orbit_push_line(shell, "[app exited] Press Enter to restart.");
}

static uint8_t orbit_poll_output(orbit_shell_t *shell) {
    if (!shell || shell->child_stdout_fd < 0) return 0u;

    uint8_t changed = 0u;

    char buf[256];
    for (uint32_t i = 0u; i < ORBIT_MAX_POLL_READS; i++) {
        uint64_t n = peos_fread((uint64_t)shell->child_stdout_fd, buf, sizeof(buf));
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
    }

    if (!event) return;

    uint16_t code = orbit_norm_keycode(event->code);

    if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
        shell->shift_down = event->pressed ? 1u : 0u;
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

static const char *orbit_get_line(const orbit_shell_t *shell, size_t logical_index) {
    if (!shell || logical_index >= shell->line_count) return "";
    size_t slot = (shell->line_head + logical_index) % ORBIT_MAX_LINES;
    return shell->lines[slot];
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
        sl_fill_rect(ctx, body_x, body_y, body_w, body_h, 20u, 22u, 30u);
        sl_ui_begin(&shell->ui.ui, ctx, win,
                    ctx->cursor_x, ctx->cursor_y,
                    shell->ui.mouse_down, shell->ui.mouse_pressed, shell->ui.mouse_released);

        for (size_t i = 0u; i < shell->ui.cmd_count; i++) {
            orbit_ui_cmd_t *cmd = &shell->ui.cmds[i];
            uint32_t x = body_x + cmd->x;
            uint32_t y = body_y + cmd->y;
            if (cmd->type == ORBIT_UI_CMD_LABEL) {
                sl_ui_label(&shell->ui.ui, x, y, cmd->text, cmd->r, cmd->g, cmd->b);
            } else if (cmd->type == ORBIT_UI_CMD_BUTTON) {
                if (sl_ui_button(&shell->ui.ui, cmd->id, x, y, cmd->w, cmd->h, cmd->text)) {
                    orbit_send_ui_event_button(shell, cmd->id);
                }
            } else if (cmd->type == ORBIT_UI_CMD_SLIDER) {
                uint32_t prev = cmd->value;
                if (sl_ui_slider_u32(&shell->ui.ui, cmd->id, x, y, cmd->w,
                                     cmd->min_v, cmd->max_v, &cmd->value)) {
                    if (cmd->value != prev) {
                        orbit_send_ui_event_slider(shell, cmd->id, cmd->value);
                    }
                }
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

        shell->ui.mouse_pressed = 0u;
        shell->ui.mouse_released = 0u;
        return;
    }

    uint32_t text_x = body_x + 4u;
    uint32_t text_y = body_y + 4u;
    uint32_t text_w = (body_w > 8u) ? (body_w - 8u) : 0u;
    uint32_t text_h = (body_h > 8u) ? (body_h - 8u) : 0u;
    if (text_w < 8u || text_h < 8u) return;

    uint32_t cols = text_w / 8u;
    uint32_t rows = text_h / 8u;
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
            sl_draw_text(ctx, text_x, text_y + (r * 8u), clipped, 200u, 230u, 200u);
        } else {
            sl_draw_text(ctx, text_x, text_y + (r * 8u), src, 200u, 230u, 200u);
        }
    }

    if (shell->child_pid <= 0 && shell->pending_len == 0u) {
        const char *hint = "[Enter] start app";
        size_t n = peos_strlen(hint);
        if (n > cols) {
            hint += (n - cols);
        }
        sl_draw_text(ctx, text_x, text_y + ((history_rows - 1u) * 8u), hint, 170u, 170u, 170u);
    } else {
        size_t idx = (size_t)(shell - &g_orbit[0]);
        if (idx < ORBIT_APP_SLOTS) {
            g_orbit_app[idx].pid = (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u);
        }
    }
}

void sl_app_host_tick(sl_ctx_t *ctx) {
    if (!ctx) return;
    uint64_t now = peos_time_ns();
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        orbit_shell_t *shell = &g_orbit[i];
        if (!shell->initialized) continue;
        if (shell->next_poll_ns > now) continue;
        shell->next_poll_ns = now + ORBIT_POLL_INTERVAL_NS;

        if (orbit_poll_output(shell)) {
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
