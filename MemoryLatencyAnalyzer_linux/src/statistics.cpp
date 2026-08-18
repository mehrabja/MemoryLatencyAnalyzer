#include "statistics.hpp"
#include <numeric>
#include <cmath>
#include <algorithm>

double Statistics::mean(const std::vector<uint64_t>& data) {
    if (data.empty()) return 0.0;
    uint64_t sum = std::accumulate(data.begin(), data.end(), 0ULL);
    return static_cast<double>(sum) / data.size();
}

double Statistics::stddev(const std::vector<uint64_t>& data, double mean_value) {
    if (data.size() < 2) return 0.0;
    double sum_sq = 0.0;
    for (auto v : data) {
        double diff = static_cast<double>(v) - mean_value;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / (data.size() - 1));
}

uint64_t Statistics::min_value(const std::vector<uint64_t>& data) {
    return data.empty() ? 0 : *std::min_element(data.begin(), data.end());
}

uint64_t Statistics::max_value(const std::vector<uint64_t>& data) {
    return data.empty() ? 0 : *std::max_element(data.begin(), data.end());
}