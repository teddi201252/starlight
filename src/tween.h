#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SL_TWEEN_EASE_LINEAR = 0,
    SL_TWEEN_EASE_OUT_CUBIC = 1,
    SL_TWEEN_EASE_IN_OUT_CUBIC = 2,
} sl_tween_easing_t;

typedef void (*sl_tween_finish_cb_t)(uint32_t id, void *userdata);

void sl_tween_init(void);
void sl_tween_reset(void);
void sl_tween_update(uint32_t dt_ms);

void sl_tween_set_enabled(uint8_t enabled);
uint8_t sl_tween_is_enabled(void);

void sl_tween_set_duration_scale_pct(uint32_t scale_pct);
uint32_t sl_tween_get_duration_scale_pct(void);

uint8_t sl_tween_to_i32(uint32_t id, int32_t *target, int32_t to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata);
uint8_t sl_tween_to_u32(uint32_t id, uint32_t *target, uint32_t to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata);
uint8_t sl_tween_to_f32(uint32_t id, float *target, float to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata);

void sl_tween_set_i32_immediate(int32_t *target, int32_t value);
void sl_tween_set_u32_immediate(uint32_t *target, uint32_t value);
void sl_tween_set_f32_immediate(float *target, float value);

void sl_tween_cancel_id(uint32_t id);
void sl_tween_cancel_target(const void *target);
uint8_t sl_tween_is_active_id(uint32_t id);
uint8_t sl_tween_is_active_target(const void *target);

#ifdef __cplusplus
}
#endif
