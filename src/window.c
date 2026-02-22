#include "window.h"

int sl_window_init(sl_window_t *win, sl_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!win) return 0;
    peos_memset(win, 0, sizeof(*win));
    win->ctx = ctx;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->z_index = 0u;
    win->focused = 0u;
    return 1;
}

void sl_window_shutdown(sl_window_t *win) {
    (void)win;
}

int sl_window_render(sl_window_t *win) {
    if (!win) return 0;
    // For now render a black box on position and size of the window. In the future we can render the actual framebuffer content here.
    sl_fill_rect(win->ctx, win->x, win->y, win->width, win->height,
                 0u, 0u, 0u);

    if (win->width >= 2u && win->height >= 2u) {
        uint8_t br = win->focused ? 255u : 220u;
        uint8_t bg = win->focused ? 230u : 220u;
        uint8_t bb = win->focused ? 120u : 220u;
        sl_fill_rect(win->ctx, win->x, win->y, win->width, 1u, br, bg, bb);
        sl_fill_rect(win->ctx, win->x, win->y + win->height - 1u, win->width, 1u, br, bg, bb);
        sl_fill_rect(win->ctx, win->x, win->y, 1u, win->height, br, bg, bb);
        sl_fill_rect(win->ctx, win->x + win->width - 1u, win->y, 1u, win->height, br, bg, bb);
    }
    return 1;
}