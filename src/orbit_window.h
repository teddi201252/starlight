#pragma once

#include "window.h"

#include <stddef.h>

app_instance_t *sl_orbit_app_instance(void);
app_instance_t *sl_orbit_app_instance_at(size_t index);
void sl_orbit_app_reset(void);
