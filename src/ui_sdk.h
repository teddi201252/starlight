#pragma once

#include "starlight_internal.h"
#include "render/pixels.h"

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
    uint32_t text_caret_id;
    uint32_t text_caret_pos;
    uint32_t text_area_scroll_line;
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

void sl_ui_begin(sl_ui_ctx_t *ui, sl_ctx_t *ctx, const sl_window_t *win,
                 uint32_t cursor_x, uint32_t cursor_y,
                 uint8_t mouse_down, uint8_t mouse_pressed, uint8_t mouse_released,
                 int32_t wheel_x, int32_t wheel_y);

void sl_ui_label(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, const char *text,
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
                           const char *current_text);
uint8_t sl_ui_select_cycle_state(sl_ui_ctx_t *ui, uint32_t id,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 uint32_t option_count, uint32_t *selected,
                                 const char *current_text, uint8_t enabled);

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
void sl_ui_rect(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                uint8_t r, uint8_t g, uint8_t b);
void sl_ui_rect_a(sl_ui_ctx_t *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
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
