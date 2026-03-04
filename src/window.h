#pragma once 
#include "starlight.h"
#include "starlight_input.h"
#include "render/pixels.h"

typedef struct sl_window sl_window_t;
typedef struct app_instance app_instance_t;

typedef void (*sl_app_on_input_fn)(sl_ctx_t *ctx, const sl_input_frame_t *frame,
                                   const peos_input_event_t *event, void *userdata);
typedef void (*sl_app_on_render_fn)(sl_ctx_t *ctx, const sl_window_t *win, void *userdata);
typedef void (*sl_app_on_close_fn)(void *userdata);

#define SL_WIN_CLOSE_BTN_W 14u
#define SL_WIN_CLOSE_BTN_H 14u
#define SL_WIN_CLOSE_BTN_MARGIN 3u

struct app_instance {
    uint32_t pid;
    uint32_t tid;
    sl_app_on_input_fn on_input;
    sl_app_on_render_fn on_render;
    sl_app_on_close_fn on_close;
    void *userdata;
};

struct sl_window {
    sl_ctx_t *ctx;

    // Window info
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z_index;
    uint8_t focused;
    uint32_t top_bar_height;

    // Task
    app_instance_t* task;
    uint8_t* client_surface;
    uint32_t client_width;
    uint32_t client_height;
    uint32_t client_stride;
    uint8_t *anim_underlay;
    uint32_t anim_underlay_stride;
    uint32_t anim_underlay_height;
    uint8_t cache_valid;
    uint8_t force_redraw;
    uint8_t anim_opacity;
    uint16_t fps_focused;
    uint16_t fps_unfocused;
    uint64_t next_render_ns;
    uint64_t last_activity_ns;
};

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, app_instance_t* task, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sl_window_shutdown(sl_window_t *win);
int sl_window_render(sl_window_t *win);
void sl_window_set_fps_profile(sl_window_t *win, uint16_t focused_fps, uint16_t unfocused_fps);
void sl_window_mark_activity(sl_window_t *win, uint64_t now_ns);
