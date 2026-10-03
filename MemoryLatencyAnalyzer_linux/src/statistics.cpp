#include "statistics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

double quantile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted.front();

    const double position = q * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) return sorted[lower];

    const double weight = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

Statistics::Summary summarize_impl(const std::vector<double>& samples) {
    Statistics::Summary result;
    result.count = samples.size();
    if (samples.empty()) return result;

    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end());

    const long double sum =
        std::accumulate(samples.begin(), samples.end(), 0.0L);

    result.mean =
        static_cast<double>(sum / static_cast<long double>(samples.size()));
    result.median = quantile(sorted, 0.50);
    result.p95 = quantile(sorted, 0.95);
    result.p99 = quantile(sorted, 0.99);
    result.min = sorted.front();
    result.max = sorted.back();

    if (samples.size() >= 2) {
        long double squared = 0.0L;
        for (const double value : samples) {
            const long double diff =
                static_cast<long double>(value) - result.mean;
            squared += diff * diff;
        }

        result.stddev = std::sqrt(static_cast<double>(
            squared / static_cast<long double>(samples.size() - 1)));
    }

    return result;
}

} // namespace

namespace Statistics {

Summary summarize(const std::vector<double>& samples) {
    return summarize_impl(samples);
}

Summary summarize(const std::vector<std::uint64_t>& samples) {
    std::vector<double> converted;
    converted.reserve(samples.size());

    for (const std::uint64_t value : samples) {
        converted.push_back(static_cast<double>(value));
    }

    return summarize_impl(converted);
}

} // namespace Statistics
