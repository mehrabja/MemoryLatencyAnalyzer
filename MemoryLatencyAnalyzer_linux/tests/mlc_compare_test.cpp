#include "mlc_compare.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>

int main() {
    const std::string idle =
        "Intel(R) Memory Latency Checker v3.13\n"
        "Each iteration took 78.25 ns.\n";

    const auto idle_latency =
        MlcComparison::parse_idle_latency_ns(idle);
    assert(idle_latency.has_value());
    assert(std::abs(*idle_latency - 78.25) < 1e-9);

    const std::string loaded =
        "Inject Latency Bandwidth\n"
        "Delay (ns) MB/sec\n"
        "00100 95.50 123456.0\n"
        "00800 110.25 110000.0\n"
        "04000 150.75 90000.0\n";

    const auto loaded_latency =
        MlcComparison::parse_loaded_latency_ns(
            loaded, 800U);
    assert(loaded_latency.has_value());
    assert(std::abs(*loaded_latency - 110.25) < 1e-9);

    const auto missing =
        MlcComparison::parse_loaded_latency_ns(
            loaded, 9999U);
    assert(!missing.has_value());

    return 0;
}
