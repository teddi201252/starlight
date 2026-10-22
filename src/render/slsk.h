#pragma once

// The .slsk skin file: sprites, masks and font atlases baked at build time by
// tools/slskin from starlight/skin/skin.cfg, loaded by Starlight with a single
// read. Shared by the tool (host) and Starlight: plain C, little endian.
//
// Layout: header, entry table, then each entry's data at its offset.

#include <stdint.h>

#define SLSK_MAGIC   0x4B534C53u   // "SLSK"
#define SLSK_VERSION 1u
#define SLSK_NAME_MAX 32u

enum {
    SLSK_KIND_OPAQUE = 1,   // BGRA32, every alpha 255: copy rows
    SLSK_KIND_PREMUL = 2,   // BGRA32, premultiplied alpha: blend
    SLSK_KIND_MASK   = 3,   // 8-bit coverage, tinted when drawn
    SLSK_KIND_FONT   = 4,   // slsk_font_t, glyph table, 8-bit atlas
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t file_size;
} slsk_header_t;

typedef struct {
    char name[SLSK_NAME_MAX];   // NUL-terminated
    uint16_t kind;
    uint16_t flags;
    uint16_t width;             // sprite size (for fonts: 0)
    uint16_t height;
    // Nine-slice insets: the corners are fixed, the edges between them and
    // the centre stretch. 0 on both sides of an axis = that axis never
    // stretches.
    uint16_t slice_l, slice_t, slice_r, slice_b;
    // Shadow margin: the shape starts pad_l/pad_t pixels inside the sprite.
    // Draw the sprite at (x - pad_l, y - pad_t) for a shape at (x, y).
    uint16_t pad_l, pad_t, pad_r, pad_b;
    uint32_t offset;            // from the start of the file
    uint32_t size;
} slsk_entry_t;

// SLSK_KIND_FONT data: this header, glyph_count glyphs sorted by codepoint,
// kern_count kerning pairs sorted by (left, right), then the atlas.
typedef struct {
    int16_t ascent;             // pixels above the baseline
    int16_t descent;            // pixels below it (positive)
    int16_t line_height;        // baseline to baseline
    uint16_t glyph_count;
    uint16_t kern_count;
    uint16_t atlas_w;
    uint16_t atlas_h;
    uint16_t px;                // the size it was baked at
    uint32_t glyphs_off;        // from the start of this header
    uint32_t kerns_off;
    uint32_t atlas_off;
} slsk_font_t;

typedef struct {
    uint32_t codepoint;
    uint16_t x, y, w, h;        // in the atlas
    int16_t xoff, yoff;         // from the pen position on the baseline
    uint16_t advance;           // 26.6 fixed point pixels
    uint16_t reserved;
} slsk_glyph_t;

typedef struct {
    uint16_t left, right;       // glyph indexes
    int16_t adjust;             // 26.6 fixed point pixels
    uint16_t reserved;
} slsk_kern_t;

_Static_assert(sizeof(slsk_header_t) == 16, "slsk header");
_Static_assert(sizeof(slsk_entry_t) == 64, "slsk entry");
_Static_assert(sizeof(slsk_font_t) == 28, "slsk font");
_Static_assert(sizeof(slsk_glyph_t) == 20, "slsk glyph");
_Static_assert(sizeof(slsk_kern_t) == 8, "slsk kern");
