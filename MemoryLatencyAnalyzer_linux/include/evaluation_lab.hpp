#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct RobustMetrics {
    std::size_t input_count = 0;
    std::size_t kept_count = 0;
    std::size_t rejected_count = 0;
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
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
    double threshold = 4.5;
    bool threshold_exceeded = false;
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

    std::size_t aligned_samples = 0;
    double alignment_loss_percent = 0.0;

    RobustMetrics fixed_metrics;
    RobustMetrics random_metrics;

    double snr_linear = 0.0;
    double snr_db = 0.0;
    double classification_error_percent = 0.0;
    double repeatability_cv_percent = 0.0;

    TvlaResult tvla;

    bool repeatability_ok = false;
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

RobustMetrics robust_metrics(
    const std::vector<double>& samples,
    double outlier_threshold = 3.5);

double snr_linear(
    const std::vector<double>& first,
    const std::vector<double>& second);

double classification_error_rate(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples);

TvlaResult welch_tvla(
    const std::vector<double>& fixed_samples,
    const std::vector<double>& random_samples,
    double threshold = 4.5);

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
            "evaluation_lab_report.json");
};
