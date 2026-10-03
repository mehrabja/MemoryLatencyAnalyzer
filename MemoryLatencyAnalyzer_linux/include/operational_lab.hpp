#pragma once

#include <string>
#include <vector>

struct OperationalScenarioResult {
    std::string name;
    std::string stage;
    std::string expected_control;
    std::string telemetry_source;
    std::string expected_action;
    bool detected = false;
    bool recovered = false;
};

struct OperationalLabResult {
    int runs = 0;
    int events_generated = 0;
    int detected_events = 0;
    int missed_events = 0;
    int false_positive_events = 0;
    int recovery_attempts = 0;
    int recovered_events = 0;

    double detection_rate_percent = 0.0;
    double false_positive_rate_percent = 0.0;
    double recovery_rate_percent = 0.0;

    std::string reference_boundary;
    std::string report_path;
    std::vector<OperationalScenarioResult> scenarios;
    bool report_written = false;
};

class OperationalLab {
public:
    // Safe end-to-end simulation. It does not create persistence, sockets,
    // subprocesses, privilege changes, external-memory access, or live C2.
    static OperationalLabResult run(
        int runs = 3,
        const std::string& report_path =
            "operational_lab_report.json");
};
