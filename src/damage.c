#include "damage.h"

#include <stdint.h>

#define SL_DAMAGE_WORK_CAP 256u

uint64_t sl_damage_area(const sl_damage_rect_t *rect) {
    return rect ? (uint64_t)rect->w * rect->h : 0u;
}

int sl_damage_overlap_or_touch(const sl_damage_rect_t *a, const sl_damage_rect_t *b) {
    if (!a || !b || a->w == 0u || a->h == 0u || b->w == 0u || b->h == 0u) return 0;
    uint64_t ax2 = (uint64_t)a->x + a->w;
    uint64_t ay2 = (uint64_t)a->y + a->h;
    uint64_t bx2 = (uint64_t)b->x + b->w;
    uint64_t by2 = (uint64_t)b->y + b->h;
    return !(ax2 < b->x || bx2 < a->x || ay2 < b->y || by2 < a->y);
}

sl_damage_rect_t sl_damage_union(const sl_damage_rect_t *a, const sl_damage_rect_t *b) {
    sl_damage_rect_t empty = {0u, 0u, 0u, 0u};
    if (!a) return b ? *b : empty;
    if (!b) return *a;
    uint32_t x = a->x < b->x ? a->x : b->x;
    uint32_t y = a->y < b->y ? a->y : b->y;
    uint64_t ax2 = (uint64_t)a->x + a->w;
    uint64_t ay2 = (uint64_t)a->y + a->h;
    uint64_t bx2 = (uint64_t)b->x + b->w;
    uint64_t by2 = (uint64_t)b->y + b->h;
    uint64_t x2 = ax2 > bx2 ? ax2 : bx2;
    uint64_t y2 = ay2 > by2 ? ay2 : by2;
    return (sl_damage_rect_t){ x, y, (uint32_t)(x2 - x), (uint32_t)(y2 - y) };
}

int sl_damage_intersection(const sl_damage_rect_t *a, const sl_damage_rect_t *b,
                           sl_damage_rect_t *out) {
    if (!a || !b || !out || a->w == 0u || a->h == 0u || b->w == 0u || b->h == 0u) return 0;
    uint32_t x = a->x > b->x ? a->x : b->x;
    uint32_t y = a->y > b->y ? a->y : b->y;
    uint64_t ax2 = (uint64_t)a->x + a->w;
    uint64_t ay2 = (uint64_t)a->y + a->h;
    uint64_t bx2 = (uint64_t)b->x + b->w;
    uint64_t by2 = (uint64_t)b->y + b->h;
    uint64_t x2 = ax2 < bx2 ? ax2 : bx2;
    uint64_t y2 = ay2 < by2 ? ay2 : by2;
    if (x2 <= x || y2 <= y) return 0;
    *out = (sl_damage_rect_t){ x, y, (uint32_t)(x2 - x), (uint32_t)(y2 - y) };
    return 1;
}

static void sl_damage_merge_touching(sl_damage_rect_t *work, size_t *count) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t i = 0u; i < *count && !changed; i++) {
            for (size_t j = i + 1u; j < *count; j++) {
                if (!sl_damage_overlap_or_touch(&work[i], &work[j])) continue;
                work[i] = sl_damage_union(&work[i], &work[j]);
                work[j] = work[*count - 1u];
                (*count)--;
                changed = 1;
                break;
            }
        }
    }
}

size_t sl_damage_reduce(const sl_damage_rect_t *input, size_t input_count,
                        sl_damage_rect_t *output, size_t output_cap,
                        uint32_t screen_width, uint32_t screen_height,
                        uint32_t full_threshold_pct, uint8_t *out_full) {
    if (out_full) *out_full = 0u;
    if (!input || !output || !out_full || input_count == 0u ||
        input_count > SL_DAMAGE_WORK_CAP || output_cap == 0u ||
        screen_width == 0u || screen_height == 0u) return 0u;

    sl_damage_rect_t work[SL_DAMAGE_WORK_CAP];
    size_t count = input_count;
    for (size_t i = 0u; i < count; i++) work[i] = input[i];
    sl_damage_merge_touching(work, &count);

    while (count > output_cap) {
        size_t best_i = 0u, best_j = 1u;
        uint64_t best_penalty = UINT64_MAX;
        for (size_t i = 0u; i + 1u < count; i++) {
            for (size_t j = i + 1u; j < count; j++) {
                sl_damage_rect_t merged = sl_damage_union(&work[i], &work[j]);
                uint64_t merged_area = sl_damage_area(&merged);
                uint64_t old_area = sl_damage_area(&work[i]) + sl_damage_area(&work[j]);
                uint64_t penalty = merged_area > old_area ? merged_area - old_area : 0u;
                if (penalty < best_penalty) {
                    best_penalty = penalty;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        work[best_i] = sl_damage_union(&work[best_i], &work[best_j]);
        work[best_j] = work[count - 1u];
        count--;
        sl_damage_merge_touching(work, &count);
    }

    uint64_t pixels = 0u;
    for (size_t i = 0u; i < count; i++) pixels += sl_damage_area(&work[i]);
    uint64_t screen = (uint64_t)screen_width * screen_height;
    if (full_threshold_pct <= 100u && pixels * 100u >= screen * full_threshold_pct) {
        output[0] = (sl_damage_rect_t){ 0u, 0u, screen_width, screen_height };
        *out_full = 1u;
        return 1u;
    }
    for (size_t i = 0u; i < count; i++) output[i] = work[i];
    return count;
}
