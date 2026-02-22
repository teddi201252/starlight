#pragma once 
#include "starlight.h"
#include "render/pixels.h"

typedef struct {
    sl_ctx_t *ctx;

    // Window info
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z_index;
    uint8_t focused;
} sl_window_t;

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sl_window_shutdown(sl_window_t *win);
int sl_window_render(sl_window_t *win);