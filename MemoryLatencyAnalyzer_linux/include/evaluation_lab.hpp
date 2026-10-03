#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class OutlierMethod {
    MAD,
    IQR,
};

enum class MultipleTestingMethod {
    None,
    Bonferroni,
    BenjaminiHochberg,
};

struct ConfidenceInterval {
    double lower = 0.0;
    double upper = 0.0;
};

struct BootstrapSummary {
    std::size_t resamples = 0;
    ConfidenceInterval mean_ci;
    ConfidenceInterval median_ci;
};

struct RobustMetrics {
    std::size_t input_count = 0;
    std::size_t kept_count = 0;
    std::size_t rejected_count = 0;
    double rejected_percent = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
    BootstrapSummary bootstrap;
};

struct OutlierComparison {
    std::size_t mad_rejected_count = 0;
    double mad_rejected_percent = 0.0;
    std::size_t iqr_rejected_count = 0;
    double iqr_rejected_percent = 0.0;
};

struct TvlaResult {
    std::size_t fixed_count = 0;
    std::size_t random_count = 0;
    double fixed_mean = 0.0;
    double random_mean = 0.0;
    double fixed_stddev = 0.0;
    double random_stddev = 0.0;
    double t_statistic = 0.0;
    double abs_t = 0.0;
    double degrees_of_freedom = 0.0;
    double p_value = 1.0;
    double cohen_d = 0.0;
    double adjusted_p_value = 1.0;
    double threshold = 4.5;
    bool threshold_exceeded = false;
    bool significant = false;
};

struct PlatformSignature {
    std::string vendor;
    std::string brand;
    std::string os_release;
    std::string machine;
    int physical_cores = 0;
    int logical_cpus = 0;
    int packages = 0;
    bool smt_active = false;
};

struct ControlEvaluation {
    std::string control;
    std::string scenario;
    std::string expected_action;
    std::string evidence;
    bool executed = false;
};

struct EvaluationLabResult {
    int runs = 0;
    int trace_samples = 0;
    int averaging_window = 0;
    int simulated_capture_faults = 0;
    int recovered_captures = 0;
    double recovery_rate_percent = 0.0;

    std::size_t aligned_samples = 0;
    double alignment_loss_percent = 0.0;

    RobustMetrics fixed_metrics;
    RobustMetrics random_metrics;
    RobustMetrics fixed_iqr_metrics;
    RobustMetrics random_iqr_metrics;
    OutlierComparison fixed_outliers;
    OutlierComparison random_outliers;

    double snr_linear = 0.0;
    double snr_db = 0.0;
    double classification_error_percent = 0.0;
    double repeatability_cv_percent = 0.0;
    double repeatability_icc = 0.0;

    TvlaResult tvla;
    TvlaResult tvla_clean;
    std::vector<TvlaResult> round_tvla;
    std::vector<double> run_medians;
    MultipleTestingMethod multiple_testing_method =
        MultipleTestingMethod::BenjaminiHochberg;
    OutlierMethod primary_outlier_method = OutlierMethod::MAD;

    bool repeatability_ok = false;
    bool leakage_detected = false;
    bool criteria_documented = true;

    PlatformSignature platform;
    std::vector<ControlEvaluation> controls;

    bool report_written = false;
    std::string report_path;
};

namespace Evaluation {

std::vector<double> align_samples(
    const std::vector<double>& first,
    const std::vector<double>& second);

std::vector<double> average_blocks(
    const std::vector<double>& samples,
    std::size_t window);

std::vector<double> remove_mad_outliers(
    const std::vector<double>& samples,
    double threshold = 3.5);

std::vector<double> remove_iqr_outliers(
    const std::vector<double>& samples,
    double multiplier = 1.5);

std::vector<double> remove_outliers(
    const std::vector<double>& samples,
    OutlierMethod method,
    double threshold = 3.5);

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    double outlier_threshold = 3.5);

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    OutlierMethod method,
    double outlier_threshold);

BootstrapSummary bootstrap_ci(
    const std::vector<double>& samples,
    std::size_t resamples = 10000U,
    std::uint64_t seed = 0xB00757A7ULL);

double snr_linear(
    const std::vector<double>& first,
    const std::vector<double>& second,
    OutlierMethod method = OutlierMethod::MAD);

double classification_error_rate(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples);

double cohen_d(
    const std::vector<double>& first,
    const std::vector<double>& second);

TvlaResult welch_tvla(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples,
    double threshold = 4.5);

std::vector<double> adjust_p_values(
    const std::vector<double>& p_values,
    MultipleTestingMethod method);

double icc_a1(
    const std::vector<std::vector<double>>& measurements);

PlatformSignature platform_signature();

} // namespace Evaluation

class EvaluationLab {
public:
    // Complete local validation pipeline. All traces and security events are
    // synthetic and remain inside this process.
    static EvaluationLabResult run(
        int runs = 3,
        int trace_samples = 2048,
        std::size_t averaging_window = 4,
        const std::string& report_path =
            "evaluation_lab_report.json",
        OutlierMethod outlier_method = OutlierMethod::MAD,
        MultipleTestingMethod multiple_testing_method =
            MultipleTestingMethod::BenjaminiHochberg,
        std::size_t bootstrap_resamples = 10000U);
};