#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "statistics.hpp"

struct MeasurementResult {
    double mean_cycles;
    double stddev_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
    std::string label;
};

class LatencyMeasurer {
public:
    explicit LatencyMeasurer(size_t requested_buffer_size = 0);
    ~LatencyMeasurer();

    MeasurementResult measure_hit(int iterations, int warmup);
    MeasurementResult measure_miss(int iterations, int warmup, size_t stride = 4096);
    MeasurementResult measure_load(int iterations, int warmup);
    MeasurementResult measure_store(int iterations, int warmup);

    std::vector<MeasurementResult> measure_strides(
        const std::vector<size_t>& strides,
        int iterations,
        int warmup);

    size_t buffer_size() const { return buffer_size_; }

private:
    char* buffer_;
    size_t buffer_size_;
    volatile int sink_;

    uint64_t single_access(volatile char* addr);
    void flush_line(volatile char* addr);
};