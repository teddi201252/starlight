#pragma once

#include "peos/peos.h"

typedef struct {
    peos_fb_info_t fb;
    uint8_t *frame;
    size_t frame_bytes;
    size_t map_bytes;
    uint8_t bytespp;
    int input_raw;
    uint32_t cursor_x;
    uint32_t cursor_y;
} sl_ctx_t;
