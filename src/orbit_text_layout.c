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
