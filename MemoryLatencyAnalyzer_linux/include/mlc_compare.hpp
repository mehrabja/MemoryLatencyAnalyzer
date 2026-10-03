#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct MlcComparisonPoint {
    std::string scenario;
    std::uint64_t delay_cycles = 0;
    double ours_ns = 0.0;
    double mlc_ns = 0.0;
    double absolute_delta_ns = 0.0;
    double percent_delta = 0.0;
    std::string status;
};

struct MlcComparisonReport {
    bool available = false;
    std::string binary;
    std::string version = "unknown";
    std::string diagnostic;
    int primary_cpu = -1;
    std::size_t latency_buffer_bytes = 0;
    std::size_t total_load_bytes = 0;
    std::size_t load_worker_count = 0;
    std::string numa_policy;
    std::vector<MlcComparisonPoint> results;
    bool report_written = false;
    std::string csv_path;
    std::string json_path;
};

class MlcComparison {
public:
    static std::string find_mlc();
    static std::optional<double> parse_idle_latency_ns(
        std::string_view output);
    static std::optional<double> parse_loaded_latency_ns(
        std::string_view output,
        std::uint64_t delay_cycles);

    static MlcComparisonReport run(
        const std::vector<int>& allowed_cpus,
        int primary_cpu,
        std::size_t buffer_bytes,
        std::size_t total_load_bytes,
        int iterations,
        int rounds,
        const std::vector<std::uint64_t>& delays_cycles,
        const std::string& csv_path,
        const std::string& json_path);
};
