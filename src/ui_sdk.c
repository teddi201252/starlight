#include "ui_sdk.h"

#include "keyboard_layout.h"

static sl_ui_theme_t g_runtime_theme;
static uint32_t g_runtime_theme_version = 1u;
static uint8_t g_runtime_theme_initialized = 0u;

static sl_ui_color_t sl_ui_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    sl_ui_color_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

void sl_ui_theme_set_default(sl_ui_theme_t *theme) {
    if (!theme) return;
    theme->colors[SL_UI_COLOR_BG_CANVAS] = sl_ui_rgba(26u, 26u, 26u, 255u);
    theme->colors[SL_UI_COLOR_BG_PANEL] = sl_ui_rgba(38u, 38u, 38u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL] = sl_ui_rgba(100u, 100u, 100u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_HOVER] = sl_ui_rgba(120u, 120u, 120u, 255u);
    theme->colors[SL_UI_COLOR_BG_CONTROL_ACTIVE] = sl_ui_rgba(80u, 80u, 80u, 255u);
    theme->colors[SL_UI_COLOR_BORDER] = sl_ui_rgba(130u, 130u, 130u, 255u);
    theme->colors[SL_UI_COLOR_BORDER_FOCUS] = sl_ui_rgba(120u, 200u, 255u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_PRIMARY] = sl_ui_rgba(240u, 240u, 240u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_SECONDARY] = sl_ui_rgba(184u, 184u, 184u, 255u);
    theme->colors[SL_UI_COLOR_TEXT_DISABLED] = sl_ui_rgba(150u, 150u, 150u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT] = sl_ui_rgba(70u, 150u, 80u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_HOVER] = sl_ui_rgba(90u, 170u, 100u, 255u);
    theme->colors[SL_UI_COLOR_ACCENT_ACTIVE] = sl_ui_rgba(54u, 132u, 64u, 255u);
    theme->colors[SL_UI_COLOR_DANGER] = sl_ui_rgba(180u, 62u, 62u, 255u);
    theme->colors[SL_UI_COLOR_SUCCESS] = sl_ui_rgba(88u, 168u, 96u, 255u);
    theme->colors[SL_UI_COLOR_WARNING] = sl_ui_rgba(190u, 148u, 66u, 255u);
}

void sl_ui_theme_apply(sl_ui_ctx_t *ui, const sl_ui_theme_t *theme) {
    if (!ui || !theme) return;
    ui->theme = *theme;
    ui->theme_initialized = 1u;
    ui->theme_version = g_runtime_theme_version;
}

void sl_ui_theme_reset(sl_ui_ctx_t *ui) {
    if (!ui) return;
    sl_ui_theme_set_default(&ui->theme);
    ui->theme_initialized = 1u;
    ui->theme_version = g_runtime_theme_version;
}

void sl_ui_theme_set_accent(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b) {
    sl_ui_theme_set_accent_a(ui, r, g, b, 255u);
}

void sl_ui_theme_set_accent_a(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ui) return;
    if (!ui->theme_initialized) sl_ui_theme_reset(ui);
    ui->theme.colors[SL_UI_COLOR_ACCENT] = sl_ui_rgba(r, g, b, a);
    ui->theme.colors[SL_UI_COLOR_ACCENT_HOVER] = sl_ui_rgba(
        (uint8_t)((r + 28u > 255u) ? 255u : r + 28u),
        (uint8_t)((g + 28u > 255u) ? 255u : g + 28u),
        (uint8_t)((b + 28u > 255u) ? 255u : b + 28u), a);
    ui->theme.colors[SL_UI_COLOR_ACCENT_ACTIVE] = sl_ui_rgba(
        (uint8_t)((r > 24u) ? (r - 24u) : 0u),
        (uint8_t)((g > 24u) ? (g - 24u) : 0u),
        (uint8_t)((b > 24u) ? (b - 24u) : 0u), a);
}

const sl_ui_color_t *sl_ui_theme_color(const sl_ui_ctx_t *ui, sl_ui_color_token_t token) {
    static const sl_ui_color_t fallback = {255u, 0u, 255u, 255u};
    if (!ui || token >= SL_UI_COLOR_COUNT) return &fallback;
    return &ui->theme.colors[token];
}

void sl_ui_theme_runtime_set(const sl_ui_theme_t *theme) {
    if (!theme) return;
    g_runtime_theme = *theme;
    g_runtime_theme_initialized = 1u;
    g_runtime_theme_version++;
    if (g_runtime_theme_version == 0u) g_runtime_theme_version = 1u;
}

void sl_ui_theme_runtime_get(sl_ui_theme_t *out) {
    if (!out) return;
    if (!g_runtime_theme_initialized) {
        sl_ui_theme_set_default(&g_runtime_theme);
        g_runtime_theme_initialized = 1u;
    }
    *out = g_runtime_theme;
}

static void sl_ui_fill_tok(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h, sl_ui_color_token_t token) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    if (c->a >= 255u) sl_fill_rect(ui->ctx, x, y, w, h, c->r, c->g, c->b);
    else sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, c->a);
}

static void sl_ui_fill_selection(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, SL_UI_COLOR_ACCENT);
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, 96u);
}

static void sl_ui_text_tok(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text, sl_ui_color_token_t token) {
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    sl_draw_text_px(ui->ctx, x, y, text, c->r, c->g, c->b, ui->font_px);
}

static uint32_t sl_ui_font_w(const sl_ui_ctx_t *ui) {
    uint32_t w = sl_text_cell_w_px(ui ? ui->font_px : 0u);
    return w ? w : 8u;
}

static uint32_t sl_ui_font_h(const sl_ui_ctx_t *ui) {
    uint32_t h = sl_text_cell_h_px(ui ? ui->font_px : 0u);
    return h ? h : 8u;
}

static uint32_t sl_ui_text_center_y(const sl_ui_ctx_t *ui, uint32_t y, uint32_t h) {
    uint32_t fh = sl_ui_font_h(ui);
    return y + ((h > fh) ? ((h - fh) / 2u) : 0u);
}

static void sl_ui_selection_collapse(sl_ui_ctx_t *ui, uint32_t id, uint32_t pos) {
    if (!ui) return;
    ui->text_select_id = id;
    ui->text_select_anchor_pos = pos;
    ui->text_select_focus_pos = pos;
}

static uint8_t sl_ui_selection_range(const sl_ui_ctx_t *ui, uint32_t id, uint32_t *out_start, uint32_t *out_end) {
    uint32_t a = 0u, b = 0u;
    if (!ui || ui->text_select_id != id) return 0u;
    a = ui->text_select_anchor_pos;
    b = ui->text_select_focus_pos;
    if (a == b) return 0u;
    if (a > b) {
        uint32_t tmp = a;
        a = b;
        b = tmp;
    }
    if (out_start) *out_start = a;
    if (out_end) *out_end = b;
    return 1u;
}

static void sl_ui_move_caret(sl_ui_ctx_t *ui, uint32_t id, uint32_t old_pos, uint32_t new_pos, uint8_t extend_selection) {
    if (!ui) return;
    ui->text_caret_id = id;
    ui->text_caret_pos = new_pos;
    if (extend_selection) {
        if (ui->text_select_id != id || ui->text_select_anchor_pos == ui->text_select_focus_pos) {
            ui->text_select_id = id;
            ui->text_select_anchor_pos = old_pos;
        }
        ui->text_select_focus_pos = new_pos;
        return;
    }
    sl_ui_selection_collapse(ui, id, new_pos);
}

static uint8_t sl_ui_text_delete_selection(sl_ui_ctx_t *ui, uint32_t id,
                                           char *text, uint32_t *len, uint32_t *caret);
static uint8_t sl_ui_text_apply_paste(sl_ui_ctx_t *ui, uint32_t id,
                                      char *text, uint32_t *len, uint32_t text_cap, uint32_t *caret);
static uint8_t sl_ui_utf8_is_cont(char c);
static uint32_t sl_ui_utf8_count_between(const char *text, uint32_t start, uint32_t end);
static uint32_t sl_ui_utf8_prev_pos(const char *text, uint32_t pos);
static uint32_t sl_ui_utf8_advance_cols(const char *text, uint32_t pos, uint32_t len, uint32_t cols);
static uint32_t sl_ui_text_line_col_at(const char *text, uint32_t pos, uint32_t *col_out);

static uint8_t sl_ui_clipboard_copy_selection(const sl_ui_ctx_t *ui, uint32_t id, const char *text) {
    uint32_t sel_start = 0u, sel_end = 0u;
    if (!ui || !text) return 0u;
    if (!sl_ui_selection_range(ui, id, &sel_start, &sel_end)) return 0u;
    return sl_clipboard_set_text(text + sel_start, sel_end - sel_start);
}

static void sl_ui_text_input_copy_point(const sl_ui_ctx_t *ui,
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                        const char *text, uint32_t len,
                                        int32_t *out_x, int32_t *out_y) {
    uint32_t fw = 0u, vis_chars = 0u, start = 0u, caret_draw = 0u, text_x = 0u, text_y = 0u;
    uint32_t input_caret_pos = 0u;
    if (!ui || !out_x || !out_y) return;
    fw = sl_ui_font_w(ui);
    vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    text_x = x + 6u;
    text_y = sl_ui_text_center_y(ui, y, h);
    input_caret_pos = ui->text_caret_pos;
    start = ui->text_input_scroll_col;
    if (start > len) start = len;
    if (start > 0u && sl_ui_utf8_is_cont(text[start])) start = sl_ui_utf8_prev_pos(text, start);
    if (input_caret_pos < start) input_caret_pos = start;
    caret_draw = sl_ui_utf8_count_between(text, start, input_caret_pos);
    if (vis_chars > 0u && caret_draw > vis_chars) caret_draw = vis_chars;
    *out_x = (int32_t)(text_x + (caret_draw * fw));
    *out_y = (int32_t)(text_y + (sl_ui_font_h(ui) / 2u));
}

static void sl_ui_text_area_copy_point(const sl_ui_ctx_t *ui,
                                       uint32_t x, uint32_t y,
                                       const char *text,
                                       int32_t *out_x, int32_t *out_y) {
    uint32_t caret_col = 0u, caret_line = 0u, fw = 0u, fh = 0u;
    uint32_t draw_col = 0u, draw_line = 0u;
    if (!ui || !out_x || !out_y) return;
    fw = sl_ui_font_w(ui);
    fh = sl_ui_font_h(ui);
    caret_line = sl_ui_text_line_col_at(text, ui->text_caret_pos, &caret_col);
    draw_col = (caret_col > ui->text_area_scroll_col) ? (caret_col - ui->text_area_scroll_col) : 0u;
    draw_line = (caret_line > ui->text_area_scroll_line) ? (caret_line - ui->text_area_scroll_line) : 0u;
    *out_x = (int32_t)(x + 6u + (draw_col * fw));
    *out_y = (int32_t)(y + 6u + (draw_line * fh) + (fh / 2u));
}

static void sl_ui_clipboard_notify_copy_feedback(const sl_ui_ctx_t *ui,
                                                 const char *text,
                                                 uint32_t sel_start, uint32_t sel_end,
                                                 int32_t start_x, int32_t start_y) {
    if (!ui || !ui->ctx || !text || sel_end <= sel_start) return;
    sl_clipboard_notify_copy(ui->ctx, text + sel_start, sel_end - sel_start,
                             start_x, start_y,
                             (int32_t)ui->ctx->cursor_x, (int32_t)ui->ctx->cursor_y);
}

static uint32_t sl_ui_text_input_hit_pos(sl_ui_ctx_t *ui,
                                         uint32_t x, uint32_t w,
                                         const char *text, uint32_t len) {
    uint32_t fw, vis_chars, start, max_scroll_col;
    if (!ui || !text) return 0u;
    fw = sl_ui_font_w(ui);
    vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    start = ui->text_input_scroll_col;
    max_scroll_col = (len > vis_chars) ? (len - vis_chars) : 0u;
    if (start > max_scroll_col) start = max_scroll_col;
    if (start > 0u && sl_ui_utf8_is_cont(text[start])) start = sl_ui_utf8_prev_pos(text, start);
    if (ui->cursor_x <= x + 6u) return start;
    if (vis_chars == 0u) return start;
    {
        uint32_t rel = ui->cursor_x - (x + 6u);
        uint32_t col = rel / fw;
        if (col > vis_chars) col = vis_chars;
        return sl_ui_utf8_advance_cols(text, start, len, col);
    }
}

void sl_ui_set_font_px(sl_ui_ctx_t *ui, uint32_t font_px) {
    if (!ui) return;
    ui->font_px = font_px;
}

uint32_t sl_ui_get_font_px(const sl_ui_ctx_t *ui) {
    if (!ui) return 0u;
    return ui->font_px;
}

static sl_ui_color_token_t sl_ui_state_fill_token(sl_ui_control_state_t state) {
    switch (state) {
        case SL_UI_STATE_HOVER: return SL_UI_COLOR_BG_CONTROL_HOVER;
        case SL_UI_STATE_ACTIVE: return SL_UI_COLOR_BG_CONTROL_ACTIVE;
        case SL_UI_STATE_DISABLED: return SL_UI_COLOR_BG_PANEL;
        case SL_UI_STATE_FOCUSED: return SL_UI_COLOR_BG_PANEL;
        case SL_UI_STATE_NORMAL:
        default: return SL_UI_COLOR_BG_CONTROL;
    }
}

static sl_ui_color_token_t sl_ui_state_text_token(sl_ui_control_state_t state) {
    return (state == SL_UI_STATE_DISABLED) ? SL_UI_COLOR_TEXT_DISABLED : SL_UI_COLOR_TEXT_PRIMARY;
}

static sl_ui_color_token_t sl_ui_state_border_token(sl_ui_control_state_t state) {
    return (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BORDER_FOCUS : SL_UI_COLOR_BORDER;
}

static int sl_ui_point_in_rect(uint32_t px, uint32_t py,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0u || h == 0u) return 0;
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released,
                 int32_t wheel_x, int32_t wheel_y,
                 uint32_t clipboard_cmds, uint8_t caps_lock_on) {
    if (!ui) return;
    ui->ctx = ctx;
    ui->win = win;
    ui->cursor_x = cursor_x;
    ui->cursor_y = cursor_y;
    ui->mouse_down = mouse_down;
    ui->mouse_pressed = mouse_pressed;
    ui->mouse_released = mouse_released;
    ui->wheel_x = wheel_x;
    ui->wheel_y = wheel_y;
    ui->clipboard_cmds = clipboard_cmds;
    ui->caps_lock_on = caps_lock_on;
    ui->wheel_consumed = 0u;
    ui->select_overlay_pending = 0u;
    ui->select_overlay_options_pipe = NULL;
    if (!g_runtime_theme_initialized) {
        sl_ui_theme_set_default(&g_runtime_theme);
        g_runtime_theme_initialized = 1u;
    }
    if (!ui->theme_initialized || ui->theme_version != g_runtime_theme_version) {
        ui->theme = g_runtime_theme;
        ui->theme_initialized = 1u;
        ui->theme_version = g_runtime_theme_version;
    }
}

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 sl_ui_color_token_t token) {
    if (!ui || !ui->ctx || !text) return;
    sl_ui_text_tok(ui, x, y, text, token);
}

void sl_ui_label_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || !text) return;
    sl_draw_text_px(ui->ctx, x, y, text, r, g, b, ui->font_px);
}

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text) {
    return sl_ui_button_state(ui, id, x, y, w, h, text, 1u);
}

uint8_t sl_ui_button_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !text || w < 8u || h < 8u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) {
        ui->active_id = id;
    }

    int active = (ui->active_id == id);
    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;

    sl_ui_fill_tok(ui, x, y, w, h, sl_ui_state_fill_token(state));
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, SL_UI_COLOR_BG_CANVAS);
    sl_ui_text_tok(ui, x + 6u, sl_ui_text_center_y(ui, y, h), text, sl_ui_state_text_token(state));
    if (!enabled && active) ui->active_id = 0u;

    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        return hovered ? 1u : 0u;
    }

    return 0u;
}

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value) {
    return sl_ui_slider_u32_state(ui, id, x, y, w, min_v, max_v, value, 1u);
}

uint8_t sl_ui_slider_u32_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w,
                               uint32_t min_v, uint32_t max_v, uint32_t *value,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 24u || min_v > max_v) return 0u;

    uint32_t h = 16u;
    uint32_t knob_w = 10u;
    uint32_t track_x = x + 2u;
    uint32_t track_w = w - 4u;

    if (enabled && sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h) && ui->mouse_pressed) {
        ui->active_id = id;
    }

    uint8_t changed = 0u;
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t cx = ui->cursor_x;
        if (cx < track_x) cx = track_x;
        if (cx > track_x + track_w) cx = track_x + track_w;

        uint32_t range = max_v - min_v;
        uint32_t nv = min_v;
        if (range > 0u && track_w > 0u) {
            nv = min_v + (uint32_t)(((uint64_t)(cx - track_x) * (uint64_t)range) / (uint64_t)track_w);
        }
        if (nv != *value) {
            *value = nv;
            changed = 1u;
        }
    }

    if (ui->mouse_released && ui->active_id == id) {
        ui->active_id = 0u;
    }

    sl_ui_control_state_t state = enabled ? SL_UI_STATE_NORMAL : SL_UI_STATE_DISABLED;
    if (enabled && ui->active_id == id && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    sl_ui_fill_tok(ui, x, y + 6u, w, 4u, (state == SL_UI_STATE_DISABLED) ? SL_UI_COLOR_BG_CANVAS : SL_UI_COLOR_BG_CONTROL);

    uint32_t knob_x = track_x;
    if (max_v > min_v) {
        uint32_t range = max_v - min_v;
        uint32_t rel = *value;
        if (rel < min_v) rel = min_v;
        if (rel > max_v) rel = max_v;
        rel -= min_v;
        knob_x = track_x + (uint32_t)(((uint64_t)rel * (uint64_t)track_w) / (uint64_t)range);
    }
    if (knob_x + knob_w > x + w) knob_x = (x + w > knob_w) ? (x + w - knob_w) : x;

    sl_ui_fill_tok(ui, knob_x, y + 2u, knob_w, 12u,
                   (state == SL_UI_STATE_DISABLED) ? SL_UI_COLOR_TEXT_DISABLED : SL_UI_COLOR_ACCENT);
    return changed;
}

static uint16_t sl_ui_norm_keycode(uint16_t code) {
    if ((code & 0xFF00u) == 0xE000u) {
        return (uint16_t)(code & 0x00FFu);
    }
    return code;
}

static uint32_t sl_ui_strlen(const char *s) {
    uint32_t n = 0u;
    if (!s) return 0u;
    while (s[n]) n++;
    return n;
}

static uint8_t sl_ui_utf8_is_cont(char c) {
    return (((uint8_t)c & 0xC0u) == 0x80u) ? 1u : 0u;
}

static uint32_t sl_ui_utf8_prev_pos(const char *text, uint32_t pos) {
    if (!text || pos == 0u) return 0u;
    pos--;
    while (pos > 0u && sl_ui_utf8_is_cont(text[pos])) pos--;
    return pos;
}

static uint32_t sl_ui_utf8_next_pos(const char *text, uint32_t pos, uint32_t len) {
    if (!text || pos >= len) return len;
    pos++;
    while (pos < len && sl_ui_utf8_is_cont(text[pos])) pos++;
    return pos;
}

static uint32_t sl_ui_utf8_count_between(const char *text, uint32_t start, uint32_t end) {
    uint32_t count = 0u;
    if (!text) return 0u;
    while (start < end && text[start]) {
        start = sl_ui_utf8_next_pos(text, start, end);
        count++;
    }
    return count;
}

static uint32_t sl_ui_utf8_advance_cols(const char *text, uint32_t pos, uint32_t len, uint32_t cols) {
    while (cols > 0u && pos < len && text[pos]) {
        pos = sl_ui_utf8_next_pos(text, pos, len);
        cols--;
    }
    return pos;
}

uint8_t sl_ui_toggle(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint8_t *value, const char *text) {
    return sl_ui_toggle_state(ui, id, x, y, w, h, value, text, 1u);
}

uint8_t sl_ui_toggle_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint8_t *value, const char *text, uint8_t enabled) {
    if (!ui || !ui->ctx || !value || w < 20u || h < 12u) return 0u;
    if (!text) text = "";

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            *value = *value ? 0u : 1u;
            changed = 1u;
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, sl_ui_state_fill_token(state));
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, SL_UI_COLOR_BG_CANVAS);

    uint32_t sw_h = (h > 6u) ? (h - 6u) : 6u;
    uint32_t sw_w = sw_h * 2u;
    if (sw_w < 20u) sw_w = 20u;
    if (sw_w + 8u > w) sw_w = (w > 8u) ? (w - 8u) : w;
    uint32_t sw_x = x + w - sw_w - 4u;
    uint32_t sw_y = y + ((h > sw_h) ? ((h - sw_h) / 2u) : 0u);

    sl_ui_fill_tok(ui, sw_x, sw_y, sw_w, sw_h,
                   *value ? (enabled ? SL_UI_COLOR_ACCENT : SL_UI_COLOR_BG_PANEL)
                          : SL_UI_COLOR_BG_CANVAS);

    uint32_t knob_w = (sw_w > 4u) ? (sw_w / 2u - 1u) : sw_w;
    uint32_t knob_x = *value ? (sw_x + sw_w - knob_w - 1u) : (sw_x + 1u);
    sl_ui_fill_tok(ui, knob_x, sw_y + 1u, knob_w, (sw_h > 2u) ? (sw_h - 2u) : sw_h,
                   enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);

    if (text[0]) {
        uint32_t text_x = x + 6u;
        uint32_t text_y = sl_ui_text_center_y(ui, y, h);
        if (text_x + sl_ui_font_w(ui) < sw_x) {
            sl_ui_text_tok(ui, text_x, text_y, text,
                           enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        }
    }
    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

uint8_t sl_ui_select_cycle(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t option_count, uint32_t *selected,
                           const char *current_text,
                           const char *options_pipe) {
    return sl_ui_select_cycle_state(ui, id, x, y, w, h, option_count, selected, current_text, options_pipe, 1u);
}

static void sl_ui_select_option_text(const char *options_pipe, uint32_t index,
                                     char *out, uint32_t out_cap) {
    uint32_t idx = 0u;
    uint32_t n = 0u;
    const char *start = options_pipe;
    const char *p = options_pipe;
    if (!out || out_cap == 0u) return;
    out[0] = '\0';
    if (!options_pipe || options_pipe[0] == '\0') return;

    while (1) {
        if (*p == '|' || *p == '\0') {
            if (idx == index) {
                const char *q = start;
                while (q < p && n + 1u < out_cap) {
                    out[n++] = *q++;
                }
                out[n] = '\0';
                return;
            }
            if (*p == '\0') break;
            idx++;
            start = p + 1;
        }
        p++;
    }
}

uint8_t sl_ui_select_cycle_state(sl_ui_ctx_t *ui, uint32_t id,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 uint32_t option_count, uint32_t *selected,
                                 const char *current_text, const char *options_pipe,
                                 uint8_t enabled) {
    return sl_ui_select_cycle_state_ex(ui, id,
                                       x, y, w, h,
                                       option_count, selected,
                                       current_text, options_pipe,
                                       NULL, 0u, enabled);
}

uint8_t sl_ui_select_cycle_state_ex(sl_ui_ctx_t *ui, uint32_t id,
                                    uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    uint32_t option_count, uint32_t *selected,
                                    const char *current_text, const char *options_pipe,
                                    const peos_input_event_t *events, uint8_t event_count,
                                    uint8_t enabled) {
    if (!ui || !ui->ctx || !selected || !current_text || w < 16u || h < 12u) return 0u;
    if (option_count == 0u) return 0u;
    if (*selected >= option_count) *selected = 0u;

    uint32_t row_h = (h < 20u) ? 20u : h;
    uint32_t max_items = 6u;
    uint32_t desired_items = (option_count < max_items) ? option_count : max_items;

    uint32_t body_top = 0u;
    uint32_t body_bottom = ui->ctx->fb.height;
    if (ui->win) {
        uint32_t by, bh;
        sl_window_body_rect(ui->win, NULL, &by, NULL, &bh);
        if (bh > 0u) {
            body_top = by;
            body_bottom = by + bh;
        }
    }

    uint32_t trigger_bottom = y + h;
    uint32_t space_below = (body_bottom > trigger_bottom) ? (body_bottom - trigger_bottom) : 0u;
    uint32_t space_above = (y > body_top) ? (y - body_top) : 0u;
    uint32_t fit_below = (space_below > 2u) ? ((space_below - 2u) / row_h) : 0u;
    uint32_t fit_above = (space_above > 2u) ? ((space_above - 2u) / row_h) : 0u;

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
    uint32_t popup_y = open_above ? ((y >= popup_h) ? (y - popup_h) : body_top) : (y + h);
    if (!open_above && popup_y + popup_h > body_bottom) {
        popup_y = (y >= popup_h) ? (y - popup_h) : body_top;
    }
    if (popup_y < body_top) popup_y = body_top;
    if (popup_y + popup_h > body_bottom && body_bottom > popup_h) {
        popup_y = body_bottom - popup_h;
    }
    int was_open = (ui->select_open_id == id) ? 1 : 0;
    int open = was_open;
    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    uint8_t has_scrollbar = (option_count > visible_items && w > 20u) ? 1u : 0u;
    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
    uint32_t list_w = (w > scrollbar_w) ? (w - scrollbar_w) : w;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    int popup_hovered = open ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, popup_y, w, popup_h) : 0;
    int popup_list_hovered = open ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, popup_y, list_w, popup_h) : 0;
    int popup_scroll_hovered = (open && has_scrollbar)
        ? sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x + list_w, popup_y, w - list_w, popup_h) : 0;
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) {
            if (open) ui->select_open_id = 0u;
            else ui->select_open_id = id;
            open = (ui->select_open_id == id) ? 1 : 0;
        }
    }
    if (!enabled && open) {
        ui->select_open_id = 0u;
        open = 0;
    }
    if (open && !was_open) {
        uint32_t start = 0u;
        if (*selected >= visible_items) {
            start = *selected - (visible_items - 1u);
            if (start > max_start) start = max_start;
        }
        ui->select_scroll_idx = start;
    }
    if (ui->select_scroll_idx > max_start) ui->select_scroll_idx = max_start;

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (open || (active && ui->mouse_down)) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, sl_ui_state_fill_token(state));
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, SL_UI_COLOR_BG_CANVAS);

    uint32_t text_y = sl_ui_text_center_y(ui, y, h);
    sl_ui_text_tok(ui, x + 6u, text_y, current_text,
                   enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
    sl_ui_text_tok(ui, x + w - 10u, text_y, open ? "v" : ">",
                   enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);
    if (!enabled && active) ui->active_id = 0u;

    if (open) {
        if (enabled && popup_hovered && !ui->wheel_consumed && max_start > 0u) {
            int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
            if (wheel != 0) {
                int64_t nv = (int64_t)ui->select_scroll_idx - (int64_t)wheel;
                if (nv < 0) nv = 0;
                if (nv > (int64_t)max_start) nv = (int64_t)max_start;
                ui->select_scroll_idx = (uint32_t)nv;
                ui->wheel_consumed = 1u;
            }
        }

        if (enabled && popup_scroll_hovered && has_scrollbar && ui->mouse_pressed) {
            uint32_t track_y = popup_y + 2u;
            uint32_t track_h = (popup_h > 4u) ? (popup_h - 4u) : 0u;
            if (track_h > 0u && max_start > 0u) {
                uint32_t thumb_h = (uint32_t)(((uint64_t)visible_items * (uint64_t)track_h) / (uint64_t)option_count);
                if (thumb_h < 8u) thumb_h = 8u;
                if (thumb_h > track_h) thumb_h = track_h;
                uint32_t travel = track_h - thumb_h;
                uint32_t cy = ui->cursor_y;
                if (cy < track_y) cy = track_y;
                if (cy > track_y + track_h) cy = track_y + track_h;
                uint32_t pos = (cy > (track_y + (thumb_h / 2u))) ? (cy - track_y - (thumb_h / 2u)) : 0u;
                if (pos > travel) pos = travel;
                ui->select_scroll_idx = (travel > 0u)
                    ? (uint32_t)(((uint64_t)pos * (uint64_t)max_start) / (uint64_t)travel)
                    : 0u;
            }
        }

        for (uint8_t i = 0u; i < event_count; i++) {
            uint16_t code = sl_ui_norm_keycode(events[i].code);
            if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
                ui->shift_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LCTRL || code == PEOS_KEY_RCTRL_NORM) {
                ui->ctrl_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LALT || code == PEOS_KEY_RALT_NORM) {
                ui->alt_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
                ui->super_down = events[i].pressed ? 1u : 0u;
                continue;
            }
            if (!enabled || !events[i].pressed) continue;

            if (code == PEOS_KEY_ESC) {
                ui->select_open_id = 0u;
                open = 0;
                break;
            }
            if (code == PEOS_KEY_ENTER) {
                ui->select_open_id = 0u;
                open = 0;
                break;
            }
            if (code == PEOS_KEY_UP_NORM) {
                if (*selected > 0u) {
                    (*selected)--;
                    changed = 1u;
                }
            } else if (code == PEOS_KEY_DOWN_NORM) {
                if ((*selected + 1u) < option_count) {
                    (*selected)++;
                    changed = 1u;
                }
            } else {
                continue;
            }

            if (*selected < ui->select_scroll_idx) {
                ui->select_scroll_idx = *selected;
            } else if (*selected >= ui->select_scroll_idx + visible_items) {
                ui->select_scroll_idx = *selected - visible_items + 1u;
            }
            if (ui->select_scroll_idx > max_start) ui->select_scroll_idx = max_start;
        }

        uint32_t start_idx = ui->select_scroll_idx;
        if (start_idx > max_start) start_idx = max_start;
        ui->select_scroll_idx = start_idx;

        ui->select_overlay_pending = 1u;
        ui->select_overlay_id = id;
        ui->select_overlay_x = x;
        ui->select_overlay_y = popup_y;
        ui->select_overlay_w = w;
        ui->select_overlay_row_h = row_h;
        ui->select_overlay_start_idx = start_idx;
        ui->select_overlay_visible_items = visible_items;
        ui->select_overlay_option_count = option_count;
        ui->select_overlay_selected = *selected;
        ui->select_overlay_options_pipe = options_pipe;

        if (enabled && ui->mouse_released && !active) {
            if (popup_list_hovered) {
                uint32_t rel = (ui->cursor_y >= popup_y + 1u) ? (ui->cursor_y - (popup_y + 1u)) : 0u;
                uint32_t item = rel / row_h;
                if (item < visible_items) {
                    uint32_t picked = start_idx + item;
                    if (picked < option_count && *selected != picked) {
                        *selected = picked;
                        changed = 1u;
                    }
                }
                ui->select_open_id = 0u;
            } else if (!hovered) {
                ui->select_open_id = 0u;
            }
        }
    }

    return changed;
}

void sl_ui_draw_overlays(sl_ui_ctx_t *ui) {
    if (!ui || !ui->ctx || !ui->select_overlay_pending) return;

    uint32_t x = ui->select_overlay_x;
    uint32_t y = ui->select_overlay_y;
    uint32_t w = ui->select_overlay_w;
    uint32_t row_h = ui->select_overlay_row_h;
    uint32_t start_idx = ui->select_overlay_start_idx;
    uint32_t visible_items = ui->select_overlay_visible_items;
    uint32_t option_count = ui->select_overlay_option_count;
    uint32_t selected = ui->select_overlay_selected;
    const char *options_pipe = ui->select_overlay_options_pipe;
    uint32_t popup_h = visible_items * row_h + 2u;
    uint8_t has_scrollbar = (option_count > visible_items && w > 20u) ? 1u : 0u;
    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
    uint32_t list_w = (w > scrollbar_w) ? (w - scrollbar_w) : w;
    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    if (start_idx > max_start) start_idx = max_start;

    if (ui->ctx) {
        sl_fill_rect_a(ui->ctx, x + 1u, y + popup_h, w, 2u, 0u, 0u, 0u, 72u);
        sl_fill_rect_a(ui->ctx, x + w, y + 1u, 2u, popup_h + 1u, 0u, 0u, 0u, 72u);
    }

    sl_ui_fill_tok(ui, x, y, w, popup_h, SL_UI_COLOR_BG_PANEL);
    sl_ui_fill_tok(ui, x, y, w, 1u, SL_UI_COLOR_BORDER);
    sl_ui_fill_tok(ui, x, y + popup_h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, popup_h, SL_UI_COLOR_BORDER);
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, popup_h, SL_UI_COLOR_BG_CANVAS);

    for (uint32_t i = 0u; i < visible_items; i++) {
        uint32_t opt_idx = start_idx + i;
        uint32_t iy = y + 1u + i * row_h;
        if (opt_idx >= option_count) break;

        int item_hover = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x + 1u, iy, list_w - 2u, row_h);
        if (item_hover) sl_ui_fill_tok(ui, x + 1u, iy, list_w - 2u, row_h, SL_UI_COLOR_BG_CONTROL_HOVER);
        else if (opt_idx == selected) sl_ui_fill_tok(ui, x + 1u, iy, list_w - 2u, row_h, SL_UI_COLOR_BG_CONTROL_ACTIVE);

        char opt_text[128];
        sl_ui_select_option_text(options_pipe, opt_idx, opt_text, (uint32_t)sizeof(opt_text));
        sl_ui_text_tok(ui, x + 6u, sl_ui_text_center_y(ui, iy, row_h), opt_text, SL_UI_COLOR_TEXT_PRIMARY);
    }

    if (has_scrollbar) {
        uint32_t sx = x + list_w;
        uint32_t sy = y + 1u;
        uint32_t sw = w - list_w;
        uint32_t sh = (popup_h > 2u) ? (popup_h - 2u) : 0u;
        if (sw > 0u && sh > 0u) {
            sl_ui_fill_tok(ui, sx, sy, sw, sh, SL_UI_COLOR_BG_CANVAS);

            uint32_t track_x = sx + 2u;
            uint32_t track_y = sy + 1u;
            uint32_t track_w = (sw > 4u) ? (sw - 4u) : sw;
            uint32_t track_h = (sh > 2u) ? (sh - 2u) : sh;
            if (track_w > 0u && track_h > 0u) {
                sl_ui_fill_tok(ui, track_x, track_y, track_w, track_h, SL_UI_COLOR_BG_CONTROL);
                uint32_t thumb_h = (uint32_t)(((uint64_t)visible_items * (uint64_t)track_h) / (uint64_t)option_count);
                if (thumb_h < 8u) thumb_h = 8u;
                if (thumb_h > track_h) thumb_h = track_h;
                uint32_t travel = track_h - thumb_h;
                uint32_t thumb_y = (max_start > 0u)
                    ? (track_y + (uint32_t)(((uint64_t)start_idx * (uint64_t)travel) / (uint64_t)max_start))
                    : track_y;
                sl_ui_fill_tok(ui, track_x, thumb_y, track_w, thumb_h, SL_UI_COLOR_ACCENT);
            }
        }
    }
}

uint8_t sl_ui_text_input(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         char *text, uint32_t text_cap,
                         const peos_input_event_t *events, uint8_t event_count) {
    return sl_ui_text_input_state(ui, id, x, y, w, h, text, text_cap, events, event_count, 1u);
}

uint8_t sl_ui_text_input_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               char *text, uint32_t text_cap,
                               const peos_input_event_t *events, uint8_t event_count,
                               uint8_t enabled) {
    if (!ui || !ui->ctx || !text || text_cap < 2u || w < 16u || h < 12u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && ui->mouse_pressed) {
        if (hovered) {
            ui->focused_id = id;
            ui->active_id = id;
        }
        else if (ui->focused_id == id) ui->focused_id = 0u;
    }
    if (!enabled && ui->focused_id == id) ui->focused_id = 0u;
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    int focused = (ui->focused_id == id);

    uint32_t len = sl_ui_strlen(text);
    if (len >= text_cap) {
        len = text_cap - 1u;
        text[len] = '\0';
    }
    if (focused && ui->text_caret_id != id) {
        ui->text_caret_id = id;
        ui->text_caret_pos = len;
        ui->text_input_scroll_col = 0u;
        sl_ui_selection_collapse(ui, id, len);
    }
    if (ui->text_caret_id == id && ui->text_caret_pos > len) ui->text_caret_pos = len;
    if (ui->text_caret_id == id && ui->text_caret_pos > 0u &&
        sl_ui_utf8_is_cont(text[ui->text_caret_pos])) {
        ui->text_caret_pos = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
    }
    if (ui->text_select_id == id) {
        if (ui->text_select_anchor_pos > len) ui->text_select_anchor_pos = len;
        if (ui->text_select_focus_pos > len) ui->text_select_focus_pos = len;
    }

    if (enabled && ui->mouse_pressed && hovered) {
        ui->text_caret_pos = sl_ui_text_input_hit_pos(ui, x, w, text, len);
        if (ui->text_caret_pos > len) ui->text_caret_pos = len;
        sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
    }
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t old_pos = ui->text_caret_pos;
        uint32_t new_pos = sl_ui_text_input_hit_pos(ui, x, w, text, len);
        if (new_pos > len) new_pos = len;
        sl_ui_move_caret(ui, id, old_pos, new_pos, 1u);
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    uint8_t changed = 0u;
    if (enabled && focused) {
        if (ui->clipboard_cmds & SL_CLIP_CMD_COPY) {
            uint32_t sel_start = 0u, sel_end = 0u;
            int32_t fx = 0, fy = 0;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end) &&
                sl_ui_clipboard_copy_selection(ui, id, text)) {
                sl_ui_text_input_copy_point(ui, x, y, w, h, text, len, &fx, &fy);
                sl_ui_clipboard_notify_copy_feedback(ui, text, sel_start, sel_end, fx, fy);
            }
            ui->clipboard_cmds &= ~SL_CLIP_CMD_COPY;
        }
        if (ui->clipboard_cmds & SL_CLIP_CMD_PASTE) {
            if (sl_ui_text_apply_paste(ui, id, text, &len, text_cap, &ui->text_caret_pos)) changed = 1u;
            ui->clipboard_cmds &= ~SL_CLIP_CMD_PASTE;
        }
    }
    for (uint8_t i = 0u; i < event_count; i++) {
        uint16_t code = sl_ui_norm_keycode(events[i].code);
        if (sl_keyboard_event_is_altgr(&events[i])) {
            ui->altgr_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
            ui->shift_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LCTRL) {
            ui->ctrl_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LALT) {
            ui->alt_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
            ui->super_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (!enabled || !focused || !events[i].pressed) continue;
        if (ui->ctrl_down && code == PEOS_KEY_A) {
            ui->text_caret_pos = len;
            ui->text_select_id = id;
            ui->text_select_anchor_pos = 0u;
            ui->text_select_focus_pos = len;
            continue;
        }
        if (code == PEOS_KEY_BACKSPACE) {
            if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) {
                changed = 1u;
            } else if (ui->text_caret_pos > 0u && len > 0u) {
                uint32_t prev = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
                uint32_t removed = ui->text_caret_pos - prev;
                for (uint32_t j = prev; j + removed <= len; j++) {
                    text[j] = text[j + removed];
                }
                ui->text_caret_pos = prev;
                len -= removed;
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (code == PEOS_KEY_LEFT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_start, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos > 0u) new_pos = sl_ui_utf8_prev_pos(text, new_pos);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_RIGHT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_end, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos < len) new_pos = sl_ui_utf8_next_pos(text, new_pos, len);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_HOME_NORM) {
            sl_ui_move_caret(ui, id, ui->text_caret_pos, 0u, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_END_NORM) {
            sl_ui_move_caret(ui, id, ui->text_caret_pos, len, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_ESC) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (code == PEOS_KEY_ENTER) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (ui->ctrl_down || ui->alt_down || ui->super_down) {
            continue;
        }

        char input[8];
        size_t input_len = sl_keyboard_translate_event(&events[i], ui->shift_down,
                                                       ui->caps_lock_on,
                                                       ui->altgr_down,
                                                       input, sizeof(input));
        if (input_len > 0u && sl_ui_selection_range(ui, id, NULL, NULL)) {
            if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
        }
        if (input_len > 0u && len + input_len < text_cap) {
            for (uint32_t j = len + 1u; j > ui->text_caret_pos; j--) {
                text[j + (uint32_t)input_len - 1u] = text[j - 1u];
            }
            for (uint32_t j = 0u; j < (uint32_t)input_len; j++) {
                text[ui->text_caret_pos + j] = input[j];
            }
            ui->text_caret_pos += (uint32_t)input_len;
            len += (uint32_t)input_len;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            changed = 1u;
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (focused) state = SL_UI_STATE_FOCUSED;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BG_PANEL : SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BORDER_FOCUS : SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BORDER_FOCUS : SL_UI_COLOR_BG_CANVAS);

    uint32_t fw = sl_ui_font_w(ui);
    uint32_t fh = sl_ui_font_h(ui);
    uint32_t vis_chars = (w > 12u) ? ((w - 12u) / fw) : 0u;
    if (vis_chars > 0u) {
        uint8_t caret_visible = (enabled && focused &&
                                 ((peos_time_ns() / 500000000ull) & 1ull)) ? 1u : 0u;
        uint32_t input_caret_pos = (ui->text_caret_id == id) ? ui->text_caret_pos : len;
        if (ui->text_input_scroll_col > len) ui->text_input_scroll_col = len;
        if (ui->text_input_scroll_col > 0u && sl_ui_utf8_is_cont(text[ui->text_input_scroll_col])) {
            ui->text_input_scroll_col = sl_ui_utf8_prev_pos(text, ui->text_input_scroll_col);
        }
        if (focused) {
            if (input_caret_pos < ui->text_input_scroll_col) ui->text_input_scroll_col = input_caret_pos;
            while (sl_ui_utf8_count_between(text, ui->text_input_scroll_col, input_caret_pos) >= vis_chars &&
                   ui->text_input_scroll_col < input_caret_pos) {
                ui->text_input_scroll_col = sl_ui_utf8_next_pos(text, ui->text_input_scroll_col, len);
            }
        }
        uint32_t start = focused ? ui->text_input_scroll_col : 0u;
        char draw_buf[192];
        uint32_t n = 0u;
        uint32_t pos = start;
        uint32_t cols = 0u;
        while (text[pos] && n + 5u < (uint32_t)sizeof(draw_buf) && cols < vis_chars) {
            uint32_t next = sl_ui_utf8_next_pos(text, pos, len);
            while (pos < next && n + 1u < (uint32_t)sizeof(draw_buf)) {
                draw_buf[n++] = text[pos++];
            }
            cols++;
        }
        uint32_t visible_end = pos;
        uint32_t caret_draw = (input_caret_pos >= start) ?
            sl_ui_utf8_count_between(text, start, input_caret_pos) : 0u;
        if (caret_draw > vis_chars) caret_draw = vis_chars;
        draw_buf[n] = '\0';
        uint32_t text_x = x + 6u;
        uint32_t text_y = sl_ui_text_center_y(ui, y, h);
        {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                uint32_t clip_start = (sel_start > start) ? sel_start : start;
                uint32_t clip_end = (sel_end < visible_end) ? sel_end : visible_end;
                if (clip_end > clip_start) {
                    uint32_t sel_col0 = sl_ui_utf8_count_between(text, start, clip_start);
                    uint32_t sel_col1 = sl_ui_utf8_count_between(text, start, clip_end);
                    if (sel_col1 > sel_col0) {
                        sl_ui_fill_selection(ui, text_x + sel_col0 * fw, text_y,
                                             (sel_col1 - sel_col0) * fw, fh);
                    }
                }
            }
        }
        sl_ui_text_tok(ui, text_x, text_y, draw_buf,
                       enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        if (caret_visible) {
            uint32_t caret_px = text_x + (caret_draw * fw);
            uint32_t caret_max = text_x + (vis_chars * fw);
            if (caret_px >= caret_max) caret_px = caret_max - 1u;
            sl_ui_fill_tok(ui, caret_px, text_y, 1u, fh, SL_UI_COLOR_BORDER_FOCUS);
        }
    }
    return changed;
}

uint8_t sl_ui_text_area(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        char *text, uint32_t text_cap,
                        const peos_input_event_t *events, uint8_t event_count) {
    return sl_ui_text_area_state(ui, id, x, y, w, h, text, text_cap, events, event_count, 1u);
}

static uint32_t sl_ui_text_line_start(const char *text, uint32_t pos) {
    while (pos > 0u && text[pos - 1u] != '\n') pos--;
    return pos;
}

static uint32_t sl_ui_text_line_end(const char *text, uint32_t start) {
    uint32_t i = start;
    while (text[i] && text[i] != '\n') i++;
    return i;
}

static uint32_t sl_ui_text_total_lines(const char *text) {
    uint32_t lines = 1u;
    for (uint32_t i = 0u; text[i]; i++) {
        if (text[i] == '\n') lines++;
    }
    return lines;
}

static uint32_t sl_ui_text_max_line_len(const char *text) {
    if (!text) return 0u;
    uint32_t max_len = 0u;
    uint32_t cur_len = 0u;
    for (uint32_t i = 0u;;) {
        char c = text[i];
        if (c == '\0' || c == '\n') {
            if (cur_len > max_len) max_len = cur_len;
            if (c == '\0') break;
            cur_len = 0u;
            i++;
            continue;
        }
        i = sl_ui_utf8_next_pos(text, i, sl_ui_strlen(text));
        cur_len++;
    }
    return max_len;
}

static uint32_t sl_ui_text_line_col_at(const char *text, uint32_t pos, uint32_t *col_out) {
    uint32_t line = 0u;
    uint32_t col = 0u;
    uint32_t len = sl_ui_strlen(text);
    for (uint32_t i = 0u; i < pos && text[i];) {
        if (text[i] == '\n') {
            line++;
            col = 0u;
            i++;
        } else {
            i = sl_ui_utf8_next_pos(text, i, len);
            col++;
        }
    }
    if (col_out) *col_out = col;
    return line;
}

static uint32_t sl_ui_text_pos_from_line_col(const char *text, uint32_t target_line, uint32_t target_col) {
    uint32_t line = 0u;
    uint32_t i = 0u;
    if (!text) return 0u;

    while (text[i] && line < target_line) {
        if (text[i] == '\n') line++;
        i++;
    }
    if (line < target_line) return i;

    uint32_t line_start = i;
    uint32_t line_end = sl_ui_text_line_end(text, line_start);
    uint32_t line_len = sl_ui_utf8_count_between(text, line_start, line_end);
    if (target_col > line_len) target_col = line_len;
    return sl_ui_utf8_advance_cols(text, line_start, line_end, target_col);
}

static uint32_t sl_ui_text_area_hit_pos(sl_ui_ctx_t *ui,
                                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                        const char *text) {
    uint32_t content_x, content_y, content_w, content_h, fw, fh, vis_cols, vis_lines;
    uint32_t click_row = 0u, click_col = 0u, click_line;
    if (!ui || !text) return 0u;
    content_x = x + 6u;
    content_y = y + 6u;
    content_w = (w > 12u) ? (w - 12u) : 0u;
    content_h = (h > 12u) ? (h - 12u) : 0u;
    fw = sl_ui_font_w(ui);
    fh = sl_ui_font_h(ui);
    vis_cols = content_w / fw;
    vis_lines = content_h / fh;
    if (vis_cols == 0u || vis_lines == 0u) return 0u;
    if (ui->cursor_y > content_y) click_row = (ui->cursor_y - content_y) / fh;
    if (click_row >= vis_lines) click_row = vis_lines - 1u;
    if (ui->cursor_x > content_x) click_col = (ui->cursor_x - content_x) / fw;
    if (click_col > vis_cols) click_col = vis_cols;
    click_col += ui->text_area_scroll_col;
    click_line = ui->text_area_scroll_line + click_row;
    return sl_ui_text_pos_from_line_col(text, click_line, click_col);
}

static void sl_ui_text_insert_bytes(char *text, uint32_t *len, uint32_t cap,
                                    uint32_t *caret, const char *bytes, uint32_t byte_len) {
    if (!text || !len || !caret || !bytes || byte_len == 0u || *len + byte_len >= cap) return;
    for (uint32_t i = *len + 1u; i > *caret; i--) {
        text[i + byte_len - 1u] = text[i - 1u];
    }
    for (uint32_t i = 0u; i < byte_len; i++) text[*caret + i] = bytes[i];
    *len += byte_len;
    *caret += byte_len;
}

static void sl_ui_text_insert_char(char *text, uint32_t *len, uint32_t cap, uint32_t *caret, char c) {
    sl_ui_text_insert_bytes(text, len, cap, caret, &c, 1u);
}

static uint8_t sl_ui_text_delete_range(char *text, uint32_t *len, uint32_t start, uint32_t end) {
    if (!text || !len || start >= end || end > *len) return 0u;
    uint32_t removed = end - start;
    for (uint32_t i = start; i + removed <= *len; i++) {
        text[i] = text[i + removed];
    }
    *len -= removed;
    return 1u;
}

static uint8_t sl_ui_text_delete_selection(sl_ui_ctx_t *ui, uint32_t id,
                                           char *text, uint32_t *len, uint32_t *caret) {
    uint32_t sel_start = 0u;
    uint32_t sel_end = 0u;
    if (!ui || !text || !len || !caret) return 0u;
    if (!sl_ui_selection_range(ui, id, &sel_start, &sel_end)) return 0u;
    if (!sl_ui_text_delete_range(text, len, sel_start, sel_end)) return 0u;
    *caret = sel_start;
    sl_ui_selection_collapse(ui, id, sel_start);
    return 1u;
}

static uint8_t sl_ui_text_apply_paste(sl_ui_ctx_t *ui, uint32_t id,
                                      char *text, uint32_t *len, uint32_t text_cap, uint32_t *caret) {
    const char *clip_text = NULL;
    uint32_t clip_len = 0u;
    uint32_t old_len = 0u;
    if (!ui || !text || !len || !caret || text_cap < 2u) return 0u;
    clip_text = sl_clipboard_current_text(&clip_len);
    if (!clip_text || clip_len == 0u) return 0u;
    if (sl_ui_selection_range(ui, id, NULL, NULL)) {
        (void)sl_ui_text_delete_selection(ui, id, text, len, caret);
    }
    old_len = *len;
    sl_ui_text_insert_bytes(text, len, text_cap, caret, clip_text, clip_len);
    if (*len == old_len) return 0u;
    sl_ui_selection_collapse(ui, id, *caret);
    return 1u;
}

static void sl_ui_text_backspace(char *text, uint32_t *len, uint32_t *caret) {
    if (!text || !len || !caret || *caret == 0u || *len == 0u) return;
    uint32_t prev = sl_ui_utf8_prev_pos(text, *caret);
    uint32_t removed = *caret - prev;
    for (uint32_t i = prev; i + removed <= *len; i++) {
        text[i] = text[i + removed];
    }
    *caret = prev;
    *len -= removed;
}

uint8_t sl_ui_text_area_state(sl_ui_ctx_t *ui, uint32_t id,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              char *text, uint32_t text_cap,
                              const peos_input_event_t *events, uint8_t event_count,
                              uint8_t enabled) {
    if (!ui || !ui->ctx || !text || text_cap < 2u || w < 24u || h < 24u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && ui->mouse_pressed) {
        if (hovered) {
            ui->focused_id = id;
            ui->active_id = id;
        }
        else if (ui->focused_id == id) ui->focused_id = 0u;
    }
    if (!enabled && ui->focused_id == id) ui->focused_id = 0u;
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    int focused = (ui->focused_id == id);

    uint32_t len = sl_ui_strlen(text);
    if (len >= text_cap) {
        len = text_cap - 1u;
        text[len] = '\0';
    }

    if (focused && ui->text_caret_id != id) {
        ui->text_caret_id = id;
        ui->text_caret_pos = len;
        ui->text_area_scroll_line = 0u;
        ui->text_area_scroll_col = 0u;
        sl_ui_selection_collapse(ui, id, len);
    }
    if (ui->text_caret_id == id && ui->text_caret_pos > len) ui->text_caret_pos = len;
    if (ui->text_caret_id == id && ui->text_caret_pos > 0u &&
        sl_ui_utf8_is_cont(text[ui->text_caret_pos])) {
        ui->text_caret_pos = sl_ui_utf8_prev_pos(text, ui->text_caret_pos);
    }
    if (ui->text_select_id == id) {
        if (ui->text_select_anchor_pos > len) ui->text_select_anchor_pos = len;
        if (ui->text_select_focus_pos > len) ui->text_select_focus_pos = len;
    }

    uint8_t changed = 0u;
    if (enabled && focused) {
        if (ui->clipboard_cmds & SL_CLIP_CMD_COPY) {
            uint32_t sel_start = 0u, sel_end = 0u;
            int32_t fx = 0, fy = 0;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end) &&
                sl_ui_clipboard_copy_selection(ui, id, text)) {
                sl_ui_text_area_copy_point(ui, x, y, text, &fx, &fy);
                sl_ui_clipboard_notify_copy_feedback(ui, text, sel_start, sel_end, fx, fy);
            }
            ui->clipboard_cmds &= ~SL_CLIP_CMD_COPY;
        }
        if (ui->clipboard_cmds & SL_CLIP_CMD_PASTE) {
            if (sl_ui_text_apply_paste(ui, id, text, &len, text_cap, &ui->text_caret_pos)) changed = 1u;
            ui->clipboard_cmds &= ~SL_CLIP_CMD_PASTE;
        }
    }
    for (uint8_t i = 0u; i < event_count; i++) {
        uint16_t code = sl_ui_norm_keycode(events[i].code);
        if (sl_keyboard_event_is_altgr(&events[i])) {
            ui->altgr_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSHIFT || code == PEOS_KEY_RSHIFT) {
            ui->shift_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LCTRL) {
            ui->ctrl_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LALT) {
            ui->alt_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (code == PEOS_KEY_LSUPER_NORM || code == PEOS_KEY_RSUPER_NORM) {
            ui->super_down = events[i].pressed ? 1u : 0u;
            continue;
        }
        if (!enabled || !focused || !events[i].pressed) continue;
        if (ui->ctrl_down && code == PEOS_KEY_A) {
            ui->text_caret_pos = len;
            ui->text_select_id = id;
            ui->text_select_anchor_pos = 0u;
            ui->text_select_focus_pos = len;
            continue;
        }

        if (code == PEOS_KEY_BACKSPACE) {
            uint32_t old_len = len;
            if (!sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) {
                sl_ui_text_backspace(text, &len, &ui->text_caret_pos);
                if (len != old_len) sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            }
            if (len != old_len) changed = 1u;
            continue;
        }
        if (code == PEOS_KEY_ESC) {
            ui->focused_id = 0u;
            focused = 0;
            sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
            continue;
        }
        if (code == PEOS_KEY_ENTER) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_char(text, &len, text_cap, &ui->text_caret_pos, '\n');
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (code == PEOS_KEY_LEFT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_start, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos > 0u) new_pos = sl_ui_utf8_prev_pos(text, new_pos);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_RIGHT_NORM) {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (!ui->shift_down && sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                sl_ui_move_caret(ui, id, ui->text_caret_pos, sel_end, 0u);
            } else {
                uint32_t old_pos = ui->text_caret_pos;
                uint32_t new_pos = old_pos;
                if (new_pos < len) new_pos = sl_ui_utf8_next_pos(text, new_pos, len);
                sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            }
            continue;
        }
        if (code == PEOS_KEY_UP_NORM || code == PEOS_KEY_DOWN_NORM) {
            uint32_t old_pos = ui->text_caret_pos;
            uint32_t new_pos = old_pos;
            uint32_t line_start = sl_ui_text_line_start(text, old_pos);
            uint32_t col = sl_ui_utf8_count_between(text, line_start, old_pos);
            if (code == PEOS_KEY_UP_NORM) {
                if (line_start > 0u) {
                    uint32_t prev_end = line_start - 1u;
                    uint32_t prev_start = sl_ui_text_line_start(text, prev_end);
                    uint32_t prev_len = sl_ui_utf8_count_between(text, prev_start, prev_end);
                    new_pos = sl_ui_utf8_advance_cols(text, prev_start, prev_end,
                                                      (col < prev_len) ? col : prev_len);
                }
            } else {
                uint32_t line_end = sl_ui_text_line_end(text, line_start);
                if (text[line_end] == '\n') {
                    uint32_t next_start = line_end + 1u;
                    uint32_t next_end = sl_ui_text_line_end(text, next_start);
                    uint32_t next_len = sl_ui_utf8_count_between(text, next_start, next_end);
                    new_pos = sl_ui_utf8_advance_cols(text, next_start, next_end,
                                                      (col < next_len) ? col : next_len);
                }
            }
            sl_ui_move_caret(ui, id, old_pos, new_pos, ui->shift_down);
            continue;
        }
        if (code == PEOS_KEY_TAB) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_char(text, &len, text_cap, &ui->text_caret_pos, '\t');
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
            continue;
        }
        if (ui->ctrl_down || ui->alt_down || ui->super_down) {
            continue;
        }

        char input[8];
        size_t input_len = sl_keyboard_translate_event(&events[i], ui->shift_down,
                                                       ui->caps_lock_on,
                                                       ui->altgr_down,
                                                       input, sizeof(input));
        if (input_len > 0u) {
            uint32_t old_len = len;
            if (sl_ui_selection_range(ui, id, NULL, NULL)) {
                if (sl_ui_text_delete_selection(ui, id, text, &len, &ui->text_caret_pos)) changed = 1u;
            }
            sl_ui_text_insert_bytes(text, &len, text_cap, &ui->text_caret_pos,
                                    input, (uint32_t)input_len);
            if (len != old_len) {
                sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
                changed = 1u;
            }
        }
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (focused) state = SL_UI_STATE_FOCUSED;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BG_PANEL : SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BORDER_FOCUS : SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, (state == SL_UI_STATE_FOCUSED) ? SL_UI_COLOR_BORDER_FOCUS : SL_UI_COLOR_BG_CANVAS);

    uint32_t content_x = x + 6u;
    uint32_t content_y = y + 6u;
    uint32_t content_w = (w > 12u) ? (w - 12u) : 0u;
    uint32_t content_h = (h > 12u) ? (h - 12u) : 0u;
    uint32_t fw = sl_ui_font_w(ui);
    uint32_t fh = sl_ui_font_h(ui);
    uint32_t vis_cols = content_w / fw;
    uint32_t vis_lines = content_h / fh;
    if (vis_cols == 0u || vis_lines == 0u) return changed;

    uint32_t total_lines = sl_ui_text_total_lines(text);
    uint32_t max_scroll = (total_lines > vis_lines) ? (total_lines - vis_lines) : 0u;
    if (ui->text_area_scroll_line > max_scroll) ui->text_area_scroll_line = max_scroll;
    uint32_t max_line_len = sl_ui_text_max_line_len(text);
    uint32_t max_scroll_col = (max_line_len > vis_cols) ? (max_line_len - vis_cols) : 0u;
    if (ui->text_area_scroll_col > max_scroll_col) ui->text_area_scroll_col = max_scroll_col;

    if (enabled && ui->mouse_pressed && hovered) {
        ui->text_caret_pos = sl_ui_text_area_hit_pos(ui, x, y, w, h, text);
        if (ui->text_caret_pos > len) ui->text_caret_pos = len;
        sl_ui_selection_collapse(ui, id, ui->text_caret_pos);
    }
    if (enabled && ui->active_id == id && ui->mouse_down) {
        uint32_t old_pos = ui->text_caret_pos;
        uint32_t new_pos = sl_ui_text_area_hit_pos(ui, x, y, w, h, text);
        if (new_pos > len) new_pos = len;
        sl_ui_move_caret(ui, id, old_pos, new_pos, 1u);
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    if (enabled && hovered && !ui->wheel_consumed && max_scroll > 0u) {
        int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
        if (wheel != 0) {
            int64_t nv = (int64_t)ui->text_area_scroll_line - (int64_t)wheel;
            if (nv < 0) nv = 0;
            if (nv > (int64_t)max_scroll) nv = (int64_t)max_scroll;
            ui->text_area_scroll_line = (uint32_t)nv;
            ui->wheel_consumed = 1u;
        }
    }

    uint32_t area_caret_pos = (ui->text_caret_id == id) ? ui->text_caret_pos : len;
    uint32_t caret_col = 0u;
    uint32_t caret_line = sl_ui_text_line_col_at(text, area_caret_pos, &caret_col);
    uint8_t caret_visible = (enabled && focused &&
                             (((peos_time_ns() / 500000000ull) & 1ull) != 0ull)) ? 1u : 0u;
    if (caret_line < ui->text_area_scroll_line) ui->text_area_scroll_line = caret_line;
    if (caret_line >= ui->text_area_scroll_line + vis_lines) {
        ui->text_area_scroll_line = caret_line - vis_lines + 1u;
    }
    if (caret_col < ui->text_area_scroll_col) ui->text_area_scroll_col = caret_col;
    if (caret_col >= ui->text_area_scroll_col + vis_cols) {
        ui->text_area_scroll_col = caret_col - vis_cols + 1u;
    }
    if (ui->text_area_scroll_col > max_scroll_col) ui->text_area_scroll_col = max_scroll_col;

    uint32_t line_idx = 0u;
    uint32_t start = 0u;
    while (line_idx < ui->text_area_scroll_line && text[start]) {
        if (text[start] == '\n') line_idx++;
        start++;
    }

    for (uint32_t row = 0u; row < vis_lines; row++) {
        uint32_t ly = content_y + (row * fh);
        if (!text[start] && !(line_idx == caret_line && caret_col == 0u)) {
            line_idx++;
            continue;
        }

        uint32_t end = sl_ui_text_line_end(text, start);
        char draw_buf[256];
        uint32_t dn = 0u;
        uint32_t i = start;
        uint32_t col = 0u;
        while (i < end && col < ui->text_area_scroll_col) {
            i = sl_ui_utf8_next_pos(text, i, end);
            col++;
        }
        while (i < end && dn + 5u < (uint32_t)sizeof(draw_buf) && col < ui->text_area_scroll_col + vis_cols) {
            uint32_t next = sl_ui_utf8_next_pos(text, i, end);
            if (text[i] == '\t') {
                draw_buf[dn++] = ' ';
            } else {
                while (i < next && dn + 1u < (uint32_t)sizeof(draw_buf)) {
                    draw_buf[dn++] = text[i++];
                }
            }
            i = next;
            col++;
        }

        draw_buf[dn] = '\0';

        {
            uint32_t sel_start = 0u, sel_end = 0u;
            if (sl_ui_selection_range(ui, id, &sel_start, &sel_end)) {
                uint32_t clip_start = (sel_start > start) ? sel_start : start;
                uint32_t clip_end = (sel_end < end) ? sel_end : end;
                if (clip_end > clip_start) {
                    uint32_t col0 = sl_ui_utf8_count_between(text, start, clip_start);
                    uint32_t col1 = sl_ui_utf8_count_between(text, start, clip_end);
                    if (col1 > col0) {
                        uint32_t draw_col0 = (col0 > ui->text_area_scroll_col) ? (col0 - ui->text_area_scroll_col) : 0u;
                        uint32_t draw_col1 = (col1 > ui->text_area_scroll_col) ? (col1 - ui->text_area_scroll_col) : 0u;
                        if (draw_col0 > vis_cols) draw_col0 = vis_cols;
                        if (draw_col1 > vis_cols) draw_col1 = vis_cols;
                        if (draw_col1 > draw_col0) {
                            sl_ui_fill_selection(ui, content_x + draw_col0 * fw, ly,
                                                 (draw_col1 - draw_col0) * fw, fh);
                        }
                    }
                }
            }
        }
        sl_ui_text_tok(ui, content_x, ly, draw_buf,
                       enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
        if (caret_visible && line_idx == caret_line) {
            uint32_t caret_x = 0u;
            if (caret_col > ui->text_area_scroll_col) {
                caret_x = caret_col - ui->text_area_scroll_col;
            }
            if (caret_x > vis_cols) caret_x = vis_cols;
            uint32_t caret_px = content_x + (caret_x * fw);
            uint32_t caret_max = content_x + (vis_cols * fw);
            if (caret_px >= caret_max) caret_px = caret_max - 1u;
            sl_ui_fill_tok(ui, caret_px, ly, 1u, fh, SL_UI_COLOR_BORDER_FOCUS);
        }

        if (text[end] == '\n') start = end + 1u;
        else start = end;
        line_idx++;
    }

    return changed;
}

uint8_t sl_ui_scroll_v(sl_ui_ctx_t *ui, uint32_t id,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t content_h, uint32_t *offset,
                       uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h) {
    return sl_ui_scroll_v_state(ui, id, x, y, w, h, content_h, offset, hit_x, hit_y, hit_w, hit_h, 1u);
}

uint8_t sl_ui_scroll_v_state(sl_ui_ctx_t *ui, uint32_t id,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t content_h, uint32_t *offset,
                             uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h,
                             uint8_t enabled) {
    if (!ui || !ui->ctx || !offset || w < 8u || h < 20u) return 0u;

    uint32_t max_off = 0u;
    if (content_h > h) max_off = content_h - h;
    if (*offset > max_off) *offset = max_off;

    uint32_t track_x = x + 2u;
    uint32_t track_y = y + 2u;
    uint32_t track_w = (w > 4u) ? (w - 4u) : w;
    uint32_t track_h = (h > 4u) ? (h - 4u) : h;
    if (track_w == 0u || track_h < 8u) return 0u;

    uint32_t knob_h = track_h;
    if (content_h > h && content_h > 0u) {
        knob_h = (uint32_t)(((uint64_t)track_h * (uint64_t)h) / (uint64_t)content_h);
        if (knob_h < 12u) knob_h = 12u;
        if (knob_h > track_h) knob_h = track_h;
    }
    uint32_t travel = (track_h > knob_h) ? (track_h - knob_h) : 0u;
    uint32_t knob_y = track_y;
    if (travel > 0u && max_off > 0u) {
        knob_y = track_y + (uint32_t)(((uint64_t)(*offset) * (uint64_t)travel) / (uint64_t)max_off);
    }

    if (hit_w == 0u || hit_h == 0u) {
        hit_x = x;
        hit_y = y;
        hit_w = w;
        hit_h = h;
    }

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, hit_x, hit_y, hit_w, hit_h);
    int bar_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    int knob_hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, track_x, knob_y, track_w, knob_h);
    if (enabled && bar_hovered && ui->mouse_pressed) {
        ui->active_id = id;
        if (!knob_hovered && travel > 0u && max_off > 0u) {
            uint32_t cy = ui->cursor_y;
            if (cy < track_y) cy = track_y;
            if (cy > track_y + track_h) cy = track_y + track_h;
            uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
            if (knob_pos > travel) knob_pos = travel;
            *offset = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        }
    }

    uint8_t changed = 0u;
    if (enabled && hovered && !ui->wheel_consumed && max_off > 0u) {
        int32_t wheel = (ui->wheel_y != 0) ? ui->wheel_y : ui->wheel_x;
        if (wheel != 0) {
            int32_t step = (int32_t)(h / 5u);
            if (step < 12) step = 12;
            if (step > 64) step = 64;

            int64_t nv = (int64_t)(*offset) - ((int64_t)wheel * (int64_t)step);
            if (nv < 0) nv = 0;
            if (nv > (int64_t)max_off) nv = (int64_t)max_off;
            if ((uint32_t)nv != *offset) {
                *offset = (uint32_t)nv;
                changed = 1u;
            }
            ui->wheel_consumed = 1u;
        }
    }

    if (enabled && ui->active_id == id && ui->mouse_down && travel > 0u && max_off > 0u) {
        uint32_t cy = ui->cursor_y;
        if (cy < track_y) cy = track_y;
        if (cy > track_y + track_h) cy = track_y + track_h;
        uint32_t knob_pos = (cy > (track_y + (knob_h / 2u))) ? (cy - track_y - (knob_h / 2u)) : 0u;
        if (knob_pos > travel) knob_pos = travel;
        uint32_t nv = (uint32_t)(((uint64_t)knob_pos * (uint64_t)max_off) / (uint64_t)travel);
        if (nv != *offset) {
            *offset = nv;
            changed = 1u;
        }
    }
    if (ui->mouse_released && ui->active_id == id) ui->active_id = 0u;

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (ui->active_id == id && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (bar_hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, (state == SL_UI_STATE_DISABLED) ? SL_UI_COLOR_BG_CANVAS : SL_UI_COLOR_BG_PANEL);
    sl_ui_fill_tok(ui, x, y, w, 1u, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
    sl_ui_fill_tok(ui, x, y, 1u, h, sl_ui_state_border_token(state));
    sl_ui_fill_tok(ui, x + w - 1u, y, 1u, h, SL_UI_COLOR_BG_CANVAS);

    sl_ui_fill_tok(ui, track_x, track_y, track_w, track_h,
                   enabled ? SL_UI_COLOR_BG_CONTROL_ACTIVE : SL_UI_COLOR_BG_CANVAS);
    sl_ui_color_token_t knob_t = SL_UI_COLOR_BG_CONTROL;
    if (!enabled) knob_t = SL_UI_COLOR_TEXT_DISABLED;
    else if (ui->active_id == id && ui->mouse_down) knob_t = SL_UI_COLOR_ACCENT_ACTIVE;
    else if (knob_hovered) knob_t = SL_UI_COLOR_ACCENT_HOVER;
    sl_ui_fill_tok(ui, track_x, knob_y, track_w, knob_h, knob_t);
    if (!enabled && ui->active_id == id) ui->active_id = 0u;
    return changed;
}

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride) {
    if (!ui || !ui->ctx || !rgba || w == 0u || h == 0u || src_w == 0u || src_h == 0u) return;
    sl_blit_rgba_stretch(ui->ctx, x, y, w, h, rgba, src_w, src_h, src_stride);
}

void sl_ui_image(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const sl_image_t *image) {
    if (!ui || !ui->ctx || !image || !image->data || w == 0u || h == 0u) return;
    sl_blit_image_stretch(ui->ctx, x, y, w, h, image);
}

void sl_ui_rect(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                sl_ui_color_token_t token) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_ui_fill_tok(ui, x, y, w, h, token);
}

void sl_ui_rect_a(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  sl_ui_color_token_t token, uint8_t a) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    const sl_ui_color_t *c = sl_ui_theme_color(ui, token);
    uint32_t aa = ((uint32_t)c->a * (uint32_t)a) / 255u;
    sl_fill_rect_a(ui->ctx, x, y, w, h, c->r, c->g, c->b, (uint8_t)aa);
}

void sl_ui_rect_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect(ui->ctx, x, y, w, h, r, g, b);
}

void sl_ui_rect_a_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ui || !ui->ctx || w == 0u || h == 0u) return;
    sl_fill_rect_a(ui->ctx, x, y, w, h, r, g, b, a);
}

void sl_ui_separator(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w) {
    if (!ui || !ui->ctx || w == 0u) return;
    sl_ui_fill_tok(ui, x, y, w, 1u, SL_UI_COLOR_BORDER);
    sl_ui_fill_tok(ui, x, y + 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);
}

uint8_t sl_ui_list_item(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const char *title, const char *subtitle, uint8_t enabled) {
    if (!title) title = "";
    if (!subtitle) subtitle = "";
    if (!ui || !ui->ctx || w < 16u || h < 16u) return 0u;

    int hovered = sl_ui_point_in_rect(ui->cursor_x, ui->cursor_y, x, y, w, h);
    if (enabled && hovered && ui->mouse_pressed) ui->active_id = id;
    int active = (ui->active_id == id);

    uint8_t changed = 0u;
    if (enabled && ui->mouse_released && active) {
        ui->active_id = 0u;
        if (hovered) changed = 1u;
    }

    sl_ui_control_state_t state = SL_UI_STATE_NORMAL;
    if (!enabled) state = SL_UI_STATE_DISABLED;
    else if (active && ui->mouse_down) state = SL_UI_STATE_ACTIVE;
    else if (hovered) state = SL_UI_STATE_HOVER;
    sl_ui_fill_tok(ui, x, y, w, h, sl_ui_state_fill_token(state));
    sl_ui_fill_tok(ui, x, y + h - 1u, w, 1u, SL_UI_COLOR_BG_CANVAS);

    uint32_t fh = sl_ui_font_h(ui);
    uint32_t title_y = y + 4u;
    sl_ui_text_tok(ui, x + 8u, title_y, title,
                   enabled ? SL_UI_COLOR_TEXT_PRIMARY : SL_UI_COLOR_TEXT_DISABLED);
    if (subtitle[0]) {
        uint32_t sub_y = title_y + fh + 2u;
        if (sub_y + fh > y + h) {
            sub_y = (y + h > fh) ? (y + h - fh) : y;
        }
        sl_ui_text_tok(ui, x + 8u, sub_y, subtitle,
                       enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);
    }
    sl_ui_text_tok(ui, x + w - 10u, sl_ui_text_center_y(ui, y, h), ">",
                   enabled ? SL_UI_COLOR_TEXT_SECONDARY : SL_UI_COLOR_TEXT_DISABLED);

    if (!enabled && active) ui->active_id = 0u;
    return changed;
}

void sl_ui_stack_begin(sl_ui_stack_t *stack,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       sl_ui_stack_axis_t axis, uint32_t spacing) {
    if (!stack) return;
    stack->x = x;
    stack->y = y;
    stack->w = w;
    stack->h = h;
    stack->spacing = spacing;
    stack->cursor = 0u;
    stack->axis = axis;
}

uint8_t sl_ui_stack_next(sl_ui_stack_t *stack, uint32_t primary_size,
                         uint32_t *out_x, uint32_t *out_y,
                         uint32_t *out_w, uint32_t *out_h) {
    if (!stack || !out_x || !out_y || !out_w || !out_h || primary_size == 0u) return 0u;

    if (stack->axis == SL_UI_STACK_VERTICAL) {
        if (stack->cursor > stack->h) return 0u;
        if (primary_size > (stack->h - stack->cursor)) return 0u;

        *out_x = stack->x;
        *out_y = stack->y + stack->cursor;
        *out_w = stack->w;
        *out_h = primary_size;
    } else {
        if (stack->cursor > stack->w) return 0u;
        if (primary_size > (stack->w - stack->cursor)) return 0u;

        *out_x = stack->x + stack->cursor;
        *out_y = stack->y;
        *out_w = primary_size;
        *out_h = stack->h;
    }

    uint32_t next_cursor = stack->cursor + primary_size;
    if (next_cursor < stack->cursor) return 0u;
    if (stack->spacing > 0u) {
        if (next_cursor > 0xFFFFFFFFu - stack->spacing) return 0u;
        next_cursor += stack->spacing;
    }
    stack->cursor = next_cursor;
    return 1u;
}

void sl_ui_grid_begin(sl_ui_grid_t *grid,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t cols, uint32_t rows,
                      uint32_t gap_x, uint32_t gap_y) {
    if (!grid) return;
    grid->x = x;
    grid->y = y;
    grid->w = w;
    grid->h = h;
    grid->cols = cols;
    grid->rows = rows;
    grid->gap_x = gap_x;
    grid->gap_y = gap_y;
    grid->cursor = 0u;
}

static uint8_t sl_ui_grid_metrics(const sl_ui_grid_t *grid,
                                  uint32_t *cell_w, uint32_t *cell_h) {
    if (!grid || !cell_w || !cell_h || grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t gaps_x = (uint64_t)(grid->cols - 1u) * (uint64_t)grid->gap_x;
    uint64_t gaps_y = (uint64_t)(grid->rows - 1u) * (uint64_t)grid->gap_y;
    if (gaps_x >= (uint64_t)grid->w || gaps_y >= (uint64_t)grid->h) return 0u;

    *cell_w = (uint32_t)(((uint64_t)grid->w - gaps_x) / (uint64_t)grid->cols);
    *cell_h = (uint32_t)(((uint64_t)grid->h - gaps_y) / (uint64_t)grid->rows);
    return (*cell_w > 0u && *cell_h > 0u) ? 1u : 0u;
}

uint8_t sl_ui_grid_cell(const sl_ui_grid_t *grid, uint32_t col, uint32_t row,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (col >= grid->cols || row >= grid->rows) return 0u;

    uint32_t cell_w = 0u;
    uint32_t cell_h = 0u;
    if (!sl_ui_grid_metrics(grid, &cell_w, &cell_h)) return 0u;

    uint64_t step_x = (uint64_t)cell_w + (uint64_t)grid->gap_x;
    uint64_t step_y = (uint64_t)cell_h + (uint64_t)grid->gap_y;
    uint64_t px = (uint64_t)grid->x + ((uint64_t)col * step_x);
    uint64_t py = (uint64_t)grid->y + ((uint64_t)row * step_y);
    if (px > 0xFFFFFFFFu || py > 0xFFFFFFFFu) return 0u;

    *out_x = (uint32_t)px;
    *out_y = (uint32_t)py;
    *out_w = cell_w;
    *out_h = cell_h;
    return 1u;
}

uint8_t sl_ui_grid_next(sl_ui_grid_t *grid,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h) {
    if (!grid || !out_x || !out_y || !out_w || !out_h) return 0u;
    if (grid->cols == 0u || grid->rows == 0u) return 0u;

    uint64_t total = (uint64_t)grid->cols * (uint64_t)grid->rows;
    if ((uint64_t)grid->cursor >= total) return 0u;

    uint32_t col = grid->cursor % grid->cols;
    uint32_t row = grid->cursor / grid->cols;
    if (!sl_ui_grid_cell(grid, col, row, out_x, out_y, out_w, out_h)) return 0u;

    grid->cursor++;
    return 1u;
}
