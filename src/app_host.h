#pragma once

#include "peos/path_scope_abi.h"
#include "window.h"

#include <stddef.h>

app_instance_t *sl_app_host_instance(void);
app_instance_t *sl_app_host_instance_at(size_t index);
app_instance_t *sl_app_host_instance_peek_at(size_t index);
void sl_app_host_reset(void);
void sl_app_host_tick(sl_ctx_t *ctx);
void sl_app_host_slots_clear(void);
void sl_app_host_slots_use_default(void);
int sl_app_host_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args);
/* The app's bundle folder, where its resources/<language>/ are (after slot_configure). */
int sl_app_host_slot_set_bundle(size_t index, const char *bundle_dir);
/* The language SLUI apps get ("@SLUI HOST LANG"); running ones are told at once. */
void sl_app_host_set_language(const char *code);
/* Limits the rights of the app started in this slot (call after slot_configure). */
int sl_app_host_slot_restrict_caps(size_t index, uint64_t caps_mask);
/* Limits the filesystem the app may touch (call after slot_configure). */
int sl_app_host_slot_restrict_paths(size_t index, const peos_path_grant_t *grants, size_t count);
size_t sl_app_host_next_slot_index(void);
size_t sl_app_host_slot_count_enabled(void);
/* What this input changes in the focused app: 1, the rect to add to the
 * damage (the window is drawn again whole); 2, only the hover moved, added
 * to what the window redraws in part (then sl_windows_mark_app_pixels);
 * 0, nothing. */
int sl_app_host_input_dirty_rect(const sl_window_t *win, const sl_input_frame_t *frame,
                                 const void *userdata,
                                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
/* The cursor inside the app's body at x,y (sl_cursor_kind_t): the text one
 * over a field or the terminal, the hand over a control, "not allowed" over
 * a disabled one, the arrow elsewhere. */
int sl_app_host_cursor(const sl_window_t *win, const void *userdata, uint32_t x, uint32_t y);
/* The app has shown something (its first SLUI frame, its framebuffer, or
 * the terminal's first line). */
int sl_app_host_ready(const void *userdata);
int sl_app_host_cache_update_rect(const sl_window_t *win, const void *userdata,
                                  uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
/* Something in the app blinks at the caret's pace: the terminal's prompt, a
 * focused text field. */
int sl_app_host_has_caret(const void *userdata);
/* The focused SLUI text field (and its shadow) is all that these keys, or
 * the caret's blink (frame NULL), change: added to what the window redraws
 * in part (then sl_windows_mark_app_pixels). 0: the body changes. */
int sl_app_host_mark_field(void *userdata, const sl_input_frame_t *frame);
/* The pointer is the app's own pixels' (held there, or over them): what it
 * does there the app draws, and says where (PIXELS_DAMAGE). */
int sl_app_host_pixels_have_pointer(const sl_ctx_t *ctx, const void *userdata);
/* Where the app's own pixels are on the screen (SLUI PIXELS); 0: none. */
int sl_app_host_pixels_view(const void *userdata, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h);
/* Where the window changed since it was last asked, on the screen and
 * inside where the app draws ("HOST PIXELS_DAMAGE", the commands a frame
 * changed, the field typed in); 0: nowhere. */
int sl_app_host_pixels_rect(const sl_window_t *win, const void *userdata,
                            uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
