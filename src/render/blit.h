#pragma once

// Drawing the baked skin (slsk.h) into a BGR(A) buffer: nine-slice sprites,
// tinted 8-bit masks, text from the font atlases. Pure functions over a
// buffer, so the host tests can check them pixel by pixel; pixels.c wraps
// them for the Starlight frame.
//
// Opaque sprites are copied row by row, premultiplied ones blended; the
// stretched middle of a nine-slice sprite is one pixel wide, so it becomes a
// fill.

#include <stddef.h>
#include <stdint.h>

#include "slsk.h"

typedef struct {
    uint8_t *px;            // pixel (0,0)
    size_t stride;          // bytes per row
    uint8_t bpp;            // 4 (BGRA32) or 3 (BGR24)
    // Drawable area, half open, already inside the buffer.
    int32_t clip_x0, clip_y0, clip_x1, clip_y1;
} sl_target_t;

// Sprite `e` (pixels `data`) stretched to w x h at (x, y). The box includes
// the shadow margins (pad_*); an axis that does not stretch is drawn at the
// sprite's own size whatever w or h say.
void sl_blit_sprite(const sl_target_t *t, const slsk_entry_t *e, const uint8_t *data,
                    int32_t x, int32_t y, uint32_t w, uint32_t h);

// An 8-bit coverage mask tinted with r,g,b and scaled by alpha (255 = as is).
void sl_blit_mask(const sl_target_t *t, const uint8_t *mask, uint32_t mw, uint32_t mh, size_t mstride,
                  int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);

// UTF-8 text with a baked font, pen starting at x on `baseline`. Returns the
// pen position after the last glyph. Codepoints the font lacks are skipped.
int32_t sl_text_draw(const sl_target_t *t, const slsk_font_t *font, int32_t x, int32_t baseline,
                     const char *utf8, uint8_t r, uint8_t g, uint8_t b);
// Advance of the text in pixels (rounded), as sl_text_draw would move the pen.
uint32_t sl_text_width(const slsk_font_t *font, const char *utf8);

// One codepoint from *s, advancing it; 0xFFFD for a broken sequence.
uint32_t sl_utf8_next(const char **s);
