#include "window.h"

#include "starlight_internal.h"
#include "app_host.h"

#include <stdlib.h>

#define SL_FPS_BOOST_FOCUSED 60u
#define SL_FPS_BOOST_UNFOCUSED 24u
#define SL_ACTIVITY_BOOST_NS 300000000ull
#define SL_ACTIVITY_IDLE_NS 2500000000ull

static int sl_window_ensure_surface(sl_window_t *win) {
    if (!win || !win->ctx || win->width == 0u || win->height == 0u) return 0;
    uint32_t stride = win->width * win->ctx->bytespp;
    size_t bytes = (size_t)stride * (size_t)win->height;
    if (bytes == 0u) return 0;

    if (win->client_surface && win->client_width == win->width &&
        win->client_height == win->height && win->client_stride == stride) {
        return 1;
    }

    uint8_t *new_surface = (uint8_t *)realloc(win->client_surface, bytes);
    if (!new_surface) return 0;
    win->client_surface = new_surface;
    win->client_width = win->width;
    win->client_height = win->height;
    win->client_stride = stride;
    return 1;
}

static int sl_window_ensure_anim_underlay(sl_window_t *win) {
    if (!win || !win->ctx || win->width == 0u || win->height == 0u) return 0;
    uint32_t stride = win->width * win->ctx->bytespp;
    size_t bytes = (size_t)stride * (size_t)win->height;
    if (bytes == 0u) return 0;

    if (win->anim_underlay &&
        win->anim_underlay_stride == stride &&
        win->anim_underlay_height == win->height) {
        return 1;
    }

    uint8_t *new_buf = (uint8_t *)realloc(win->anim_underlay, bytes);
    if (!new_buf) return 0;
    win->anim_underlay = new_buf;
    win->anim_underlay_stride = stride;
    win->anim_underlay_height = win->height;
    return 1;
}

static int sl_window_copy_frame_to_cache(sl_window_t *win) {
    if (!win || !win->ctx || !win->ctx->frame) return 0;
    if (!sl_window_ensure_surface(win)) return 0;

    int64_t x0 = (int64_t)win->x;
    int64_t y0 = (int64_t)win->y;
    int64_t x1 = x0 + (int64_t)win->width;
    int64_t y1 = y0 + (int64_t)win->height;

    if (x1 <= 0 || y1 <= 0) return 0;
    if (x0 >= (int64_t)win->ctx->fb.width || y0 >= (int64_t)win->ctx->fb.height) return 0;

    int64_t cx0 = (x0 < 0) ? 0 : x0;
    int64_t cy0 = (y0 < 0) ? 0 : y0;
    int64_t cx1 = (x1 > (int64_t)win->ctx->fb.width) ? (int64_t)win->ctx->fb.width : x1;
    int64_t cy1 = (y1 > (int64_t)win->ctx->fb.height) ? (int64_t)win->ctx->fb.height : y1;
    if (cx1 <= cx0 || cy1 <= cy0) return 0;

    uint32_t copy_w = (uint32_t)(cx1 - cx0);
    uint32_t copy_h = (uint32_t)(cy1 - cy0);
    uint32_t off_x = (uint32_t)(cx0 - x0);
    uint32_t off_y = (uint32_t)(cy0 - y0);
    size_t row_bytes = (size_t)copy_w * (size_t)win->ctx->bytespp;

    for (uint32_t row = 0; row < copy_h; row++) {
        uint8_t *dst = win->client_surface +
                       ((size_t)(off_y + row) * (size_t)win->client_stride) +
                       ((size_t)off_x * (size_t)win->ctx->bytespp);
        const uint8_t *src = win->ctx->frame +
                             ((size_t)(cy0 + row) * win->ctx->row_stride) +
                             ((size_t)cx0 * (size_t)win->ctx->bytespp);
        peos_memcpy(dst, src, row_bytes);
    }

    return 1;
}

static int sl_window_copy_frame_to_cache_rect(sl_window_t *win,
                                              uint32_t rx, uint32_t ry,
                                              uint32_t rw, uint32_t rh) {
    if (!win || !win->ctx || !win->ctx->frame) return 0;
    if (!sl_window_ensure_surface(win)) return 0;
    if (rw == 0u || rh == 0u) return 0;

    int64_t wx0 = (int64_t)win->x;
    int64_t wy0 = (int64_t)win->y;
    int64_t wx1 = wx0 + (int64_t)win->width;
    int64_t wy1 = wy0 + (int64_t)win->height;
    int64_t rx0 = (int64_t)rx;
    int64_t ry0 = (int64_t)ry;
    int64_t rx1 = rx0 + (int64_t)rw;
    int64_t ry1 = ry0 + (int64_t)rh;

    int64_t ix0 = (wx0 > rx0) ? wx0 : rx0;
    int64_t iy0 = (wy0 > ry0) ? wy0 : ry0;
    int64_t ix1 = (wx1 < rx1) ? wx1 : rx1;
    int64_t iy1 = (wy1 < ry1) ? wy1 : ry1;
    if (ix1 <= ix0 || iy1 <= iy0) return 0;

    int64_t cx0 = (ix0 < 0) ? 0 : ix0;
    int64_t cy0 = (iy0 < 0) ? 0 : iy0;
    int64_t cx1 = (ix1 > (int64_t)win->ctx->fb.width) ? (int64_t)win->ctx->fb.width : ix1;
    int64_t cy1 = (iy1 > (int64_t)win->ctx->fb.height) ? (int64_t)win->ctx->fb.height : iy1;
    if (cx1 <= cx0 || cy1 <= cy0) return 0;

    uint32_t copy_w = (uint32_t)(cx1 - cx0);
    uint32_t copy_h = (uint32_t)(cy1 - cy0);
    uint32_t off_x = (uint32_t)(cx0 - wx0);
    uint32_t off_y = (uint32_t)(cy0 - wy0);
    size_t row_bytes = (size_t)copy_w * (size_t)win->ctx->bytespp;

    for (uint32_t row = 0u; row < copy_h; row++) {
        uint8_t *dst = win->client_surface +
                       ((size_t)(off_y + row) * (size_t)win->client_stride) +
                       ((size_t)off_x * (size_t)win->ctx->bytespp);
        const uint8_t *src = win->ctx->frame +
                             ((size_t)(cy0 + row) * win->ctx->row_stride) +
                             ((size_t)cx0 * (size_t)win->ctx->bytespp);
        peos_memcpy(dst, src, row_bytes);
    }

    return 1;
}

static void sl_window_blit_cache_rect(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
                                      void *userdata) {
    sl_window_t *win = (sl_window_t *)userdata;
    if (!win || !win->ctx || !win->ctx->frame || !win->client_surface) return;
    int64_t x0 = (int64_t)win->x;
    int64_t y0 = (int64_t)win->y;
    int64_t x1 = x0 + (int64_t)win->width;
    int64_t y1 = y0 + (int64_t)win->height;

    int64_t rx0 = (int64_t)dx;
    int64_t ry0 = (int64_t)dy;
    int64_t rx1 = rx0 + (int64_t)dw;
    int64_t ry1 = ry0 + (int64_t)dh;

    int64_t ix0 = (x0 > rx0) ? x0 : rx0;
    int64_t iy0 = (y0 > ry0) ? y0 : ry0;
    int64_t ix1 = (x1 < rx1) ? x1 : rx1;
    int64_t iy1 = (y1 < ry1) ? y1 : ry1;
    if (ix1 <= ix0 || iy1 <= iy0) return;

    int64_t cx0 = (ix0 < 0) ? 0 : ix0;
    int64_t cy0 = (iy0 < 0) ? 0 : iy0;
    int64_t cx1 = (ix1 > (int64_t)win->ctx->fb.width) ? (int64_t)win->ctx->fb.width : ix1;
    int64_t cy1 = (iy1 > (int64_t)win->ctx->fb.height) ? (int64_t)win->ctx->fb.height : iy1;
    if (cx1 <= cx0 || cy1 <= cy0) return;

    uint32_t copy_w = (uint32_t)(cx1 - cx0);
    uint32_t copy_h = (uint32_t)(cy1 - cy0);
    uint32_t off_x = (uint32_t)(cx0 - x0);
    uint32_t off_y = (uint32_t)(cy0 - y0);
    size_t row_bytes = (size_t)copy_w * (size_t)win->ctx->bytespp;

    for (uint32_t row = 0; row < copy_h; row++) {
        const uint8_t *src = win->client_surface +
                             ((size_t)(off_y + row) * (size_t)win->client_stride) +
                             ((size_t)off_x * (size_t)win->ctx->bytespp);
        uint8_t *dst = win->ctx->frame +
                       ((size_t)(cy0 + row) * win->ctx->row_stride) +
                       ((size_t)cx0 * (size_t)win->ctx->bytespp);
        peos_memcpy(dst, src, row_bytes);
    }
}

static int sl_window_blit_cache_to_frame_dirty(sl_window_t *win) {
    if (!win || !win->ctx || !win->ctx->frame || !win->client_surface) return 0;
    if (win->client_width != win->width || win->client_height != win->height) return 0;
    sl_dirty_for_each_intersection(win->x, win->y, win->width, win->height,
                                   sl_window_blit_cache_rect, win);
    return 1;
}

void sl_window_set_fps_profile(sl_window_t *win, uint16_t focused_fps, uint16_t unfocused_fps) {
    if (!win) return;
    win->fps_focused = focused_fps;
    win->fps_unfocused = unfocused_fps;
    win->force_redraw = 1u;
    win->next_render_ns = 0ull;
}

void sl_window_mark_activity(sl_window_t *win, uint64_t now_ns) {
    if (!win) return;
    if (now_ns == 0ull) now_ns = peos_time_ns();
    win->last_activity_ns = now_ns;
    win->next_render_ns = 0ull;
}

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, app_instance_t* task, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!win) return 0;
    peos_memset(win, 0, sizeof(*win));
    win->ctx = ctx;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->z_index = 0u;
    win->focused = 0u;
    win->top_bar_height = 20u;
    win->cache_valid = 0u;
    win->force_redraw = 1u;
    win->anim_opacity = 255u;
    win->fps_focused = 60u;
    win->fps_unfocused = 24u;
    win->next_render_ns = 0ull;
    win->last_activity_ns = peos_time_ns();
    if (task) {
        win->task = task;
    }
    return 1;
}

void sl_window_shutdown(sl_window_t *win) {
    if (!win) return;
    if (win->client_surface) {
        free(win->client_surface);
        win->client_surface = NULL;
    }
    if (win->anim_underlay) {
        free(win->anim_underlay);
        win->anim_underlay = NULL;
    }
    win->client_width = 0u;
    win->client_height = 0u;
    win->client_stride = 0u;
    win->anim_underlay_stride = 0u;
    win->anim_underlay_height = 0u;
    win->cache_valid = 0u;
}

int sl_window_render(sl_window_t *win) {
    if (!win) return 0;

    uint8_t cache_was_valid = win->cache_valid;
    uint8_t anim_opacity = win->anim_opacity;
    uint8_t need_anim_blend = (anim_opacity < 255u) ? 1u : 0u;
    uint8_t *underlay = NULL;
    uint32_t underlay_stride = 0u;
    int64_t ux0 = 0, uy0 = 0, ux1 = 0, uy1 = 0;
    uint32_t ucopy_w = 0u, ucopy_h = 0u, uoff_x = 0u, uoff_y = 0u;

    uint64_t now = peos_time_ns();
    uint16_t base_fps = win->focused ? win->fps_focused : win->fps_unfocused;
    uint16_t target_fps = base_fps;
    uint64_t idle_ns = (now > win->last_activity_ns) ? (now - win->last_activity_ns) : 0ull;

    if (idle_ns <= SL_ACTIVITY_BOOST_NS) {
        uint16_t boost = win->focused ? SL_FPS_BOOST_FOCUSED : SL_FPS_BOOST_UNFOCUSED;
        if (boost > target_fps) target_fps = boost;
    } else if (idle_ns >= SL_ACTIVITY_IDLE_NS) {
        uint16_t min_idle = win->focused ? 18u : 6u;
        uint16_t decayed = (base_fps > 1u) ? (uint16_t)(base_fps / 2u) : base_fps;
        if (decayed < min_idle) decayed = min_idle;
        target_fps = decayed;
    }

    uint8_t due = 1u;
    if (target_fps > 0u && win->cache_valid && !win->force_redraw && win->next_render_ns > now) {
        due = 0u;
    }

    if (!due) {
        if (sl_window_blit_cache_to_frame_dirty(win)) return 1;
        due = 1u;
    }

    if (!due) return 1;

    if (need_anim_blend) {
        int64_t x0 = (int64_t)win->x;
        int64_t y0 = (int64_t)win->y;
        int64_t x1 = x0 + (int64_t)win->width;
        int64_t y1 = y0 + (int64_t)win->height;
        ux0 = (x0 < 0) ? 0 : x0;
        uy0 = (y0 < 0) ? 0 : y0;
        ux1 = (x1 > (int64_t)win->ctx->fb.width) ? (int64_t)win->ctx->fb.width : x1;
        uy1 = (y1 > (int64_t)win->ctx->fb.height) ? (int64_t)win->ctx->fb.height : y1;
        if (ux1 > ux0 && uy1 > uy0) {
            if (sl_window_ensure_anim_underlay(win)) {
                underlay = win->anim_underlay;
                underlay_stride = win->anim_underlay_stride;
                if (underlay_stride == 0u) {
                    need_anim_blend = 0u;
                } else {
                ucopy_w = (uint32_t)(ux1 - ux0);
                ucopy_h = (uint32_t)(uy1 - uy0);
                uoff_x = (uint32_t)(ux0 - x0);
                uoff_y = (uint32_t)(uy0 - y0);
                size_t row_bytes = (size_t)ucopy_w * (size_t)win->ctx->bytespp;
                for (uint32_t row = 0; row < ucopy_h; row++) {
                    uint8_t *dst = underlay +
                                   ((size_t)(uoff_y + row) * (size_t)underlay_stride) +
                                   ((size_t)uoff_x * (size_t)win->ctx->bytespp);
                    const uint8_t *src = win->ctx->frame +
                                         ((size_t)(uy0 + row) * win->ctx->row_stride) +
                                         ((size_t)ux0 * (size_t)win->ctx->bytespp);
                    peos_memcpy(dst, src, row_bytes);
                }
                }
            } else {
                need_anim_blend = 0u;
            }
        } else {
            need_anim_blend = 0u;
        }
    }

    // For now render a black box on position and size of the window. In the future we can render the actual framebuffer content here.
    sl_fill_rect(win->ctx, win->x, win->y, win->width, win->height,
                 0u, 0u, 0u);

    if (win->width >= 2u && win->height >= 2u) {
        uint8_t br = win->focused ? 255u : 220u;
        uint8_t bg = win->focused ? 230u : 220u;
        uint8_t bb = win->focused ? 120u : 220u;
        sl_fill_rect(win->ctx, win->x, win->y, win->width, win->top_bar_height, br, bg, bb);
        sl_fill_rect(win->ctx, win->x, win->y + win->height - 1u, win->width, 1u, br, bg, bb);
        sl_fill_rect(win->ctx, win->x, win->y, 1u, win->height, br, bg, bb);
        sl_fill_rect(win->ctx, win->x + win->width - 1u, win->y, 1u, win->height, br, bg, bb);

        if (win->width > (SL_WIN_CLOSE_BTN_W + SL_WIN_CLOSE_BTN_MARGIN + 2u) &&
            win->top_bar_height > (SL_WIN_CLOSE_BTN_H + 2u)) {
            uint32_t cb_x = win->x + win->width - SL_WIN_CLOSE_BTN_MARGIN - SL_WIN_CLOSE_BTN_W - 1u;
            uint32_t cb_y = win->y + 1u + ((win->top_bar_height - 2u - SL_WIN_CLOSE_BTN_H) / 2u);
            sl_fill_rect(win->ctx, cb_x, cb_y, SL_WIN_CLOSE_BTN_W, SL_WIN_CLOSE_BTN_H, 176u, 48u, 48u);
            sl_draw_text(win->ctx, cb_x + 3u, cb_y + 3u, "x", 255u, 255u, 255u);
        }
    }

    if (win->task && win->task->on_render) {
        win->task->on_render(win->ctx, win, win->task->userdata);
    }

    if (need_anim_blend && underlay) {
        uint32_t inv = (uint32_t)(255u - anim_opacity);
        if (ucopy_w > 0u && ucopy_h > 0u) {
            for (uint32_t row = 0; row < ucopy_h; row++) {
                uint8_t *dst = win->ctx->frame +
                               ((size_t)(uy0 + row) * win->ctx->row_stride) +
                               ((size_t)ux0 * (size_t)win->ctx->bytespp);
                const uint8_t *bg = underlay +
                                    ((size_t)(uoff_y + row) * (size_t)underlay_stride) +
                                    ((size_t)uoff_x * (size_t)win->ctx->bytespp);
                for (uint32_t col = 0u; col < ucopy_w; col++) {
                    uint8_t *d = dst + ((size_t)col * (size_t)win->ctx->bytespp);
                    const uint8_t *b = bg + ((size_t)col * (size_t)win->ctx->bytespp);
                    d[0] = (uint8_t)((((uint32_t)d[0] * anim_opacity) + ((uint32_t)b[0] * inv) + 127u) / 255u);
                    d[1] = (uint8_t)((((uint32_t)d[1] * anim_opacity) + ((uint32_t)b[1] * inv) + 127u) / 255u);
                    d[2] = (uint8_t)((((uint32_t)d[2] * anim_opacity) + ((uint32_t)b[2] * inv) + 127u) / 255u);
                    if (win->ctx->bytespp == 4u) d[3] = 0xFFu;
                }
            }
        }
    }
    if (cache_was_valid && !win->force_redraw && !need_anim_blend &&
        win->task && win->task->userdata) {
        uint32_t rx = 0u, ry = 0u, rw = 0u, rh = 0u;
        if (sl_app_host_cache_update_rect(win, win->task->userdata, &rx, &ry, &rw, &rh)) {
            win->cache_valid = sl_window_copy_frame_to_cache_rect(win, rx, ry, rw, rh) ? 1u : 0u;
        } else {
            win->cache_valid = sl_window_copy_frame_to_cache(win) ? 1u : 0u;
        }
    } else {
        win->cache_valid = sl_window_copy_frame_to_cache(win) ? 1u : 0u;
    }
    win->force_redraw = 0u;
    if (target_fps > 0u) {
        uint64_t interval = 1000000000ull / (uint64_t)target_fps;
        if (interval == 0ull) interval = 1ull;
        win->next_render_ns = now + interval;
    } else {
        win->next_render_ns = now;
    }

    return 1;
}
