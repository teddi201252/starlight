#include "keyboard_layout.h"

#include "resources.h"

#include "peos/peos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SL_KEYMAP_KEYS 256u
#define SL_KEYMAP_LEVELS 4u
#define SL_KEYMAP_TEXT_MAX 8u
#define SL_KEYMAP_LINE_MAX 192u
#define SL_KEYMAP_PATH_MAX 192u
#define SL_KEYMAP_FILE_MAX 8192u

typedef struct {
    char text[SL_KEYMAP_LEVELS][SL_KEYMAP_TEXT_MAX];
} sl_keymap_entry_t;

static sl_keymap_entry_t g_keymap[SL_KEYMAP_KEYS];

static uint8_t sl_keymap_entry_is_ascii_alpha(const char *text) {
    return (text && text[0] && text[1] == '\0' &&
            ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'))) ? 1u : 0u;
}

static uint8_t sl_keymap_is_alpha_key(uint16_t code) {
    const char *base = NULL;
    const char *shift = NULL;
    if (code >= SL_KEYMAP_KEYS) return 0u;
    base = g_keymap[code].text[0];
    shift = g_keymap[code].text[1];
    if (!sl_keymap_entry_is_ascii_alpha(base) || !sl_keymap_entry_is_ascii_alpha(shift)) return 0u;
    if ((base[0] >= 'a' && base[0] <= 'z') &&
        (shift[0] >= 'A' && shift[0] <= 'Z') &&
        (uint8_t)(shift[0] - 'A') == (uint8_t)(base[0] - 'a')) {
        return 1u;
    }
    if ((base[0] >= 'A' && base[0] <= 'Z') &&
        (shift[0] >= 'a' && shift[0] <= 'z') &&
        (uint8_t)(base[0] - 'A') == (uint8_t)(shift[0] - 'a')) {
        return 1u;
    }
    return 0u;
}

static void sl_keymap_clear(void) {
    peos_memset(g_keymap, 0, sizeof(g_keymap));
}

static void sl_keymap_set(uint16_t code, uint8_t level, const char *text) {
    if (code >= SL_KEYMAP_KEYS || level >= SL_KEYMAP_LEVELS) return;
    char *dst = g_keymap[code].text[level];
    size_t i = 0u;
    if (!text || !text[0] || strcmp(text, "none") == 0) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < SL_KEYMAP_TEXT_MAX && text[i]) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = '\0';
}

static void sl_keymap_set_pair(uint16_t code, const char *normal, const char *shift) {
    sl_keymap_set(code, 0u, normal);
    sl_keymap_set(code, 1u, shift);
}

static void sl_keymap_init_us(void) {
    sl_keymap_clear();

    sl_keymap_set_pair(0x02u, "1", "!");
    sl_keymap_set_pair(0x03u, "2", "@");
    sl_keymap_set_pair(0x04u, "3", "#");
    sl_keymap_set_pair(0x05u, "4", "$");
    sl_keymap_set_pair(0x06u, "5", "%");
    sl_keymap_set_pair(0x07u, "6", "^");
    sl_keymap_set_pair(0x08u, "7", "&");
    sl_keymap_set_pair(0x09u, "8", "*");
    sl_keymap_set_pair(0x0Au, "9", "(");
    sl_keymap_set_pair(0x0Bu, "0", ")");
    sl_keymap_set_pair(0x0Cu, "-", "_");
    sl_keymap_set_pair(0x0Du, "=", "+");
    sl_keymap_set_pair(0x10u, "q", "Q");
    sl_keymap_set_pair(0x11u, "w", "W");
    sl_keymap_set_pair(0x12u, "e", "E");
    sl_keymap_set_pair(0x13u, "r", "R");
    sl_keymap_set_pair(0x14u, "t", "T");
    sl_keymap_set_pair(0x15u, "y", "Y");
    sl_keymap_set_pair(0x16u, "u", "U");
    sl_keymap_set_pair(0x17u, "i", "I");
    sl_keymap_set_pair(0x18u, "o", "O");
    sl_keymap_set_pair(0x19u, "p", "P");
    sl_keymap_set_pair(0x1Au, "[", "{");
    sl_keymap_set_pair(0x1Bu, "]", "}");
    sl_keymap_set_pair(0x1Eu, "a", "A");
    sl_keymap_set_pair(0x1Fu, "s", "S");
    sl_keymap_set_pair(0x20u, "d", "D");
    sl_keymap_set_pair(0x21u, "f", "F");
    sl_keymap_set_pair(0x22u, "g", "G");
    sl_keymap_set_pair(0x23u, "h", "H");
    sl_keymap_set_pair(0x24u, "j", "J");
    sl_keymap_set_pair(0x25u, "k", "K");
    sl_keymap_set_pair(0x26u, "l", "L");
    sl_keymap_set_pair(0x27u, ";", ":");
    sl_keymap_set_pair(0x28u, "'", "\"");
    sl_keymap_set_pair(0x29u, "`", "~");
    sl_keymap_set_pair(0x2Bu, "\\", "|");
    sl_keymap_set_pair(0x2Cu, "z", "Z");
    sl_keymap_set_pair(0x2Du, "x", "X");
    sl_keymap_set_pair(0x2Eu, "c", "C");
    sl_keymap_set_pair(0x2Fu, "v", "V");
    sl_keymap_set_pair(0x30u, "b", "B");
    sl_keymap_set_pair(0x31u, "n", "N");
    sl_keymap_set_pair(0x32u, "m", "M");
    sl_keymap_set_pair(0x33u, ",", "<");
    sl_keymap_set_pair(0x34u, ".", ">");
    sl_keymap_set_pair(0x35u, "/", "?");
    sl_keymap_set_pair(0x39u, " ", " ");
}

static char *sl_keymap_trim(char *s) {
    size_t n = 0u;
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    n = strlen(s);
    while (n > 0u) {
        char c = s[n - 1u];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[n - 1u] = '\0';
        n--;
    }
    return s;
}

static int sl_keymap_hex_nib(char c, uint32_t *out) {
    if (!out) return 0;
    if (c >= '0' && c <= '9') {
        *out = (uint32_t)(c - '0');
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (uint32_t)(10 + (c - 'a'));
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint32_t)(10 + (c - 'A'));
        return 1;
    }
    return 0;
}

static int sl_keymap_parse_hex(const char *s, uint32_t *out) {
    uint32_t v = 0u;
    if (!s || !s[0] || !out) return 0;
    while (*s) {
        uint32_t nib = 0u;
        if (!sl_keymap_hex_nib(*s, &nib)) return 0;
        if (v > 0x0FFFFFFFu) return 0;
        v = (v << 4u) | nib;
        s++;
    }
    *out = v;
    return 1;
}

static size_t sl_keymap_utf8_encode(uint32_t cp, char *out, size_t out_cap) {
    if (!out || out_cap == 0u) return 0u;
    if (cp <= 0x7Fu) {
        if (out_cap < 2u) return 0u;
        out[0] = (char)cp;
        out[1] = '\0';
        return 1u;
    }
    if (cp <= 0x7FFu) {
        if (out_cap < 3u) return 0u;
        out[0] = (char)(0xC0u | ((cp >> 6u) & 0x1Fu));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
        return 2u;
    }
    if (cp <= 0xFFFFu) {
        if (out_cap < 4u) return 0u;
        out[0] = (char)(0xE0u | ((cp >> 12u) & 0x0Fu));
        out[1] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
        return 3u;
    }
    if (cp <= 0x10FFFFu) {
        if (out_cap < 5u) return 0u;
        out[0] = (char)(0xF0u | ((cp >> 18u) & 0x07u));
        out[1] = (char)(0x80u | ((cp >> 12u) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
        return 4u;
    }
    return 0u;
}

static int sl_keymap_token_to_text(const char *tok, char *out, size_t out_cap) {
    uint32_t cp = 0u;
    size_t i = 0u;
    if (!tok || !out || out_cap == 0u) return 0;
    out[0] = '\0';
    if (!tok[0] || strcmp(tok, "none") == 0) return 1;
    if (strcmp(tok, "space") == 0 || strcmp(tok, "SPACE") == 0) {
        if (out_cap < 2u) return 0;
        out[0] = ' ';
        out[1] = '\0';
        return 1;
    }
    if (strcmp(tok, "tab") == 0 || strcmp(tok, "TAB") == 0) {
        if (out_cap < 2u) return 0;
        out[0] = '\t';
        out[1] = '\0';
        return 1;
    }
    if ((tok[0] == 'U' || tok[0] == 'u') && tok[1] == '+') {
        if (!sl_keymap_parse_hex(tok + 2, &cp)) return 0;
        return sl_keymap_utf8_encode(cp, out, out_cap) > 0u;
    }
    while (i + 1u < out_cap && tok[i]) {
        out[i] = tok[i];
        i++;
    }
    out[i] = '\0';
    return 1;
}

static char *sl_keymap_load_text_file(const char *path, size_t max_bytes) {
    FILE *f = NULL;
    long sz = 0;
    char *buf = NULL;
    size_t got = 0u;
    if (!path || !path[0] || max_bytes == 0u) return NULL;

    f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || (size_t)sz > max_bytes) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static void sl_keymap_resolve_path(const char *name_or_path, char *out, size_t out_cap) {
    size_t i = 0u;
    size_t n = 0u;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!name_or_path || !name_or_path[0]) return;
    if (name_or_path[0] == '/') {
        while (i + 1u < out_cap && name_or_path[i]) {
            out[i] = name_or_path[i];
            i++;
        }
        out[i] = '\0';
        return;
    }
    n = strlen(name_or_path);
    if (n > 7u && strcmp(name_or_path + n - 7u, ".keymap") == 0) {
        (void)snprintf(out, out_cap, "%s/%s", SL_RESOURCE_KEYMAP_DIR, name_or_path);
        return;
    }
    (void)snprintf(out, out_cap, "%s/%s.keymap", SL_RESOURCE_KEYMAP_DIR, name_or_path);
}

static uint16_t sl_keymap_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) return (uint16_t)(code & 0x00FFu);
    return code;
}

static void sl_keymap_parse_line(char *line) {
    char *p = sl_keymap_trim(line);
    char *eq = NULL;
    uint32_t code32 = 0u;
    uint16_t code = 0u;
    uint8_t level = 0u;
    char *scan = NULL;

    if (!p[0] || p[0] == '#' || p[0] == ';') return;
    eq = strchr(p, '=');
    if (!eq) return;
    *eq = '\0';
    char *key = sl_keymap_trim(p);
    char *val = sl_keymap_trim(eq + 1);

    if (strncmp(key, "key.", 4) != 0) return;
    if (!sl_keymap_parse_hex(key + 4, &code32) || code32 >= SL_KEYMAP_KEYS) return;
    code = (uint16_t)code32;

    scan = val;
    while (*scan && level < SL_KEYMAP_LEVELS) {
        char *tok = NULL;
        while (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n') scan++;
        if (*scan == '\0' || *scan == '#' || *scan == ';') break;
        tok = scan;
        while (*scan && *scan != ' ' && *scan != '\t' && *scan != '\r' && *scan != '\n') scan++;
        if (*scan) {
            *scan = '\0';
            scan++;
        }
        char text[SL_KEYMAP_TEXT_MAX];
        if (sl_keymap_token_to_text(tok, text, sizeof(text))) {
            sl_keymap_set(code, level, text);
        }
        level++;
    }
}

int sl_keyboard_layout_load(const char *name_or_path) {
    char path[SL_KEYMAP_PATH_MAX];
    char *cfg = NULL;
    char line[SL_KEYMAP_LINE_MAX];
    size_t li = 0u;

    sl_keymap_init_us();
    if (!name_or_path || !name_or_path[0] || strcmp(name_or_path, "us") == 0) return 1;

    sl_keymap_resolve_path(name_or_path, path, sizeof(path));
    cfg = sl_keymap_load_text_file(path, SL_KEYMAP_FILE_MAX);
    if (!cfg) return 0;

    for (size_t i = 0u;; i++) {
        char c = cfg[i];
        if (c != '\0' && c != '\n') {
            if (li + 1u < sizeof(line)) line[li++] = c;
            continue;
        }
        line[li] = '\0';
        li = 0u;
        sl_keymap_parse_line(line);
        if (c == '\0') break;
    }

    free(cfg);
    return 1;
}

size_t sl_keyboard_translate(uint16_t keycode, uint32_t mods, char *out, size_t out_cap) {
    uint16_t code = sl_keymap_norm_keycode(keycode);
    uint8_t shift = (mods & SL_KEYMOD_SHIFT) ? 1u : 0u;
    uint8_t altgr = (mods & SL_KEYMOD_ALTGR) ? 1u : 0u;
    uint8_t level = altgr ? (uint8_t)(shift ? 3u : 2u) : (uint8_t)(shift ? 1u : 0u);
    const char *src = NULL;
    size_t n = 0u;
    if (!out || out_cap == 0u) return 0u;
    out[0] = '\0';
    if (code >= SL_KEYMAP_KEYS) return 0u;

    src = g_keymap[code].text[level];
    if (!src[0] && altgr && shift) src = g_keymap[code].text[2];
    if (!src[0]) return 0u;
    while (n + 1u < out_cap && src[n]) {
        out[n] = src[n];
        n++;
    }
    out[n] = '\0';
    return n;
}

size_t sl_keyboard_translate_event(const peos_input_event_t *event,
                                   uint8_t shift_down,
                                   uint8_t caps_lock_on,
                                   uint8_t altgr_down,
                                   char *out,
                                   size_t out_cap) {
    uint32_t mods = 0u;
    uint16_t code = 0u;
    uint8_t effective_shift = shift_down ? 1u : 0u;
    if (!event || !event->pressed) {
        if (out && out_cap > 0u) out[0] = '\0';
        return 0u;
    }
    code = sl_keymap_norm_keycode(event->code);
    if (caps_lock_on && !altgr_down && sl_keymap_is_alpha_key(code)) {
        effective_shift ^= 1u;
    }
    if (effective_shift) mods |= SL_KEYMOD_SHIFT;
    if (altgr_down) mods |= SL_KEYMOD_ALTGR;
    return sl_keyboard_translate(code, mods, out, out_cap);
}

uint8_t sl_keyboard_event_is_altgr(const peos_input_event_t *event) {
    return (event && event->code == PEOS_KEY_RALT) ? 1u : 0u;
}
