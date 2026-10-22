#pragma once

// The baked skin (skin.slsk, see slsk.h): validated once when it is loaded,
// then read in place. Nothing is copied or converted.

#include <stddef.h>
#include <stdint.h>

#include "slsk.h"

typedef struct {
    uint8_t *buf;                   // the whole file, owned by the skin
    size_t len;
    const slsk_entry_t *entries;
    uint32_t count;
} sl_skin_t;

// Check every offset, size and table in buf[0..len) and point skin at it.
// Returns 1 if the file is sound, 0 otherwise (skin is then left empty).
// buf must be 16-byte aligned; skin does not take ownership.
int sl_skin_parse(sl_skin_t *skin, uint8_t *buf, size_t len);

// Read and parse a skin file with one read. Returns 1 on success.
int sl_skin_load(sl_skin_t *skin, const char *path);
void sl_skin_free(sl_skin_t *skin);

// Entry by name, or NULL. Meant for start-up: callers keep the pointer.
const slsk_entry_t *sl_skin_find(const sl_skin_t *skin, const char *name);
static inline const uint8_t *sl_skin_data(const sl_skin_t *skin, const slsk_entry_t *e) {
    return skin->buf + e->offset;
}

// Font tables of a SLSK_KIND_FONT entry.
static inline const slsk_font_t *sl_skin_font(const sl_skin_t *skin, const slsk_entry_t *e) {
    return (const slsk_font_t *)(const void *)sl_skin_data(skin, e);
}
// Glyph index for a codepoint, or -1 (binary search: glyphs are sorted).
int sl_skin_glyph_index(const slsk_font_t *font, uint32_t codepoint);
// Kerning between two glyph indexes, 26.6 fixed point (0 if none).
int sl_skin_kern(const slsk_font_t *font, int left, int right);

static inline const slsk_glyph_t *sl_skin_glyphs(const slsk_font_t *font) {
    return (const slsk_glyph_t *)(const void *)((const uint8_t *)font + font->glyphs_off);
}
static inline const uint8_t *sl_skin_atlas(const slsk_font_t *font) {
    return (const uint8_t *)font + font->atlas_off;
}
