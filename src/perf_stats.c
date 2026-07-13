#include "perf_stats.h"

#include "peos/peos.h"

#include <stdio.h>

#define SL_PERF_SAMPLE_CAP 256u
#define SL_PERF_REPORT_NS 1000000000ull

typedef struct {
    uint8_t enabled;
    uint64_t report_start_ns;
    uint64_t frame_start_ns;
    uint64_t render_ns;
    uint64_t composite_ns;
    uint64_t copy_ns;
    uint64_t flush_ns;
    uint64_t pixels;
    uint64_t bytes;
    uint64_t rects;
    uint64_t frames;
    uint64_t full_frames;
    uint64_t samples[SL_PERF_SAMPLE_CAP];
    size_t sample_count;
} sl_perf_state_t;

static sl_perf_state_t g_perf;

static void sl_perf_reset_window(uint64_t now_ns) {
    uint8_t enabled = g_perf.enabled;
    g_perf = (sl_perf_state_t){0};
    g_perf.enabled = enabled;
    g_perf.report_start_ns = now_ns;
}

void sl_perf_init(uint8_t enabled) {
    g_perf = (sl_perf_state_t){0};
    g_perf.enabled = enabled ? 1u : 0u;
    g_perf.report_start_ns = peos_time_ns();
}

void sl_perf_frame_begin(uint64_t now_ns) {
    if (g_perf.enabled) g_perf.frame_start_ns = now_ns;
}

void sl_perf_add_render(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.render_ns += elapsed_ns;
}

void sl_perf_add_composite(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.composite_ns += elapsed_ns;
}

void sl_perf_record_present(uint64_t copy_ns, uint64_t flush_ns,
                            size_t rect_count, uint64_t pixels,
                            uint8_t bytespp, uint8_t full) {
    if (!g_perf.enabled) return;
    g_perf.copy_ns += copy_ns;
    g_perf.flush_ns += flush_ns;
    g_perf.rects += rect_count;
    g_perf.pixels += pixels;
    g_perf.bytes += pixels * bytespp;
    if (full) g_perf.full_frames++;
}

static void sl_perf_sort_samples(uint64_t *values, size_t count) {
    for (size_t i = 1u; i < count; i++) {
        uint64_t value = values[i];
        size_t j = i;
        while (j > 0u && values[j - 1u] > value) {
            values[j] = values[j - 1u];
            j--;
        }
        values[j] = value;
    }
}

void sl_perf_frame_end(uint64_t now_ns) {
    if (!g_perf.enabled || g_perf.frame_start_ns == 0u) return;
    uint64_t frame_ns = now_ns - g_perf.frame_start_ns;
    g_perf.frames++;
    if (g_perf.sample_count < SL_PERF_SAMPLE_CAP) {
        g_perf.samples[g_perf.sample_count++] = frame_ns;
    }
    if (now_ns - g_perf.report_start_ns < SL_PERF_REPORT_NS) return;

    sl_perf_sort_samples(g_perf.samples, g_perf.sample_count);
    uint64_t p50 = 0u, p95 = 0u;
    if (g_perf.sample_count > 0u) {
        p50 = g_perf.samples[(g_perf.sample_count - 1u) * 50u / 100u];
        p95 = g_perf.samples[(g_perf.sample_count - 1u) * 95u / 100u];
    }
    uint64_t frames = g_perf.frames ? g_perf.frames : 1u;
    printf("[GFXPERF] frames=%llu frame_p50_us=%llu frame_p95_us=%llu "
           "render_us=%llu composite_us=%llu copy_us=%llu flush_us=%llu "
           "rects=%llu pixels=%llu bytes=%llu full=%llu\n",
           (unsigned long long)g_perf.frames,
           (unsigned long long)(p50 / 1000u),
           (unsigned long long)(p95 / 1000u),
           (unsigned long long)(g_perf.render_ns / frames / 1000u),
           (unsigned long long)(g_perf.composite_ns / frames / 1000u),
           (unsigned long long)(g_perf.copy_ns / frames / 1000u),
           (unsigned long long)(g_perf.flush_ns / frames / 1000u),
           (unsigned long long)g_perf.rects,
           (unsigned long long)g_perf.pixels,
           (unsigned long long)g_perf.bytes,
           (unsigned long long)g_perf.full_frames);
    sl_perf_reset_window(now_ns);
}
