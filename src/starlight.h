#pragma once

#include "peos/peos.h"
#include "resources.h"
#include "render/image_decode.h"

typedef struct {
    peos_fb_info_t fb;
    uint8_t *frame;
    size_t row_stride;
    size_t frame_bytes;
    size_t map_bytes;
    uint8_t bytespp;
    uint8_t fb_direct;
    uint8_t hw_cursor;
    uint8_t cursor_saved_valid;
    uint8_t cursor_saved[36]; // 3x3 cursor * max 4 bytes/pixel
    int input_raw;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cursor_saved_x;
    uint32_t cursor_saved_y;
    uint32_t cursor_saved_w;
    uint32_t cursor_saved_h;
    uint32_t hw_cursor_scale_pct;
    char cursor_path[160];
    char background_path[160];
    uint32_t display_width;
    uint32_t display_height;

    // Background
    sl_image_t bg_image;
    uint8_t bg_loaded;

    // FPS Counter
    uint64_t fps_last_ns;
    uint32_t fps_frames;
    uint32_t fps_value;
} sl_ctx_t;
