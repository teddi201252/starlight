#include "orbit_window.h"

#include <stddef.h>
#include <stdint.h>

#define ORBIT_MAX_LINES 256u
#define ORBIT_LINE_CHARS 160u
#define ORBIT_KEY_BACKSPACE 0x000Eu
#define ORBIT_APP_SLOTS 2u
#define ORBIT_MAX_POLL_READS 16u

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
} orbit_shell_t;

static orbit_shell_t g_orbit[ORBIT_APP_SLOTS];
static app_instance_t g_orbit_app[ORBIT_APP_SLOTS];
static uint8_t g_orbit_apps_bound = 0u;

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

static void orbit_reset_buffer(orbit_shell_t *shell) {
    if (!shell) return;
    shell->line_head = 0u;
    shell->line_count = 0u;
    shell->pending_len = 0u;
    shell->pending_line[0] = '\0';
}

static void orbit_feed_char(orbit_shell_t *shell, char c) {
    if (!shell) return;
    if (c == '\r') return;

    if (c == '\n') {
        shell->pending_line[shell->pending_len] = '\0';
        orbit_push_line(shell, shell->pending_line);
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

    const char *path = "/bin/orbit";
    int64_t pid = peos_spawnv(path, peos_strlen(path), NULL,
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

static void orbit_mark_child_exited(orbit_shell_t *shell) {
    if (!shell) return;
    orbit_close_fds(shell);
    shell->child_pid = -1;
    orbit_push_line(shell, "[orbit exited] Press Enter to restart.");
}

static void orbit_poll_output(orbit_shell_t *shell) {
    if (!shell || shell->child_stdout_fd < 0) return;

    char buf[256];
    for (uint32_t i = 0u; i < ORBIT_MAX_POLL_READS; i++) {
        uint64_t n = peos_fread((uint64_t)shell->child_stdout_fd, buf, sizeof(buf));
        if ((int64_t)n < 0) {
            break;
        }
        if (n == 0u) {
            break;
        }
        orbit_feed_text(shell, buf, (size_t)n);
    }

    if (shell->child_pid > 0) {
        int status = 0;
        uint64_t w = peos_waitpid_flags((uint64_t)shell->child_pid, &status, PEOS_WAIT_NOHANG);
        if (w == (uint64_t)shell->child_pid) {
            orbit_mark_child_exited(shell);
        }
    }
}

static void orbit_send_input(orbit_shell_t *shell, const char *buf, size_t len) {
    if (!shell || !buf || len == 0u) return;
    if (shell->child_pid <= 0) {
        if (!orbit_spawn_process(shell)) {
            orbit_push_line(shell, "failed to start /bin/orbit");
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
    (void)frame;
    if (!event) return;

    orbit_shell_t *shell = (orbit_shell_t *)userdata;
    if (!shell) return;

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

    orbit_poll_output(shell);

    uint32_t body_x = win->x + 2u;
    uint32_t body_y = win->y + win->top_bar_height + 1u;
    uint32_t body_w = (win->width > 4u) ? (win->width - 4u) : 0u;
    uint32_t body_h = (win->height > (win->top_bar_height + 3u)) ? (win->height - win->top_bar_height - 3u) : 0u;

    if (body_w == 0u || body_h == 0u) return;

    sl_fill_rect(ctx, body_x, body_y, body_w, body_h, 12u, 12u, 12u);
    sl_draw_text(ctx, win->x + 6u, win->y + 6u, "Orbit", 20u, 20u, 20u);

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
        const char *hint = "[Enter] start orbit";
        size_t n = peos_strlen(hint);
        if (n > cols) {
            hint += (n - cols);
        }
        sl_draw_text(ctx, text_x, text_y + ((history_rows - 1u) * 8u), hint, 170u, 170u, 170u);
    } else {
        g_orbit_app[0].pid = (shell == &g_orbit[0]) ? (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u) : g_orbit_app[0].pid;
        g_orbit_app[1].pid = (shell == &g_orbit[1]) ? (uint32_t)((shell->child_pid > 0) ? shell->child_pid : 0u) : g_orbit_app[1].pid;
    }
}

static void orbit_bind_apps(void) {
    if (g_orbit_apps_bound) return;
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        g_orbit_app[i].pid = 0u;
        g_orbit_app[i].tid = 0u;
        g_orbit_app[i].on_input = orbit_on_input;
        g_orbit_app[i].on_render = orbit_on_render;
        g_orbit_app[i].userdata = &g_orbit[i];
    }
    g_orbit_apps_bound = 1u;
}

void sl_orbit_app_reset(void) {
    orbit_bind_apps();
    for (size_t i = 0u; i < ORBIT_APP_SLOTS; i++) {
        orbit_stop_process(&g_orbit[i]);
        peos_memset(&g_orbit[i], 0, sizeof(g_orbit[i]));
        g_orbit[i].child_pid = -1;
        g_orbit[i].child_stdin_fd = -1;
        g_orbit[i].child_stdout_fd = -1;
        orbit_reset_buffer(&g_orbit[i]);

        if (!orbit_spawn_process(&g_orbit[i])) {
            orbit_push_line(&g_orbit[i], "failed to start /bin/orbit");
        }

        g_orbit_app[i].pid = (uint32_t)((g_orbit[i].child_pid > 0) ? g_orbit[i].child_pid : 0u);
        g_orbit[i].initialized = 1u;
    }
}

app_instance_t *sl_orbit_app_instance(void) {
    return sl_orbit_app_instance_at(0u);
}

app_instance_t *sl_orbit_app_instance_at(size_t index) {
    orbit_bind_apps();
    if (index >= ORBIT_APP_SLOTS) return NULL;
    if (!g_orbit[index].initialized) {
        peos_memset(&g_orbit[index], 0, sizeof(g_orbit[index]));
        g_orbit[index].child_pid = -1;
        g_orbit[index].child_stdin_fd = -1;
        g_orbit[index].child_stdout_fd = -1;
        orbit_reset_buffer(&g_orbit[index]);

        if (!orbit_spawn_process(&g_orbit[index])) {
            orbit_push_line(&g_orbit[index], "failed to start /bin/orbit");
        }

        g_orbit_app[index].pid = (uint32_t)((g_orbit[index].child_pid > 0) ? g_orbit[index].child_pid : 0u);
        g_orbit[index].initialized = 1u;
    } else {
        g_orbit_app[index].pid = (uint32_t)((g_orbit[index].child_pid > 0) ? g_orbit[index].child_pid : 0u);
    }
    return &g_orbit_app[index];
}
