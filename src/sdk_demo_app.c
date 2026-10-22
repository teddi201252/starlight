#include "sdk_demo_app.h"

#include "ui_sdk.h"

typedef struct {
    sl_ui_ctx_t ui;
    uint8_t mouse_down;
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    int32_t mouse_wheel_x;
    int32_t mouse_wheel_y;
    uint8_t caps_lock_on;
    uint32_t clipboard_cmds;
    uint32_t slider;
    uint32_t clicks;
    uint8_t image_rgba[32u * 32u * 4u];
    uint8_t image_ready;
} sdk_demo_state_t;

static sdk_demo_state_t g_demo;
static app_instance_t g_demo_app;
static uint8_t g_demo_bound = 0u;

#define SDK_DEMO_BTN_X 10u
#define SDK_DEMO_BTN_Y 28u
#define SDK_DEMO_BTN_W 120u
#define SDK_DEMO_BTN_H 24u

static int sdk_demo_point_in_rect(uint32_t px, uint32_t py,
                                  uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (px < x || py < y) return 0;
    if (px >= x + w || py >= y + h) return 0;
    return 1;
}

static void sdk_demo_prepare_image(sdk_demo_state_t *s) {
    if (!s || s->image_ready) return;
    for (uint32_t y = 0u; y < 32u; y++) {
        for (uint32_t x = 0u; x < 32u; x++) {
            uint32_t i = (y * 32u + x) * 4u;
            uint8_t checker = (uint8_t)(((x / 4u) ^ (y / 4u)) & 1u);
            s->image_rgba[i + 0u] = checker ? 80u : 20u;
            s->image_rgba[i + 1u] = checker ? 180u : 80u;
            s->image_rgba[i + 2u] = checker ? 250u : 40u;
            s->image_rgba[i + 3u] = 255u;
        }
    }
    s->image_ready = 1u;
}

static void sdk_demo_on_input(sl_ctx_t *ctx, const sl_input_frame_t *frame,
                              const peos_input_event_t *event, void *userdata) {
    (void)ctx;
    sdk_demo_state_t *s = (sdk_demo_state_t *)userdata;
    if (!s) return;

    if (frame) {
        s->mouse_down = (frame->mouse_buttons & 0x01u) ? 1u : 0u;
        s->mouse_pressed = frame->mouse_left_pressed;
        s->mouse_released = frame->mouse_left_released;
        s->mouse_wheel_x = frame->mouse_wheel_x;
        s->mouse_wheel_y = frame->mouse_wheel_y;
        s->caps_lock_on = frame->caps_lock_on;
        s->clipboard_cmds = frame->clipboard_cmds;

        if (frame->mouse_left_released) {
            uint32_t body_x = 0u;
            uint32_t body_y = 0u;
            if (s->ui.win) sl_window_body_rect(s->ui.win, &body_x, &body_y, NULL, NULL);
            uint32_t btn_x = body_x + SDK_DEMO_BTN_X;
            uint32_t btn_y = body_y + SDK_DEMO_BTN_Y;
            if (sdk_demo_point_in_rect(ctx->cursor_x, ctx->cursor_y,
                                       btn_x, btn_y, SDK_DEMO_BTN_W, SDK_DEMO_BTN_H)) {
                s->clicks++;
            }
        }
    }

    if (event && event->pressed && event->code == PEOS_KEY_ENTER) {
        s->clicks++;
    }
}

static void sdk_demo_u32_to_str(uint32_t value, char *out, size_t cap) {
    if (!out || cap == 0u) return;
    char tmp[16];
    size_t n = 0u;
    if (value == 0u) {
        tmp[n++] = '0';
    } else {
        while (value > 0u && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    size_t w = 0u;
    while (n > 0u && w + 1u < cap) {
        out[w++] = tmp[--n];
    }
    out[w] = '\0';
}

static void sdk_demo_on_render(sl_ctx_t *ctx, const sl_window_t *win, void *userdata) {
    sdk_demo_state_t *s = (sdk_demo_state_t *)userdata;
    if (!ctx || !win || !s) return;

    sdk_demo_prepare_image(s);

    uint32_t body_x, body_y, body_w, body_h;
    sl_window_body_rect(win, &body_x, &body_y, &body_w, &body_h);
    if (body_w == 0u || body_h == 0u) return;

    sl_fill_rect(ctx, body_x, body_y, body_w, body_h, 18u, 20u, 28u);

    sl_ui_begin(&s->ui, ctx, win,
                ctx->cursor_x, ctx->cursor_y,
                s->mouse_down, s->mouse_pressed, s->mouse_released,
                s->mouse_wheel_x, s->mouse_wheel_y,
                s->clipboard_cmds, s->caps_lock_on);

    uint32_t x = body_x + 10u;
    uint32_t y = body_y + 10u;

    sl_ui_label(&s->ui, x, y, "PeOS Starlight UI SDK (MVP)", SL_UI_COLOR_TEXT_PRIMARY);
    y += 18u;

    (void)sl_ui_button(&s->ui, 1u, x + SDK_DEMO_BTN_X - 10u, y, SDK_DEMO_BTN_W, SDK_DEMO_BTN_H, "Click me");

    char clicks_str[16];
    sdk_demo_u32_to_str(s->clicks, clicks_str, sizeof(clicks_str));
    sl_ui_label(&s->ui, x + 132u, y + 8u, "clicks:", SL_UI_COLOR_TEXT_SECONDARY);
    sl_ui_label(&s->ui, x + 188u, y + 8u, clicks_str, SL_UI_COLOR_TEXT_PRIMARY);
    s->clipboard_cmds = 0u;

    y += 36u;
    sl_ui_label(&s->ui, x, y, "Slider:", SL_UI_COLOR_TEXT_SECONDARY);
    y += 14u;
    (void)sl_ui_slider_u32(&s->ui, 2u, x, y, 220u, 0u, 100u, &s->slider);

    char slider_str[16];
    sdk_demo_u32_to_str(s->slider, slider_str, sizeof(slider_str));
    sl_ui_label(&s->ui, x + 230u, y + 4u, slider_str, SL_UI_COLOR_TEXT_PRIMARY);

    y += 28u;
    sl_ui_label(&s->ui, x, y, "Image:", SL_UI_COLOR_TEXT_SECONDARY);
    y += 14u;
    sl_ui_image_rgba(&s->ui, x, y, 96u, 96u, s->image_rgba, 32u, 32u, 32u * 4u);

    s->mouse_pressed = 0u;
    s->mouse_released = 0u;
    s->mouse_wheel_x = 0;
    s->mouse_wheel_y = 0;
}

app_instance_t *sl_sdk_demo_app_instance(void) {
    if (!g_demo_bound) {
        peos_memset(&g_demo, 0, sizeof(g_demo));
        g_demo.slider = 35u;

        peos_memset(&g_demo_app, 0, sizeof(g_demo_app));
        g_demo_app.on_input = sdk_demo_on_input;
        g_demo_app.on_render = sdk_demo_on_render;
        g_demo_app.userdata = &g_demo;
        g_demo_bound = 1u;
    }
    return &g_demo_app;
}
