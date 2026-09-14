#include "render_ops.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#define SL_RENDER_HAS_SSE2 1
#else
#define SL_RENDER_HAS_SSE2 0
#endif

static uint8_t sl_render_blend_byte(uint8_t src, uint8_t dst, uint8_t alpha) {
    uint32_t value = (uint32_t)src * alpha + (uint32_t)dst * (255u - alpha) + 128u;
    return (uint8_t)((value + (value >> 8u)) >> 8u);
}

void sl_render_fill_bgra32(uint32_t *dst, size_t pixel_count, uint32_t packed_bgra) {
    if (!dst) return;
#if SL_RENDER_HAS_SSE2
    __m128i value = _mm_set1_epi32((int)packed_bgra);
    while (pixel_count >= 4u) {
        _mm_storeu_si128((__m128i *)(void *)dst, value);
        dst += 4;
        pixel_count -= 4u;
    }
#endif
    while (pixel_count-- > 0u) *dst++ = packed_bgra;
}

void sl_render_blend_fill_bgra32(uint32_t *dst, size_t pixel_count,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!dst || a == 0u) return;
    if (a == 255u) {
        sl_render_fill_bgra32(dst, pixel_count,
                              0xFF000000u | ((uint32_t)r << 16u) |
                              ((uint32_t)g << 8u) | b);
        return;
    }

#if SL_RENDER_HAS_SSE2
    __m128i zero = _mm_setzero_si128();
    __m128i src = _mm_set1_epi32((int)(0xFF000000u | ((uint32_t)r << 16u) |
                                           ((uint32_t)g << 8u) | b));
    __m128i alpha = _mm_set1_epi16((short)a);
    __m128i inverse = _mm_set1_epi16((short)(255u - a));
    __m128i rounding = _mm_set1_epi16(128);

    while (pixel_count >= 4u) {
        __m128i d8 = _mm_loadu_si128((const __m128i *)(const void *)dst);
        __m128i d_lo = _mm_unpacklo_epi8(d8, zero);
        __m128i d_hi = _mm_unpackhi_epi8(d8, zero);
        __m128i s_lo = _mm_unpacklo_epi8(src, zero);
        __m128i s_hi = _mm_unpackhi_epi8(src, zero);
        __m128i lo = _mm_add_epi16(_mm_mullo_epi16(s_lo, alpha),
                                   _mm_mullo_epi16(d_lo, inverse));
        __m128i hi = _mm_add_epi16(_mm_mullo_epi16(s_hi, alpha),
                                   _mm_mullo_epi16(d_hi, inverse));
        lo = _mm_add_epi16(lo, rounding);
        hi = _mm_add_epi16(hi, rounding);
        lo = _mm_srli_epi16(_mm_add_epi16(lo, _mm_srli_epi16(lo, 8)), 8);
        hi = _mm_srli_epi16(_mm_add_epi16(hi, _mm_srli_epi16(hi, 8)), 8);
        _mm_storeu_si128((__m128i *)(void *)dst, _mm_packus_epi16(lo, hi));
        dst += 4;
        pixel_count -= 4u;
    }
#endif

    while (pixel_count-- > 0u) {
        uint8_t *d = (uint8_t *)(void *)dst;
        d[0] = sl_render_blend_byte(b, d[0], a);
        d[1] = sl_render_blend_byte(g, d[1], a);
        d[2] = sl_render_blend_byte(r, d[2], a);
        d[3] = 0xFFu;
        dst++;
    }
}

void sl_render_copy_opaque_rgba_to_bgra32(uint32_t *dst, const uint32_t *src,
                                          size_t pixel_count) {
    if (!dst || !src) return;
#if SL_RENDER_HAS_SSE2
    __m128i rb_mask = _mm_set1_epi32(0x00FF00FF);
    __m128i g_mask = _mm_set1_epi32(0x0000FF00);
    __m128i alpha = _mm_set1_epi32((int)0xFF000000u);
    while (pixel_count >= 4u) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)src);
        __m128i rb = _mm_and_si128(value, rb_mask);
        __m128i swapped = _mm_or_si128(_mm_slli_epi32(rb, 16), _mm_srli_epi32(rb, 16));
        __m128i out = _mm_or_si128(alpha,
                                  _mm_or_si128(swapped, _mm_and_si128(value, g_mask)));
        _mm_storeu_si128((__m128i *)(void *)dst, out);
        src += 4;
        dst += 4;
        pixel_count -= 4u;
    }
#endif
    while (pixel_count-- > 0u) {
        uint32_t value = *src++;
        *dst++ = 0xFF000000u | ((value & 0x000000FFu) << 16u) |
                 (value & 0x0000FF00u) | ((value & 0x00FF0000u) >> 16u);
    }
}
