#include "operational_lab.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    const std::string path = "operational_lab_test.json";
    const auto result = OperationalLab::run(2, path);

    assert(result.runs == 2);
    assert(result.events_generated >= 20);
    assert(result.detected_events > 0);
    assert(result.missed_events == 0);
    assert(result.false_positive_events == 2);
    assert(result.recovery_attempts == result.recovered_events);
    assert(result.detection_rate_percent == 100.0);
    assert(result.false_positive_rate_percent == 5.0);
    assert(result.recovery_rate_percent == 100.0);
    assert(result.report_written);
    assert(!result.scenarios.empty());

    bool saw_credential_access = false;
    bool saw_injection_simulation = false;
    bool saw_lateral_movement = false;
    bool saw_c2_simulation = false;

    for (const auto& scenario : result.scenarios) {
        if (scenario.name == "synthetic_credential_access") {
            saw_credential_access = true;
        }
        if (scenario.name == "synthetic_process_injection") {
            saw_injection_simulation = true;
        }
        if (scenario.name == "synthetic_lateral_movement") {
            saw_lateral_movement = true;
        }
        if (scenario.name == "synthetic_command_channel") {
            saw_c2_simulation = true;
        }

        assert(scenario.detected);
        assert(scenario.recovered);
        assert(!scenario.telemetry_source.empty());
        assert(!scenario.expected_action.empty());
    }

    assert(saw_credential_access);
    assert(saw_injection_simulation);
    assert(saw_lateral_movement);
    assert(saw_c2_simulation);

    std::remove(path.c_str());
    return 0;
}
