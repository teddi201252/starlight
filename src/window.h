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

// Soft UI frame (design/starlight-soft-ui, tavola Finestre): a raised body
// with radius 32, a 40 px title bar 18 px from the top, the app's body
// below it with 22 px of pearl around.
#define SL_WIN_RADIUS 32u
#define SL_WIN_PAD_X 22u
#define SL_WIN_PAD_TOP 18u
#define SL_WIN_BAR_H 40u
#define SL_WIN_BODY_TOP (SL_WIN_PAD_TOP + SL_WIN_BAR_H + 18u)
#define SL_WIN_PAD_BOTTOM 22u
// Inside this inset the window is opaque pearl whatever the corners do: it is
// what the window cache keeps. Shadow and corners are drawn around it.
#define SL_WIN_OPAQUE_INSET 10u
// Title bar buttons: 28 px circles, 10 px apart, from the left padding.
#define SL_WIN_BTN 28u
#define SL_WIN_BTN_GAP 10u
// What the frame adds around the body: a window asked for W x H by an app
// gets this much more so that its body keeps W-4 x H-23 as before.
#define SL_WIN_FRAME_EXTRA_W (2u * SL_WIN_PAD_X - 4u)
#define SL_WIN_FRAME_EXTRA_H (SL_WIN_BODY_TOP + SL_WIN_PAD_BOTTOM - 23u)

enum { SL_WIN_BTN_CLOSE = 0, SL_WIN_BTN_MIN = 1, SL_WIN_BTN_MAX = 2, SL_WIN_BTN_NONE = -1 };

typedef struct {
    int32_t x, y;
    uint32_t w, h;
} sl_win_rect_t;

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
    uint8_t minimized;          // hidden; the dock brings it back
    uint8_t maximized;          // restore_* holds the rect to go back to
    uint8_t dragging;           // moved by its title bar: shown from the cache
    int8_t pressed_btn;         // title bar button under a press, SL_WIN_BTN_NONE
    uint32_t top_bar_height;    // the title bar: dragging starts here
    uint32_t restore_x, restore_y, restore_w, restore_h;
    char title[64];

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
    uint64_t opened_ns;         // when it opened: the wait cursor until the app shows something
};

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, app_instance_t* task, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sl_window_shutdown(sl_window_t *win);
int sl_window_render(sl_window_t *win);

// The app's area, where its on_render draws.
void sl_window_body_rect(const sl_window_t *win, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
// Everything the window touches on screen, shadow included.
sl_win_rect_t sl_window_outer_rect(const sl_window_t *win);
// Mark the window dirty: all of it (moved, focus changed) or the body only
// (the app redrew).
void sl_window_dirty_all(const sl_ctx_t *ctx, const sl_window_t *win);
void sl_window_dirty_body(const sl_ctx_t *ctx, const sl_window_t *win);
// Which title bar button is at (x, y), or SL_WIN_BTN_NONE.
int sl_window_button_at(const sl_window_t *win, uint32_t x, uint32_t y);
void sl_window_button_rect(const sl_window_t *win, int button, uint32_t *x, uint32_t *y);
void sl_window_set_fps_profile(sl_window_t *win, uint16_t focused_fps, uint16_t unfocused_fps);
void sl_window_mark_activity(sl_window_t *win, uint64_t now_ns);
