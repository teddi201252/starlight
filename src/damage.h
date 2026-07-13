#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} sl_damage_rect_t;

uint64_t sl_damage_area(const sl_damage_rect_t *rect);
int sl_damage_overlap_or_touch(const sl_damage_rect_t *a, const sl_damage_rect_t *b);
sl_damage_rect_t sl_damage_union(const sl_damage_rect_t *a, const sl_damage_rect_t *b);
int sl_damage_intersection(const sl_damage_rect_t *a, const sl_damage_rect_t *b,
                           sl_damage_rect_t *out);
size_t sl_damage_reduce(const sl_damage_rect_t *input, size_t input_count,
                        sl_damage_rect_t *output, size_t output_cap,
                        uint32_t screen_width, uint32_t screen_height,
                        uint32_t full_threshold_pct, uint8_t *out_full);
