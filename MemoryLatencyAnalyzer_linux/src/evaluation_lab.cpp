#include "evaluation_lab.hpp"

#include "cpu_capability.hpp"
#include "cpu_info.hpp"
#include "timer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <utility>
#include <vector>

namespace {

constexpr double kAlpha = 0.05;
constexpr std::size_t kMaxBetaIterations = 200U;
constexpr long double kBetaEpsilon = 3.0e-14L;
constexpr long double kBetaFpmIn = 1.0e-300L;

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
    if (samples.size() < 2U) return 0.0;

    long double squared = 0.0L;
    for (const double value : samples) {
        const long double diff =
            static_cast<long double>(value) - mean;
        squared += diff * diff;
    }

    return std::sqrt(static_cast<double>(
        squared /
        static_cast<long double>(samples.size() - 1U)));
}

double median_of(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());

    const std::size_t middle = samples.size() / 2U;
    if (samples.size() % 2U == 0U) {
        return (samples[middle - 1U] + samples[middle]) / 2.0;
    }
    return samples[middle];
}

double quantile_of_sorted(
    const std::vector<double>& sorted,
    double probability) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1U) return sorted.front();

    const double p =
        std::clamp(probability, 0.0, 1.0);
    const double position =
        p * static_cast<double>(sorted.size() - 1U);
    const std::size_t lower =
        static_cast<std::size_t>(std::floor(position));
    const std::size_t upper =
        std::min(lower + 1U, sorted.size() - 1U);
    const double fraction =
        position - static_cast<double>(lower);

    return sorted[lower] +
           fraction * (sorted[upper] - sorted[lower]);
}

long double beta_continued_fraction(
    long double a,
    long double b,
    long double x) {
    const long double qab = a + b;
    const long double qap = a + 1.0L;
    const long double qam = a - 1.0L;

    long double c = 1.0L;
    long double d =
        1.0L - qab * x / qap;
    if (std::abs(d) < kBetaFpmIn) d = kBetaFpmIn;
    d = 1.0L / d;

    long double h = d;

    for (std::size_t m = 1U; m <= kMaxBetaIterations; ++m) {
        const long double m_ld =
            static_cast<long double>(m);
        const long double m2 =
            2.0L * m_ld;

        long double aa =
            m_ld * (b - m_ld) * x /
            ((qam + m2) * (a + m2));

        d = 1.0L + aa * d;
        if (std::abs(d) < kBetaFpmIn) d = kBetaFpmIn;
        c = 1.0L + aa / c;
        if (std::abs(c) < kBetaFpmIn) c = kBetaFpmIn;
        d = 1.0L / d;
        h *= d * c;

        aa =
            -(a + m_ld) * (qab + m_ld) * x /
            ((a + m2) * (qap + m2));

        d = 1.0L + aa * d;
        if (std::abs(d) < kBetaFpmIn) d = kBetaFpmIn;
        c = 1.0L + aa / c;
        if (std::abs(c) < kBetaFpmIn) c = kBetaFpmIn;
        d = 1.0L / d;

        const long double delta = d * c;
        h *= delta;

        if (std::abs(delta - 1.0L) < kBetaEpsilon) {
            break;
        }
    }

    return h;
}

double regularized_incomplete_beta(
    double a,
    double b,
    double x) {
    if (a <= 0.0 || b <= 0.0) return 0.0;
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;

    const long double a_ld =
        static_cast<long double>(a);
    const long double b_ld =
        static_cast<long double>(b);
    const long double x_ld =
        static_cast<long double>(x);

    const long double log_beta =
        std::lgamma(a_ld + b_ld) -
        std::lgammal(a_ld) -
        std::lgammal(b_ld);

    const long double front =
        std::exp(
            log_beta +
            a_ld * std::log(x_ld) +
            b_ld * std::log1p(-x_ld));

    long double value = 0.0L;
    if (x_ld < (a_ld + 1.0L) /
                   (a_ld + b_ld + 2.0L)) {
        value =
            front * beta_continued_fraction(
                         a_ld, b_ld, x_ld) /
            a_ld;
    } else {
        value =
            1.0L -
            front * beta_continued_fraction(
                         b_ld, a_ld, 1.0L - x_ld) /
            b_ld;
    }

    return std::clamp(
        static_cast<double>(value),
        0.0,
        1.0);
}

double student_t_two_sided_p(
    double abs_t,
    double degrees_of_freedom) {
    if (abs_t <= 0.0) return 1.0;
    if (!std::isfinite(abs_t) ||
        degrees_of_freedom <= 0.0) {
        return abs_t > 0.0 ? 0.0 : 1.0;
    }

    const double df = degrees_of_freedom;
    const double x =
        df / (df + abs_t * abs_t);

    return regularized_incomplete_beta(
        0.5 * df,
        0.5,
        x);
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

const char* outlier_method_name(OutlierMethod method) {
    return method == OutlierMethod::MAD ? "MAD" : "IQR";
}

const char* multiple_testing_method_name(
    MultipleTestingMethod method) {
    switch (method) {
    case MultipleTestingMethod::Bonferroni:
        return "Bonferroni";
    case MultipleTestingMethod::BenjaminiHochberg:
        return "Benjamini-Hochberg";
    case MultipleTestingMethod::None:
        return "none";
    }
    return "none";
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
            static_cast<std::uint64_t>(value) +
            0x9E3779B97F4A7C15ULL;

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

double repeatability_cv(
    const std::vector<double>& values) {
    if (values.size() < 2U) return 0.0;
    const double mean = mean_of(values);
    if (std::abs(mean) < 1e-12) return 0.0;
    return 100.0 * stddev_of(values, mean) /
           std::abs(mean);
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

RobustMetrics robust_metrics_internal(
    const std::vector<double>& samples,
    OutlierMethod method,
    double outlier_threshold,
    std::size_t bootstrap_resamples,
    std::uint64_t bootstrap_seed) {
    RobustMetrics result;
    result.input_count = samples.size();

    const auto filtered =
        Evaluation::remove_outliers(
            samples,
            method,
            outlier_threshold);

    result.kept_count = filtered.size();
    result.rejected_count =
        result.input_count - result.kept_count;
    result.rejected_percent =
        result.input_count == 0U
            ? 0.0
            : 100.0 * static_cast<double>(
                result.rejected_count) /
                static_cast<double>(result.input_count);

    result.mean = mean_of(filtered);
    result.median = median_of(filtered);
    result.stddev =
        stddev_of(filtered, result.mean);

    if (bootstrap_resamples > 0U && !filtered.empty()) {
        result.bootstrap =
            Evaluation::bootstrap_ci(
                filtered,
                bootstrap_resamples,
                bootstrap_seed);
    }
    return result;
}

bool write_report(
    const std::string& path,
    const EvaluationLabResult& result) {
    std::ofstream out(path);
    if (!out) return false;

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"lab\": \"evaluation_validation_lab\",\n";
    out << "  \"scope\": \"local_synthetic_reference_fixture\",\n";
    out << "  \"runs\": " << result.runs << ",\n";
    out << "  \"trace_samples\": " << result.trace_samples << ",\n";
    out << "  \"averaging_window\": "
        << result.averaging_window << ",\n";
    out << "  \"outlier_method\": \""
        << outlier_method_name(result.primary_outlier_method)
        << "\",\n";
    out << "  \"bootstrap_resamples\": "
        << result.fixed_metrics.bootstrap.resamples << ",\n";
    out << "  \"multiple_testing_method\": \""
        << multiple_testing_method_name(
               result.multiple_testing_method)
        << "\",\n";
    out << "  \"alpha\": " << kAlpha << ",\n";

    out << "  \"simulated_capture_faults\": "
        << result.simulated_capture_faults << ",\n";
    out << "  \"recovered_captures\": "
        << result.recovered_captures << ",\n";
    out << "  \"recovery_rate_percent\": "
        << result.recovery_rate_percent << ",\n";
    out << "  \"aligned_samples\": "
        << result.aligned_samples << ",\n";
    out << "  \"alignment_loss_percent\": "
        << result.alignment_loss_percent << ",\n";

    out << "  \"fixed_metrics\": {\n";
    out << "    \"input_count\": "
        << result.fixed_metrics.input_count << ",\n";
    out << "    \"kept_count\": "
        << result.fixed_metrics.kept_count << ",\n";
    out << "    \"rejected_count\": "
        << result.fixed_metrics.rejected_count << ",\n";
    out << "    \"rejected_percent\": "
        << result.fixed_metrics.rejected_percent << ",\n";
    out << "    \"mean\": " << result.fixed_metrics.mean << ",\n";
    out << "    \"mean_ci95\": { \"lower\": "
        << result.fixed_metrics.bootstrap.mean_ci.lower
        << ", \"upper\": "
        << result.fixed_metrics.bootstrap.mean_ci.upper
        << " },\n";
    out << "    \"median\": "
        << result.fixed_metrics.median << ",\n";
    out << "    \"median_ci95\": { \"lower\": "
        << result.fixed_metrics.bootstrap.median_ci.lower
        << ", \"upper\": "
        << result.fixed_metrics.bootstrap.median_ci.upper
        << " },\n";
    out << "    \"stddev\": "
        << result.fixed_metrics.stddev << "\n";
    out << "  },\n";

    out << "  \"random_metrics\": {\n";
    out << "    \"input_count\": "
        << result.random_metrics.input_count << ",\n";
    out << "    \"kept_count\": "
        << result.random_metrics.kept_count << ",\n";
    out << "    \"rejected_count\": "
        << result.random_metrics.rejected_count << ",\n";
    out << "    \"rejected_percent\": "
        << result.random_metrics.rejected_percent << ",\n";
    out << "    \"mean\": " << result.random_metrics.mean << ",\n";
    out << "    \"mean_ci95\": { \"lower\": "
        << result.random_metrics.bootstrap.mean_ci.lower
        << ", \"upper\": "
        << result.random_metrics.bootstrap.mean_ci.upper
        << " },\n";
    out << "    \"median\": "
        << result.random_metrics.median << ",\n";
    out << "    \"median_ci95\": { \"lower\": "
        << result.random_metrics.bootstrap.median_ci.lower
        << ", \"upper\": "
        << result.random_metrics.bootstrap.median_ci.upper
        << " },\n";
    out << "    \"stddev\": "
        << result.random_metrics.stddev << "\n";
    out << "  },\n";

    out << "  \"outlier_comparison\": {\n";
    out << "    \"fixed\": { \"mad_rejected_count\": "
        << result.fixed_outliers.mad_rejected_count
        << ", \"mad_rejected_percent\": "
        << result.fixed_outliers.mad_rejected_percent
        << ", \"iqr_rejected_count\": "
        << result.fixed_outliers.iqr_rejected_count
        << ", \"iqr_rejected_percent\": "
        << result.fixed_outliers.iqr_rejected_percent
        << " },\n";
    out << "    \"random\": { \"mad_rejected_count\": "
        << result.random_outliers.mad_rejected_count
        << ", \"mad_rejected_percent\": "
        << result.random_outliers.mad_rejected_percent
        << ", \"iqr_rejected_count\": "
        << result.random_outliers.iqr_rejected_count
        << ", \"iqr_rejected_percent\": "
        << result.random_outliers.iqr_rejected_percent
        << " }\n";
    out << "  },\n";

    out << "  \"snr_linear\": "
        << result.snr_linear << ",\n";
    out << "  \"snr_db\": "
        << result.snr_db << ",\n";
    out << "  \"classification_error_percent\": "
        << result.classification_error_percent << ",\n";
    out << "  \"repeatability_cv_percent\": "
        << result.repeatability_cv_percent << ",\n";
    out << "  \"repeatability_icc_a1\": "
        << result.repeatability_icc << ",\n";

    out << "  \"tvla\": {\n";
    out << "    \"fixed_count\": "
        << result.tvla.fixed_count << ",\n";
    out << "    \"random_count\": "
        << result.tvla.random_count << ",\n";
    out << "    \"t_statistic\": "
        << result.tvla.t_statistic << ",\n";
    out << "    \"abs_t\": "
        << result.tvla.abs_t << ",\n";
    out << "    \"welch_satterthwaite_df\": "
        << result.tvla.degrees_of_freedom << ",\n";
    out << "    \"p_value\": "
        << result.tvla.p_value << ",\n";
    out << "    \"cohen_d\": "
        << result.tvla.cohen_d << ",\n";
    out << "    \"adjusted_p_value\": "
        << result.tvla.adjusted_p_value << ",\n";
    out << "    \"reference_threshold\": "
        << result.tvla.threshold << ",\n";
    out << "    \"reference_threshold_exceeded\": "
        << (result.tvla.threshold_exceeded ? "true" : "false")
        << ",\n";
    out << "    \"significant_after_adjustment\": "
        << (result.tvla.significant ? "true" : "false")
        << "\n";
    out << "  },\n";

    out << "  \"tvla_rounds\": [\n";
    for (std::size_t i = 0; i < result.round_tvla.size(); ++i) {
        const auto& tvla = result.round_tvla[i];
        out << "    { \"round\": " << i + 1U
            << ", \"t\": " << tvla.t_statistic
            << ", \"df\": " << tvla.degrees_of_freedom
            << ", \"p_value\": " << tvla.p_value
            << ", \"cohen_d\": " << tvla.cohen_d
            << ", \"adjusted_p_value\": "
            << tvla.adjusted_p_value
            << ", \"significant\": "
            << (tvla.significant ? "true" : "false")
            << " }";
        if (i + 1U != result.round_tvla.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"tvla_clean\": {\n";
    out << "    \"t_statistic\": "
        << result.tvla_clean.t_statistic << ",\n";
    out << "    \"abs_t\": "
        << result.tvla_clean.abs_t << ",\n";
    out << "    \"welch_satterthwaite_df\": "
        << result.tvla_clean.degrees_of_freedom << ",\n";
    out << "    \"p_value\": "
        << result.tvla_clean.p_value << ",\n";
    out << "    \"cohen_d\": "
        << result.tvla_clean.cohen_d << ",\n";
    out << "    \"reference_threshold_exceeded\": "
        << (result.tvla_clean.threshold_exceeded ? "true" : "false")
        << "\n";
    out << "  },\n";

    out << "  \"acceptance_criteria\": {\n";
    out << "    \"repeatability_cv_percent_max\": 10.0,\n";
    out << "    \"classification_error_percent_max\": 5.0,\n";
    out << "    \"tvla_reference_threshold\": 4.5\n";
    out << "  },\n";

    out << "  \"repeatability\": {\n";
    out << "    \"cv_percent\": "
        << result.repeatability_cv_percent << ",\n";
    out << "    \"icc\": "
        << result.repeatability_icc << ",\n";
    out << "    \"icc_model\": "
           "\"two-way random effects, single measurement\",\n";
    out << "    \"icc_definition\": "
           "\"absolute agreement [ICC(A,1)]\"\n";
    out << "  },\n";

    out << "  \"platform\": {\n";
    out << "    \"vendor\": \""
        << json_escape(result.platform.vendor) << "\",\n";
    out << "    \"brand\": \""
        << json_escape(result.platform.brand) << "\",\n";
    out << "    \"os_release\": \""
        << json_escape(result.platform.os_release) << "\",\n";
    out << "    \"machine\": \""
        << json_escape(result.platform.machine) << "\",\n";
    out << "    \"physical_cores\": "
        << result.platform.physical_cores << ",\n";
    out << "    \"logical_cpus\": "
        << result.platform.logical_cpus << ",\n";
    out << "    \"packages\": "
        << result.platform.packages << ",\n";
    out << "    \"smt_active\": "
        << (result.platform.smt_active ? "true" : "false")
        << "\n";
    out << "  },\n";

    out << "  \"run_medians\": [";
    for (std::size_t i = 0; i < result.run_medians.size(); ++i) {
        if (i != 0U) out << ", ";
        out << result.run_medians[i];
    }
    out << "],\n";

    out << "  \"repeatability_ok\": "
        << (result.repeatability_ok ? "true" : "false")
        << ",\n";
    out << "  \"leakage_detected\": "
        << (result.leakage_detected ? "true" : "false")
        << ",\n";

    out << "  \"control_evaluation\": [\n";
    for (std::size_t i = 0; i < result.controls.size(); ++i) {
        const auto& c = result.controls[i];
        out << "    {\n";
        out << "      \"control\": \""
            << json_escape(c.control) << "\",\n";
        out << "      \"scenario\": \""
            << json_escape(c.scenario) << "\",\n";
        out << "      \"expected_action\": \""
            << json_escape(c.expected_action) << "\",\n";
        out << "      \"executed\": "
            << (c.executed ? "true" : "false") << ",\n";
        out << "      \"evidence\": \""
            << json_escape(c.evidence) << "\"\n";
        out << "    }";
        if (i + 1U != result.controls.size()) out << ",";
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
    if (samples.empty() || window == 0U) return {};

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
    return remove_outliers(
        samples,
        OutlierMethod::MAD,
        threshold);
}

std::vector<double> remove_iqr_outliers(
    const std::vector<double>& samples,
    double multiplier) {
    if (samples.size() < 4U || multiplier <= 0.0) {
        return samples;
    }

    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end());

    const double q1 =
        quantile_of_sorted(sorted, 0.25);
    const double q3 =
        quantile_of_sorted(sorted, 0.75);
    const double iqr = q3 - q1;

    if (iqr <= 1e-12) {
        return samples;
    }

    const double lower =
        q1 - multiplier * iqr;
    const double upper =
        q3 + multiplier * iqr;

    std::vector<double> filtered;
    filtered.reserve(samples.size());
    for (const double value : samples) {
        if (value >= lower && value <= upper) {
            filtered.push_back(value);
        }
    }
    return filtered;
}

std::vector<double> remove_outliers(
    const std::vector<double>& samples,
    OutlierMethod method,
    double threshold) {
    if (method == OutlierMethod::IQR) {
        return remove_iqr_outliers(samples, threshold);
    }

    if (samples.size() < 5U || threshold <= 0.0) {
        return samples;
    }

    const double median = median_of(samples);

    std::vector<double> deviations;
    deviations.reserve(samples.size());
    for (const double value : samples) {
        deviations.push_back(
            std::abs(value - median));
    }

    const double mad = median_of(deviations);
    if (mad <= 1e-12) {
        return samples;
    }

    const double scale = 1.4826 * mad;

    std::vector<double> filtered;
    filtered.reserve(samples.size());

    for (const double value : samples) {
        if (std::abs(value - median) <=
            threshold * scale) {
            filtered.push_back(value);
        }
    }

    return filtered;
}

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    double outlier_threshold) {
    return robust_metrics(
        samples,
        OutlierMethod::MAD,
        outlier_threshold);
}

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    OutlierMethod method,
    double outlier_threshold) {
    return robust_metrics_internal(
        samples,
        method,
        outlier_threshold,
        10000U,
        0xB00757A7ULL);
}

BootstrapSummary bootstrap_ci(
    const std::vector<double>& samples,
    std::size_t resamples,
    std::uint64_t seed) {
    BootstrapSummary result;
    result.resamples = resamples;

    if (samples.empty() || resamples == 0U) {
        return result;
    }

    if (samples.size() == 1U) {
        result.mean_ci = {samples.front(), samples.front()};
        result.median_ci = {samples.front(), samples.front()};
        return result;
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> index_dist(
        0U,
        samples.size() - 1U);

    std::vector<double> means;
    std::vector<double> medians;
    std::vector<double> resampled(samples.size());
    means.reserve(resamples);
    medians.reserve(resamples);

    for (std::size_t b = 0; b < resamples; ++b) {
        long double sum = 0.0L;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double value =
                samples[index_dist(rng)];
            resampled[i] = value;
            sum += value;
        }
        means.push_back(
            static_cast<double>(
                sum / static_cast<long double>(
                    samples.size())));
        medians.push_back(median_of(resampled));
    }

    std::sort(means.begin(), means.end());
    std::sort(medians.begin(), medians.end());

    result.mean_ci = {
        quantile_of_sorted(means, 0.025),
        quantile_of_sorted(means, 0.975)
    };
    result.median_ci = {
        quantile_of_sorted(medians, 0.025),
        quantile_of_sorted(medians, 0.975)
    };
    return result;
}

double snr_linear(
    const std::vector<double>& first,
    const std::vector<double>& second,
    OutlierMethod method) {
    const auto a = robust_metrics(
        first,
        method,
        3.5);
    const auto b = robust_metrics(
        second,
        method,
        3.5);

    if (a.kept_count < 2U || b.kept_count < 2U) {
        return 0.0;
    }

    const double signal =
        std::abs(a.mean - b.mean);
    const double noise =
        std::sqrt(
            (a.stddev * a.stddev +
             b.stddev * b.stddev) /
            2.0);

    if (noise <= 1e-12) return 0.0;
    return signal / noise;
}

double classification_error_rate(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples) {
    const auto fixed = robust_metrics(
        fixed_samples,
        OutlierMethod::MAD,
        3.5);
    const auto random = robust_metrics(
        random_samples,
        OutlierMethod::MAD,
        3.5);

    if (fixed.kept_count == 0U ||
        random.kept_count == 0U) {
        return 100.0;
    }

    const double threshold =
        (fixed.mean + random.mean) / 2.0;

    std::size_t errors = 0U;

    for (const double value : fixed_samples) {
        if (value > threshold) ++errors;
    }

    for (const double value : random_samples) {
        if (value <= threshold) ++errors;
    }

    const std::size_t total =
        fixed_samples.size() +
        random_samples.size();

    return total == 0U
        ? 100.0
        : 100.0 *
              static_cast<double>(errors) /
              static_cast<double>(total);
}

double cohen_d(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    if (first.size() < 2U ||
        second.size() < 2U) {
        return 0.0;
    }

    const double first_mean = mean_of(first);
    const double second_mean = mean_of(second);
    const double first_sd =
        stddev_of(first, first_mean);
    const double second_sd =
        stddev_of(second, second_mean);

    const double df =
        static_cast<double>(
            first.size() + second.size() - 2U);
    if (df <= 0.0) return 0.0;

    const double pooled_variance =
        ((static_cast<double>(first.size() - 1U) *
          first_sd * first_sd) +
         (static_cast<double>(second.size() - 1U) *
          second_sd * second_sd)) /
        df;

    if (pooled_variance <= 1e-12) {
        return first_mean == second_mean
            ? 0.0
            : (first_mean > second_mean
                ? 1.0e300
                : -1.0e300);
    }

    return (first_mean - second_mean) /
           std::sqrt(pooled_variance);
}

TvlaResult welch_tvla(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples,
    double threshold) {
    TvlaResult result;
    result.fixed_count = fixed_samples.size();
    result.random_count = random_samples.size();
    result.threshold = threshold;

    if (fixed_samples.size() < 2U ||
        random_samples.size() < 2U) {
        return result;
    }

    result.fixed_mean = mean_of(fixed_samples);
    result.random_mean = mean_of(random_samples);
    result.fixed_stddev =
        stddev_of(
            fixed_samples,
            result.fixed_mean);
    result.random_stddev =
        stddev_of(
            random_samples,
            result.random_mean);

    const double fixed_component =
        result.fixed_stddev * result.fixed_stddev /
        static_cast<double>(result.fixed_count);
    const double random_component =
        result.random_stddev * result.random_stddev /
        static_cast<double>(result.random_count);

    const double denominator =
        std::sqrt(
            fixed_component +
            random_component);

    if (denominator <= 1e-12) {
        if (result.fixed_mean ==
            result.random_mean) {
            result.p_value = 1.0;
            result.adjusted_p_value = 1.0;
            result.cohen_d = 0.0;
            return result;
        }

        result.t_statistic =
            result.fixed_mean > result.random_mean
                ? 1.0e300
                : -1.0e300;
        result.abs_t = 1.0e300;
        result.degrees_of_freedom =
            static_cast<double>(
                result.fixed_count +
                result.random_count - 2U);
        result.p_value = 0.0;
        result.cohen_d =
            cohen_d(
                fixed_samples,
                random_samples);
    } else {
        result.t_statistic =
            (result.fixed_mean -
             result.random_mean) /
            denominator;
        result.abs_t =
            std::abs(result.t_statistic);

        const double numerator =
            (fixed_component +
             random_component) *
            (fixed_component +
             random_component);

        const double denominator_df =
            fixed_component * fixed_component /
                static_cast<double>(
                    result.fixed_count - 1U) +
            random_component * random_component /
                static_cast<double>(
                    result.random_count - 1U);

        result.degrees_of_freedom =
            denominator_df <= 1e-12
                ? static_cast<double>(
                    result.fixed_count +
                    result.random_count - 2U)
                : numerator / denominator_df;

        result.p_value =
            student_t_two_sided_p(
                result.abs_t,
                result.degrees_of_freedom);
        result.cohen_d =
            cohen_d(
                fixed_samples,
                random_samples);
    }

    result.threshold_exceeded =
        result.abs_t >= result.threshold;
    result.significant =
        result.p_value < kAlpha;
    result.adjusted_p_value =
        result.p_value;
    return result;
}

std::vector<double> adjust_p_values(
    const std::vector<double>& p_values,
    MultipleTestingMethod method) {
    const std::size_t count = p_values.size();
    if (count == 0U ||
        method == MultipleTestingMethod::None) {
        return p_values;
    }

    std::vector<double> adjusted(count, 1.0);

    if (method == MultipleTestingMethod::Bonferroni) {
        const double m =
            static_cast<double>(count);
        for (std::size_t i = 0; i < count; ++i) {
            adjusted[i] =
                std::min(
                    1.0,
                    std::clamp(p_values[i], 0.0, 1.0) *
                    m);
        }
        return adjusted;
    }

    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }

    std::sort(
        order.begin(),
        order.end(),
        [&p_values](std::size_t lhs, std::size_t rhs) {
            if (p_values[lhs] == p_values[rhs]) {
                return lhs < rhs;
            }
            return p_values[lhs] < p_values[rhs];
        });

    double running_min = 1.0;
    const double m =
        static_cast<double>(count);

    for (std::size_t rank = count; rank >= 1U; --rank) {
        const std::size_t index =
            order[rank - 1U];
        const double candidate =
            std::clamp(p_values[index], 0.0, 1.0) *
            m /
            static_cast<double>(rank);
        running_min =
            std::min(running_min, candidate);
        adjusted[index] =
            std::min(1.0, running_min);
    }

    return adjusted;
}

double icc_a1(
    const std::vector<std::vector<double>>& measurements) {
    const std::size_t n = measurements.size();
    if (n < 2U) return 0.0;

    const std::size_t k =
        measurements.front().size();
    if (k < 2U) return 0.0;

    for (const auto& row : measurements) {
        if (row.size() != k) return 0.0;
    }

    long double grand_sum = 0.0L;
    for (const auto& row : measurements) {
        for (const double value : row) {
            grand_sum += value;
        }
    }

    const long double grand_mean =
        grand_sum /
        static_cast<long double>(n * k);

    std::vector<long double> row_means(n, 0.0L);
    std::vector<long double> column_means(k, 0.0L);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            row_means[i] +=
                static_cast<long double>(
                    measurements[i][j]);
            column_means[j] +=
                static_cast<long double>(
                    measurements[i][j]);
        }
        row_means[i] /=
            static_cast<long double>(k);
    }

    for (std::size_t j = 0; j < k; ++j) {
        column_means[j] /=
            static_cast<long double>(n);
    }

    long double row_ss = 0.0L;
    for (const long double value : row_means) {
        const long double diff =
            value - grand_mean;
        row_ss += diff * diff;
    }

    long double column_ss = 0.0L;
    for (const long double value : column_means) {
        const long double diff =
            value - grand_mean;
        column_ss += diff * diff;
    }

    long double error_ss = 0.0L;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            const long double residual =
                static_cast<long double>(
                    measurements[i][j]) -
                row_means[i] -
                column_means[j] +
                grand_mean;
            error_ss += residual * residual;
        }
    }

    const long double ms_rows =
        static_cast<long double>(k) *
        row_ss /
        static_cast<long double>(n - 1U);
    const long double ms_columns =
        static_cast<long double>(n) *
        column_ss /
        static_cast<long double>(k - 1U);
    const long double ms_error =
        error_ss /
        static_cast<long double>(
            (n - 1U) * (k - 1U));

    const long double denominator =
        ms_rows +
        static_cast<long double>(k - 1U) * ms_error +
        static_cast<long double>(k) *
            (ms_columns - ms_error) /
            static_cast<long double>(n);

    if (std::abs(denominator) < 1e-18L) {
        return 0.0;
    }

    const double value =
        static_cast<double>(
            (ms_rows - ms_error) /
            denominator);

    return std::clamp(value, -1.0, 1.0);
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
    const std::string& report_path,
    OutlierMethod outlier_method,
    MultipleTestingMethod multiple_testing_method,
    std::size_t bootstrap_resamples) {
    EvaluationLabResult result;
    result.runs = runs;
    result.trace_samples = trace_samples;
    result.averaging_window =
        static_cast<int>(averaging_window);
    result.report_path = report_path;
    result.primary_outlier_method = outlier_method;
    result.multiple_testing_method =
        multiple_testing_method;
    result.platform = Evaluation::platform_signature();
    result.controls = control_matrix();

    if (runs <= 0 ||
        trace_samples <= 0 ||
        averaging_window == 0U ||
        report_path.empty()) {
        return result;
    }

    std::mt19937 rng(0x5EED1234U);
    std::vector<double> run_medians;
    std::vector<std::vector<double>> repeated_fixed_runs;

    std::vector<double> fixed_all;
    std::vector<double> random_all;
    const std::size_t reserve_count =
        static_cast<std::size_t>(trace_samples) *
        static_cast<std::size_t>(runs) /
        averaging_window;
    fixed_all.reserve(reserve_count);
    random_all.reserve(reserve_count);

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

        const std::size_t aligned_count =
            std::min(
                fixed_averaged.size(),
                random_averaged.size());

        if (aligned_count == 0U) continue;

        std::vector<double> aligned_fixed(
            fixed_averaged.begin(),
            fixed_averaged.begin() +
                static_cast<std::ptrdiff_t>(
                    aligned_count));
        std::vector<double> aligned_random(
            random_averaged.begin(),
            random_averaged.begin() +
                static_cast<std::ptrdiff_t>(
                    aligned_count));

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
        repeated_fixed_runs.push_back(
            std::move(aligned_fixed));

        result.round_tvla.push_back(
            Evaluation::welch_tvla(
                fixed_averaged,
                random_averaged));
    }

    result.aligned_samples =
        fixed_all.size();

    const std::size_t requested_pairs =
        static_cast<std::size_t>(
            std::max(0, runs)) *
        static_cast<std::size_t>(trace_samples) /
        averaging_window;

    if (requested_pairs > 0U) {
        result.alignment_loss_percent =
            100.0 *
            (1.0 -
             static_cast<double>(
                 result.aligned_samples) /
             static_cast<double>(
                 requested_pairs));
        if (result.alignment_loss_percent < 0.0) {
            result.alignment_loss_percent = 0.0;
        }
    }

    result.fixed_metrics =
        robust_metrics_internal(
            fixed_all,
            outlier_method,
            3.5,
            bootstrap_resamples,
            0xB00757A7ULL);
    result.random_metrics =
        robust_metrics_internal(
            random_all,
            outlier_method,
            3.5,
            bootstrap_resamples,
            0xB00757A8ULL);

    const auto fixed_iqr =
        robust_metrics_internal(
            fixed_all,
            OutlierMethod::IQR,
            1.5,
            0U,
            0U);
    const auto random_iqr =
        robust_metrics_internal(
            random_all,
            OutlierMethod::IQR,
            1.5,
            0U,
            0U);

    result.fixed_iqr_metrics = fixed_iqr;
    result.random_iqr_metrics = random_iqr;

    const auto fixed_mad =
        robust_metrics_internal(
            fixed_all,
            OutlierMethod::MAD,
            3.5,
            0U,
            0U);
    const auto random_mad =
        robust_metrics_internal(
            random_all,
            OutlierMethod::MAD,
            3.5,
            0U,
            0U);

    result.fixed_outliers = {
        fixed_mad.rejected_count,
        fixed_mad.rejected_percent,
        fixed_iqr.rejected_count,
        fixed_iqr.rejected_percent
    };
    result.random_outliers = {
        random_mad.rejected_count,
        random_mad.rejected_percent,
        random_iqr.rejected_count,
        random_iqr.rejected_percent
    };

    result.snr_linear =
        Evaluation::snr_linear(
            fixed_all,
            random_all,
            outlier_method);
    result.snr_db =
        result.snr_linear > 0.0
            ? 20.0 * std::log10(
                result.snr_linear)
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
        Evaluation::remove_outliers(
            fixed_all,
            outlier_method,
            3.5);
    const auto random_clean =
        Evaluation::remove_outliers(
            random_all,
            outlier_method,
            3.5);

    const TvlaResult tvla_clean =
        Evaluation::welch_tvla(
            fixed_clean,
            random_clean);
    result.tvla_clean = tvla_clean;

    result.tvla.adjusted_p_value =
        result.tvla.p_value;
    result.tvla.significant =
        result.tvla.p_value < kAlpha;

    std::vector<double> round_p_values;
    round_p_values.reserve(
        result.round_tvla.size());
    for (const auto& tvla : result.round_tvla) {
        round_p_values.push_back(tvla.p_value);
    }

    const auto adjusted_p_values =
        Evaluation::adjust_p_values(
            round_p_values,
            multiple_testing_method);

    for (std::size_t i = 0;
         i < result.round_tvla.size();
         ++i) {
        result.round_tvla[i].adjusted_p_value =
            adjusted_p_values[i];
        result.round_tvla[i].significant =
            adjusted_p_values[i] < kAlpha;
    }

    result.repeatability_cv_percent =
        repeatability_cv(run_medians);
    result.repeatability_icc =
        Evaluation::icc_a1(
            repeated_fixed_runs);

    result.recovery_rate_percent =
        result.simulated_capture_faults == 0
            ? 100.0
            : 100.0 *
                  static_cast<double>(
                      result.recovered_captures) /
                  static_cast<double>(
                      result.simulated_capture_faults);

    bool any_round_significant = false;
    for (const auto& tvla : result.round_tvla) {
        any_round_significant =
            any_round_significant ||
            tvla.significant;
    }

    result.repeatability_ok =
        result.repeatability_cv_percent <= 10.0 &&
        result.classification_error_percent <= 5.0;

    result.leakage_detected =
        result.tvla.threshold_exceeded ||
        tvla_clean.threshold_exceeded ||
        any_round_significant;

    if (!write_report(report_path, result)) {
        result.report_written = false;
        return result;
    }

    result.report_written = true;
    return result;
}