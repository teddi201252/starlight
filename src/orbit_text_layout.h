#pragma once

#include <stddef.h>
#include <stdint.h>

size_t orbit_text_row_count(size_t text_len, uint32_t columns, uint8_t include_cursor);

size_t orbit_text_copy_row(const char *text, size_t text_len,
                           uint32_t columns, size_t row_index,
                           uint8_t cursor_visible,
                           char *out, size_t out_cap);

// Rows of a line being written, with its cursor (which may be past its end).
size_t orbit_text_rows_at(size_t text_len, uint32_t columns, size_t cursor);

// Writes c at *cursor, over what is there (a terminal), padding with spaces up
// to the cursor; buf holds cap bytes with the NUL. 0 when it does not fit.
int orbit_text_put(char *buf, size_t cap, size_t *len, size_t *cursor, char c);
