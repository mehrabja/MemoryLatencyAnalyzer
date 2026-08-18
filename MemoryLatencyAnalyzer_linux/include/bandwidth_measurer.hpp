#pragma once
#include <cstddef>
#include <string>

struct BandwidthResult {
    double read_cycles;
    double write_cycles;
    double copy_cycles;
    size_t size_bytes;
    std::string allocator;
};

class BandwidthMeasurer {
public:
    static BandwidthResult measure(size_t size_bytes, const std::string& method);
};