#pragma once

// Baking the skin's surfaces (docs/skin-colors-plan.md): the part of
// tools/slskin that draws soft surfaces with their shadows, shared by the
// tool (on the host, the whole skin at build time) and by Starlight (only
// the surfaces whose colours come from the theme, when the theme changes).
//
// The recipe's colours may be variables: "color NAME RRGGBB [AA]" defines one,
// and a style writes "$NAME" or "$NAME~K", the variable moved K of the way
// from the ground (K 0 is the ground, 1 the variable, more goes past it).
// The ground is $ground, or $ground-n for a variable whose name ends in -n
// (the night ones). A variable may carry an alpha (the theme's, 0..1): a
// shadow drawn with it is that much fainter, 0 none at all.
//
// Plain C with malloc and <math.h>; the shapes need nanosvg.

#include <stddef.h>
#include <stdint.h>

#include "render/slsk.h"

#define SKB_MAX_SHADOWS 4
#define SKB_MAX_STYLES 48
#define SKB_MAX_VARS 32
#define SKB_NAME 32

typedef struct { float r, g, b; } skb_rgb_t;

typedef struct {
    float dx, dy, blur, spread;
    skb_rgb_t color;
    float alpha;
} skb_shadow_t;

typedef struct {
    char name[SKB_NAME];
    int gradient;
    skb_rgb_t fill, grad_to;
    float grad_deg, grad_stop;
    skb_shadow_t outer[SKB_MAX_SHADOWS];
    int outer_count;
    skb_shadow_t inner[SKB_MAX_SHADOWS];
    int inner_count;
    skb_rgb_t stroke;           // stroke=COLOR: a line one pixel wide inside the edge,
    float stroke_alpha;         // as strong as the colour's alpha (0: none)
} skb_style_t;

typedef struct {
    char name[SKB_NAME];
    skb_rgb_t rgb;
    float alpha;
    int overridden;             // set over the recipe (the theme's): the recipe's "color" keeps it
} skb_var_t;

typedef struct {
    slsk_entry_t e;
    uint8_t *data;              // BGRA (or the mask's coverage), malloc'd
    const skb_style_t *style;   // surfaces only: for the nine-slice check
    int radius, height;
} skb_entry_t;

typedef struct {
    skb_rgb_t ground;           // what opaque sprites are composed on
    skb_var_t vars[SKB_MAX_VARS];
    int var_count;
    skb_style_t styles[SKB_MAX_STYLES];
    int style_count;
    skb_entry_t *entries;       // malloc'd, grows
    int entry_count, entry_cap;
    char error[160];            // the first thing that went wrong (English)
} skb_t;

// Premultiplied RGBA floats.
typedef struct {
    int w, h;
    float *px;
} skb_canvas_t;

void skb_init(skb_t *s);
void skb_free(skb_t *s);

// A variable: defined by the recipe ("color"), or set over it (the theme's).
// An override keeps its value when the recipe defines the variable later.
int skb_var_set(skb_t *s, const char *name, skb_rgb_t rgb, float alpha, int override);
// "RRGGBB", "$name" or "$name~K": 0, or -1 with s->error.
int skb_color(skb_t *s, const char *text, skb_rgb_t *out, float *alpha);

// One recipe line already split in tokens, if it is one of the lines that
// are about colours: color, ground, style, surface, tilebase. 1 done, 0 not
// one of these (the caller's), -1 wrong (s->error).
int skb_recipe_line(skb_t *s, char **tok, int n);
const skb_style_t *skb_find_style(const skb_t *s, const char *name);

// The drawing, for the tool's own sprites (tiles with an icon, shapes).
skb_canvas_t skb_canvas_new(int w, int h);
void skb_canvas_fill(skb_canvas_t *c, skb_rgb_t col);
void skb_canvas_paint(skb_canvas_t *c, const float *cov, skb_rgb_t col, float alpha);
void skb_draw_surface(skb_canvas_t *c, const skb_style_t *st, float x, float y, float w, float h, float r);
void skb_blur(float *m, int w, int h, float sigma);
void skb_style_pads(const skb_style_t *st, int pad[4]);
uint8_t *skb_canvas_bgra(const skb_canvas_t *c);
void skb_clear_faint(uint8_t *bgra, size_t pixels);
void skb_trim_faint(skb_entry_t *en);
skb_entry_t *skb_add_entry(skb_t *s, const char *name, uint16_t kind);

// The sprites. 0, or -1 with s->error.
int skb_surface(skb_t *s, const char *name, const skb_style_t *st, int radius, int height, int alpha);
// An empty tile ("SET-blank"); the tool's tiles with an icon draw it over.
int skb_tile(skb_t *s, const char *name, const skb_style_t *st, int size, int radius, int alpha,
             void (*icon)(skb_canvas_t *c, float x, float y, void *user), void *user);

// The whole file (header, table, data) in one malloc'd buffer: its size, 0
// when there is no memory.
size_t skb_write(const skb_t *s, uint8_t **out);
