#include "defensive_lab.hpp"

#include "spectre_v1.hpp"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
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

struct LabEvent {
    const char* name;
    const char* action;
    const char* disposition;
    bool alert;
    bool confirmed;
};

std::vector<LabEvent> make_events() {
    return {
        {"spectre_reliability_fixture", "observe", "allowed_local_fixture", true, true},
        {"simulated_local_collection", "simulate", "no_secret_export", true, true},
        {"simulated_export", "simulate", "metrics_only_local_file", true, true},
        {"network_egress", "simulate", "blocked_not_performed", false, false},
        {"persistence", "simulate", "blocked_not_performed", false, false},
    };
}

bool write_report(
    const std::string& path,
    const SpectreLabResult& spectre,
    double byte_accuracy,
    double exact_rate,
    const std::vector<LabEvent>& events) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    int alerts = 0;
    int confirmed = 0;
    int false_positives = 0;

    for (const auto& event : events) {
        if (event.alert) {
            ++alerts;
            if (event.confirmed) {
                ++confirmed;
            } else {
                ++false_positives;
            }
        }
    }

    out << std::fixed << std::setprecision(2);
    out << "{\n";
    out << "  \"lab\": \"phase5_defensive_lab\",\n";
    out << "  \"scope\": \"self_contained_local_fixture\",\n";
    out << "  \"runs\": " << spectre.runs << ",\n";
    out << "  \"tries_per_byte\": " << spectre.tries_per_byte << ",\n";
    out << "  \"byte_accuracy_percent\": " << byte_accuracy << ",\n";
    out << "  \"exact_recovery_rate_percent\": " << exact_rate << ",\n";
    out << "  \"simulated_alerts\": " << alerts << ",\n";
    out << "  \"simulated_confirmed_alerts\": " << confirmed << ",\n";
    out << "  \"simulated_false_positives\": " << false_positives << ",\n";
    out << "  \"network_access_performed\": false,\n";
    out << "  \"persistence_performed\": false,\n";
    out << "  \"external_process_access\": false,\n";
    out << "  \"events\": [\n";

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        out << "    {\n";
        out << "      \"name\": \"" << json_escape(event.name) << "\",\n";
        out << "      \"action\": \"" << json_escape(event.action) << "\",\n";
        out << "      \"disposition\": \"" << json_escape(event.disposition) << "\",\n";
        out << "      \"alert\": " << (event.alert ? "true" : "false") << ",\n";
        out << "      \"confirmed\": " << (event.confirmed ? "true" : "false") << "\n";
        out << "    }";
        if (i + 1 != events.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace

DefensiveLabResult DefensiveLab::run(
    int runs,
    int tries_per_byte,
    const std::string& report_path) {
    DefensiveLabResult result;
    result.runs = runs;
    result.tries_per_byte = tries_per_byte;
    result.report_path = report_path;

    if (runs <= 0 || tries_per_byte <= 0 || report_path.empty()) {
        return result;
    }

    const SpectreLabResult spectre =
        SpectreV1::run_reliability_lab(runs, tries_per_byte);

    result.byte_accuracy_percent =
        spectre.total_bytes == 0
            ? 0.0
            : 100.0 * static_cast<double>(spectre.correct_bytes) /
                  static_cast<double>(spectre.total_bytes);

    result.exact_recovery_rate_percent =
        spectre.runs == 0
            ? 0.0
            : 100.0 * static_cast<double>(spectre.exact_matches) /
                  static_cast<double>(spectre.runs);

    const auto events = make_events();

    for (const auto& event : events) {
        if (event.alert) {
            ++result.simulated_alerts;
            if (event.confirmed) {
                ++result.simulated_confirmed_alerts;
            } else {
                ++result.simulated_false_positives;
            }
        }
    }

    result.report_written =
        write_report(
            report_path,
            spectre,
            result.byte_accuracy_percent,
            result.exact_recovery_rate_percent,
            events);

    return result;
}
