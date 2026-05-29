#pragma once

#include "starlight_input.h"

#include <stdint.h>

void sl_clipboard_shutdown(void);
uint8_t sl_clipboard_set_text(const char *text, uint32_t text_len);
const char *sl_clipboard_current_text(uint32_t *out_len);
uint8_t sl_clipboard_has_text(void);

uint8_t sl_clipboard_handle_input(sl_ctx_t *ctx, sl_input_frame_t *frame);
void sl_clipboard_render_overlay(sl_ctx_t *ctx);
