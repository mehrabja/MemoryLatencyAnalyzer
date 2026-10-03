#pragma once

#include <cstdint>
#include <string>

#include "statistics.hpp"

struct SharedMemoryResult {
    Statistics::Summary stats;
    int iterations = 0;
    int producer_cpu = -1;
    int consumer_cpu = -1;
    bool success = false;
    std::string error_message;
};

class SharedMemoryMeasurer {
public:
    static SharedMemoryResult measure_cross_process(
        int iterations,
        int warmup,
        int producer_cpu,
        int consumer_cpu);
};
