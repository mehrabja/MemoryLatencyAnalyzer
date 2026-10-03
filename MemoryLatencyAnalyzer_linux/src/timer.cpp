#include "timer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <thread>
#include <vector>

#include <x86intrin.h>

namespace Timer {

void compiler_barrier() {
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

std::uint64_t read_tsc_start(unsigned* aux) {
    unsigned local_aux = 0;
    compiler_barrier();
    _mm_lfence();
    const std::uint64_t ticks = __rdtscp(&local_aux);
    _mm_lfence();
    compiler_barrier();
    if (aux) *aux = local_aux;
    return ticks;
}

std::uint64_t read_tsc_end(unsigned* aux) {
    unsigned local_aux = 0;
    compiler_barrier();
    _mm_lfence();
    const std::uint64_t ticks = __rdtscp(&local_aux);
    _mm_lfence();
    compiler_barrier();
    if (aux) *aux = local_aux;
    return ticks;
}

std::uint64_t monotonic_raw_ns() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        throw std::runtime_error("clock_gettime(CLOCK_MONOTONIC_RAW) failed");
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

double calibrate_tsc_hz(int rounds, int interval_ms) {
    if (rounds <= 0 || interval_ms <= 0) {
        throw std::invalid_argument("TSC calibration parameters must be positive");
    }

    std::vector<double> estimates;
    estimates.reserve(static_cast<std::size_t>(rounds));

    for (int i = 0; i < rounds; ++i) {
        unsigned aux_start = 0;
        const std::uint64_t ns_start = monotonic_raw_ns();
        const std::uint64_t tsc_start = read_tsc_start(&aux_start);

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));

        unsigned aux_end = 0;
        const std::uint64_t tsc_end = read_tsc_end(&aux_end);
        const std::uint64_t ns_end = monotonic_raw_ns();

        const std::uint64_t elapsed_ns = ns_end - ns_start;
        if (elapsed_ns == 0 || aux_start != aux_end) continue;

        estimates.push_back(
            static_cast<double>(tsc_end - tsc_start) /
            (static_cast<double>(elapsed_ns) * 1e-9)
        );
    }

    if (estimates.empty()) {
        throw std::runtime_error("Unable to calibrate TSC rate");
    }

    std::sort(estimates.begin(), estimates.end());
    return estimates[estimates.size() / 2];
}

std::uint64_t measure_overhead_cycles(int samples) {
    if (samples <= 0) return 0;

    std::vector<std::uint64_t> values;
    values.reserve(static_cast<std::size_t>(samples));

    for (int i = 0; i < samples; ++i) {
        compiler_barrier();
        const std::uint64_t t0 = read_tsc_start();
        compiler_barrier();
        const std::uint64_t t1 = read_tsc_end();
        compiler_barrier();
        values.push_back(t1 - t0);
    }

    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void clflush(const volatile void* address) {
    _mm_clflush(const_cast<const void*>(address));
}

void clflush_fence() {
    _mm_mfence();
}

} // namespace Timer
