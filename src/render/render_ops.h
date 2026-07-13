#pragma once

#include <stddef.h>
#include <stdint.h>

void sl_render_fill_bgra32(uint32_t *dst, size_t pixel_count, uint32_t packed_bgra);
void sl_render_blend_fill_bgra32(uint32_t *dst, size_t pixel_count,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sl_render_copy_opaque_rgba_to_bgra32(uint32_t *dst, const uint32_t *src,
                                          size_t pixel_count);
