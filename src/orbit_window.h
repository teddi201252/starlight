#pragma once

#include "app_host.h"

// Legacy Orbit-prefixed API kept for compatibility during refactor.
#define SL_ORBIT_LEGACY_REMOVE_AFTER "2026-12"

#if defined(__GNUC__) || defined(__clang__)
#define SL_ORBIT_DEPRECATED __attribute__((deprecated("Use sl_app_host_* API")))
#else
#define SL_ORBIT_DEPRECATED
#endif

app_instance_t *sl_orbit_app_instance(void) SL_ORBIT_DEPRECATED;
app_instance_t *sl_orbit_app_instance_at(size_t index) SL_ORBIT_DEPRECATED;
void sl_orbit_app_reset(void) SL_ORBIT_DEPRECATED;
void sl_orbit_tick(sl_ctx_t *ctx) SL_ORBIT_DEPRECATED;
void sl_orbit_slots_clear(void) SL_ORBIT_DEPRECATED;
void sl_orbit_slots_use_default(void) SL_ORBIT_DEPRECATED;
int sl_orbit_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args) SL_ORBIT_DEPRECATED;
size_t sl_orbit_slot_count_enabled(void) SL_ORBIT_DEPRECATED;
