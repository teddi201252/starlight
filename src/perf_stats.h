#pragma once

#include <stddef.h>
#include <stdint.h>

void sl_perf_init(uint8_t enabled);
void sl_perf_frame_begin(uint64_t now_ns);
void sl_perf_add_render(uint64_t elapsed_ns);
void sl_perf_add_composite(uint64_t elapsed_ns);
void sl_perf_record_present(uint64_t copy_ns, uint64_t flush_ns,
                            size_t rect_count, uint64_t pixels,
                            uint8_t bytespp, uint8_t full);
void sl_perf_frame_end(uint64_t now_ns);
