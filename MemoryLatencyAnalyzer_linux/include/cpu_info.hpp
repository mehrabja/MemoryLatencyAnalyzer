#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct CacheLevelInfo {
    int level = 0;
    std::size_t size_bytes = 0;
    std::size_t line_size = 0;
    std::string type;
};

struct CpuTopologyInfo {
    int logical_cpus = 0;
    int physical_cores = 0;
    int packages = 0;
    bool smt_capable = false;
    bool smt_active = false;
};

class CpuInfo {
public:
    static std::string model_name();
    static std::vector<CacheLevelInfo> detect_caches();
    static std::size_t cache_line_size();
    static CpuTopologyInfo topology();
    static bool has_hyperthreading();
    static int logical_cores();
    static int physical_cores();

    static int choose_distinct_cpu(
        int primary_cpu,
        const std::vector<int>& allowed_cpus);
};
