#include "operational_lab.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    const std::string path = "operational_lab_test.json";
    const auto result = OperationalLab::run(2, path);

    assert(result.runs == 2);
    assert(result.events_generated > 0);
    assert(result.detected_events > 0);
    assert(result.missed_events == 0);
    assert(result.false_positive_events == 2);
    assert(result.recovery_attempts == result.recovered_events);
    assert(result.detection_rate_percent == 100.0);
    assert(result.false_positive_rate_percent == 5.0);
    assert(result.recovery_rate_percent == 100.0);
    assert(result.report_written);
    assert(!result.scenarios.empty());

    std::remove(path.c_str());
    return 0;
}
