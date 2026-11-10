// The interface scale: Starlight draws in a logical frame smaller than the
// screen and this enlarges the changed rectangles onto the framebuffer.
// Whole ratios (2x, 3x) copy pixels, so they stay sharp; the others
// interpolate (bilinear, 32-bit pixels; nearest for 24-bit).
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t lw, lh;           // logical frame
    uint32_t pw, ph;           // screen
    uint32_t *x0, *y0;         // for each screen column/row: the logical one
    uint8_t *fx, *fy;          // and how far towards the next (0..255)
    uint32_t *line0, *line1;   // two screen rows being built (32-bit pixels)
    uint8_t whole;             // an exact whole ratio on both axes
} sl_upscale_t;

// The logical size for a screen and a scale in percent (100..300).
void sl_upscale_logical_size(uint32_t pw, uint32_t ph, uint32_t scale_pct, uint32_t *lw, uint32_t *lh);
int sl_upscale_init(sl_upscale_t *u, uint32_t lw, uint32_t lh, uint32_t pw, uint32_t ph);
void sl_upscale_free(sl_upscale_t *u);
// The screen rectangle a logical one covers (one pixel more around it, for
// the interpolation), clamped to the screen. 0 when empty.
int sl_upscale_rect(const sl_upscale_t *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t *px,
                    uint32_t *py, uint32_t *pw, uint32_t *ph);
// Fills that screen rectangle of dst from src (the logical frame).
void sl_upscale_blit(const sl_upscale_t *u, const uint8_t *src, size_t src_stride, uint8_t *dst, size_t dst_stride,
                     uint32_t bytespp, uint32_t px, uint32_t py, uint32_t pw, uint32_t ph);
// A logical position for a screen one (absolute pointers).
uint32_t sl_upscale_to_logical_x(const sl_upscale_t *u, int64_t px);
uint32_t sl_upscale_to_logical_y(const sl_upscale_t *u, int64_t py);
