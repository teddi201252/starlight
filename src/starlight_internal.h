#pragma once

#include "starlight.h"
#include "window.h"

#include <stddef.h>
#include <stdint.h>

#define SL_CURSOR_W 3u
#define SL_CURSOR_H 3u

void sl_audio_init(void);
void sl_audio_shutdown(void);

int sl_init(sl_ctx_t *ctx);
void sl_shutdown(sl_ctx_t *ctx);
int sl_handle_input(sl_ctx_t *ctx);
void sl_render_full(sl_ctx_t *ctx);
void sl_render_dirty_background(sl_ctx_t *ctx);
int sl_present(sl_ctx_t *ctx);
void sl_frame_pace(uint64_t *next_deadline_ns);

void sl_cursor_init(sl_ctx_t *ctx);
void sl_render_cursor(sl_ctx_t *ctx);
void sl_render_cursor_at(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_sw_cursor_capture(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_sw_cursor_restore(sl_ctx_t *ctx);
void sl_hw_cursor_sync_pos(sl_ctx_t *ctx);

void sl_windows_create_test(sl_ctx_t *ctx, size_t count);
void sl_windows_create_default(sl_ctx_t *ctx);
void sl_windows_render_all(sl_ctx_t *ctx);
void sl_windows_focus_at_cursor(uint32_t x, uint32_t y);
int sl_windows_close_at_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_windows_destroy_all(void);
void sl_windows_mark_focused_activity(void);
void sl_window_move(sl_window_t *win, uint32_t new_x, uint32_t new_y);
sl_window_t* sl_window_get_focused(void);
void sl_launcher_render(sl_ctx_t *ctx);
int sl_launcher_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_launcher_handle_hover(sl_ctx_t *ctx);
uint32_t sl_launcher_reserved_left(const sl_ctx_t *ctx);

void sl_dirty_reset(void);
void sl_dirty_mark_full(void);
void sl_dirty_add(const sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int sl_dirty_intersects_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int sl_dirty_get_bounds(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
void sl_windows_mark_app_dirty(const sl_ctx_t *ctx, const void *userdata);

void sl_app_host_tick(sl_ctx_t *ctx);

char *sl_u32_to_str(uint32_t value, char *out, size_t out_cap);
