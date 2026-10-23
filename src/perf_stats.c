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
    uint64_t app_ns;
    uint64_t cache_ns;
    uint64_t windows_ns;
    uint64_t dock_ns;
    uint64_t desk_ns;
    uint64_t copy_ns;
    uint64_t flush_ns;
    uint64_t pixels;
    uint64_t bytes;
    uint64_t rects;
    uint64_t frames;
    uint64_t full_frames;
    uint64_t samples[SL_PERF_SAMPLE_CAP];
    uint64_t work_samples[SL_PERF_SAMPLE_CAP];
    size_t sample_count;
    size_t work_count;
    // The slowest frame of the second, part by part: what the p95 is made of.
    uint64_t at_begin[9];   // render, composite, app, cache, copy, flush, windows, dock, desk
    uint64_t slow_work, slow_part[9], slow_pixels, at_begin_pixels;
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

static void sl_perf_parts(uint64_t out[9]) {
    out[0] = g_perf.render_ns;
    out[1] = g_perf.composite_ns;
    out[2] = g_perf.app_ns;
    out[3] = g_perf.cache_ns;
    out[4] = g_perf.copy_ns;
    out[5] = g_perf.flush_ns;
    out[6] = g_perf.windows_ns;
    out[7] = g_perf.dock_ns;
    out[8] = g_perf.desk_ns;
}

void sl_perf_add_windows(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.windows_ns += elapsed_ns;
}

void sl_perf_add_dock(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.dock_ns += elapsed_ns;
}

void sl_perf_add_desk(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.desk_ns += elapsed_ns;
}

void sl_perf_frame_begin(uint64_t now_ns) {
    if (!g_perf.enabled) return;
    g_perf.frame_start_ns = now_ns;
    sl_perf_parts(g_perf.at_begin);
    g_perf.at_begin_pixels = g_perf.pixels;
}

// Frame work done: from frame_begin up to here, without the pacing wait.
void sl_perf_frame_work_done(uint64_t now_ns) {
    if (!g_perf.enabled || g_perf.frame_start_ns == 0u) return;
    uint64_t work = now_ns - g_perf.frame_start_ns;
    if (g_perf.work_count < SL_PERF_SAMPLE_CAP) {
        g_perf.work_samples[g_perf.work_count++] = work;
    }
    if (work > g_perf.slow_work) {
        uint64_t now_parts[9];
        sl_perf_parts(now_parts);
        g_perf.slow_work = work;
        for (int i = 0; i < 9; i++) g_perf.slow_part[i] = now_parts[i] - g_perf.at_begin[i];
        g_perf.slow_pixels = g_perf.pixels - g_perf.at_begin_pixels;
    }
}

void sl_perf_add_render(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.render_ns += elapsed_ns;
}

void sl_perf_add_app(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.app_ns += elapsed_ns;
}

void sl_perf_add_cache(uint64_t elapsed_ns) {
    if (g_perf.enabled) g_perf.cache_ns += elapsed_ns;
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
    sl_perf_sort_samples(g_perf.work_samples, g_perf.work_count);
    uint64_t p50 = 0u, p95 = 0u, work_p50 = 0u, work_p95 = 0u;
    if (g_perf.sample_count > 0u) {
        p50 = g_perf.samples[(g_perf.sample_count - 1u) * 50u / 100u];
        p95 = g_perf.samples[(g_perf.sample_count - 1u) * 95u / 100u];
    }
    if (g_perf.work_count > 0u) {
        work_p50 = g_perf.work_samples[(g_perf.work_count - 1u) * 50u / 100u];
        work_p95 = g_perf.work_samples[(g_perf.work_count - 1u) * 95u / 100u];
    }
    uint64_t frames = g_perf.frames ? g_perf.frames : 1u;
    // bg_us is the background repaint of the dirty rects, render_us everything
    // drawn on top of it (windows, launcher, overlays, cursor); app_us and
    // cache_us are the parts of it spent in the apps' bodies and in the
    // window caches.
    printf("[GFXPERF] frames=%llu frame_p50_us=%llu frame_p95_us=%llu "
           "work_p50_us=%llu work_p95_us=%llu "
           "render_us=%llu bg_us=%llu copy_us=%llu flush_us=%llu "
           "rects=%llu pixels=%llu bytes=%llu full=%llu app_us=%llu cache_us=%llu "
           "slow_us=%llu slow_render_us=%llu slow_bg_us=%llu slow_app_us=%llu slow_cache_us=%llu "
           "slow_copy_us=%llu slow_flush_us=%llu slow_pixels=%llu "
           "slow_windows_us=%llu slow_dock_us=%llu slow_desk_us=%llu\n",
           (unsigned long long)g_perf.frames,
           (unsigned long long)(p50 / 1000u),
           (unsigned long long)(p95 / 1000u),
           (unsigned long long)(work_p50 / 1000u),
           (unsigned long long)(work_p95 / 1000u),
           (unsigned long long)(g_perf.render_ns / frames / 1000u),
           (unsigned long long)(g_perf.composite_ns / frames / 1000u),
           (unsigned long long)(g_perf.copy_ns / frames / 1000u),
           (unsigned long long)(g_perf.flush_ns / frames / 1000u),
           (unsigned long long)g_perf.rects,
           (unsigned long long)g_perf.pixels,
           (unsigned long long)g_perf.bytes,
           (unsigned long long)g_perf.full_frames,
           (unsigned long long)(g_perf.app_ns / frames / 1000u),
           (unsigned long long)(g_perf.cache_ns / frames / 1000u),
           (unsigned long long)(g_perf.slow_work / 1000u),
           (unsigned long long)(g_perf.slow_part[0] / 1000u),
           (unsigned long long)(g_perf.slow_part[1] / 1000u),
           (unsigned long long)(g_perf.slow_part[2] / 1000u),
           (unsigned long long)(g_perf.slow_part[3] / 1000u),
           (unsigned long long)(g_perf.slow_part[4] / 1000u),
           (unsigned long long)(g_perf.slow_part[5] / 1000u),
           (unsigned long long)g_perf.slow_pixels,
           (unsigned long long)(g_perf.slow_part[6] / 1000u),
           (unsigned long long)(g_perf.slow_part[7] / 1000u),
           (unsigned long long)(g_perf.slow_part[8] / 1000u));
    sl_perf_reset_window(now_ns);
}
