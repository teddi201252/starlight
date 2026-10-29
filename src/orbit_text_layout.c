#include "orbit_text_layout.h"

size_t orbit_text_row_count(size_t text_len, uint32_t columns, uint8_t include_cursor) {
    if (columns == 0u) return 0u;
    if (include_cursor) {
        return (text_len / columns) + 1u;
    }
    if (text_len == 0u) return 1u;
    return ((text_len - 1u) / columns) + 1u;
}

size_t orbit_text_copy_row(const char *text, size_t text_len,
                           uint32_t columns, size_t row_index,
                           uint8_t cursor_visible,
                           char *out, size_t out_cap) {
    if (!out || out_cap == 0u) return 0u;
    out[0] = '\0';
    if (!text || columns == 0u || row_index > SIZE_MAX / columns) return 0u;

    size_t offset = row_index * columns;
    if (offset > text_len) return 0u;

    size_t available = text_len - offset;
    size_t count = (available < columns) ? available : columns;
    if (count >= out_cap) count = out_cap - 1u;
    for (size_t i = 0u; i < count; i++) {
        out[i] = text[offset + i];
    }

    if (cursor_visible && offset + count == text_len &&
        count < columns && count + 1u < out_cap) {
        out[count++] = '_';
    }
    out[count] = '\0';
    return count;
}

size_t orbit_text_rows_at(size_t text_len, uint32_t columns, size_t cursor) {
    if (columns == 0u) return 0u;
    size_t rows = orbit_text_row_count(text_len, columns, 0u);
    size_t cursor_rows = (cursor / columns) + 1u;
    return (cursor_rows > rows) ? cursor_rows : rows;
}

int orbit_text_put(char *buf, size_t cap, size_t *len, size_t *cursor, char c) {
    if (!buf || !len || !cursor) return 0;
    size_t at = *cursor;
    if (at + 2u > cap) return 0;
    while (*len < at) buf[(*len)++] = ' ';   // the cursor was moved past the end
    buf[at] = c;
    if (at == *len) (*len)++;
    buf[*len] = '\0';
    *cursor = at + 1u;
    return 1;
}
