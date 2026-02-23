#include "orbit_window.h"

#include "orbit_shell.h"

#include <stddef.h>
#include <stdint.h>

#define ORBIT_MAX_LINES 256u
#define ORBIT_LINE_CHARS 160u
#define ORBIT_INPUT_CHARS 128u
#define ORBIT_KEY_BACKSPACE 0x000Eu

typedef struct {
    char lines[ORBIT_MAX_LINES][ORBIT_LINE_CHARS];
    size_t line_head;
    size_t line_count;
    char input[ORBIT_INPUT_CHARS];
    size_t input_len;
    uint8_t shift_down;
    uint8_t initialized;
} orbit_shell_t;

static orbit_shell_t g_orbit;

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

static void orbit_push_text(orbit_shell_t *shell, const char *text) {
    if (!shell || !text) return;

    char line[ORBIT_LINE_CHARS];
    size_t n = 0u;

    for (size_t i = 0u;; i++) {
        char c = text[i];
        if (c == '\n' || c == '\0') {
            line[n] = '\0';
            orbit_push_line(shell, line);
            n = 0u;
            if (c == '\0') break;
            continue;
        }

        if (n + 1u < sizeof(line)) {
            line[n++] = c;
        }
    }
}

static void orbit_clear_lines(orbit_shell_t *shell) {
    if (!shell) return;
    shell->line_head = 0u;
    shell->line_count = 0u;
}

static void orbit_ui_writer(const char *text, size_t len, void *ctx) {
    orbit_shell_t *shell = (orbit_shell_t *)ctx;
    if (!shell || !text || len == 0u) return;

    char line[ORBIT_LINE_CHARS];
    size_t ln = 0u;
    for (size_t i = 0u; i < len; i++) {
        char c = text[i];
        if (c == '\n') {
            line[ln] = '\0';
            orbit_push_line(shell, line);
            ln = 0u;
            continue;
        }
        if (ln + 1u < sizeof(line)) {
            line[ln++] = c;
        }
    }

    if (ln > 0u) {
        line[ln] = '\0';
        orbit_push_line(shell, line);
    }
}

static uint64_t orbit_exec_capture(const char *path, const char *arg_str, void *ctx) {
    orbit_shell_t *shell = (orbit_shell_t *)ctx;
    if (!shell || !path || !*path) return (uint64_t)-1;

    uint64_t pipe_fds[2] = {0, 0};
    if (peos_pipe(pipe_fds) != 0) return (uint64_t)-1;

    int64_t rd_fd = (int64_t)pipe_fds[0];
    int64_t wr_fd = (int64_t)pipe_fds[1];
    int64_t pid = peos_spawnv(path, peos_strlen(path), arg_str, -1, wr_fd, wr_fd);

    (void)peos_fclose((uint64_t)wr_fd);
    if (pid <= 0) {
        (void)peos_fclose((uint64_t)rd_fd);
        return (uint64_t)-1;
    }

    char buf[256];
    for (;;) {
        uint64_t n = peos_fread((uint64_t)rd_fd, buf, sizeof(buf));
        if ((int64_t)n < 0) {
            (void)peos_fclose((uint64_t)rd_fd);
            return (uint64_t)-1;
        }
        if (n > 0) {
            orbit_ui_writer(buf, (size_t)n, shell);
        }

        int status = 0;
        uint64_t w = peos_waitpid_flags((uint64_t)pid, &status, PEOS_WAIT_NOHANG);
        if ((int64_t)w < 0) {
            (void)peos_fclose((uint64_t)rd_fd);
            return (uint64_t)-1;
        }

        if (w == (uint64_t)pid && n == 0) {
            break;
        }

        if (n == 0) {
            peos_sleep(1);
        }
    }

    (void)peos_fclose((uint64_t)rd_fd);
    return 0;
}

static void orbit_execute_line(orbit_shell_t *shell, const char *line) {
    if (!shell || !line) return;

    char cmdline[ORBIT_LINE_CHARS];
    size_t p = 0u;
    const char *prompt = orbit_shell_prompt();
    while (prompt[p] && p + 1u < sizeof(cmdline)) {
        cmdline[p] = prompt[p];
        p++;
    }
    size_t i = 0u;
    while (line[i] && p + 1u < sizeof(cmdline)) {
        cmdline[p++] = line[i++];
    }
    cmdline[p] = '\0';
    orbit_push_line(shell, cmdline);

    orbit_shell_io_t io = {
        .write = orbit_ui_writer,
        .execv = orbit_exec_capture,
        .ctx = shell,
    };

    int clear_requested = 0;
    int exit_requested = 0;
    orbit_shell_execute_line(&io, line, &clear_requested, &exit_requested);

    if (clear_requested) {
        orbit_clear_lines(shell);
    }
    if (exit_requested) {
        orbit_push_text(shell, "Use ESC to leave Starlight.");
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

static void orbit_on_input(sl_ctx_t *ctx, const sl_input_frame_t *frame,
                           const peos_input_event_t *event) {
    (void)ctx;
    (void)frame;
    if (!event) return;

    orbit_shell_t *shell = &g_orbit;

    if (event->code == PEOS_KEY_LSHIFT || event->code == PEOS_KEY_RSHIFT) {
        shell->shift_down = event->pressed ? 1u : 0u;
        return;
    }

    if (!event->pressed) return;

    if (event->code == PEOS_KEY_ENTER) {
        shell->input[shell->input_len] = '\0';
        orbit_execute_line(shell, shell->input);
        shell->input_len = 0u;
        shell->input[0] = '\0';
        return;
    }

    if (event->code == ORBIT_KEY_BACKSPACE) {
        if (shell->input_len > 0u) {
            shell->input_len--;
            shell->input[shell->input_len] = '\0';
        }
        return;
    }

    char c = orbit_key_to_char(event->code, shell->shift_down);
    if (!c) return;

    if (shell->input_len + 1u < sizeof(shell->input)) {
        shell->input[shell->input_len++] = c;
        shell->input[shell->input_len] = '\0';
    }
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
    sl_draw_text(ctx, win->x + 6u, win->y + 6u, "Orbit", 20u, 20u, 20u);

    uint32_t text_x = body_x + 4u;
    uint32_t text_y = body_y + 4u;
    uint32_t text_w = (body_w > 8u) ? (body_w - 8u) : 0u;
    uint32_t text_h = (body_h > 8u) ? (body_h - 8u) : 0u;
    if (text_w < 8u || text_h < 16u) return;

    uint32_t cols = text_w / 8u;
    uint32_t rows = text_h / 8u;
    if (cols < 8u || rows < 2u) return;

    uint32_t history_rows = rows - 1u;
    size_t start = 0u;
    if (shell->line_count > history_rows) {
        start = shell->line_count - history_rows;
    }

    for (uint32_t r = 0u; r < history_rows; r++) {
        size_t idx = start + r;
        if (idx >= shell->line_count) break;
        sl_draw_text(ctx, text_x, text_y + (r * 8u), orbit_get_line(shell, idx), 200u, 230u, 200u);
    }

    char prompt_line[ORBIT_LINE_CHARS];
    size_t n = 0u;
    const char *prompt = orbit_shell_prompt();
    while (prompt[n] && n + 1u < sizeof(prompt_line)) {
        prompt_line[n] = prompt[n];
        n++;
    }
    for (size_t i = 0u; shell->input[i] && n + 1u < sizeof(prompt_line); i++) {
        prompt_line[n++] = shell->input[i];
    }

    uint64_t blink = (peos_time_ns() / 500000000ull) & 1ull;
    if (win->focused && blink && n + 1u < sizeof(prompt_line)) {
        prompt_line[n++] = '_';
    }
    prompt_line[n] = '\0';

    if (n > cols) {
        size_t keep = cols;
        const char *tail = prompt_line + (n - keep);
        char clipped[ORBIT_LINE_CHARS];
        orbit_copy_str(clipped, sizeof(clipped), tail);
        sl_draw_text(ctx, text_x, text_y + (history_rows * 8u), clipped, 255u, 255u, 255u);
    } else {
        sl_draw_text(ctx, text_x, text_y + (history_rows * 8u), prompt_line, 255u, 255u, 255u);
    }
}

static app_instance_t g_orbit_app = {
    .pid = 0u,
    .tid = 0u,
    .on_input = orbit_on_input,
    .on_render = orbit_on_render,
    .userdata = &g_orbit,
};

void sl_orbit_app_reset(void) {
    peos_memset(&g_orbit, 0, sizeof(g_orbit));

    orbit_shell_io_t io = {
        .write = orbit_ui_writer,
        .execv = orbit_exec_capture,
        .ctx = &g_orbit,
    };
    orbit_shell_print_banner(&io);

    g_orbit.initialized = 1u;
}

app_instance_t *sl_orbit_app_instance(void) {
    if (!g_orbit.initialized) {
        sl_orbit_app_reset();
    }
    return &g_orbit_app;
}
