#pragma once

#include <cstdint>
#include <string>

struct CpuCapabilityInfo {
    std::string vendor = "Unknown";
    std::string brand = "Unknown";
    unsigned family = 0;
    unsigned model = 0;
    unsigned stepping = 0;
    bool sse = false;
    bool sse2 = false;
    bool sse4_2 = false;
    bool avx = false;
    bool avx2 = false;
    bool avx512f = false;
    double max_frequency_mhz = 0.0;
};

struct CpuComputeResult {
    double seconds = 0.0;
    double gflops = 0.0;
    double gintops = 0.0;
};

class CpuCapability {
public:
    static CpuCapabilityInfo detect();
    static CpuComputeResult benchmark(double seconds = 1.0);
};
