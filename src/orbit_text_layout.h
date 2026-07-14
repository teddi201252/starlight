#pragma once

#include <stddef.h>
#include <stdint.h>

size_t orbit_text_row_count(size_t text_len, uint32_t columns, uint8_t include_cursor);

size_t orbit_text_copy_row(const char *text, size_t text_len,
                           uint32_t columns, size_t row_index,
                           uint8_t cursor_visible,
                           char *out, size_t out_cap);
