#include "latency_measurer.hpp"
#include "timer.hpp"
#include "cpu_info.hpp"
#include "settings.hpp"
#include <sys/mman.h>
#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <cstring>

LatencyMeasurer::LatencyMeasurer(size_t requested_buffer_size) : sink_(0) {
    size_t l3 = CpuInfo::get_l3_size();
    buffer_size_ = requested_buffer_size > 0
                   ? requested_buffer_size
                   : std::max(Config::MIN_BUFFER_SIZE, static_cast<size_t>(l3 * 2.5));

    buffer_size_ = (buffer_size_ + 4095) & ~4095ULL;

    // معادل mmap(MAP_PRIVATE | MAP_ANONYMOUS) برای VirtualAlloc(MEM_COMMIT | MEM_RESERVE)
    void* p = mmap(nullptr, buffer_size_, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) {
        throw std::runtime_error(std::string("mmap failed: ") + std::strerror(errno));
    }
    buffer_ = static_cast<char*>(p);

    for (size_t i = 0; i < buffer_size_; ++i) {
        buffer_[i] = static_cast<char>(i & 0xFF);
    }
}

LatencyMeasurer::~LatencyMeasurer() {
    if (buffer_) {
        munmap(buffer_, buffer_size_);
    }
}

void LatencyMeasurer::flush_line(volatile char* addr) {
    Timer::clflush(addr);
}

uint64_t LatencyMeasurer::single_access(volatile char* addr) {
    Timer::serialize();
    uint64_t start = Timer::rdtsc();
    sink_ = *addr;
    Timer::serialize();
    return Timer::rdtsc() - start;
}

MeasurementResult LatencyMeasurer::measure_hit(int iterations, int warmup) {
    for (int i = 0; i < warmup; ++i) {
        sink_ = buffer_[0];
    }

    std::vector<uint64_t> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        times.push_back(single_access(&buffer_[0]));
    }

    double m = Statistics::mean(times);
    return {
        m,
        Statistics::stddev(times, m),
        Statistics::min_value(times),
        Statistics::max_value(times),
        "Cache Hit"
    };
}

MeasurementResult LatencyMeasurer::measure_miss(int iterations, int warmup, size_t stride) {
    for (size_t i = 0; i < buffer_size_; i += 4096) {
        flush_line(&buffer_[i]);
    }

    for (int i = 0; i < warmup; ++i) {
        size_t off = (static_cast<size_t>(i) * stride) % (buffer_size_ - 64);
        flush_line(&buffer_[off]);
        sink_ = buffer_[off];
    }

    for (size_t i = 0; i < buffer_size_; i += 4096) {
        flush_line(&buffer_[i]);
    }

    std::vector<uint64_t> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        size_t off = (static_cast<size_t>(i) * stride) % (buffer_size_ - 64);
        flush_line(&buffer_[off]);
        times.push_back(single_access(&buffer_[off]));
    }

    double m = Statistics::mean(times);
    return {
        m,
        Statistics::stddev(times, m),
        Statistics::min_value(times),
        Statistics::max_value(times),
        "Cache Miss (stride " + std::to_string(stride) + ")"
    };
}

MeasurementResult LatencyMeasurer::measure_load(int iterations, int warmup) {
    return measure_hit(iterations, warmup);
}

MeasurementResult LatencyMeasurer::measure_store(int iterations, int warmup) {
    for (int i = 0; i < warmup; ++i) {
        buffer_[0] = static_cast<char>(i);
    }

    std::vector<uint64_t> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        Timer::serialize();
        uint64_t start = Timer::rdtsc();
        buffer_[0] = static_cast<char>(i & 0xFF);
        Timer::serialize();
        times.push_back(Timer::rdtsc() - start);
    }

    double m = Statistics::mean(times);
    return {
        m,
        Statistics::stddev(times, m),
        Statistics::min_value(times),
        Statistics::max_value(times),
        "Store Latency"
    };
}

std::vector<MeasurementResult> LatencyMeasurer::measure_strides(
    const std::vector<size_t>& strides,
    int iterations,
    int warmup)
{
    std::vector<MeasurementResult> results;
    for (size_t s : strides) {
        results.push_back(measure_miss(iterations, warmup, s));
    }
    return results;
}