// A stick that comes in (Settings, Dischi: "Aprila in Curiosity"): when
// diskd mounts a disk on /mediaN and /system/config/disks.cfg says open=1,
// Curiosity opens on it. It looks only when the disks changed (the kernel's
// disk descriptor, read without waiting once a frame).
#pragma once

#include "starlight.h"

void sl_stick_tick(sl_ctx_t *ctx);
// The disks' watch, for the idle desktop.
void sl_stick_wake(void);
