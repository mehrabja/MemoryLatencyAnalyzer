#include "evaluation_lab.hpp"

#include "cpu_capability.hpp"
#include "cpu_info.hpp"
#include "timer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <utility>
#include <vector>

namespace {

double mean_of(const std::vector<double>& samples) {
    if (samples.empty()) return 0.0;

    long double sum = 0.0L;
    for (const double value : samples) {
        sum += value;
    }
    return static_cast<double>(
        sum / static_cast<long double>(samples.size()));
}

double stddev_of(
    const std::vector<double>& samples,
    double mean) {
    if (samples.size() < 2) return 0.0;

    long double squared = 0.0L;
    for (const double value : samples) {
        const long double diff =
            static_cast<long double>(value) - mean;
        squared += diff * diff;
    }

    return std::sqrt(static_cast<double>(
        squared /
        static_cast<long double>(samples.size() - 1)));
}

double median_of(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());

    const std::size_t middle = samples.size() / 2;
    if (samples.size() % 2 == 0) {
        return (samples[middle - 1] + samples[middle]) / 2.0;
    }
    return samples[middle];
}

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

std::vector<double> collect_trace(
    std::size_t count,
    bool random_class,
    bool leaky_fixture,
    std::mt19937& rng) {
    std::vector<double> trace;
    trace.reserve(count);

    std::uniform_int_distribution<int> byte_dist(0, 255);
    volatile std::uint64_t sink = 0;

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t value =
            random_class
                ? static_cast<std::uint8_t>(byte_dist(rng))
                : static_cast<std::uint8_t>(0xA5U);

        const int extra =
            leaky_fixture
                ? static_cast<int>(value & 0x0FU)
                : 0;
        const int rounds = 80 + extra * 8;

        const std::uint64_t start =
            Timer::monotonic_raw_ns();

        std::uint64_t x =
            static_cast<std::uint64_t>(value) + 0x9E3779B97F4A7C15ULL;

        for (int j = 0; j < rounds; ++j) {
            x ^= x >> 13U;
            x *= 0xBF58476D1CE4E5B9ULL;
            x ^= x >> 17U;
        }

        sink ^= x;
        const std::uint64_t end =
            Timer::monotonic_raw_ns();

        trace.push_back(
            static_cast<double>(end - start));
    }

    (void)sink;
    return trace;
}

double repeatability_cv(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double mean = mean_of(values);
    if (std::abs(mean) < 1e-12) return 0.0;
    return 100.0 * stddev_of(values, mean) / std::abs(mean);
}

std::vector<ControlEvaluation> control_matrix() {
    return {
        {
            "EDR",
            "simulated suspicious execution",
            "alert and retain telemetry",
            "synthetic event only; no real process execution",
            false
        },
        {
            "DLP",
            "simulated sensitive-data export",
            "alert or block the transfer",
            "synthetic event only; no data leaves the process",
            false
        },
        {
            "Microsegmentation",
            "simulated unauthorized network egress",
            "deny the flow",
            "synthetic policy event only; no socket is opened",
            false
        },
        {
            "Least privilege",
            "simulated protected-resource access path",
            "deny access",
            "synthetic authorization event only",
            false
        },
        {
            "Network monitoring",
            "simulated command-channel pattern",
            "generate observable network telemetry",
            "synthetic event only; no command channel exists",
            false
        },
        {
            "Persistence monitoring",
            "simulated persistence attempt",
            "alert and retain an audit event",
            "synthetic event only; no persistence mechanism is created",
            false
        },
        {
            "Stealth/evasion monitoring",
            "simulated defense-evasion indicator",
            "raise a reviewable signal",
            "synthetic event only; no evasion logic is executed",
            false
        },
        {
            "Data-flow monitoring",
            "simulated sensitive-data path",
            "track source-to-sink movement",
            "synthetic event only; data remains local",
            false
        },
    };
}

bool write_report(
    const std::string& path,
    const EvaluationLabResult& result,
    const TvlaResult& tvla_clean,
    const std::vector<double>& run_medians) {
    std::ofstream out(path);
    if (!out) return false;

    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"lab\": \"evaluation_validation_lab\",\n";
    out << "  \"scope\": \"local_synthetic_reference_fixture\",\n";
    out << "  \"runs\": " << result.runs << ",\n";
    out << "  \"trace_samples\": " << result.trace_samples << ",\n";
    out << "  \"averaging_window\": "
        << result.averaging_window << ",\n";
    out << "  \"simulated_capture_faults\": "
        << result.simulated_capture_faults << ",\n";
    out << "  \"recovered_captures\": "
        << result.recovered_captures << ",\n";
    out << "  \"aligned_samples\": "
        << result.aligned_samples << ",\n";
    out << "  \"alignment_loss_percent\": "
        << result.alignment_loss_percent << ",\n";
    out << "  \"fixed_input_count\": "
        << result.fixed_metrics.input_count << ",\n";
    out << "  \"fixed_kept_count\": "
        << result.fixed_metrics.kept_count << ",\n";
    out << "  \"fixed_rejected_count\": "
        << result.fixed_metrics.rejected_count << ",\n";
    out << "  \"random_input_count\": "
        << result.random_metrics.input_count << ",\n";
    out << "  \"random_kept_count\": "
        << result.random_metrics.kept_count << ",\n";
    out << "  \"random_rejected_count\": "
        << result.random_metrics.rejected_count << ",\n";
    out << "  \"snr_linear\": "
        << result.snr_linear << ",\n";
    out << "  \"snr_db\": "
        << result.snr_db << ",\n";
    out << "  \"classification_error_percent\": "
        << result.classification_error_percent << ",\n";
    out << "  \"repeatability_cv_percent\": "
        << result.repeatability_cv_percent << ",\n";
    out << "  \"tvla_raw_t\": "
        << result.tvla.t_statistic << ",\n";
    out << "  \"tvla_clean_t\": "
        << tvla_clean.t_statistic << ",\n";
    out << "  \"tvla_threshold\": "
        << result.tvla.threshold << ",\n";
    out << "  \"tvla_raw_threshold_exceeded\": "
        << (result.tvla.threshold_exceeded ? "true" : "false") << ",\n";
    out << "  \"tvla_clean_threshold_exceeded\": "
        << (tvla_clean.threshold_exceeded ? "true" : "false") << ",\n";
    out << "  \"repeatability_ok\": "
        << (result.repeatability_ok ? "true" : "false") << ",\n";
    out << "  \"leakage_detected\": "
        << (result.leakage_detected ? "true" : "false") << ",\n";
    out << "  \"acceptance_criteria\": {\n";
    out << "    \"repeatability_cv_percent_max\": 10.0,\n";
    out << "    \"classification_error_percent_max\": 5.0,\n";
    out << "    \"tvla_reference_threshold\": 4.5\n";
    out << "  },\n";

    out << "  \"platform\": {\n";
    out << "    \"vendor\": \"" << json_escape(result.platform.vendor) << "\",\n";
    out << "    \"brand\": \"" << json_escape(result.platform.brand) << "\",\n";
    out << "    \"os_release\": \"" << json_escape(result.platform.os_release) << "\",\n";
    out << "    \"machine\": \"" << json_escape(result.platform.machine) << "\",\n";
    out << "    \"physical_cores\": " << result.platform.physical_cores << ",\n";
    out << "    \"logical_cpus\": " << result.platform.logical_cpus << ",\n";
    out << "    \"packages\": " << result.platform.packages << ",\n";
    out << "    \"smt_active\": "
        << (result.platform.smt_active ? "true" : "false") << "\n";
    out << "  },\n";

    out << "  \"run_medians\": [";
    for (std::size_t i = 0; i < run_medians.size(); ++i) {
        if (i != 0) out << ", ";
        out << run_medians[i];
    }
    out << "],\n";

    out << "  \"control_evaluation\": [\n";
    for (std::size_t i = 0; i < result.controls.size(); ++i) {
        const auto& c = result.controls[i];
        out << "    {\n";
        out << "      \"control\": \"" << json_escape(c.control) << "\",\n";
        out << "      \"scenario\": \"" << json_escape(c.scenario) << "\",\n";
        out << "      \"expected_action\": \"" << json_escape(c.expected_action) << "\",\n";
        out << "      \"executed\": " << (c.executed ? "true" : "false") << ",\n";
        out << "      \"evidence\": \"" << json_escape(c.evidence) << "\"\n";
        out << "    }";
        if (i + 1 != result.controls.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    return static_cast<bool>(out);
}

} // namespace

namespace Evaluation {

std::vector<double> align_samples(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    const std::size_t count =
        std::min(first.size(), second.size());

    return std::vector<double>(
        first.begin(),
        first.begin() +
            static_cast<std::ptrdiff_t>(count));
}

std::vector<double> average_blocks(
    const std::vector<double>& samples,
    std::size_t window) {
    if (samples.empty() || window == 0) return {};

    const std::size_t count =
        samples.size() / window;

    std::vector<double> result;
    result.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        long double sum = 0.0L;
        for (std::size_t j = 0; j < window; ++j) {
            sum += samples[i * window + j];
        }
        result.push_back(
            static_cast<double>(
                sum / static_cast<long double>(window)));
    }

    return result;
}

std::vector<double> remove_mad_outliers(
    const std::vector<double>& samples,
    double threshold) {
    if (samples.size() < 5 || threshold <= 0.0) {
        return samples;
    }

    const double median = median_of(samples);

    std::vector<double> deviations;
    deviations.reserve(samples.size());
    for (const double value : samples) {
        deviations.push_back(std::abs(value - median));
    }

    const double mad = median_of(deviations);
    if (mad <= 1e-12) {
        return samples;
    }

    const double scale = 1.4826 * mad;

    std::vector<double> filtered;
    filtered.reserve(samples.size());

    for (const double value : samples) {
        if (std::abs(value - median) <= threshold * scale) {
            filtered.push_back(value);
        }
    }

    return filtered;
}

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    double outlier_threshold) {
    RobustMetrics result;
    result.input_count = samples.size();

    const auto filtered =
        remove_mad_outliers(
            samples,
            outlier_threshold);

    result.kept_count = filtered.size();
    result.rejected_count =
        result.input_count - result.kept_count;

    result.mean = mean_of(filtered);
    result.median = median_of(filtered);
    result.stddev = stddev_of(filtered, result.mean);
    return result;
}

double snr_linear(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    const auto a = robust_metrics(first);
    const auto b = robust_metrics(second);

    if (a.kept_count < 2 || b.kept_count < 2) {
        return 0.0;
    }

    const double signal =
        std::abs(a.mean - b.mean);
    const double noise =
        std::sqrt(
            (a.stddev * a.stddev +
             b.stddev * b.stddev) / 2.0);

    if (noise <= 1e-12) return 0.0;
    return signal / noise;
}

double classification_error_rate(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples) {
    const auto fixed = robust_metrics(fixed_samples);
    const auto random = robust_metrics(random_samples);

    if (fixed.kept_count == 0 ||
        random.kept_count == 0) {
        return 100.0;
    }

    const double threshold =
        (fixed.mean + random.mean) / 2.0;

    std::size_t errors = 0;

    for (const double value : fixed_samples) {
        if (value > threshold) ++errors;
    }

    for (const double value : random_samples) {
        if (value <= threshold) ++errors;
    }

    const std::size_t total =
        fixed_samples.size() + random_samples.size();

    return total == 0
        ? 100.0
        : 100.0 *
              static_cast<double>(errors) /
              static_cast<double>(total);
}

TvlaResult welch_tvla(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples,
    double threshold) {
    TvlaResult result;
    result.fixed_count = fixed_samples.size();
    result.random_count = random_samples.size();
    result.threshold = threshold;

    if (fixed_samples.size() < 2 ||
        random_samples.size() < 2) {
        return result;
    }

    result.fixed_mean = mean_of(fixed_samples);
    result.random_mean = mean_of(random_samples);
    result.fixed_stddev =
        stddev_of(fixed_samples, result.fixed_mean);
    result.random_stddev =
        stddev_of(random_samples, result.random_mean);

    const double denominator =
        std::sqrt(
            (result.fixed_stddev * result.fixed_stddev /
             static_cast<double>(result.fixed_count)) +
            (result.random_stddev * result.random_stddev /
             static_cast<double>(result.random_count)));

    if (denominator <= 1e-12) {
        return result;
    }

    result.t_statistic =
        (result.fixed_mean - result.random_mean) /
        denominator;
    result.abs_t = std::abs(result.t_statistic);
    result.threshold_exceeded =
        result.abs_t >= result.threshold;
    return result;
}

PlatformSignature platform_signature() {
    PlatformSignature result;
    const auto capability = CpuCapability::detect();
    const auto topology = CpuInfo::topology();

    result.vendor = capability.vendor;
    result.brand = capability.brand;
    result.physical_cores = topology.physical_cores;
    result.logical_cpus = topology.logical_cpus;
    result.packages = topology.packages;
    result.smt_active = topology.smt_active;

    utsname name{};
    if (uname(&name) == 0) {
        result.os_release = name.release;
        result.machine = name.machine;
    }

    return result;
}

} // namespace Evaluation

EvaluationLabResult EvaluationLab::run(
    int runs,
    int trace_samples,
    std::size_t averaging_window,
    const std::string& report_path) {
    EvaluationLabResult result;
    result.runs = runs;
    result.trace_samples = trace_samples;
    result.averaging_window =
        static_cast<int>(averaging_window);
    result.report_path = report_path;
    result.platform = Evaluation::platform_signature();
    result.controls = control_matrix();

    if (runs <= 0 ||
        trace_samples <= 0 ||
        averaging_window == 0 ||
        report_path.empty()) {
        return result;
    }

    std::mt19937 rng(0x5EED1234U);
    std::vector<double> run_medians;

    std::vector<double> fixed_all;
    std::vector<double> random_all;
    fixed_all.reserve(
        static_cast<std::size_t>(trace_samples) *
        static_cast<std::size_t>(runs));
    random_all.reserve(fixed_all.capacity());

    for (int run = 0; run < runs; ++run) {
        auto fixed =
            collect_trace(
                static_cast<std::size_t>(trace_samples),
                false,
                true,
                rng);

        auto random =
            collect_trace(
                static_cast<std::size_t>(trace_samples),
                true,
                true,
                rng);

        if (run == 0 && fixed.size() > 8U) {
            ++result.simulated_capture_faults;
            fixed.resize(
                fixed.size() - 8U);

            const auto recovery =
                collect_trace(
                    8U,
                    false,
                    true,
                    rng);
            fixed.insert(
                fixed.end(),
                recovery.begin(),
                recovery.end());
            if (fixed.size() ==
                static_cast<std::size_t>(trace_samples)) {
                ++result.recovered_captures;
            }
        }

        const auto fixed_averaged =
            Evaluation::average_blocks(
                fixed,
                averaging_window);
        const auto random_averaged =
            Evaluation::average_blocks(
                random,
                averaging_window);

        const auto aligned_fixed =
            Evaluation::align_samples(
                fixed_averaged,
                random_averaged);

        const std::size_t aligned_count =
            aligned_fixed.size();

        if (aligned_count == 0) continue;

        std::vector<double> aligned_random;
        aligned_random.reserve(aligned_count);
        aligned_random.assign(
            random_averaged.begin(),
            random_averaged.begin() +
                static_cast<std::ptrdiff_t>(aligned_count));

        fixed_all.insert(
            fixed_all.end(),
            aligned_fixed.begin(),
            aligned_fixed.end());
        random_all.insert(
            random_all.end(),
            aligned_random.begin(),
            aligned_random.end());

        run_medians.push_back(
            median_of(aligned_fixed));
    }

    result.aligned_samples = fixed_all.size();

    const std::size_t requested_pairs =
        static_cast<std::size_t>(std::max(0, runs)) *
        static_cast<std::size_t>(trace_samples) /
        averaging_window;

    if (requested_pairs > 0) {
        result.alignment_loss_percent =
            100.0 *
            (1.0 -
             static_cast<double>(result.aligned_samples) /
             static_cast<double>(requested_pairs));
    }

    result.fixed_metrics =
        Evaluation::robust_metrics(fixed_all);
    result.random_metrics =
        Evaluation::robust_metrics(random_all);

    result.snr_linear =
        Evaluation::snr_linear(
            fixed_all,
            random_all);

    result.snr_db =
        result.snr_linear > 0.0
            ? 20.0 * std::log10(result.snr_linear)
            : 0.0;

    result.classification_error_percent =
        Evaluation::classification_error_rate(
            fixed_all,
            random_all);

    result.tvla =
        Evaluation::welch_tvla(
            fixed_all,
            random_all);

    const auto fixed_clean =
        Evaluation::remove_mad_outliers(fixed_all);
    const auto random_clean =
        Evaluation::remove_mad_outliers(random_all);

    const auto averaged_fixed_clean =
        Evaluation::average_blocks(
            fixed_clean,
            1);
    const auto averaged_random_clean =
        Evaluation::average_blocks(
            random_clean,
            1);

    const auto tvla_clean =
        Evaluation::welch_tvla(
            averaged_fixed_clean,
            averaged_random_clean);

    result.repeatability_cv_percent =
        repeatability_cv(run_medians);

    result.repeatability_ok =
        result.repeatability_cv_percent <= 10.0 &&
        result.classification_error_percent <= 5.0;
    result.leakage_detected = result.tvla.threshold_exceeded ||
                              tvla_clean.threshold_exceeded;

    (void)tvla_clean;

    if (!write_report(
            report_path,
            result,
            tvla_clean,
            run_medians)) {
        result.report_written = false;
        return result;
    }

    result.report_written = true;
    return result;
}
