#include "latency_measurer.hpp"

#include "settings.hpp"
#include "timer.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>
#include <optional>

#include <sys/mman.h>

namespace {

std::optional<PmuSnapshot> stop_pmu(
    PmuCounters* pmu,
    bool started) {
    if (!pmu || !started) return std::nullopt;
    (void)pmu->stop();
    return pmu->snapshot();
}

} // namespace

LatencyMeasurer::LatencyMeasurer(std::size_t requested_buffer_size,
                                 std::size_t cache_line_size)
    : cache_line_size_(cache_line_size == 0 ? Config::CACHE_LINE_SIZE
                                            : cache_line_size) {
    const std::size_t line = 64;
    if (cache_line_size_ != line) {
        cache_line_size_ = line;
    }

    const std::size_t minimum = Config::MIN_BUFFER_SIZE;
    buffer_size_ = requested_buffer_size == 0
                       ? minimum
                       : std::max(requested_buffer_size, minimum);

    buffer_size_ = (buffer_size_ / line) * line;
    if (buffer_size_ < minimum) buffer_size_ = minimum;

    void* memory = mmap(nullptr, buffer_size_,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        throw std::runtime_error("mmap failed while allocating latency buffer");
    }

    buffer_ = static_cast<Node*>(memory);
    node_count_ = buffer_size_ / line;

    for (std::size_t i = 0; i < node_count_; ++i) {
        buffer_[i].next = reinterpret_cast<std::uintptr_t>(&buffer_[i]);
    }

    timer_overhead_cycles_ = Timer::measure_overhead_cycles(2000);
    build_self_loop();

    for (std::size_t i = 0; i < node_count_; i += 512) {
        buffer_[i].next = reinterpret_cast<std::uintptr_t>(&buffer_[i]);
    }
}

LatencyMeasurer::~LatencyMeasurer() {
    if (buffer_) {
        munmap(buffer_, buffer_size_);
    }
}

std::uint64_t LatencyMeasurer::subtract_overhead(
    std::uint64_t elapsed,
    std::uint64_t overhead) noexcept {
    return elapsed > overhead ? elapsed - overhead : 0;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
double LatencyMeasurer::timed_pointer_chase(
    Node* start,
    std::size_t steps) const {
    volatile Node* current = start;

    Timer::compiler_barrier();
    const std::uint64_t t0 = Timer::read_tsc_start();
    for (std::size_t i = 0; i < steps; ++i) {
        current = reinterpret_cast<volatile Node*>(current->next);
    }
    Timer::compiler_barrier();
    const std::uint64_t t1 = Timer::read_tsc_end();
    Timer::compiler_barrier();

    sink_ ^= static_cast<std::uint8_t>(
        reinterpret_cast<std::uintptr_t>(current) & 0xFFU);

    const std::uint64_t net =
        subtract_overhead(t1 - t0, timer_overhead_cycles_);
    return static_cast<double>(net) / static_cast<double>(steps);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
double LatencyMeasurer::timed_store_batch(std::size_t steps) const {
    volatile Node* nodes = buffer_;

    Timer::compiler_barrier();
    const std::uint64_t t0 = Timer::read_tsc_start();

    for (std::size_t i = 0; i < steps; ++i) {
        const std::size_t slot = i & 15U;
        nodes[slot].next = reinterpret_cast<std::uintptr_t>(&nodes[slot]);
    }

    Timer::compiler_barrier();
    const std::uint64_t t1 = Timer::read_tsc_end();
    Timer::compiler_barrier();

    const std::uint64_t net =
        subtract_overhead(t1 - t0, timer_overhead_cycles_);
    return static_cast<double>(net) / static_cast<double>(steps);
}

void LatencyMeasurer::build_self_loop() {
    buffer_[0].next = reinterpret_cast<std::uintptr_t>(&buffer_[0]);
}

void LatencyMeasurer::build_random_ring(std::uint64_t seed) {
    random_order_.resize(node_count_);
    for (std::size_t i = 0; i < node_count_; ++i) {
        random_order_[i] = i;
    }

    std::mt19937_64 rng(seed);
    std::shuffle(random_order_.begin(), random_order_.end(), rng);

    for (std::size_t i = 0; i < node_count_; ++i) {
        const std::size_t current = random_order_[i];
        const std::size_t next = random_order_[(i + 1) % node_count_];
        buffer_[current].next =
            reinterpret_cast<std::uintptr_t>(&buffer_[next]);
    }
}

void LatencyMeasurer::build_stride_ring(std::size_t stride_bytes) {
    const std::size_t stride_lines =
        std::max<std::size_t>(1, stride_bytes / cache_line_size_);

    for (std::size_t i = 0; i < node_count_; ++i) {
        const std::size_t next =
            (i + stride_lines) % node_count_;
        buffer_[i].next =
            reinterpret_cast<std::uintptr_t>(&buffer_[next]);
    }
}

void LatencyMeasurer::flush_random_path(std::size_t start_position,
                                        std::size_t steps) const {
    for (std::size_t i = 0; i < steps; ++i) {
        const std::size_t position =
            (start_position + i) % random_order_.size();
        Timer::clflush(&buffer_[random_order_[position]]);
    }
    Timer::clflush_fence();
}

MeasurementResult LatencyMeasurer::measure_hit(
    int iterations,
    int warmup,
    int rounds,
    PmuCounters* pmu) {
    if (iterations <= 0 || warmup < 0 || rounds <= 0) {
        throw std::invalid_argument("Invalid hit benchmark configuration");
    }

    build_self_loop();
    Node* start = &buffer_[0];

    for (int i = 0; i < warmup; ++i) {
        (void)timed_pointer_chase(start, kLatencyBatch);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations) *
                    static_cast<std::size_t>(rounds));

    const bool pmu_started =
        pmu != nullptr && pmu->start();

    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < iterations; ++i) {
            samples.push_back(
                timed_pointer_chase(start, kLatencyBatch));
        }
    }

    return MeasurementResult{
        "Cache Hit (L1 pointer-chasing latency)",
        Statistics::summarize(samples),
        timer_overhead_cycles_,
        stop_pmu(pmu, pmu_started)
    };
}

MeasurementResult LatencyMeasurer::measure_forced_miss(
    int iterations,
    int warmup,
    int rounds,
    PmuCounters* pmu) {
    if (iterations <= 0 || warmup < 0 || rounds <= 0 || node_count_ < 64) {
        throw std::invalid_argument("Invalid forced-miss benchmark configuration");
    }

    build_random_ring(0xC0FFEE123456789ULL);

    for (int i = 0; i < warmup; ++i) {
        const std::size_t start_position =
            static_cast<std::size_t>(i) % random_order_.size();
        Node* start = &buffer_[random_order_[start_position]];
        flush_random_path(start_position, kLatencyBatch);
        (void)timed_pointer_chase(start, kLatencyBatch);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations) *
                    static_cast<std::size_t>(rounds));

    const bool pmu_started =
        pmu != nullptr && pmu->start();

    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < iterations; ++i) {
            const std::size_t start_position =
                (static_cast<std::size_t>(round) *
                 static_cast<std::size_t>(iterations) +
                 static_cast<std::size_t>(i)) %
                random_order_.size();

            Node* start = &buffer_[random_order_[start_position]];
            flush_random_path(start_position, kLatencyBatch);
            samples.push_back(
                timed_pointer_chase(start, kLatencyBatch));
        }
    }

    return MeasurementResult{
        "Forced Cache Miss (random pointer chase + CLFLUSH)",
        Statistics::summarize(samples),
        timer_overhead_cycles_,
        stop_pmu(pmu, pmu_started)
    };
}

MeasurementResult LatencyMeasurer::measure_store(
    int iterations,
    int warmup,
    int rounds,
    PmuCounters* pmu) {
    if (iterations <= 0 || warmup < 0 || rounds <= 0) {
        throw std::invalid_argument("Invalid store benchmark configuration");
    }

    for (int i = 0; i < warmup; ++i) {
        (void)timed_store_batch(kStoreBatch);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations) *
                    static_cast<std::size_t>(rounds));

    const bool pmu_started =
        pmu != nullptr && pmu->start();

    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < iterations; ++i) {
            samples.push_back(timed_store_batch(kStoreBatch));
        }
    }

    return MeasurementResult{
        "Store (hot lines; issue throughput per store)",
        Statistics::summarize(samples),
        timer_overhead_cycles_,
        stop_pmu(pmu, pmu_started)
    };
}

std::vector<MeasurementResult> LatencyMeasurer::measure_strides(
    const std::vector<std::size_t>& strides,
    int iterations,
    int warmup,
    int rounds,
    PmuCounters* pmu) {
    if (iterations <= 0 || warmup < 0 || rounds <= 0) {
        throw std::invalid_argument("Invalid stride benchmark configuration");
    }

    std::vector<MeasurementResult> results;
    results.reserve(strides.size());

    for (const std::size_t raw_stride : strides) {
        const std::size_t stride =
            std::max(cache_line_size_,
                     (raw_stride / cache_line_size_) * cache_line_size_);

        build_stride_ring(stride);
        Node* start = &buffer_[0];

        for (int i = 0; i < warmup; ++i) {
            (void)timed_pointer_chase(start, kLatencyBatch);
            start = reinterpret_cast<Node*>(start->next);
        }

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(iterations) *
                        static_cast<std::size_t>(rounds));

        const bool pmu_started =
            pmu != nullptr && pmu->start();

        for (int round = 0; round < rounds; ++round) {
            Node* current = &buffer_[
                static_cast<std::size_t>(round) % node_count_
            ];

            for (int i = 0; i < iterations; ++i) {
                samples.push_back(
                    timed_pointer_chase(current, kLatencyBatch));
                current = reinterpret_cast<Node*>(current->next);
            }
        }

        results.push_back(MeasurementResult{
            "Stride " + std::to_string(stride) +
                " B (dependent pointer chase)",
            Statistics::summarize(samples),
            timer_overhead_cycles_,
            stop_pmu(pmu, pmu_started)
        });
    }

    return results;
}
