#pragma once

#include "starlight.h"
#include "clipboard.h"
#include "tween.h"
#include "window.h"

#include <stddef.h>
#include <stdint.h>

// The cursors of the Soft UI set (Soft UI plan, phase 11): which one shows
// depends on what is under the mouse.
typedef enum {
    SL_CUR_ARROW = 0,
    SL_CUR_HAND,
    SL_CUR_TEXT,
    SL_CUR_RESIZE_H,
    SL_CUR_RESIZE_V,
    SL_CUR_RESIZE_NWSE,
    SL_CUR_RESIZE_NESW,
    SL_CUR_MOVE,
    SL_CUR_WAIT,
    SL_CUR_NO,
    SL_CUR_COUNT
} sl_cursor_kind_t;

void sl_audio_init(void);
void sl_audio_shutdown(void);
void sl_audio_play_copy_effect(void);

int sl_init(sl_ctx_t *ctx);
void sl_shutdown(sl_ctx_t *ctx);
void sl_reload_display(sl_ctx_t *ctx);
void sl_reload_theme(sl_ctx_t *ctx);
int sl_handle_input(sl_ctx_t *ctx);
void sl_render_full(sl_ctx_t *ctx);
void sl_render_dirty_background(sl_ctx_t *ctx);
int sl_present(sl_ctx_t *ctx);
void sl_frame_pace(uint64_t *next_deadline_ns, int64_t input_fd, int drew);

void sl_cursor_init(sl_ctx_t *ctx);
// Show this cursor (nothing to do if it shows already): a new image for the
// hardware cursor, or the one the software cursor draws.
void sl_cursor_set(sl_ctx_t *ctx, sl_cursor_kind_t kind);
// Once a frame: pick the cursor for what is under the mouse.
void sl_cursor_update(sl_ctx_t *ctx);
void sl_render_cursor(sl_ctx_t *ctx);
void sl_sw_cursor_capture(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_sw_cursor_restore(sl_ctx_t *ctx);
// The software cursor's rectangle now, marked dirty.
void sl_sw_cursor_dirty(sl_ctx_t *ctx);
void sl_hw_cursor_sync_pos(sl_ctx_t *ctx);

void sl_windows_create_test(sl_ctx_t *ctx, size_t count);
void sl_windows_create_default(sl_ctx_t *ctx);
// The theme was reloaded: every window draws its inside again.
void sl_windows_theme_changed(sl_ctx_t *ctx);
void sl_windows_render_all(sl_ctx_t *ctx);
void sl_windows_focus_at_cursor(uint32_t x, uint32_t y);
// The opaque inside of the top window on screen, which covers whatever is
// under it this frame (none while it fades or if every window is minimized).
int sl_windows_top_opaque_rect(const sl_ctx_t *ctx, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
int sl_windows_close_at_cursor(sl_ctx_t *ctx, uint32_t x, uint32_t y);
// The cursor each part wants at x,y (sl_cursor_kind_t), or -1: not theirs.
int sl_launcher_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y);
int sl_windows_cursor(const sl_ctx_t *ctx, uint32_t x, uint32_t y);
// An app was just opened and has not shown anything yet.
uint8_t sl_windows_launching(void);
// Resizing a window by its edges (Soft UI plan, phase 12).
int sl_windows_resize_press(sl_ctx_t *ctx, uint32_t x, uint32_t y);   // 1: an edge was taken
uint8_t sl_windows_resizing(void);
void sl_windows_resize_move(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_windows_resize_release(sl_ctx_t *ctx);
// Remember where a window is (windows.cfg): it opens there again.
void sl_windows_remember(const sl_window_t *win);
void sl_windows_toggle_maximize(sl_ctx_t *ctx);
// A window (not minimized) covers x,y.
int sl_windows_any_at(uint32_t x, uint32_t y);
// Open (or bring forward) the app with this title, case aside: 0 if none.
int sl_windows_open_app_title(sl_ctx_t *ctx, const char *title);
int sl_windows_title_button_held(sl_ctx_t *ctx, uint32_t x, uint32_t y);
int sl_windows_title_button_release(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_windows_destroy_all(void);
void sl_windows_mark_focused_activity(void);
int sl_windows_open_app_bundle_path(sl_ctx_t *ctx, const char *bundle_path);
void sl_window_move(sl_window_t *win, uint32_t new_x, uint32_t new_y);
sl_window_t* sl_window_get_focused(void);
void sl_launcher_render(sl_ctx_t *ctx);
int sl_launcher_handle_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
// The app manager is open: keys type in its search field.
uint8_t sl_launcher_manager_open(void);
void sl_launcher_handle_key(sl_ctx_t *ctx, const peos_input_event_t *ev, uint8_t caps_lock_on);
int sl_launcher_handle_right_click(sl_ctx_t *ctx, uint32_t x, uint32_t y);
void sl_launcher_handle_context_hover(sl_ctx_t *ctx);
void sl_launcher_handle_hover(sl_ctx_t *ctx);
uint32_t sl_launcher_reserved_left(const sl_ctx_t *ctx);
// The app manager, open or opening, covers this point (it takes the mouse).
int sl_launcher_covers_point(const sl_ctx_t *ctx, uint32_t x, uint32_t y);
// The left button came up: a pressed dock tile pops back.
void sl_launcher_handle_release(sl_ctx_t *ctx);
// Redraw the dock (a badge changed).
void sl_launcher_mark_dirty(sl_ctx_t *ctx);

void sl_dirty_reset(void);
void sl_dirty_mark_full(void);
void sl_dirty_add(const sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
uint32_t sl_dirty_request_count(void);
int sl_dirty_intersects_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
typedef void (*sl_dirty_rect_fn)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 void *userdata);
void sl_dirty_for_each_intersection(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    sl_dirty_rect_fn fn, void *userdata);
void sl_windows_mark_app_dirty(const sl_ctx_t *ctx, const void *userdata);
void sl_windows_tick_animations(sl_ctx_t *ctx);

void sl_app_host_tick(sl_ctx_t *ctx);
int sl_app_host_input_dirty_rect(const sl_window_t *win, const sl_input_frame_t *frame,
                                 const void *userdata,
                                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);

char *sl_u32_to_str(uint32_t value, char *out, size_t out_cap);
