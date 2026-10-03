#include "statistics.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    const std::vector<std::uint64_t> samples{1, 2, 3, 4, 5};
    const auto summary = Statistics::summarize(samples);

    assert(summary.count == 5);
    assert(summary.mean == 3.0);
    assert(summary.median == 3.0);
    assert(summary.min == 1.0);
    assert(summary.max == 5.0);
    assert(summary.p95 > 4.0);
    assert(summary.p99 > summary.p95);

    const std::vector<double> fractional{
        1.25, 2.50, 3.75
    };
    const auto fractional_summary =
        Statistics::summarize(fractional);

    assert(fractional_summary.mean == 2.5);
    assert(fractional_summary.median == 2.5);
    return 0;
}
