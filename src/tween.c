#include "tween.h"

#define SL_TWEEN_MAX 128u

typedef enum {
    SL_TWEEN_VALUE_I32 = 0,
    SL_TWEEN_VALUE_U32 = 1,
    SL_TWEEN_VALUE_F32 = 2,
} sl_tween_value_type_t;

typedef struct {
    uint8_t active;
    uint8_t value_type;
    uint16_t _pad;
    uint32_t id;
    void *target;
    union { int32_t i32; uint32_t u32; float f32; } from;
    union { int32_t i32; uint32_t u32; float f32; } to;
    uint32_t duration_ms;
    uint32_t elapsed_ms;
    sl_tween_easing_t easing;
    sl_tween_finish_cb_t on_finish;
    void *userdata;
} sl_tween_t;

static sl_tween_t g_tweens[SL_TWEEN_MAX];
static uint8_t g_enabled = 1u;
static uint32_t g_duration_scale_pct = 100u;

static float sl_tween_clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static uint32_t sl_tween_scaled_duration(uint32_t duration_ms) {
    uint64_t scaled;
    if (duration_ms == 0u) return 0u;
    scaled = ((uint64_t)duration_ms * (uint64_t)g_duration_scale_pct) / 100ull;
    if (scaled == 0ull) scaled = 1ull;
    if (scaled > 0xFFFFFFFFull) scaled = 0xFFFFFFFFull;
    return (uint32_t)scaled;
}

static float sl_tween_apply_ease(float t, sl_tween_easing_t easing) {
    float x = sl_tween_clamp01(t);
    if (easing == SL_TWEEN_EASE_OUT_CUBIC) {
        float inv = 1.0f - x;
        return 1.0f - (inv * inv * inv);
    }
    if (easing == SL_TWEEN_EASE_IN_OUT_CUBIC) {
        if (x < 0.5f) {
            return 4.0f * x * x * x;
        }
        {
            float inv = 1.0f - x;
            return 1.0f - (4.0f * inv * inv * inv);
        }
    }
    return x;
}

static int32_t sl_tween_round_i32(float v) {
    return (v >= 0.0f) ? (int32_t)(v + 0.5f) : (int32_t)(v - 0.5f);
}

static uint8_t sl_tween_assign_now(sl_tween_value_type_t type, void *target, const void *value_ptr) {
    if (!target || !value_ptr) return 0u;
    if (type == SL_TWEEN_VALUE_I32) {
        *(int32_t *)target = *(const int32_t *)value_ptr;
        return 1u;
    }
    if (type == SL_TWEEN_VALUE_U32) {
        *(uint32_t *)target = *(const uint32_t *)value_ptr;
        return 1u;
    }
    if (type == SL_TWEEN_VALUE_F32) {
        *(float *)target = *(const float *)value_ptr;
        return 1u;
    }
    return 0u;
}

static int32_t sl_tween_find_index_by_target(const void *target, sl_tween_value_type_t type) {
    if (!target) return -1;
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (!g_tweens[i].active) continue;
        if (g_tweens[i].target == target && g_tweens[i].value_type == (uint8_t)type) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sl_tween_find_free_slot(void) {
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (!g_tweens[i].active) return (int32_t)i;
    }
    return -1;
}

static uint8_t sl_tween_start_common(uint32_t id, void *target, const void *to_ptr,
                                     uint32_t duration_ms, sl_tween_easing_t easing,
                                     sl_tween_finish_cb_t on_finish, void *userdata,
                                     sl_tween_value_type_t type) {
    int32_t slot = -1;
    uint32_t scaled_duration;
    if (!target || !to_ptr) return 0u;

    if (!g_enabled || g_duration_scale_pct == 0u || duration_ms == 0u) {
        return sl_tween_assign_now(type, target, to_ptr);
    }

    slot = sl_tween_find_index_by_target(target, type);
    if (slot < 0) slot = sl_tween_find_free_slot();
    if (slot < 0) return 0u;

    scaled_duration = sl_tween_scaled_duration(duration_ms);
    if (scaled_duration == 0u) {
        return sl_tween_assign_now(type, target, to_ptr);
    }

    g_tweens[slot].active = 1u;
    g_tweens[slot].value_type = (uint8_t)type;
    g_tweens[slot].id = id;
    g_tweens[slot].target = target;
    g_tweens[slot].duration_ms = scaled_duration;
    g_tweens[slot].elapsed_ms = 0u;
    g_tweens[slot].easing = easing;
    g_tweens[slot].on_finish = on_finish;
    g_tweens[slot].userdata = userdata;

    if (type == SL_TWEEN_VALUE_I32) {
        g_tweens[slot].from.i32 = *(int32_t *)target;
        g_tweens[slot].to.i32 = *(const int32_t *)to_ptr;
    } else if (type == SL_TWEEN_VALUE_U32) {
        g_tweens[slot].from.u32 = *(uint32_t *)target;
        g_tweens[slot].to.u32 = *(const uint32_t *)to_ptr;
    } else {
        g_tweens[slot].from.f32 = *(float *)target;
        g_tweens[slot].to.f32 = *(const float *)to_ptr;
    }

    return 1u;
}

void sl_tween_init(void) {
    sl_tween_reset();
}

void sl_tween_reset(void) {
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        g_tweens[i].active = 0u;
    }
}

void sl_tween_update(uint32_t dt_ms) {
    if (!g_enabled) return;
    if (dt_ms == 0u) return;

    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        sl_tween_t *t = &g_tweens[i];
        float eased;
        if (!t->active || !t->target || t->duration_ms == 0u) continue;

        if (t->elapsed_ms >= t->duration_ms || dt_ms >= (t->duration_ms - t->elapsed_ms)) {
            t->elapsed_ms = t->duration_ms;
        } else {
            t->elapsed_ms += dt_ms;
        }

        eased = sl_tween_apply_ease((float)t->elapsed_ms / (float)t->duration_ms, t->easing);

        if (t->value_type == SL_TWEEN_VALUE_I32) {
            float from = (float)t->from.i32;
            float to = (float)t->to.i32;
            *(int32_t *)t->target = sl_tween_round_i32(from + (to - from) * eased);
        } else if (t->value_type == SL_TWEEN_VALUE_U32) {
            float from = (float)t->from.u32;
            float to = (float)t->to.u32;
            int32_t rounded = sl_tween_round_i32(from + (to - from) * eased);
            *(uint32_t *)t->target = (rounded < 0) ? 0u : (uint32_t)rounded;
        } else {
            float from = t->from.f32;
            float to = t->to.f32;
            *(float *)t->target = from + (to - from) * eased;
        }

        if (t->elapsed_ms >= t->duration_ms) {
            sl_tween_finish_cb_t cb = t->on_finish;
            uint32_t id = t->id;
            void *userdata = t->userdata;
            if (t->value_type == SL_TWEEN_VALUE_I32) *(int32_t *)t->target = t->to.i32;
            else if (t->value_type == SL_TWEEN_VALUE_U32) *(uint32_t *)t->target = t->to.u32;
            else *(float *)t->target = t->to.f32;
            t->active = 0u;
            if (cb) cb(id, userdata);
        }
    }
}

void sl_tween_set_enabled(uint8_t enabled) {
    g_enabled = enabled ? 1u : 0u;
    if (!g_enabled) sl_tween_reset();
}

uint8_t sl_tween_is_enabled(void) {
    return g_enabled;
}

void sl_tween_set_duration_scale_pct(uint32_t scale_pct) {
    if (scale_pct > 1000u) scale_pct = 1000u;
    g_duration_scale_pct = scale_pct;
}

uint32_t sl_tween_get_duration_scale_pct(void) {
    return g_duration_scale_pct;
}

uint8_t sl_tween_to_i32(uint32_t id, int32_t *target, int32_t to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata) {
    return sl_tween_start_common(id, target, &to, duration_ms, easing, on_finish, userdata,
                                 SL_TWEEN_VALUE_I32);
}

uint8_t sl_tween_to_u32(uint32_t id, uint32_t *target, uint32_t to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata) {
    return sl_tween_start_common(id, target, &to, duration_ms, easing, on_finish, userdata,
                                 SL_TWEEN_VALUE_U32);
}

uint8_t sl_tween_to_f32(uint32_t id, float *target, float to,
                        uint32_t duration_ms, sl_tween_easing_t easing,
                        sl_tween_finish_cb_t on_finish, void *userdata) {
    return sl_tween_start_common(id, target, &to, duration_ms, easing, on_finish, userdata,
                                 SL_TWEEN_VALUE_F32);
}

void sl_tween_set_i32_immediate(int32_t *target, int32_t value) {
    if (!target) return;
    sl_tween_cancel_target(target);
    *target = value;
}

void sl_tween_set_u32_immediate(uint32_t *target, uint32_t value) {
    if (!target) return;
    sl_tween_cancel_target(target);
    *target = value;
}

void sl_tween_set_f32_immediate(float *target, float value) {
    if (!target) return;
    sl_tween_cancel_target(target);
    *target = value;
}

void sl_tween_cancel_id(uint32_t id) {
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (g_tweens[i].active && g_tweens[i].id == id) {
            g_tweens[i].active = 0u;
        }
    }
}

void sl_tween_cancel_target(const void *target) {
    if (!target) return;
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (g_tweens[i].active && g_tweens[i].target == target) {
            g_tweens[i].active = 0u;
        }
    }
}

uint8_t sl_tween_is_active_id(uint32_t id) {
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (g_tweens[i].active && g_tweens[i].id == id) return 1u;
    }
    return 0u;
}

uint8_t sl_tween_is_active_target(const void *target) {
    if (!target) return 0u;
    for (uint32_t i = 0u; i < SL_TWEEN_MAX; i++) {
        if (g_tweens[i].active && g_tweens[i].target == target) return 1u;
    }
    return 0u;
}
