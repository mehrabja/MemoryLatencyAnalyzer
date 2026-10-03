#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "statistics.hpp"

struct MeasurementResult {
    std::string label;
    Statistics::Summary stats;
    std::size_t timer_overhead_cycles = 0;
};

class LatencyMeasurer {
public:
    explicit LatencyMeasurer(std::size_t requested_buffer_size = 0,
                             std::size_t cache_line_size = 64);
    ~LatencyMeasurer();

    MeasurementResult measure_hit(int iterations, int warmup, int rounds);
    MeasurementResult measure_forced_miss(int iterations, int warmup, int rounds);
    MeasurementResult measure_store(int iterations, int warmup, int rounds);

    std::vector<MeasurementResult> measure_strides(
        const std::vector<std::size_t>& strides,
        int iterations,
        int warmup,
        int rounds);

    std::size_t buffer_size() const noexcept { return buffer_size_; }

private:
    struct alignas(64) Node {
        std::uintptr_t next = 0;
        std::uint8_t padding[56]{};
    };

    static_assert(sizeof(Node) == 64, "Latency nodes must occupy one cache line");

    Node* buffer_ = nullptr;
    std::size_t buffer_size_ = 0;
    std::size_t node_count_ = 0;
    std::size_t cache_line_size_ = 64;
    mutable std::uint8_t sink_ = 0;
    std::uint64_t timer_overhead_cycles_ = 0;
    std::vector<std::size_t> random_order_;

    static constexpr std::size_t kLatencyBatch = 128;
    static constexpr std::size_t kStoreBatch = 256;

    double timed_pointer_chase(Node* start, std::size_t steps) const;
    double timed_store_batch(std::size_t steps) const;

    void build_self_loop();
    void build_random_ring(std::uint64_t seed);
    void build_stride_ring(std::size_t stride_bytes);

    void flush_random_path(std::size_t start_position,
                           std::size_t steps) const;

    static std::uint64_t subtract_overhead(
        std::uint64_t elapsed,
        std::uint64_t overhead) noexcept;
};
