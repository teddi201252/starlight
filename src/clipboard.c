// The clipboard: the last copies (12), the one Caps+S pastes, and their
// manager (Caps held alone), with the Soft UI of design/appunti: a raised
// card over a night veil, a row for each copy with its number key, its age
// and its size, the keys at the bottom. Around it: the ring that fills
// around the pointer while Caps is held, and the pill that flies from the
// selection to the pointer when something is copied ("Copiato"), or shows
// by the pointer when it is pasted ("Incollato").
#include "clipboard.h"
#include "starlight_internal.h"
#include "i18n.h"
#include "ui_sdk.h"

#include "render/blit.h"
#include "render/pixels.h"
#include "render/skin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SL_CLIPBOARD_MAX_TEXT 65535u
#define SL_CLIPBOARD_HISTORY 12u
#define SL_CLIPBOARD_VISIBLE 6u
#define SL_CLIPBOARD_FEEDBACK_LABEL_MAX 64u
#define SL_CLIPBOARD_FEEDBACK_DURATION_MS 520u
#define SL_CLIPBOARD_PASTE_DURATION_MS 700u

// The card (design/appunti).
#define CB_W 600u
#define CB_PAD 26u
#define CB_PAD_B 22u
#define CB_HEAD_H 52u
#define CB_GAP 18u
#define CB_ROW_H 64u
#define CB_ROW_GAP 6u
#define CB_FOOT_H 50u
#define CB_EMPTY_H 150u
#define CB_RADIUS 32u
#define CB_SHADOW 34u          // the card's shadow, around it (damage)
#define CB_SCROLL_W 8u

// The ring around the pointer while Caps is held.
#define RING_SIZE 52u
#define RING_THICK 5u
#define RING_SHOW_NS 120000000ull   // not for a quick Caps+letter

#define NEON_R 0x59u
#define NEON_G 0xD0u
#define NEON_B 0xFFu

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

typedef struct {
    uint8_t audio_enabled;
    uint8_t visual_enabled;
    uint8_t active;
    uint8_t pasted;            // "Incollato" by the pointer, not a flight
    char label[SL_CLIPBOARD_FEEDBACK_LABEL_MAX];
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    uint64_t start_ns;
    uint32_t duration_ms;
    uint32_t last_x;
    uint32_t last_y;
    uint32_t last_w;
    uint32_t last_h;
    uint8_t last_valid;
} sl_clipboard_feedback_t;

typedef struct {
    uint8_t shown;
    uint32_t x, y;             // the ring's box
    uint32_t permille;
} sl_clipboard_ring_t;

static sl_clipboard_state_t g_clipboard;
static sl_clipboard_feedback_t g_feedback = {
    .audio_enabled = 1u,
    .visual_enabled = 1u,
};
static sl_clipboard_ring_t g_ring;

// The skin's faces, found once.
static struct {
    uint8_t tried;
    const slsk_font_t *title, *row, *small, *label, *mono, *mono_small;
    const slsk_entry_t *icon, *check;
} g_face;

static void sl_clipboard_faces(sl_ctx_t *ctx) {
    if (g_face.tried || !ctx || !ctx->skin_loaded) return;
    g_face.tried = 1u;
    const slsk_entry_t *e;
#define FACE(field, name) e = sl_skin_find(&ctx->skin, name); g_face.field = e ? sl_skin_font(&ctx->skin, e) : NULL;
    FACE(title, "ui-22-mid")
    FACE(row, "ui-15")
    FACE(small, "ui-13")
    FACE(label, "ui-14")
    FACE(mono, "mono-13")
    FACE(mono_small, "mono-12")
#undef FACE
    g_face.icon = sl_skin_find(&ctx->skin, "clip-24");
    g_face.check = sl_skin_find(&ctx->skin, "check-14");
}

static uint8_t sl_clipboard_soft(sl_ctx_t *ctx) {
    sl_clipboard_faces(ctx);
    return g_face.title && g_face.row && g_face.small && g_face.label && g_face.mono && g_face.mono_small;
}

// The cap height of a face: text sits on baseline = top + this.
static int32_t sl_cap(const slsk_font_t *f) { return f ? ((int32_t)f->px * 72 + 50) / 100 : 8; }

static float sl_clipboard_ease_out_cubic(float t) {
    float inv = 1.0f - t;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - (inv * inv * inv);
}

// ------------------------------------------------------------ text helpers

static uint32_t sl_utf8_chars(const char *s, uint32_t len) {
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < len; i++) if (((uint8_t)s[i] & 0xC0u) != 0x80u) n++;
    return n;
}

static uint32_t sl_lines(const char *s, uint32_t len) {
    uint32_t n = 1u;
    for (uint32_t i = 0u; i + 1u < len; i++) if (s[i] == '\n') n++;   // a last newline makes no line
    return n;
}

// The first line of src, tabs as spaces, cut with "…" to fit max_w in font.
static void sl_fit_line(const slsk_font_t *font, const char *src, uint32_t len, uint32_t max_w, char *out, uint32_t cap) {
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < len && n + 1u < cap; i++) {
        char c = src[i];
        if (c == '\n' || c == '\r') break;
        out[n++] = c == '\t' ? ' ' : c;
    }
    out[n] = '\0';
    if (!font || sl_text_width(font, out) <= max_w) return;
    static const char ell[] = "\xe2\x80\xa6";
    uint32_t ew = sl_text_width(font, ell);
    while (n > 0u) {
        do n--; while (n > 0u && ((uint8_t)out[n] & 0xC0u) == 0x80u);   // a whole character off
        out[n] = '\0';
        if (sl_text_width(font, out) + ew <= max_w) break;
    }
    if (n + 4u <= cap) memcpy(out + n, ell, 4u);
}

// "adesso", "14 min fa", "ieri"...
static void sl_age(uint64_t created_ns, uint64_t now_ns, char *out, size_t cap) {
    uint64_t s = (created_ns && now_ns > created_ns) ? (now_ns - created_ns) / 1000000000ull : 0ull;
    if (s < 60u) snprintf(out, cap, "%s", sl_tr("clip.now"));
    else if (s < 3600u) snprintf(out, cap, sl_tr("clip.minutes"), (unsigned)(s / 60u));
    else if (s < 86400u) snprintf(out, cap, sl_tr("clip.hours"), (unsigned)(s / 3600u));
    else if (s < 2u * 86400u) snprintf(out, cap, "%s", sl_tr("clip.yesterday"));
    else snprintf(out, cap, sl_tr("clip.days"), (unsigned)(s / 86400u));
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

// ------------------------------------------------------------ the history

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
    if (idx >= g_clipboard.count) return;
    g_clipboard.current_idx = 0u;
    if (idx == 0u) return;
    tmp = g_clipboard.items[idx];
    for (uint32_t i = idx; i > 0u; i--) {
        g_clipboard.items[i] = g_clipboard.items[i - 1u];
    }
    g_clipboard.items[0] = tmp;
}

// Takes a copy out of the history (Canc in the manager).
static void sl_clipboard_remove_index(uint32_t idx) {
    if (idx >= g_clipboard.count) return;
    sl_clipboard_item_free(&g_clipboard.items[idx]);
    for (uint32_t i = idx; i + 1u < g_clipboard.count; i++) g_clipboard.items[i] = g_clipboard.items[i + 1u];
    g_clipboard.count--;
    g_clipboard.items[g_clipboard.count] = (sl_clipboard_item_t){ NULL, 0u, 0ull };
    if (g_clipboard.current_idx == idx) g_clipboard.current_idx = 0u;
    else if (g_clipboard.current_idx > idx) g_clipboard.current_idx--;
    if (g_clipboard.manager_selected >= g_clipboard.count && g_clipboard.count > 0u)
        g_clipboard.manager_selected = g_clipboard.count - 1u;
    if (g_clipboard.count == 0u) g_clipboard.manager_selected = 0u;
}

// ------------------------------------------------------------ the card's geometry

static uint32_t sl_visible_rows(void) {
    return g_clipboard.count < SL_CLIPBOARD_VISIBLE ? g_clipboard.count : SL_CLIPBOARD_VISIBLE;
}

static uint32_t sl_first_row(uint32_t visible) {
    uint32_t start = 0u;
    if (visible == 0u) return 0u;
    if (g_clipboard.manager_selected >= visible) start = g_clipboard.manager_selected - visible + 1u;
    if (start + visible > g_clipboard.count) start = g_clipboard.count > visible ? g_clipboard.count - visible : 0u;
    return start;
}

static uint32_t sl_list_h(void) {
    uint32_t v = sl_visible_rows();
    return v ? v * CB_ROW_H + (v - 1u) * CB_ROW_GAP : CB_EMPTY_H;
}

// The card itself (without its shadow).
static void sl_card_rect(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint32_t cw = CB_W, chh = CB_PAD + CB_HEAD_H + CB_GAP + sl_list_h() + CB_GAP + CB_FOOT_H + CB_PAD_B;
    if (ctx->fb.width < cw + 48u) cw = ctx->fb.width > 48u ? ctx->fb.width - 48u : ctx->fb.width;
    if (ctx->fb.height < chh + 48u) chh = ctx->fb.height > 48u ? ctx->fb.height - 48u : ctx->fb.height;
    *w = cw;
    *h = chh;
    *x = ctx->fb.width > cw ? (ctx->fb.width - cw) / 2u : 0u;
    *y = ctx->fb.height > chh ? (ctx->fb.height - chh) / 2u : 0u;
}

void sl_clipboard_overlay_bounds(const sl_ctx_t *ctx,
                                 uint32_t *out_x, uint32_t *out_y,
                                 uint32_t *out_w, uint32_t *out_h) {
    uint32_t x = 0u, y = 0u, w = 0u, h = 0u;
    if (ctx) {
        sl_card_rect(ctx, &x, &y, &w, &h);
        // with its shadow
        uint32_t l = x > CB_SHADOW ? CB_SHADOW : x, t = y > CB_SHADOW ? CB_SHADOW : y;
        x -= l;
        y -= t;
        w += l + CB_SHADOW;
        h += t + CB_SHADOW;
        if (x + w > ctx->fb.width) w = ctx->fb.width - x;
        if (y + h > ctx->fb.height) h = ctx->fb.height - y;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static uint32_t sl_list_top(uint32_t card_y) { return card_y + CB_PAD + CB_HEAD_H + CB_GAP; }

// ------------------------------------------------------------ copying, pasting

void sl_clipboard_set_feedback_config(uint8_t audio_enabled, uint8_t visual_enabled) {
    g_feedback.audio_enabled = audio_enabled ? 1u : 0u;
    g_feedback.visual_enabled = visual_enabled ? 1u : 0u;
    if (!g_feedback.visual_enabled) {
        g_feedback.active = 0u;
        g_feedback.last_valid = 0u;
    }
}

uint8_t sl_clipboard_set_text(const char *text, uint32_t text_len) {
    char *copy;
    if (!text || text_len == 0u) return 0u;
    if (text_len > SL_CLIPBOARD_MAX_TEXT) text_len = SL_CLIPBOARD_MAX_TEXT;
    if (g_clipboard.count > 0u && sl_clipboard_text_equals(&g_clipboard.items[0], text, text_len)) {
        g_clipboard.current_idx = 0u;
        g_clipboard.items[0].created_ns = peos_time_ns();
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
    g_feedback.active = 0u;
    g_feedback.last_valid = 0u;
}

// The pill at progress (0..1): where, how big. A flight eases toward the
// pointer, getting a little smaller; the last quarter it says "Copiato".
static void sl_pill_text(float progress, const char **text, uint8_t *done) {
    *done = g_feedback.pasted || progress >= 0.75f;
    *text = g_feedback.pasted ? sl_tr("clip.pasted") : *done ? sl_tr("clip.copied") : g_feedback.label;
}

static void sl_clipboard_feedback_rect_for(float progress,
                                           uint32_t *out_x, uint32_t *out_y,
                                           uint32_t *out_w, uint32_t *out_h) {
    const char *text;
    uint8_t done;
    sl_pill_text(progress, &text, &done);
    const slsk_font_t *f = g_face.label;
    uint32_t tw = f ? sl_text_width(f, text) : sl_text_measure_width(text);
    uint32_t h = done ? 30u : 34u;
    uint32_t w = tw + (done ? 14u + 22u : 10u + 16u) + 14u;
    float eased = g_feedback.pasted ? 1.0f : sl_clipboard_ease_out_cubic(progress);
    int32_t cx = g_feedback.start_x + (int32_t)((float)(g_feedback.end_x - g_feedback.start_x) * eased);
    int32_t cy = g_feedback.start_y + (int32_t)((float)(g_feedback.end_y - g_feedback.start_y) * eased);
    if (out_x) *out_x = (cx > (int32_t)(w / 2u)) ? (uint32_t)(cx - (int32_t)(w / 2u)) : 0u;
    if (out_y) *out_y = (cy > (int32_t)(h / 2u)) ? (uint32_t)(cy - (int32_t)(h / 2u)) : 0u;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

// The pill's box with the shadow of its surface, for the damage.
static void sl_pill_damage(sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t m = 16u, l = x > m ? m : x, t = y > m ? m : y;
    sl_dirty_add(ctx, x - l, y - t, w + l + m, h + t + m);
}

static void sl_feedback_start(sl_ctx_t *ctx, uint32_t duration_ms) {
    uint32_t ox = 0u, oy = 0u, ow = 0u, oh = 0u;
    if (g_feedback.last_valid) sl_pill_damage(ctx, g_feedback.last_x, g_feedback.last_y, g_feedback.last_w, g_feedback.last_h);
    g_feedback.active = 1u;
    g_feedback.start_ns = peos_time_ns();
    g_feedback.duration_ms = duration_ms;
    sl_clipboard_feedback_rect_for(0.0f, &ox, &oy, &ow, &oh);
    g_feedback.last_x = ox;
    g_feedback.last_y = oy;
    g_feedback.last_w = ow;
    g_feedback.last_h = oh;
    g_feedback.last_valid = 1u;
    sl_pill_damage(ctx, ox, oy, ow, oh);
}

void sl_clipboard_notify_copy(sl_ctx_t *ctx, const char *text, uint32_t text_len,
                              int32_t start_x, int32_t start_y,
                              int32_t end_x, int32_t end_y) {
    if (g_feedback.audio_enabled) sl_audio_play_copy_effect();
    if (!g_feedback.visual_enabled || !ctx || !text || text_len == 0u) return;
    sl_clipboard_faces(ctx);
    sl_clipboard_compact_label(text, g_feedback.label, sizeof(g_feedback.label));
    g_feedback.pasted = 0u;
    g_feedback.start_x = start_x;
    g_feedback.start_y = start_y;
    g_feedback.end_x = end_x + 34;   // beside the pointer, not under it
    g_feedback.end_y = end_y - 22;
    sl_feedback_start(ctx, SL_CLIPBOARD_FEEDBACK_DURATION_MS);
}

// Caps+S: "Incollato" by the pointer.
static void sl_clipboard_notify_paste(sl_ctx_t *ctx) {
    if (!g_feedback.visual_enabled || !ctx || !g_clipboard.count) return;
    sl_clipboard_faces(ctx);
    g_feedback.pasted = 1u;
    g_feedback.start_x = g_feedback.end_x = (int32_t)ctx->cursor_x + 34;
    g_feedback.start_y = g_feedback.end_y = (int32_t)ctx->cursor_y - 22;
    sl_feedback_start(ctx, SL_CLIPBOARD_PASTE_DURATION_MS);
}

// ------------------------------------------------------------ every frame

static void sl_ring_tick(sl_ctx_t *ctx, uint64_t now_ns) {
    uint64_t held = 0ull;
    uint32_t pm = g_clipboard.manager_open ? 0u : sl_input_caps_hold_permille(now_ns, &held);
    uint8_t show = pm > 0u && held >= RING_SHOW_NS;
    uint32_t x = ctx->cursor_x > RING_SIZE / 2u ? ctx->cursor_x - RING_SIZE / 2u : 0u;
    uint32_t y = ctx->cursor_y > RING_SIZE / 2u ? ctx->cursor_y - RING_SIZE / 2u : 0u;
    if (!show && !g_ring.shown) return;
    if (show == g_ring.shown && x == g_ring.x && y == g_ring.y && pm / 8u == g_ring.permille / 8u) return;
    if (g_ring.shown) sl_dirty_add(ctx, g_ring.x, g_ring.y, RING_SIZE, RING_SIZE);
    g_ring.shown = show;
    g_ring.x = x;
    g_ring.y = y;
    g_ring.permille = pm;
    if (show) sl_dirty_add(ctx, x, y, RING_SIZE, RING_SIZE);
}

void sl_clipboard_tick(sl_ctx_t *ctx) {
    uint32_t cur_x = 0u, cur_y = 0u, cur_w = 0u, cur_h = 0u;
    if (!ctx) return;
    uint64_t now_ns = peos_time_ns();
    sl_ring_tick(ctx, now_ns);
    if (!g_feedback.active) return;
    if (g_feedback.duration_ms == 0u) g_feedback.duration_ms = SL_CLIPBOARD_FEEDBACK_DURATION_MS;
    float progress = (float)(now_ns - g_feedback.start_ns) / (float)((uint64_t)g_feedback.duration_ms * 1000000ull);
    if (progress >= 1.0f) {
        if (g_feedback.last_valid) sl_pill_damage(ctx, g_feedback.last_x, g_feedback.last_y, g_feedback.last_w, g_feedback.last_h);
        g_feedback.active = 0u;
        g_feedback.last_valid = 0u;
        return;
    }
    sl_clipboard_feedback_rect_for(progress, &cur_x, &cur_y, &cur_w, &cur_h);
    if (g_feedback.last_valid && cur_x == g_feedback.last_x && cur_y == g_feedback.last_y && cur_w == g_feedback.last_w &&
        cur_h == g_feedback.last_h)
        return;   // still: nothing to draw again
    if (g_feedback.last_valid) sl_pill_damage(ctx, g_feedback.last_x, g_feedback.last_y, g_feedback.last_w, g_feedback.last_h);
    g_feedback.last_x = cur_x;
    g_feedback.last_y = cur_y;
    g_feedback.last_w = cur_w;
    g_feedback.last_h = cur_h;
    g_feedback.last_valid = 1u;
    sl_pill_damage(ctx, cur_x, cur_y, cur_w, cur_h);
}

// ------------------------------------------------------------ input

static void sl_close_manager(uint8_t *out_changed, uint8_t *out_full_redraw) {
    g_clipboard.manager_open = 0u;
    if (out_changed) *out_changed = 1u;
    if (out_full_redraw) *out_full_redraw = 1u;
}

static void sl_use_item(uint32_t idx, uint8_t *out_changed, uint8_t *out_full_redraw) {
    if (idx >= g_clipboard.count) return;
    g_clipboard.manager_selected = 0u;
    sl_clipboard_promote_index(idx);
    sl_close_manager(out_changed, out_full_redraw);
}

uint8_t sl_clipboard_handle_input(sl_ctx_t *ctx, sl_input_frame_t *frame,
                                  uint8_t *out_changed, uint8_t *out_full_redraw) {
    uint32_t cx, cy, cw, ch;
    if (out_changed) *out_changed = 0u;
    if (out_full_redraw) *out_full_redraw = 0u;
    if (!ctx || !frame) return 0u;
    if ((frame->clipboard_cmds & SL_CLIP_CMD_PASTE) && !g_clipboard.manager_open) sl_clipboard_notify_paste(ctx);
    if (frame->clipboard_cmds & SL_CLIP_CMD_OPEN_MANAGER) {
        frame->clipboard_cmds &= ~SL_CLIP_CMD_OPEN_MANAGER;
        g_clipboard.manager_open = 1u;
        // the one in use selected first: Invio keeps it
        g_clipboard.manager_selected = g_clipboard.current_idx < g_clipboard.count ? g_clipboard.current_idx : 0u;
        if (g_ring.shown) sl_dirty_add(ctx, g_ring.x, g_ring.y, RING_SIZE, RING_SIZE);
        g_ring.shown = 0u;
        if (out_changed) *out_changed = 1u;
        if (out_full_redraw) *out_full_redraw = 1u;
        return 1u;
    }
    if (!g_clipboard.manager_open) return 0u;

    sl_card_rect(ctx, &cx, &cy, &cw, &ch);
    uint32_t visible = sl_visible_rows(), start_idx = sl_first_row(visible);
    uint32_t list_x = cx + CB_PAD, list_y = sl_list_top(cy);
    uint32_t list_w = cw - 2u * CB_PAD - (g_clipboard.count > SL_CLIPBOARD_VISIBLE ? CB_SCROLL_W + 10u : 0u);

    for (uint8_t i = 0u; i < frame->key_event_count; i++) {
        peos_input_event_t *ev = &frame->key_events[i];
        uint16_t code = ev->code;
        uint16_t low = (uint16_t)(code & 0xFFu);
        if (!ev->pressed) continue;
        if (code == PEOS_KEY_ESC) {
            sl_close_manager(out_changed, out_full_redraw);
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
            sl_use_item(g_clipboard.manager_selected, out_changed, out_full_redraw);
            return 1u;
        }
        // 1..9: that one at once (the scancodes 0x02..0x0A)
        if (code >= 0x02u && code <= 0x0Au) {
            uint32_t idx = (uint32_t)(code - 0x02u);
            if (idx < g_clipboard.count) sl_use_item(idx, out_changed, out_full_redraw);
            return 1u;
        }
        // Canc: out of the history (the card gets shorter: all again)
        if (low == 0x53u && g_clipboard.count > 0u) {
            sl_clipboard_remove_index(g_clipboard.manager_selected);
            if (out_changed) *out_changed = 1u;
            if (out_full_redraw) *out_full_redraw = 1u;
            return 1u;
        }
    }

    if (frame->mouse_left_pressed) {
        if (ctx->cursor_x < cx || ctx->cursor_x >= cx + cw || ctx->cursor_y < cy || ctx->cursor_y >= cy + ch) {
            sl_close_manager(out_changed, out_full_redraw);
            return 1u;
        }
        for (uint32_t row = 0u; row < visible; row++) {
            uint32_t ry = list_y + row * (CB_ROW_H + CB_ROW_GAP);
            if (ctx->cursor_x >= list_x && ctx->cursor_x < list_x + list_w && ctx->cursor_y >= ry && ctx->cursor_y < ry + CB_ROW_H) {
                sl_use_item(start_idx + row, out_changed, out_full_redraw);
                return 1u;
            }
        }
    }
    if (frame->mouse_wheel_y != 0 && g_clipboard.count > 0u) {   // the wheel moves the choice
        uint32_t sel = g_clipboard.manager_selected;
        if (frame->mouse_wheel_y > 0 && sel > 0u) sel--;
        else if (frame->mouse_wheel_y < 0 && sel + 1u < g_clipboard.count) sel++;
        if (sel != g_clipboard.manager_selected) {
            g_clipboard.manager_selected = sel;
            if (out_changed) *out_changed = 1u;
        }
    }
    return 1u;
}

// ------------------------------------------------------------ drawing

typedef struct {
    sl_target_t t;
    sl_ui_color_t ink, muted;
    uint8_t night;
} sl_cb_draw_t;

static void sl_text(sl_cb_draw_t *d, const slsk_font_t *f, int32_t x, int32_t top, const char *s, uint8_t r, uint8_t g,
                    uint8_t b) {
    (void)sl_text_draw(&d->t, f, x, top + sl_cap(f), s, r, g, b);
}

// A key cap ("Invio", "Esc", "1–9"): a small raised surface with mono text.
static uint32_t sl_kbd(sl_ctx_t *ctx, sl_cb_draw_t *d, uint32_t x, uint32_t y, const char *s) {
    uint32_t tw = sl_text_width(g_face.mono_small, s), w = tw + 16u < 26u ? 26u : tw + 16u;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED_HI, x, y, w, 26u, 9u);
    sl_text(d, g_face.mono_small, (int32_t)(x + (w - tw) / 2u), (int32_t)(y + 8u), s, d->ink.r, d->ink.g, d->ink.b);
    return w;
}

// "↑ ↓ scegli": the caps, then the word; returns the x after it.
static uint32_t sl_key_hint(sl_ctx_t *ctx, sl_cb_draw_t *d, uint32_t x, uint32_t y, const char *k1, const char *k2,
                            const char *word) {
    x += sl_kbd(ctx, d, x, y, k1) + 6u;
    if (k2) x += sl_kbd(ctx, d, x, y, k2) + 6u;
    x += 2u;
    sl_text(d, g_face.small, (int32_t)x, (int32_t)(y + 7u), word, d->muted.r, d->muted.g, d->muted.b);
    return x + sl_text_width(g_face.small, word) + 18u;
}

static void sl_draw_mask(sl_cb_draw_t *d, sl_ctx_t *ctx, const slsk_entry_t *m, int32_t x, int32_t y, uint8_t r,
                         uint8_t g, uint8_t b) {
    if (m) sl_blit_mask(&d->t, sl_skin_data(&ctx->skin, m), m->width, m->height, m->width, x, y, r, g, b, 255u);
}

static void sl_render_row(sl_ctx_t *ctx, sl_cb_draw_t *d, uint32_t idx, uint32_t x, uint32_t y, uint32_t w,
                          uint64_t now_ns) {
    const sl_clipboard_item_t *it = &g_clipboard.items[idx];
    uint8_t sel = idx == g_clipboard.manager_selected, in_use = idx == g_clipboard.current_idx;
    if (sel) {   // raised, with the neon ring around it
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, x - 3u, y - 3u, w + 6u, CB_ROW_H + 6u, 23u);
        sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED_HI, x - 1u, y - 1u, w + 2u, CB_ROW_H + 2u, 21u);
    }
    // the number key
    char key[4];
    if (idx < 9u) snprintf(key, sizeof(key), "%u", (unsigned)(idx + 1u));
    else snprintf(key, sizeof(key), "\xc2\xb7");
    uint32_t kx = x + 16u, ky = y + (CB_ROW_H - 26u) / 2u;
    if (sel) sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, kx, ky, 26u, 26u, 9u);
    uint32_t kw = sl_text_width(g_face.mono, key);
    if (sel) sl_text(d, g_face.mono, (int32_t)(kx + (26u - kw) / 2u), (int32_t)(ky + 8u), key, NEON_R, NEON_G, NEON_B);
    else sl_text(d, g_face.mono, (int32_t)(kx + (26u - kw) / 2u), (int32_t)(ky + 8u), key, d->muted.r, d->muted.g, d->muted.b);

    // the pill "IN USO" at the right
    uint32_t right = x + w - 16u;
    if (in_use) {
        const char *s = sl_tr("clip.in_use");
        uint32_t tw = sl_text_width(g_face.small, s), pw = tw + 34u;
        uint32_t px = right - pw, py = y + (CB_ROW_H - 26u) / 2u;
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, px, py, pw, 26u, 13u);
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, px + 10u, py + 10u, 6u, 6u, 3u);
        sl_text(d, g_face.small, (int32_t)(px + 22u), (int32_t)(py + 7u), s, NEON_R, NEON_G, NEON_B);
        right = px - 12u;
    }

    // the text, its first line, and under it the age and the size
    uint32_t tx = kx + 26u + 14u, tw = right > tx ? right - tx : 0u;
    char line[256], meta[96], age[40], size[40];
    sl_fit_line(g_face.row, it->text ? it->text : "", it->len, tw, line, sizeof(line));
    sl_age(it->created_ns, now_ns, age, sizeof(age));
    uint32_t lines = it->text ? sl_lines(it->text, it->len) : 1u;
    if (lines > 1u) snprintf(size, sizeof(size), sl_tr("clip.lines"), (unsigned)lines);
    else snprintf(size, sizeof(size), sl_tr("clip.chars"), (unsigned)(it->text ? sl_utf8_chars(it->text, it->len) : 0u));
    snprintf(meta, sizeof(meta), "%s \xc2\xb7 %s", age, size);
    sl_text(d, g_face.row, (int32_t)tx, (int32_t)(y + 15u), line, d->ink.r, d->ink.g, d->ink.b);
    sl_text(d, g_face.small, (int32_t)tx, (int32_t)(y + 38u), meta, d->muted.r, d->muted.g, d->muted.b);
}

static void sl_render_manager(sl_ctx_t *ctx, sl_cb_draw_t *d) {
    uint32_t cx, cy, cw, ch;
    uint64_t now_ns = peos_time_ns();
    sl_card_rect(ctx, &cx, &cy, &cw, &ch);
    // the veil: night, light enough to see where one was
    sl_fill_rect_a(ctx, 0u, 0u, ctx->fb.width, ctx->fb.height, 17u, 22u, 38u, d->night ? 115u : 87u);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED, cx, cy, cw, ch, CB_RADIUS);

    // the header: the icon in its well, the title, what Caps+S does, the count
    uint32_t x = cx + CB_PAD, y = cy + CB_PAD, inner = cw - 2u * CB_PAD;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, x, y, CB_HEAD_H, CB_HEAD_H, CB_HEAD_H / 2u);
    if (g_face.icon) {
        uint8_t r = d->night ? NEON_R : d->ink.r, g = d->night ? NEON_G : d->ink.g, b = d->night ? NEON_B : d->ink.b;
        sl_draw_mask(d, ctx, g_face.icon, (int32_t)(x + (CB_HEAD_H - g_face.icon->width) / 2u),
                     (int32_t)(y + (CB_HEAD_H - g_face.icon->height) / 2u), r, g, b);
    }
    uint32_t hx = x + CB_HEAD_H + 16u;
    sl_text(d, g_face.title, (int32_t)hx, (int32_t)(y + 5u), sl_tr("clip.title"), d->ink.r, d->ink.g, d->ink.b);
    sl_text(d, g_face.label, (int32_t)hx, (int32_t)(y + 33u),
            sl_tr(g_clipboard.count ? "clip.subtitle" : "clip.subtitle_empty"), d->muted.r, d->muted.g, d->muted.b);
    if (g_clipboard.count) {
        char pos[32];
        snprintf(pos, sizeof(pos), sl_tr("clip.position"), (unsigned)(g_clipboard.manager_selected + 1u),
                 (unsigned)g_clipboard.count);
        uint32_t pw = sl_text_width(g_face.mono, pos);
        sl_text(d, g_face.mono, (int32_t)(x + inner - pw), (int32_t)(y + 20u), pos, d->muted.r, d->muted.g, d->muted.b);
    }

    // the copies, or how to make one
    uint32_t ly = sl_list_top(cy);
    if (!g_clipboard.count) {
        sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, x, ly, inner, CB_EMPTY_H, 24u);
        const char *t1 = sl_tr("clip.empty_title"), *t2 = sl_tr("clip.empty_hint");
        uint32_t w1 = sl_text_width(g_face.row, t1), w2 = sl_text_width(g_face.label, t2);
        uint32_t kw = sl_text_width(g_face.mono_small, "Caps") + 16u + 10u + 26u + 10u;
        uint32_t kx = x + (inner > kw ? (inner - kw) / 2u : 0u);
        kx += sl_kbd(ctx, d, kx, ly + 28u, "Caps") + 6u;
        sl_text(d, g_face.label, (int32_t)kx, (int32_t)(ly + 34u), "+", d->muted.r, d->muted.g, d->muted.b);
        (void)sl_kbd(ctx, d, kx + 14u, ly + 28u, "A");
        sl_text(d, g_face.row, (int32_t)(x + (inner > w1 ? (inner - w1) / 2u : 0u)), (int32_t)(ly + 74u), t1, d->ink.r,
                d->ink.g, d->ink.b);
        sl_text(d, g_face.label, (int32_t)(x + (inner > w2 ? (inner - w2) / 2u : 0u)), (int32_t)(ly + 102u), t2,
                d->muted.r, d->muted.g, d->muted.b);
    } else {
        uint32_t visible = sl_visible_rows(), start = sl_first_row(visible);
        uint8_t scroll = g_clipboard.count > SL_CLIPBOARD_VISIBLE;
        uint32_t lw = inner - (scroll ? CB_SCROLL_W + 10u : 0u);
        for (uint32_t r = 0u; r < visible; r++)
            sl_render_row(ctx, d, start + r, x, ly + r * (CB_ROW_H + CB_ROW_GAP), lw, now_ns);
        if (scroll) {   // where these are among them all
            uint32_t sx = x + inner - CB_SCROLL_W, sh = sl_list_h();
            uint32_t th = sh * visible / g_clipboard.count, max_start = g_clipboard.count - visible;
            uint32_t ty = ly + (max_start ? (sh - th) * start / max_start : 0u);
            sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, sx, ly, CB_SCROLL_W, sh, CB_SCROLL_W / 2u);
            sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, sx + 1u, ty, CB_SCROLL_W - 2u, th, (CB_SCROLL_W - 2u) / 2u);
        }
    }

    // the keys
    uint32_t fy = ly + sl_list_h() + CB_GAP;
    sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, x, fy, inner, CB_FOOT_H, 18u);
    uint32_t ky = fy + (CB_FOOT_H - 26u) / 2u, kx = x + 16u;
    if (g_clipboard.count) {
        kx = sl_key_hint(ctx, d, kx, ky, "\xe2\x86\x91", "\xe2\x86\x93", sl_tr("clip.key_choose"));
        kx = sl_key_hint(ctx, d, kx, ky, "1\xe2\x80\x93" "9", NULL, sl_tr("clip.key_now"));
        kx = sl_key_hint(ctx, d, kx, ky, sl_tr("clip.key_enter"), NULL, sl_tr("clip.key_use"));
        kx = sl_key_hint(ctx, d, kx, ky, sl_tr("clip.key_del"), NULL, sl_tr("clip.key_remove"));
    } else {
        kx = sl_key_hint(ctx, d, kx, ky, "Caps", NULL, sl_tr("clip.key_hold"));
    }
    const char *esc = "Esc", *close = sl_tr("clip.key_close");
    uint32_t ew = sl_text_width(g_face.mono_small, esc) + 16u, cwid = sl_text_width(g_face.small, close);
    uint32_t ex = x + inner - 16u - cwid - 8u - (ew < 26u ? 26u : ew);
    if (ex > kx) (void)sl_key_hint(ctx, d, ex, ky, esc, NULL, close);
}

// The pill: night, a neon edge, the copied text (or "Copiato" with a tick).
static void sl_render_pill(sl_ctx_t *ctx, sl_cb_draw_t *d, float progress) {
    const char *text;
    uint8_t done;
    uint32_t x, y, w, h;
    sl_pill_text(progress, &text, &done);
    sl_clipboard_feedback_rect_for(progress, &x, &y, &w, &h);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, x - 1u, y - 1u, w + 2u, h + 2u, (h + 2u) / 2u);
    sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT_FLAT, x, y, w, h, h / 2u);
    uint32_t tx = x + 14u;
    if (done && g_face.check) {
        sl_draw_mask(d, ctx, g_face.check, (int32_t)(x + 12u), (int32_t)(y + (h - g_face.check->height) / 2u), NEON_R,
                     NEON_G, NEON_B);
        tx = x + 12u + g_face.check->width + 8u;
    } else if (!done) {
        sl_ui_ctx_surface(ctx, SL_UI_SURF_NEON_DOT, x + 10u, y + (h - 8u) / 2u, 8u, 8u, 4u);
        tx = x + 26u;
    }
    sl_text(d, g_face.label, (int32_t)tx, (int32_t)(y + (h - (uint32_t)sl_cap(g_face.label)) / 2u), text, 0xE8u, 0xF4u,
            0xFFu);
}

// Without the skin (it did not load): the old plain drawing.
static void sl_render_plain(sl_ctx_t *ctx) {
    if (g_feedback.active && g_feedback.visual_enabled) {
        uint32_t x, y, w, h;
        float progress = (float)(sl_frame_time_ns() - g_feedback.start_ns) / (float)((uint64_t)g_feedback.duration_ms * 1000000ull);
        if (progress < 1.0f) {
            sl_clipboard_feedback_rect_for(progress, &x, &y, &w, &h);
            sl_fill_rect_a(ctx, x, y, w, h, 60u, 92u, 138u, 160u);
            sl_draw_text(ctx, x + 6u, y + 4u, g_feedback.label, 255u, 255u, 255u);
        }
    }
    if (!g_clipboard.manager_open) return;
    uint32_t cx, cy, cw, ch;
    char line[56];
    sl_card_rect(ctx, &cx, &cy, &cw, &ch);
    sl_fill_rect_a(ctx, 0u, 0u, ctx->fb.width, ctx->fb.height, 0u, 0u, 0u, 112u);
    sl_fill_rect(ctx, cx, cy, cw, ch, 18u, 20u, 26u);
    sl_draw_text(ctx, cx + 12u, cy + 10u, sl_tr("clip.title"), 230u, 236u, 245u);
    uint32_t visible = sl_visible_rows(), start = sl_first_row(visible), ly = sl_list_top(cy);
    for (uint32_t r = 0u; r < visible; r++) {
        uint32_t idx = start + r, iy = ly + r * (CB_ROW_H + CB_ROW_GAP);
        if (idx == g_clipboard.manager_selected) sl_fill_rect(ctx, cx + 10u, iy, cw - 20u, CB_ROW_H, 52u, 84u, 130u);
        sl_clipboard_compact_label(g_clipboard.items[idx].text, line, sizeof(line));
        sl_draw_text(ctx, cx + 16u, iy + 24u, line, 230u, 236u, 245u);
    }
    if (g_clipboard.count == 0u) sl_draw_text(ctx, cx + 16u, ly + 20u, sl_tr("clip.empty_title"), 150u, 164u, 184u);
}

void sl_clipboard_render_overlay(sl_ctx_t *ctx) {
    if (!ctx) return;
    if (!sl_clipboard_soft(ctx)) {
        sl_render_plain(ctx);
        return;
    }
    sl_cb_draw_t d;
    d.t = sl_target_from_ctx(ctx);
    d.ink = *sl_ui_runtime_color(SL_UI_COLOR_TEXT_PRIMARY);
    d.muted = *sl_ui_runtime_color(SL_UI_COLOR_TEXT_SECONDARY);
    d.night = sl_ui_runtime_night();

    if (g_ring.shown && !g_clipboard.manager_open) {   // Caps held: how long until it opens
        const uint8_t track_day[3] = { 0x9Au, 0xA0u, 0xACu }, track_night[3] = { 0x3Au, 0x45u, 0x60u };
        const uint8_t fill[3] = { NEON_R, NEON_G, NEON_B };
        sl_ui_ctx_ring(ctx, g_ring.x, g_ring.y, RING_SIZE, RING_THICK, g_ring.permille, d.night ? track_night : track_day,
                       fill);
    }
    if (g_clipboard.manager_open) sl_render_manager(ctx, &d);
    if (g_feedback.active && g_feedback.visual_enabled) {
        float progress = (float)(sl_frame_time_ns() - g_feedback.start_ns) / (float)((uint64_t)g_feedback.duration_ms * 1000000ull);
        if (progress < 1.0f) sl_render_pill(ctx, &d, progress < 0.0f ? 0.0f : progress);
    }
}
