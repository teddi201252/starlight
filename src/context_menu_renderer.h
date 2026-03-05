#pragma once

#include "starlight.h"

#include <stddef.h>
#include <stdint.h>

typedef void (*sl_context_menu_label_fn)(uint32_t option_index, char *out, size_t out_cap, void *userdata);

void sl_context_menu_draw(sl_ctx_t *ctx,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t row_h,
                          uint32_t option_count,
                          uint32_t start_idx,
                          uint32_t visible_items,
                          int32_t hover_row,
                          uint8_t has_scrollbar,
                          sl_context_menu_label_fn label_fn,
                          void *label_userdata);
