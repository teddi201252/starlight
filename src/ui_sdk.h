#pragma once

#include "starlight_internal.h"
#include "render/pixels.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} sl_ui_color_t;

typedef enum {
    SL_UI_COLOR_BG_CANVAS = 0,
    SL_UI_COLOR_BG_PANEL,
    SL_UI_COLOR_BG_CONTROL,
    SL_UI_COLOR_BG_CONTROL_HOVER,
    SL_UI_COLOR_BG_CONTROL_ACTIVE,
    SL_UI_COLOR_BORDER,
    SL_UI_COLOR_BORDER_FOCUS,
    SL_UI_COLOR_TEXT_PRIMARY,
    SL_UI_COLOR_TEXT_SECONDARY,
    SL_UI_COLOR_TEXT_DISABLED,
    SL_UI_COLOR_ACCENT,
    SL_UI_COLOR_ACCENT_HOVER,
    SL_UI_COLOR_ACCENT_ACTIVE,
    SL_UI_COLOR_DANGER,
    SL_UI_COLOR_SUCCESS,
    SL_UI_COLOR_WARNING,
    SL_UI_COLOR_COUNT
} sl_ui_color_token_t;

typedef struct {
    sl_ui_color_t colors[SL_UI_COLOR_COUNT];
} sl_ui_theme_t;

typedef struct {
    sl_ctx_t *ctx;
    const sl_window_t *win;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t mouse_down;
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    int32_t wheel_x;
    int32_t wheel_y;
    uint8_t wheel_consumed;
    uint32_t active_id;
    uint32_t focused_id;
    uint8_t shift_down;
    uint8_t ctrl_down;
    uint8_t alt_down;
    uint8_t altgr_down;
    uint8_t super_down;
    uint32_t text_caret_id;
    uint32_t text_caret_pos;
    uint32_t text_input_scroll_col;
    uint32_t text_area_scroll_line;
    uint32_t text_area_scroll_col;
    uint32_t select_open_id;
    uint8_t select_overlay_pending;
    uint32_t select_overlay_id;
    uint32_t select_overlay_x;
    uint32_t select_overlay_y;
    uint32_t select_overlay_w;
    uint32_t select_overlay_row_h;
    uint32_t select_overlay_start_idx;
    uint32_t select_overlay_visible_items;
    uint32_t select_overlay_option_count;
    uint32_t select_overlay_selected;
    const char *select_overlay_options_pipe;
    uint32_t select_scroll_idx;
    uint32_t font_px;
    sl_ui_theme_t theme;
    uint8_t theme_initialized;
    uint32_t theme_version;
} sl_ui_ctx_t;

typedef enum {
    SL_UI_STACK_HORIZONTAL = 0,
    SL_UI_STACK_VERTICAL = 1,
} sl_ui_stack_axis_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t spacing;
    uint32_t cursor;
    sl_ui_stack_axis_t axis;
} sl_ui_stack_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t cols;
    uint32_t rows;
    uint32_t gap_x;
    uint32_t gap_y;
    uint32_t cursor;
} sl_ui_grid_t;

typedef enum {
    SL_UI_STATE_NORMAL = 0,
    SL_UI_STATE_HOVER,
    SL_UI_STATE_ACTIVE,
    SL_UI_STATE_DISABLED,
    SL_UI_STATE_FOCUSED,
} sl_ui_control_state_t;

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released,
                 int32_t wheel_x, int32_t wheel_y);
void sl_ui_theme_set_default(sl_ui_theme_t *theme);
void sl_ui_theme_apply(sl_ui_ctx_t *ui, const sl_ui_theme_t *theme);
void sl_ui_theme_reset(sl_ui_ctx_t *ui);
void sl_ui_theme_set_accent(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b);
void sl_ui_theme_set_accent_a(sl_ui_ctx_t *ui, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
const sl_ui_color_t *sl_ui_theme_color(const sl_ui_ctx_t *ui, sl_ui_color_token_t token);
void sl_ui_theme_runtime_set(const sl_ui_theme_t *theme);
void sl_ui_theme_runtime_get(sl_ui_theme_t *out);
void sl_ui_set_font_px(sl_ui_ctx_t *ui, uint32_t font_px);
uint32_t sl_ui_get_font_px(const sl_ui_ctx_t *ui);

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                 sl_ui_color_token_t token);
void sl_ui_label_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
                     uint8_t r, uint8_t g, uint8_t b);

uint8_t sl_ui_button(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char *text);
uint8_t sl_ui_button_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char *text, uint8_t enabled);

uint8_t sl_ui_slider_u32(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w,
                         uint32_t min_v, uint32_t max_v, uint32_t *value);
uint8_t sl_ui_slider_u32_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w,
                               uint32_t min_v, uint32_t max_v, uint32_t *value,
                               uint8_t enabled);

uint8_t sl_ui_toggle(sl_ui_ctx_t *ui, uint32_t id,
                     uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint8_t *value, const char *text);
uint8_t sl_ui_toggle_state(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint8_t *value, const char *text, uint8_t enabled);

uint8_t sl_ui_select_cycle(sl_ui_ctx_t *ui, uint32_t id,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t option_count, uint32_t *selected,
                           const char *current_text,
                           const char *options_pipe);
uint8_t sl_ui_select_cycle_state(sl_ui_ctx_t *ui, uint32_t id,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 uint32_t option_count, uint32_t *selected,
                                 const char *current_text, const char *options_pipe,
                                 uint8_t enabled);
uint8_t sl_ui_select_cycle_state_ex(sl_ui_ctx_t *ui, uint32_t id,
                                    uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    uint32_t option_count, uint32_t *selected,
                                    const char *current_text, const char *options_pipe,
                                    const peos_input_event_t *events, uint8_t event_count,
                                    uint8_t enabled);

uint8_t sl_ui_text_input(sl_ui_ctx_t *ui, uint32_t id,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         char *text, uint32_t text_cap,
                         const peos_input_event_t *events, uint8_t event_count);
uint8_t sl_ui_text_input_state(sl_ui_ctx_t *ui, uint32_t id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                               char *text, uint32_t text_cap,
                               const peos_input_event_t *events, uint8_t event_count,
                               uint8_t enabled);
uint8_t sl_ui_text_area(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        char *text, uint32_t text_cap,
                        const peos_input_event_t *events, uint8_t event_count);
uint8_t sl_ui_text_area_state(sl_ui_ctx_t *ui, uint32_t id,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              char *text, uint32_t text_cap,
                              const peos_input_event_t *events, uint8_t event_count,
                              uint8_t enabled);

uint8_t sl_ui_scroll_v(sl_ui_ctx_t *ui, uint32_t id,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t content_h, uint32_t *offset,
                       uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h);
uint8_t sl_ui_scroll_v_state(sl_ui_ctx_t *ui, uint32_t id,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t content_h, uint32_t *offset,
                             uint32_t hit_x, uint32_t hit_y, uint32_t hit_w, uint32_t hit_h,
                             uint8_t enabled);

void sl_ui_image_rgba(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      const uint8_t *rgba, uint32_t src_w, uint32_t src_h, uint32_t src_stride);
void sl_ui_image(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const sl_image_t *image);
void sl_ui_rect(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                sl_ui_color_token_t token);
void sl_ui_rect_a(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  sl_ui_color_token_t token, uint8_t a);
void sl_ui_rect_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint8_t r, uint8_t g, uint8_t b);
void sl_ui_rect_a_rgb(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sl_ui_separator(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w);
uint8_t sl_ui_list_item(sl_ui_ctx_t *ui, uint32_t id,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const char *title, const char *subtitle, uint8_t enabled);

void sl_ui_stack_begin(sl_ui_stack_t *stack,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       sl_ui_stack_axis_t axis, uint32_t spacing);

uint8_t sl_ui_stack_next(sl_ui_stack_t *stack, uint32_t primary_size,
                         uint32_t *out_x, uint32_t *out_y,
                         uint32_t *out_w, uint32_t *out_h);

void sl_ui_grid_begin(sl_ui_grid_t *grid,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t cols, uint32_t rows,
                      uint32_t gap_x, uint32_t gap_y);

uint8_t sl_ui_grid_cell(const sl_ui_grid_t *grid, uint32_t col, uint32_t row,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h);

uint8_t sl_ui_grid_next(sl_ui_grid_t *grid,
                        uint32_t *out_x, uint32_t *out_y,
                        uint32_t *out_w, uint32_t *out_h);

void sl_ui_draw_overlays(sl_ui_ctx_t *ui);
