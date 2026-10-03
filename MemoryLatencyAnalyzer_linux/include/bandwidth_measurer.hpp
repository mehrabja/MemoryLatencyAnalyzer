#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "pmu_counters.hpp"

struct BandwidthResult {
    std::string allocator;
    std::size_t size_bytes = 0;
    double read_gb_s = 0.0;
    double write_gb_s = 0.0;
    double copy_gb_s = 0.0;
    std::optional<PmuSnapshot> pmu_read;
    std::optional<PmuSnapshot> pmu_write;
    std::optional<PmuSnapshot> pmu_copy;
};

class BandwidthMeasurer {
public:
    static BandwidthResult measure(std::size_t size_bytes,
                                   const std::string& method,
                                   int repeats = 5,
                                   PmuCounters* pmu = nullptr);
};
