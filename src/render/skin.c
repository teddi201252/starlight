#include "skin.h"

#include <stdlib.h>
#include <string.h>

// In-bounds for [off, off + size) inside len, without overflow.
static int span_ok(uint64_t off, uint64_t size, uint64_t len) {
    return off <= len && size <= len - off;
}

static int font_ok(const uint8_t *data, uint32_t size) {
    if (size < sizeof(slsk_font_t)) return 0;
    const slsk_font_t *f = (const slsk_font_t *)(const void *)data;
    if (f->glyph_count == 0 || f->atlas_w == 0 || f->atlas_h == 0) return 0;
    if ((f->glyphs_off & 3u) || (f->kerns_off & 3u)) return 0;
    if (!span_ok(f->glyphs_off, (uint64_t)f->glyph_count * sizeof(slsk_glyph_t), size)) return 0;
    if (!span_ok(f->kerns_off, (uint64_t)f->kern_count * sizeof(slsk_kern_t), size)) return 0;
    if (!span_ok(f->atlas_off, (uint64_t)f->atlas_w * f->atlas_h, size)) return 0;

    const slsk_glyph_t *g = sl_skin_glyphs(f);
    for (uint32_t i = 0; i < f->glyph_count; i++) {
        if (i > 0 && g[i].codepoint <= g[i - 1].codepoint) return 0;   // sorted, no duplicates
        if ((uint32_t)g[i].x + g[i].w > f->atlas_w || (uint32_t)g[i].y + g[i].h > f->atlas_h) return 0;
    }
    const slsk_kern_t *k = (const slsk_kern_t *)(const void *)(data + f->kerns_off);
    for (uint32_t i = 0; i < f->kern_count; i++) {
        if (k[i].left >= f->glyph_count || k[i].right >= f->glyph_count) return 0;
        if (i > 0) {
            uint32_t a = ((uint32_t)k[i - 1].left << 16) | k[i - 1].right;
            uint32_t b = ((uint32_t)k[i].left << 16) | k[i].right;
            if (b <= a) return 0;
        }
    }
    return 1;
}

static int name_ok(const char *name) {
    if (name[0] == '\0') return 0;
    for (uint32_t i = 1; i < SLSK_NAME_MAX; i++) {
        if (name[i] == '\0') return 1;
    }
    return 0;
}

static int entry_ok(const slsk_entry_t *e, const uint8_t *buf, size_t len, uint64_t table_end) {
    if (!name_ok(e->name)) return 0;
    if (e->offset < table_end || (e->offset & 15u)) return 0;
    if (!span_ok(e->offset, e->size, len)) return 0;

    switch (e->kind) {
    case SLSK_KIND_OPAQUE:
    case SLSK_KIND_PREMUL:
    case SLSK_KIND_MASK: {
        uint64_t bpp = e->kind == SLSK_KIND_MASK ? 1u : 4u;
        if (e->width == 0 || e->height == 0) return 0;
        if ((uint64_t)e->width * e->height * bpp != e->size) return 0;
        // Either both insets of an axis are 0 (it does not stretch) or they
        // leave at least one pixel to stretch.
        if ((e->slice_l || e->slice_r) && (uint32_t)e->slice_l + e->slice_r >= e->width) return 0;
        if ((e->slice_t || e->slice_b) && (uint32_t)e->slice_t + e->slice_b >= e->height) return 0;
        if ((uint32_t)e->pad_l + e->pad_r >= e->width || (uint32_t)e->pad_t + e->pad_b >= e->height) return 0;
        return 1;
    }
    case SLSK_KIND_FONT:
        return font_ok(buf + e->offset, e->size);
    default:
        return 0;
    }
}

int sl_skin_parse(sl_skin_t *skin, uint8_t *buf, size_t len) {
    if (!skin) return 0;
    memset(skin, 0, sizeof(*skin));
    if (!buf || len < sizeof(slsk_header_t) || ((uintptr_t)buf & 15u)) return 0;

    const slsk_header_t *h = (const slsk_header_t *)(const void *)buf;
    if (h->magic != SLSK_MAGIC || h->version != SLSK_VERSION || h->file_size != len) return 0;
    uint64_t table_end = sizeof(slsk_header_t) + (uint64_t)h->entry_count * sizeof(slsk_entry_t);
    if (h->entry_count == 0 || table_end > len) return 0;

    const slsk_entry_t *entries = (const slsk_entry_t *)(const void *)(buf + sizeof(slsk_header_t));
    for (uint32_t i = 0; i < h->entry_count; i++) {
        if (!entry_ok(&entries[i], buf, len, table_end)) return 0;
    }
    skin->buf = buf;
    skin->len = len;
    skin->entries = entries;
    skin->count = h->entry_count;
    return 1;
}

const slsk_entry_t *sl_skin_find(const sl_skin_t *skin, const char *name) {
    if (!skin || !name) return NULL;
    for (uint32_t i = 0; i < skin->count; i++) {
        if (strcmp(skin->entries[i].name, name) == 0) return &skin->entries[i];
    }
    return NULL;
}

int sl_skin_glyph_index(const slsk_font_t *font, uint32_t codepoint) {
    const slsk_glyph_t *g = sl_skin_glyphs(font);
    int lo = 0, hi = (int)font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g[mid].codepoint == codepoint) return mid;
        if (g[mid].codepoint < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int sl_skin_kern(const slsk_font_t *font, int left, int right) {
    const slsk_kern_t *k = (const slsk_kern_t *)(const void *)((const uint8_t *)font + font->kerns_off);
    uint32_t key = ((uint32_t)left << 16) | (uint32_t)right;
    int lo = 0, hi = (int)font->kern_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t m = ((uint32_t)k[mid].left << 16) | k[mid].right;
        if (m == key) return k[mid].adjust;
        if (m < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

void sl_skin_free(sl_skin_t *skin) {
    if (!skin) return;
    free(skin->buf);
    memset(skin, 0, sizeof(*skin));
}
