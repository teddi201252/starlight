#pragma once

#include "starlight_input.h"

#include <stdint.h>

void sl_clipboard_shutdown(void);
uint8_t sl_clipboard_set_text(const char *text, uint32_t text_len);
const char *sl_clipboard_current_text(uint32_t *out_len);
uint8_t sl_clipboard_has_text(void);
void sl_clipboard_set_feedback_config(uint8_t audio_enabled, uint8_t visual_enabled);
void sl_clipboard_notify_copy(sl_ctx_t *ctx, const char *text, uint32_t text_len,
                              int32_t start_x, int32_t start_y,
                              int32_t end_x, int32_t end_y);
void sl_clipboard_tick(sl_ctx_t *ctx);

uint8_t sl_clipboard_handle_input(sl_ctx_t *ctx, sl_input_frame_t *frame,
                                  uint8_t *out_changed, uint8_t *out_full_redraw);
void sl_clipboard_overlay_bounds(const sl_ctx_t *ctx,
                                 uint32_t *out_x, uint32_t *out_y,
                                 uint32_t *out_w, uint32_t *out_h);
void sl_clipboard_render_overlay(sl_ctx_t *ctx);
