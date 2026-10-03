#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Statistics {

struct Summary {
    std::size_t count = 0;
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double stddev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

Summary summarize(const std::vector<std::uint64_t>& samples);
Summary summarize(const std::vector<double>& samples);

} // namespace Statistics
