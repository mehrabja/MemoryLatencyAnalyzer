#include "evaluation_lab.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

void expect_near(double actual, double expected, double tolerance) {
    assert(std::abs(actual - expected) <= tolerance);
}

} // namespace

int main() {
    const std::vector<double> first{1.0, 2.0, 3.0};
    const std::vector<double> second{4.0, 5.0};

    const auto aligned =
        Evaluation::align_samples(first, second);
    assert(aligned.size() == 2U);
    assert(aligned[0] == 1.0);
    assert(aligned[1] == 2.0);

    const auto averaged =
        Evaluation::average_blocks(
            std::vector<double>{1.0, 3.0, 5.0, 7.0},
            2U);
    assert(averaged.size() == 2U);
    expect_near(averaged[0], 2.0, 1e-12);
    expect_near(averaged[1], 6.0, 1e-12);

    const std::vector<double> with_outlier{
        10.0, 11.0, 12.0, 13.0,
        14.0, 15.0, 16.0, 100.0};

    const auto mad_filtered =
        Evaluation::remove_mad_outliers(with_outlier);
    assert(mad_filtered.size() == 7U);

    const auto iqr_filtered =
        Evaluation::remove_iqr_outliers(with_outlier);
    assert(iqr_filtered.size() == 7U);

    const auto mad_metrics =
        Evaluation::robust_metrics(
            with_outlier,
            OutlierMethod::MAD,
            3.5);
    const auto iqr_metrics =
        Evaluation::robust_metrics(
            with_outlier,
            OutlierMethod::IQR,
            1.5);
    assert(mad_metrics.rejected_count == 1U);
    assert(iqr_metrics.rejected_count == 1U);
    expect_near(mad_metrics.rejected_percent, 12.5, 1e-12);
    expect_near(iqr_metrics.rejected_percent, 12.5, 1e-12);

    const std::vector<double> equal_a{
        1.0, 2.0, 3.0, 4.0};
    const std::vector<double> equal_b{
        1.0, 2.0, 3.0, 4.0};

    const auto bootstrap =
        Evaluation::bootstrap_ci(
            equal_a,
            10000U,
            12345U);
    expect_near(bootstrap.mean_ci.lower, 1.0, 1e-12);
    expect_near(bootstrap.mean_ci.upper, 4.0, 1e-12);
    expect_near(bootstrap.median_ci.lower, 1.0, 1e-12);
    expect_near(bootstrap.median_ci.upper, 4.0, 1e-12);
    assert(bootstrap.resamples == 10000U);

    const auto constant_bootstrap =
        Evaluation::bootstrap_ci(
            std::vector<double>{5.0, 5.0, 5.0, 5.0},
            10000U,
            7U);
    expect_near(constant_bootstrap.mean_ci.lower, 5.0, 1e-12);
    expect_near(constant_bootstrap.mean_ci.upper, 5.0, 1e-12);
    expect_near(constant_bootstrap.median_ci.lower, 5.0, 1e-12);
    expect_near(constant_bootstrap.median_ci.upper, 5.0, 1e-12);

    const std::vector<double> effect_a{1.0, 2.0, 3.0};
    const std::vector<double> effect_b{2.0, 3.0, 4.0};
    expect_near(
        Evaluation::cohen_d(effect_a, effect_b),
        -1.0,
        1e-12);

    const auto known_tvla =
        Evaluation::welch_tvla(
            std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0},
            std::vector<double>{2.0, 3.0, 4.0, 5.0, 6.0});
    expect_near(known_tvla.t_statistic, -1.0, 1e-12);
    expect_near(
        known_tvla.degrees_of_freedom,
        8.0,
        1e-12);
    expect_near(
        known_tvla.p_value,
        0.34659350708733416,
        1e-10);
    expect_near(
        known_tvla.cohen_d,
        -0.6324555320336759,
        1e-12);
    assert(!known_tvla.threshold_exceeded);
    assert(!known_tvla.significant);

    const auto adjusted_bonf =
        Evaluation::adjust_p_values(
            std::vector<double>{0.001, 0.01, 0.04},
            MultipleTestingMethod::Bonferroni);
    expect_near(adjusted_bonf[0], 0.003, 1e-12);
    expect_near(adjusted_bonf[1], 0.03, 1e-12);
    expect_near(adjusted_bonf[2], 0.12, 1e-12);

    const auto adjusted_bh =
        Evaluation::adjust_p_values(
            std::vector<double>{0.001, 0.01, 0.04},
            MultipleTestingMethod::BenjaminiHochberg);
    expect_near(adjusted_bh[0], 0.003, 1e-12);
    expect_near(adjusted_bh[1], 0.015, 1e-12);
    expect_near(adjusted_bh[2], 0.04, 1e-12);

    const std::vector<std::vector<double>> perfect_agreement{
        {1.0, 1.0, 1.0},
        {2.0, 2.0, 2.0},
        {3.0, 3.0, 3.0},
        {4.0, 4.0, 4.0},
    };
    expect_near(
        Evaluation::icc_a1(perfect_agreement),
        1.0,
        1e-12);

    const std::vector<double> fixed{
        10.0, 11.0, 12.0, 13.0, 14.0,
        10.0, 11.0, 12.0, 13.0, 14.0};
    const std::vector<double> random{
        20.0, 21.0, 22.0, 23.0, 24.0,
        20.0, 21.0, 22.0, 23.0, 24.0};

    const auto tvla =
        Evaluation::welch_tvla(
            fixed,
            random);
    assert(tvla.abs_t > 4.5);
    assert(tvla.threshold_exceeded);
    assert(tvla.p_value < 0.05);
    assert(tvla.cohen_d < 0.0);

    const double snr =
        Evaluation::snr_linear(
            fixed,
            random);
    assert(snr > 0.0);

    const double error =
        Evaluation::classification_error_rate(
            fixed,
            random);
    assert(error == 0.0);

    return 0;
}