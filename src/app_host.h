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
