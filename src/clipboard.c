#include "clipboard.h"

#include "render/pixels.h"

#include <stdlib.h>

#define SL_CLIPBOARD_MAX_TEXT 65535u
#define SL_CLIPBOARD_HISTORY 12u
#define SL_CLIPBOARD_VISIBLE 8u

typedef struct {
    char *text;
    uint32_t len;
    uint64_t created_ns;
} sl_clipboard_item_t;

typedef struct {
    sl_clipboard_item_t items[SL_CLIPBOARD_HISTORY];
    uint32_t count;
    uint32_t current_idx;
    uint8_t manager_open;
    uint32_t manager_selected;
} sl_clipboard_state_t;

static sl_clipboard_state_t g_clipboard;

void sl_clipboard_overlay_bounds(const sl_ctx_t *ctx,
                                 uint32_t *out_x, uint32_t *out_y,
                                 uint32_t *out_w, uint32_t *out_h) {
    uint32_t panel_x = 0u, panel_y = 0u, panel_w = 0u, panel_h = 0u;
    if (!ctx) goto out;
    panel_w = 420u;
    panel_h = 240u;
    if (ctx->fb.width < panel_w + 24u) panel_w = (ctx->fb.width > 24u) ? (ctx->fb.width - 24u) : ctx->fb.width;
    if (ctx->fb.height < panel_h + 24u) panel_h = (ctx->fb.height > 24u) ? (ctx->fb.height - 24u) : ctx->fb.height;
    panel_x = (ctx->fb.width > panel_w) ? ((ctx->fb.width - panel_w) / 2u) : 0u;
    panel_y = (ctx->fb.height > panel_h) ? ((ctx->fb.height - panel_h) / 2u) : 0u;
out:
    if (out_x) *out_x = panel_x;
    if (out_y) *out_y = panel_y;
    if (out_w) *out_w = panel_w;
    if (out_h) *out_h = panel_h;
}

static void sl_clipboard_item_free(sl_clipboard_item_t *item) {
    if (!item) return;
    if (item->text) free(item->text);
    item->text = NULL;
    item->len = 0u;
    item->created_ns = 0ull;
}

static uint8_t sl_clipboard_text_equals(const sl_clipboard_item_t *item,
                                        const char *text, uint32_t text_len) {
    if (!item || !item->text || !text || item->len != text_len) return 0u;
    for (uint32_t i = 0u; i < text_len; i++) {
        if (item->text[i] != text[i]) return 0u;
    }
    return 1u;
}

static char *sl_clipboard_dup_text(const char *text, uint32_t text_len) {
    char *buf;
    if (!text || text_len == 0u) return NULL;
    if (text_len > SL_CLIPBOARD_MAX_TEXT) text_len = SL_CLIPBOARD_MAX_TEXT;
    buf = (char *)malloc((size_t)text_len + 1u);
    if (!buf) return NULL;
    for (uint32_t i = 0u; i < text_len; i++) buf[i] = text[i];
    buf[text_len] = '\0';
    return buf;
}

static void sl_clipboard_promote_index(uint32_t idx) {
    sl_clipboard_item_t tmp;
    if (idx == 0u || idx >= g_clipboard.count) return;
    tmp = g_clipboard.items[idx];
    for (uint32_t i = idx; i > 0u; i--) {
        g_clipboard.items[i] = g_clipboard.items[i - 1u];
    }
    g_clipboard.items[0] = tmp;
    g_clipboard.current_idx = 0u;
}

static void sl_clipboard_compact_label(const char *src, char *out, uint32_t out_cap) {
    uint32_t n = 0u;
    if (!out || out_cap == 0u) return;
    if (!src) src = "";
    while (src[n] && n + 1u < out_cap) {
        char c = src[n];
        out[n] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        n++;
    }
    out[n] = '\0';
    if (n > 3u && src[n]) {
        out[n - 3u] = '.';
        out[n - 2u] = '.';
        out[n - 1u] = '.';
    }
}

uint8_t sl_clipboard_set_text(const char *text, uint32_t text_len) {
    char *copy;
    if (!text || text_len == 0u) return 0u;
    if (text_len > SL_CLIPBOARD_MAX_TEXT) text_len = SL_CLIPBOARD_MAX_TEXT;
    if (g_clipboard.count > 0u && sl_clipboard_text_equals(&g_clipboard.items[0], text, text_len)) {
        g_clipboard.current_idx = 0u;
        return 1u;
    }
    copy = sl_clipboard_dup_text(text, text_len);
    if (!copy) return 0u;
    if (g_clipboard.count == SL_CLIPBOARD_HISTORY) {
        sl_clipboard_item_free(&g_clipboard.items[SL_CLIPBOARD_HISTORY - 1u]);
        g_clipboard.count--;
    }
    for (uint32_t i = g_clipboard.count; i > 0u; i--) {
        g_clipboard.items[i] = g_clipboard.items[i - 1u];
    }
    g_clipboard.items[0].text = copy;
    g_clipboard.items[0].len = text_len;
    g_clipboard.items[0].created_ns = peos_time_ns();
    g_clipboard.count++;
    g_clipboard.current_idx = 0u;
    g_clipboard.manager_selected = 0u;
    return 1u;
}

const char *sl_clipboard_current_text(uint32_t *out_len) {
    if (g_clipboard.count == 0u) {
        if (out_len) *out_len = 0u;
        return NULL;
    }
    if (g_clipboard.current_idx >= g_clipboard.count) g_clipboard.current_idx = 0u;
    if (out_len) *out_len = g_clipboard.items[g_clipboard.current_idx].len;
    return g_clipboard.items[g_clipboard.current_idx].text;
}

uint8_t sl_clipboard_has_text(void) {
    return g_clipboard.count > 0u ? 1u : 0u;
}

void sl_clipboard_shutdown(void) {
    for (uint32_t i = 0u; i < g_clipboard.count; i++) {
        sl_clipboard_item_free(&g_clipboard.items[i]);
    }
    g_clipboard.count = 0u;
    g_clipboard.current_idx = 0u;
    g_clipboard.manager_open = 0u;
    g_clipboard.manager_selected = 0u;
}

uint8_t sl_clipboard_handle_input(sl_ctx_t *ctx, sl_input_frame_t *frame,
                                  uint8_t *out_changed, uint8_t *out_full_redraw) {
    uint32_t panel_x, panel_y, panel_w, panel_h, row_h, list_x, list_y, list_w;
    uint32_t visible, start_idx;
    if (out_changed) *out_changed = 0u;
    if (out_full_redraw) *out_full_redraw = 0u;
    if (!ctx || !frame) return 0u;
    if (frame->clipboard_cmds & SL_CLIP_CMD_OPEN_MANAGER) {
        frame->clipboard_cmds &= ~SL_CLIP_CMD_OPEN_MANAGER;
        g_clipboard.manager_open = 1u;
        if (g_clipboard.manager_selected >= g_clipboard.count) g_clipboard.manager_selected = 0u;
        if (out_changed) *out_changed = 1u;
        if (out_full_redraw) *out_full_redraw = 1u;
        return 1u;
    }
    if (!g_clipboard.manager_open) return 0u;

    sl_clipboard_overlay_bounds(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    row_h = 20u;
    list_x = panel_x + 12u;
    list_y = panel_y + 52u;
    list_w = panel_w - 24u;
    visible = (g_clipboard.count < SL_CLIPBOARD_VISIBLE) ? g_clipboard.count : SL_CLIPBOARD_VISIBLE;
    if (visible == 0u) visible = 1u;
    start_idx = 0u;
    if (g_clipboard.manager_selected >= visible) start_idx = g_clipboard.manager_selected - visible + 1u;
    if (start_idx + visible > g_clipboard.count && g_clipboard.count > visible) {
        start_idx = g_clipboard.count - visible;
    }

    for (uint8_t i = 0u; i < frame->key_event_count; i++) {
        peos_input_event_t *ev = &frame->key_events[i];
        uint16_t code = ev->code;
        if (!ev->pressed) continue;
        if (code == PEOS_KEY_ESC) {
            g_clipboard.manager_open = 0u;
            if (out_changed) *out_changed = 1u;
            if (out_full_redraw) *out_full_redraw = 1u;
            return 1u;
        }
        if (code == PEOS_KEY_UP || code == PEOS_KEY_UP_NORM) {
            if (g_clipboard.manager_selected > 0u) {
                g_clipboard.manager_selected--;
                if (out_changed) *out_changed = 1u;
            }
            return 1u;
        }
        if (code == PEOS_KEY_DOWN || code == PEOS_KEY_DOWN_NORM) {
            if (g_clipboard.manager_selected + 1u < g_clipboard.count) {
                g_clipboard.manager_selected++;
                if (out_changed) *out_changed = 1u;
            }
            return 1u;
        }
        if (code == PEOS_KEY_ENTER && g_clipboard.count > 0u) {
            sl_clipboard_promote_index(g_clipboard.manager_selected);
            g_clipboard.manager_open = 0u;
            if (out_changed) *out_changed = 1u;
            if (out_full_redraw) *out_full_redraw = 1u;
            return 1u;
        }
    }

    if (frame->mouse_left_pressed) {
        if (ctx->cursor_x < panel_x || ctx->cursor_x >= panel_x + panel_w ||
            ctx->cursor_y < panel_y || ctx->cursor_y >= panel_y + panel_h) {
            g_clipboard.manager_open = 0u;
            if (out_changed) *out_changed = 1u;
            if (out_full_redraw) *out_full_redraw = 1u;
            return 1u;
        }
        if (ctx->cursor_x >= list_x && ctx->cursor_x < list_x + list_w &&
            ctx->cursor_y >= list_y && ctx->cursor_y < list_y + visible * row_h) {
            uint32_t row = (ctx->cursor_y - list_y) / row_h;
            uint32_t picked = start_idx + row;
            if (picked < g_clipboard.count) {
                g_clipboard.manager_selected = picked;
                sl_clipboard_promote_index(picked);
                g_clipboard.manager_open = 0u;
                if (out_changed) *out_changed = 1u;
                if (out_full_redraw) *out_full_redraw = 1u;
            }
            return 1u;
        }
    }

    return 1u;
}

void sl_clipboard_render_overlay(sl_ctx_t *ctx) {
    uint32_t panel_x, panel_y, panel_w, panel_h, row_h, visible, start_idx;
    uint32_t text_y, current_idx, list_y;
    char line[56];
    if (!ctx || !g_clipboard.manager_open) return;

    sl_clipboard_overlay_bounds(ctx, &panel_x, &panel_y, &panel_w, &panel_h);
    row_h = 20u;
    visible = (g_clipboard.count < SL_CLIPBOARD_VISIBLE) ? g_clipboard.count : SL_CLIPBOARD_VISIBLE;
    if (visible == 0u) visible = 1u;
    start_idx = 0u;
    if (g_clipboard.manager_selected >= visible) start_idx = g_clipboard.manager_selected - visible + 1u;
    if (start_idx + visible > g_clipboard.count && g_clipboard.count > visible) {
        start_idx = g_clipboard.count - visible;
    }

    sl_fill_rect_a(ctx, 0u, 0u, ctx->fb.width, ctx->fb.height, 0u, 0u, 0u, 112u);
    sl_fill_rect(ctx, panel_x, panel_y, panel_w, panel_h, 18u, 20u, 26u);
    sl_fill_rect(ctx, panel_x, panel_y, panel_w, 1u, 90u, 110u, 140u);
    sl_fill_rect(ctx, panel_x, panel_y + panel_h - 1u, panel_w, 1u, 8u, 8u, 10u);
    sl_fill_rect(ctx, panel_x, panel_y, 1u, panel_h, 90u, 110u, 140u);
    sl_fill_rect(ctx, panel_x + panel_w - 1u, panel_y, 1u, panel_h, 8u, 8u, 10u);

    sl_draw_text(ctx, panel_x + 12u, panel_y + 10u, "Clipboard", 230u, 236u, 245u);
    sl_draw_text(ctx, panel_x + 12u, panel_y + 28u,
                 "Caps+A copia, Caps+S incolla, Invio seleziona",
                 150u, 164u, 184u);

    list_y = panel_y + 52u;
    current_idx = g_clipboard.current_idx;
    for (uint32_t row = 0u; row < visible; row++) {
        uint32_t idx = start_idx + row;
        uint32_t iy = list_y + row * row_h;
        if (idx >= g_clipboard.count) break;
        if (idx == g_clipboard.manager_selected) {
            sl_fill_rect(ctx, panel_x + 10u, iy, panel_w - 20u, row_h - 1u, 52u, 84u, 130u);
        } else if (idx == current_idx) {
            sl_fill_rect(ctx, panel_x + 10u, iy, panel_w - 20u, row_h - 1u, 28u, 40u, 56u);
        }
        text_y = iy + 6u;
        sl_clipboard_compact_label(g_clipboard.items[idx].text, line, sizeof(line));
        sl_draw_text(ctx, panel_x + 16u, text_y, line, 230u, 236u, 245u);
    }

    if (g_clipboard.count == 0u) {
        sl_draw_text(ctx, panel_x + 16u, panel_y + 70u, "Clipboard vuota", 150u, 164u, 184u);
    }
}
