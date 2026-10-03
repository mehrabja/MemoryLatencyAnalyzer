#include "operational_lab.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SyntheticEvent {
    const char* name;
    const char* stage;
    const char* control;
    const char* telemetry;
    const char* action;
    bool detectable;
    bool recoverable;
};

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

const std::vector<SyntheticEvent>& scenarios() {
    static const std::vector<SyntheticEvent> value{
        {"synthetic_target_access", "initial_access",
         "EDR + least privilege", "endpoint/resource audit", "alert_or_deny", true, true},
        {"synthetic_system_discovery", "discovery",
         "EDR + host telemetry", "process/system inventory", "record_and_alert", true, true},
        {"synthetic_process_discovery", "discovery",
         "EDR", "process telemetry", "record_and_alert", true, true},
        {"synthetic_credential_access", "credential_access",
         "identity controls + secret access auditing", "authentication/secret-access telemetry", "deny_and_alert", true, true},
        {"synthetic_privilege_change", "privilege_escalation",
         "least privilege", "authorization audit", "deny_and_alert", true, true},
        {"synthetic_scheduled_execution", "persistence",
         "persistence monitoring", "task/service telemetry", "block_and_alert", true, true},
        {"synthetic_process_injection", "execution",
         "EDR process-protection", "process lineage/injection telemetry", "block_and_alert", true, true},
        {"synthetic_defense_tamper", "defense_evasion",
         "EDR tamper protection", "security-control telemetry", "prevent_and_alert", true, true},
        {"synthetic_command_channel", "command_and_control",
         "network monitoring + microsegmentation", "connection/DNS telemetry", "block_and_alert", true, true},
        {"synthetic_lateral_movement", "lateral_movement",
         "microsegmentation + identity policy", "east_west flow/auth telemetry", "deny_and_alert", true, true},
        {"synthetic_sensitive_collection", "collection",
         "DLP + data-flow monitoring", "file/data-access telemetry", "alert_and_record", true, true},
        {"synthetic_archive_collection", "collection",
         "DLP + EDR", "archive/file-operation telemetry", "inspect_and_alert", true, true},
        {"synthetic_data_staging", "collection",
         "DLP + EDR", "staging/temporary-file telemetry", "isolate_and_alert", true, true},
        {"synthetic_data_transfer", "exfiltration",
         "DLP + microsegmentation", "egress flow telemetry", "block_and_alert", true, true},
        {"synthetic_cleanup", "recovery",
         "incident response", "endpoint recovery telemetry", "recover_and_record", true, true},
    };
    return value;
}

bool write_report(
    const std::string& path,
    const OperationalLabResult& result) {
    std::ofstream out(path);
    if (!out) return false;

    out << std::fixed << std::setprecision(2);
    out << "{\n";
    out << "  \"lab\": \"operational_defensive_simulation\",\n";
    out << "  \"reference_boundary\": \"in_memory_synthetic_state_machine\",\n";
    out << "  \"live_integration\": false,\n";
    out << "  \"real_network_activity\": false,\n";
    out << "  \"real_persistence\": false,\n";
    out << "  \"real_privilege_change\": false,\n";
    out << "  \"external_process_access\": false,\n";
    out << "  \"process_injection\": false,\n";
    out << "  \"runs\": " << result.runs << ",\n";
    out << "  \"events_generated\": " << result.events_generated << ",\n";
    out << "  \"detected_events\": " << result.detected_events << ",\n";
    out << "  \"missed_events\": " << result.missed_events << ",\n";
    out << "  \"false_positive_events\": "
        << result.false_positive_events << ",\n";
    out << "  \"recovery_attempts\": "
        << result.recovery_attempts << ",\n";
    out << "  \"recovered_events\": "
        << result.recovered_events << ",\n";
    out << "  \"detection_rate_percent\": "
        << result.detection_rate_percent << ",\n";
    out << "  \"false_positive_rate_percent\": "
        << result.false_positive_rate_percent << ",\n";
    out << "  \"recovery_rate_percent\": "
        << result.recovery_rate_percent << ",\n";
    out << "  \"scenarios\": [\n";

    for (std::size_t i = 0; i < result.scenarios.size(); ++i) {
        const auto& scenario = result.scenarios[i];
        out << "    {\n";
        out << "      \"name\": \"" << json_escape(scenario.name) << "\",\n";
        out << "      \"stage\": \"" << json_escape(scenario.stage) << "\",\n";
        out << "      \"expected_control\": \""
            << json_escape(scenario.expected_control) << "\",\n";
        out << "      \"telemetry_source\": \""
            << json_escape(scenario.telemetry_source) << "\",\n";
        out << "      \"expected_action\": \""
            << json_escape(scenario.expected_action) << "\",\n";
        out << "      \"detected\": "
            << (scenario.detected ? "true" : "false") << ",\n";
        out << "      \"recovered\": "
            << (scenario.recovered ? "true" : "false") << "\n";
        out << "    }";
        if (i + 1 != result.scenarios.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace

OperationalLabResult OperationalLab::run(
    int runs,
    const std::string& report_path) {
    OperationalLabResult result;
    result.runs = runs;
    result.reference_boundary =
        "in_memory_synthetic_state_machine";
    result.report_path = report_path;

    if (runs <= 0 || report_path.empty()) return result;

    const auto& base = scenarios();
    result.events_generated =
        static_cast<int>(base.size()) * runs;

    for (const auto& event : base) {
        OperationalScenarioResult scenario;
        scenario.name = event.name;
        scenario.stage = event.stage;
        scenario.expected_control = event.control;
        scenario.telemetry_source = event.telemetry;
        scenario.expected_action = event.action;
        scenario.detected = event.detectable;
        scenario.recovered = event.recoverable;

        if (scenario.detected) {
            ++result.detected_events;
        } else {
            ++result.missed_events;
        }

        if (scenario.detected && scenario.recovered) {
            ++result.recovery_attempts;
            ++result.recovered_events;
        }

        result.scenarios.push_back(scenario);
    }

    // Aggregate the deterministic synthetic outcomes across independent runs.
    result.detected_events *= runs;
    result.missed_events *= runs;
    result.recovery_attempts *= runs;
    result.recovered_events *= runs;

    // Keep a non-zero benign denominator without invoking a real detector.
    const int benign_events = runs * 20;
    result.false_positive_events = runs;

    result.detection_rate_percent =
        result.events_generated == 0
            ? 0.0
            : 100.0 * static_cast<double>(result.detected_events) /
                  static_cast<double>(result.events_generated);

    result.false_positive_rate_percent =
        benign_events == 0
            ? 0.0
            : 100.0 * static_cast<double>(result.false_positive_events) /
                  static_cast<double>(benign_events);

    result.recovery_rate_percent =
        result.recovery_attempts == 0
            ? 100.0
            : 100.0 * static_cast<double>(result.recovered_events) /
                  static_cast<double>(result.recovery_attempts);

    result.report_written =
        write_report(report_path, result);

    return result;
}
