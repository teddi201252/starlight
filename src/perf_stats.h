#pragma once

#include <stddef.h>
#include <stdint.h>

void sl_perf_init(uint8_t enabled);
void sl_perf_frame_work_done(uint64_t now_ns);
void sl_perf_frame_begin(uint64_t now_ns);
void sl_perf_add_render(uint64_t elapsed_ns);
void sl_perf_add_composite(uint64_t elapsed_ns);
// Parts of render_us: the apps drawing their bodies, and copying a window's
// inside into its cache.
void sl_perf_add_app(uint64_t elapsed_ns);
void sl_perf_add_cache(uint64_t elapsed_ns);
// Parts of the frame for the slowest one's breakdown: the windows, the dock
// and the desktop layer's own redraws (clock, widgets).
void sl_perf_add_windows(uint64_t elapsed_ns);
void sl_perf_add_dock(uint64_t elapsed_ns);
void sl_perf_add_desk(uint64_t elapsed_ns);
void sl_perf_record_present(uint64_t copy_ns, uint64_t flush_ns,
                            size_t rect_count, uint64_t pixels,
                            uint8_t bytespp, uint8_t full);
void sl_perf_frame_end(uint64_t now_ns);
