#include "context_menu_renderer.h"

#include "render/pixels.h"
#include "ui_sdk.h"

void sl_context_menu_draw(sl_ctx_t *ctx,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t row_h,
                          uint32_t option_count,
                          uint32_t start_idx,
                          uint32_t visible_items,
                          int32_t hover_row,
                          uint8_t has_scrollbar,
                          sl_context_menu_label_fn label_fn,
                          void *label_userdata) {
    if (!ctx || w == 0u || h == 0u || row_h == 0u || option_count == 0u) return;
    if (visible_items == 0u) return;
    if (visible_items > option_count) visible_items = option_count;

    uint32_t max_start = (option_count > visible_items) ? (option_count - visible_items) : 0u;
    if (start_idx > max_start) start_idx = max_start;
    if (has_scrollbar && !(option_count > visible_items && w > 20u)) has_scrollbar = 0u;

    uint32_t scrollbar_w = has_scrollbar ? 8u : 0u;
    uint32_t list_w = (w > scrollbar_w) ? (w - scrollbar_w) : w;

    const slsk_font_t *face = sl_ui_ctx_face(ctx);
    if (face) {
        // Soft UI: a raised card, the row under the mouse sunk, Outfit ink.
        sl_ui_ctx_surface(ctx, SL_UI_SURF_RAISED_HI, x, y, w, h, 14u);
    } else {
        sl_fill_rect_a(ctx, x + 1u, y + h, w, 2u, 0u, 0u, 0u, 72u);
        sl_fill_rect_a(ctx, x + w, y + 1u, 2u, h + 1u, 0u, 0u, 0u, 72u);
        sl_fill_rect(ctx, x, y, w, h, 26u, 31u, 39u);
        sl_fill_rect(ctx, x, y, w, 1u, 92u, 108u, 126u);
        sl_fill_rect(ctx, x, y + h - 1u, w, 1u, 92u, 108u, 126u);
        sl_fill_rect(ctx, x, y, 1u, h, 92u, 108u, 126u);
        sl_fill_rect(ctx, x + w - 1u, y, 1u, h, 92u, 108u, 126u);
    }

    uint32_t cell_h = face ? ((uint32_t)face->px * 72u + 50u) / 100u : sl_text_cell_h();
    if (cell_h == 0u) cell_h = 8u;
    for (uint32_t i = 0u; i < visible_items; i++) {
        uint32_t opt_idx = start_idx + i;
        if (opt_idx >= option_count) break;
        uint32_t iy = y + 1u + i * row_h;
        uint8_t hover = ((hover_row >= 0) && ((uint32_t)hover_row == i)) ? 1u : 0u;
        if (hover && list_w > 8u) {
            if (face) sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, x + 4u, iy, list_w - 8u, row_h, row_h / 2u > 10u ? 10u : row_h / 2u);
            else sl_fill_rect(ctx, x + 1u, iy, list_w - 2u, row_h, 62u, 74u, 92u);
        }
        if (label_fn) {
            char label[128];
            uint32_t ty = iy + ((row_h > cell_h) ? ((row_h - cell_h) / 2u) : 0u);
            label[0] = '\0';
            label_fn(opt_idx, label, sizeof(label), label_userdata);
            const sl_ui_color_t *ink = sl_ui_runtime_color(SL_UI_COLOR_TEXT_PRIMARY);
            if (face) (void)sl_draw_text_face(ctx, face, (int32_t)(x + 12u), (int32_t)(ty + cell_h), label, ink->r, ink->g, ink->b);
            else sl_draw_text(ctx, x + 8u, ty, label, 236u, 240u, 244u);
        }
    }

    if (has_scrollbar) {
        uint32_t sx = x + list_w;
        uint32_t sy = y + 1u;
        uint32_t sw = w - list_w;
        uint32_t sh = (h > 2u) ? (h - 2u) : 0u;
        if (sw > 0u && sh > 0u) {
            if (!face) sl_fill_rect(ctx, sx, sy, sw, sh, 16u, 20u, 26u);
            uint32_t track_x = sx + 2u;
            uint32_t track_y = sy + 1u;
            uint32_t track_w = (sw > 4u) ? (sw - 4u) : sw;
            uint32_t track_h = (sh > 2u) ? (sh - 2u) : sh;
            if (track_w > 0u && track_h > 0u && max_start > 0u) {
                uint32_t thumb_h = (uint32_t)(((uint64_t)visible_items * (uint64_t)track_h) / (uint64_t)option_count);
                if (thumb_h < 8u) thumb_h = 8u;
                if (thumb_h > track_h) thumb_h = track_h;
                uint32_t travel = track_h - thumb_h;
                uint32_t thumb_y = track_y;
                if (travel > 0u) {
                    thumb_y += (uint32_t)(((uint64_t)start_idx * (uint64_t)travel) / (uint64_t)max_start);
                }
                if (face) {
                    sl_ui_ctx_surface(ctx, SL_UI_SURF_INSET, track_x, track_y, track_w, track_h, track_w / 2u);
                    sl_ui_ctx_surface(ctx, SL_UI_SURF_NIGHT, track_x, thumb_y, track_w, thumb_h, track_w / 2u);
                } else {
                    sl_fill_rect(ctx, track_x, track_y, track_w, track_h, 38u, 44u, 54u);
                    sl_fill_rect(ctx, track_x, thumb_y, track_w, thumb_h, 100u, 112u, 130u);
                }
            }
        }
    }
}
