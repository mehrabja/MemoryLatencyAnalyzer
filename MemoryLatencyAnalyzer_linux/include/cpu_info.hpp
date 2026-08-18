#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CacheLevelInfo {
    int level;
    size_t size_bytes;
    size_t line_size;
    std::string type;
};

class CpuInfo {
public:
    static std::vector<CacheLevelInfo> detect_caches();
    static size_t get_l3_size();
    static double estimate_cpu_freq_ghz();
    static bool has_hyperthreading();
    static int logical_cores();
    static int physical_cores();
};