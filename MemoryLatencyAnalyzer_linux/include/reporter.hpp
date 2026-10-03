#pragma once

#include <string>
#include <vector>

#include "bandwidth_measurer.hpp"
#include "cpu_info.hpp"
#include "latency_measurer.hpp"
#include "shared_memory_measurer.hpp"

class Reporter {
public:
    static void print_system_info(
        const std::string& cpu_model,
        const CpuTopologyInfo& topology,
        const std::vector<CacheLevelInfo>& caches,
        double tsc_hz,
        std::size_t timer_overhead_cycles,
        const std::vector<int>& allowed_cpus);

    static void print_latency(
        const std::vector<MeasurementResult>& results,
        double tsc_hz,
        bool verbose);

    static void print_bandwidth(
        const std::vector<BandwidthResult>& results);

    static void print_shared_memory(
        const SharedMemoryResult& result,
        double tsc_hz,
        bool verbose);

    static bool write_csv(
        const std::string& filename,
        const std::vector<MeasurementResult>& latency,
        const std::vector<BandwidthResult>& bandwidth,
        const SharedMemoryResult* shared_memory,
        double tsc_hz);

    static bool append_history(
        const std::string& filename,
        const std::vector<MeasurementResult>& latency,
        const std::vector<BandwidthResult>& bandwidth);
};
