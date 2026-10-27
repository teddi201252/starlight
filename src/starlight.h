#pragma once

#include "peos/peos.h"
#include "resources.h"
#include "render/image_decode.h"
#include "render/skin.h"

typedef struct {
    peos_fb_info_t fb;
    uint8_t *frame;
    uint8_t *fb_front;
    size_t row_stride;
    size_t fb_front_stride;
    size_t frame_bytes;
    size_t fb_front_bytes;
    size_t map_bytes;
    uint8_t bytespp;
    uint8_t fb_direct;
    uint8_t hw_cursor;
    uint8_t cursor_saved_valid;
    uint8_t clip_enabled;
    uint8_t cursor_theme;     // 1: the Soft UI set of the skin, 0: the single image cursor_path
    int input_raw;
    int64_t input_fd;      // input endpoint (wait-able), -1 = legacy input_poll/input_mouse
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cursor_saved_x;
    uint32_t cursor_saved_y;
    uint32_t cursor_saved_w;
    uint32_t cursor_saved_h;
    uint32_t hw_cursor_scale_pct;
    uint32_t clip_x;
    uint32_t clip_y;
    uint32_t clip_w;
    uint32_t clip_h;
    char cursor_path[160];
    char background_path[160];
    char font_manifest_path[160];
    char keyboard_layout_path[160];
    uint32_t display_width;
    uint32_t display_height;
    uint8_t animations_enabled;
    uint32_t animation_speed_pct;
    uint32_t double_click_ms;
    uint32_t keyboard_repeat_delay_ms;
    uint32_t keyboard_repeat_rate_hz;
    uint32_t clipboard_manager_hold_ms;
    uint8_t clipboard_feedback_audio_enabled;
    uint8_t clipboard_feedback_visual_enabled;
    uint8_t graphics_perf_enabled;
    uint8_t timezone_eu_central;   // timezone=Europe/Rome (CET/CEST), else UTC

    // Background
    sl_image_t bg_image;
    uint8_t bg_loaded;
    // The background already scaled to the screen, in the frame's format:
    // restoring it under a dirty rect is a row copy.
    uint8_t *bg_frame;
    size_t bg_frame_stride;
    // The desktop layer (desktop.c): bg_frame with the bar and the widgets,
    // same stride. The background is restored from it when it exists.
    uint8_t *desk_frame;

    // Baked sprites and font atlases (tools/slskin), read in place.
    sl_skin_t skin;
    uint8_t skin_loaded;

    // FPS Counter
    uint64_t fps_last_ns;
    uint32_t fps_frames;
    uint32_t fps_value;
    // show_performance=1: the performance card (fps, work per frame) at the
    // bottom right; the work is measured from the frame's start to present.
    uint8_t show_perf;
    uint8_t perf_shown;          // drawn last frame: its place to repaint when it goes
    uint64_t perf_work_ns, perf_work_max_ns;
    uint32_t perf_work_avg_us, perf_work_peak_us;
} sl_ctx_t;
