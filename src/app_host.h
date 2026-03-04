#pragma once

#include "window.h"

#include <stddef.h>

app_instance_t *sl_app_host_instance(void);
app_instance_t *sl_app_host_instance_at(size_t index);
void sl_app_host_reset(void);
void sl_app_host_tick(sl_ctx_t *ctx);
void sl_app_host_slots_clear(void);
void sl_app_host_slots_use_default(void);
int sl_app_host_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args);
size_t sl_app_host_slot_count_enabled(void);
int sl_app_host_input_dirty_rect(const sl_window_t *win, const sl_input_frame_t *frame,
                                 const void *userdata,
                                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
