#pragma once

#include "peos/peos.h"

#include <stddef.h>
#include <stdint.h>

#define SL_KEYMOD_SHIFT  (1u << 0)
#define SL_KEYMOD_ALTGR  (1u << 4)

int sl_keyboard_layout_load(const char *name_or_path);
size_t sl_keyboard_translate(uint16_t keycode, uint32_t mods, char *out, size_t out_cap);
size_t sl_keyboard_translate_event(const peos_input_event_t *event,
                                   uint8_t shift_down,
                                   uint8_t caps_lock_on,
                                   uint8_t altgr_down,
                                   char *out,
                                   size_t out_cap);
uint8_t sl_keyboard_event_is_altgr(const peos_input_event_t *event);
