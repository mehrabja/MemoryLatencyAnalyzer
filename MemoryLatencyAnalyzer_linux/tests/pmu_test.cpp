#include "pmu_counters.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main() {
    std::string error;
    auto counters = PmuCounters::create(error);

    if (!counters) {
        std::cerr << "PMU unavailable: " << error << '\n';
        return 77;
    }

    if (!counters->start()) {
        std::cerr << "PMU start failed: "
                  << counters->last_error() << '\n';
        return 77;
    }

    volatile std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < 1000000U; ++i) {
        sink += i;
    }

    (void)sink;
    if (!counters->stop()) {
        std::cerr << "PMU stop failed: "
                  << counters->last_error() << '\n';
        return 77;
    }

    const auto snapshot = counters->snapshot();
    if (!snapshot.valid || snapshot.counters.empty()) {
        std::cerr << "PMU read failed: "
                  << snapshot.error << '\n';
        return 77;
    }

    return 0;
}
