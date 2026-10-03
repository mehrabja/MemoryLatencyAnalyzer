#include "evaluation_lab.hpp"

#include <cassert>
#include <cmath>
#include <vector>

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
    assert(std::abs(averaged[0] - 2.0) < 1e-12);
    assert(std::abs(averaged[1] - 6.0) < 1e-12);

    const auto filtered =
        Evaluation::remove_mad_outliers(
            std::vector<double>{
                10.0, 11.0, 12.0, 13.0,
                14.0, 15.0, 16.0, 100.0});
    assert(filtered.size() == 7U);

    const std::vector<double> fixed(100U, 10.0);
    const std::vector<double> random(100U, 20.0);

    const auto tvla =
        Evaluation::welch_tvla(
            fixed,
            random);
    assert(tvla.abs_t > 4.5);
    assert(tvla.threshold_exceeded);

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
