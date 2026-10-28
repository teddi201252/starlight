#pragma once

// Starlight's sentences (Technical Alpha plan, phase 4): read once from
// /system/lang/<language>/String.lang when the language is set, then looked
// up in a sorted table. Missing ones come from English, then the key.

#include <stdint.h>

// Loads the tables for code ("it", "en"); 1 if the language changed.
int sl_i18n_set(const char *code);
const char *sl_i18n_code(void);
const char *sl_tr(const char *key);
// key.one when n is 1, else key.other.
const char *sl_tr_n(const char *key, uint32_t n);
