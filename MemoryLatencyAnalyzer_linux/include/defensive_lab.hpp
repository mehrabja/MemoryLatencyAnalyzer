#pragma once

#include <string>

struct DefensiveLabResult {
    int runs = 0;
    int tries_per_byte = 0;
    double byte_accuracy_percent = 0.0;
    double exact_recovery_rate_percent = 0.0;
    int simulated_alerts = 0;
    int simulated_confirmed_alerts = 0;
    int simulated_false_positives = 0;
    bool report_written = false;
    std::string report_path;
};

class DefensiveLab {
public:
    // End-to-end defensive simulation using only the built-in Spectre fixture.
    // No network, persistence, process injection, or external target access.
    static DefensiveLabResult run(
        int runs = 5,
        int tries_per_byte = 999,
        const std::string& report_path =
            "phase5_defensive_report.json");
};
