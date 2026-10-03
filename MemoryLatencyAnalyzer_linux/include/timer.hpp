#pragma once

#include <cstdint>

namespace Timer {

void compiler_barrier();

std::uint64_t read_tsc_start(unsigned* aux = nullptr);
std::uint64_t read_tsc_end(unsigned* aux = nullptr);

std::uint64_t monotonic_raw_ns();

double calibrate_tsc_hz(int rounds, int interval_ms);

std::uint64_t measure_overhead_cycles(int samples);

void clflush(const volatile void* address);
void clflush_fence();

} // namespace Timer
