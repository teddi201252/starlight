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
