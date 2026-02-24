#include "orbit_window.h"

void sl_orbit_tick(sl_ctx_t *ctx) {
    sl_app_host_tick(ctx);
}

void sl_orbit_slots_clear(void) {
    sl_app_host_slots_clear();
}

int sl_orbit_slot_configure(size_t index, const char *title, const char *exec_path, const char *exec_args) {
    return sl_app_host_slot_configure(index, title, exec_path, exec_args);
}

void sl_orbit_slots_use_default(void) {
    sl_app_host_slots_use_default();
}

size_t sl_orbit_slot_count_enabled(void) {
    return sl_app_host_slot_count_enabled();
}

void sl_orbit_app_reset(void) {
    sl_app_host_reset();
}

app_instance_t *sl_orbit_app_instance(void) {
    return sl_app_host_instance();
}

app_instance_t *sl_orbit_app_instance_at(size_t index) {
    return sl_app_host_instance_at(index);
}
