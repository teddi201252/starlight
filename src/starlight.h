#pragma once

#include "peos/peos.h"

typedef struct {
    peos_fb_info_t fb;
    uint8_t *frame;
    size_t frame_bytes;
    size_t map_bytes;
    uint8_t bytespp;
    uint8_t fb_direct;
    uint8_t cursor_saved_valid;
    uint8_t cursor_saved[36]; // 3x3 cursor * max 4 bytes/pixel
    int input_raw;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cursor_saved_x;
    uint32_t cursor_saved_y;
    uint32_t cursor_saved_w;
    uint32_t cursor_saved_h;

    // FPS Counter
    uint64_t fps_last_ns;
    uint32_t fps_frames;
    uint32_t fps_value;
} sl_ctx_t;


