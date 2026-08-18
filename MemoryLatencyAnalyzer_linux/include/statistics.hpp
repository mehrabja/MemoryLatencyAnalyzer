#pragma once
#include <vector>
#include <cstdint>

class Statistics {
public:
    static double mean(const std::vector<uint64_t>& data);
    static double stddev(const std::vector<uint64_t>& data, double mean_value);
    static uint64_t min_value(const std::vector<uint64_t>& data);
    static uint64_t max_value(const std::vector<uint64_t>& data);
};