#include "reporter.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

double cycles_to_ns(double cycles, double tsc_hz) {
    if (tsc_hz <= 0.0) return 0.0;
    return cycles * 1e9 / tsc_hz;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace

void Reporter::print_system_info(
    const std::string& cpu_model,
    const CpuTopologyInfo& topology,
    const std::vector<CacheLevelInfo>& caches,
    double tsc_hz,
    std::size_t timer_overhead_cycles,
    const std::vector<int>& allowed_cpus) {
    std::cout << "CPU: " << cpu_model << '
'
              << "Logical CPUs allowed: " << allowed_cpus.size() << '
'
              << "Physical cores: " << topology.physical_cores
              << " | Packages: " << topology.packages
              << " | SMT active: "
              << (topology.smt_active ? "yes" : "no") << '
'
              << "TSC rate: " << std::fixed << std::setprecision(3)
              << (tsc_hz / 1e9) << " GHz
"
              << "Timing overhead: " << timer_overhead_cycles
              << " cycles
";

    std::cout << "Cache hierarchy:
";
    for (const auto& cache : caches) {
        std::cout << "  L" << cache.level << ' '
                  << cache.type << ": "
                  << (static_cast<double>(cache.size_bytes) / 1024.0)
                  << " KiB, line " << cache.line_size << " B
";
    }
    std::cout << '
';
}

void Reporter::print_latency(
    const std::vector<MeasurementResult>& results,
    double tsc_hz,
    bool verbose) {
    std::cout << "===== Latency / access cost =====
";

    for (const auto& result : results) {
        const auto& s = result.stats;

        std::cout << result.label << '
'
                  << "  mean   : "
                  << std::fixed << std::setprecision(2)
                  << s.mean << " cycles ("
                  << cycles_to_ns(s.mean, tsc_hz) << " ns)
"
                  << "  median : "
                  << s.median << " cycles ("
                  << cycles_to_ns(s.median, tsc_hz) << " ns)
";

        if (verbose) {
            std::cout << "  p95    : " << s.p95 << " cycles
"
                      << "  p99    : " << s.p99 << " cycles
"
                      << "  stddev : " << s.stddev << " cycles
"
                      << "  min/max: " << s.min << " / "
                      << s.max << " cycles
";
        }

        std::cout << "  samples: " << s.count << "

";
    }
}

void Reporter::print_bandwidth(
    const std::vector<BandwidthResult>& results) {
    if (results.empty()) return;

    std::cout << "===== Bandwidth =====
";
    for (const auto& result : results) {
        std::cout << result.allocator
                  << " (" << std::fixed << std::setprecision(2)
                  << (static_cast<double>(result.size_bytes) /
                      (1024.0 * 1024.0))
                  << " MiB)
"
                  << "  read : " << result.read_gb_s << " GiB/s
"
                  << "  write: " << result.write_gb_s << " GiB/s
"
                  << "  copy : " << result.copy_gb_s << " GiB/s

";
    }
}

void Reporter::print_shared_memory(
    const SharedMemoryResult& result,
    double tsc_hz,
    bool verbose) {
    if (!result.success) {
        std::cout << "Shared memory test: "
                  << result.error_message << '
';
        return;
    }

    std::cout << "===== Cross-process shared memory =====
"
              << "producer CPU: " << result.producer_cpu
              << " | consumer CPU: " << result.consumer_cpu << '
'
              << "round trip: " << std::fixed << std::setprecision(2)
              << result.stats.mean << " cycles ("
              << cycles_to_ns(result.stats.mean, tsc_hz)
              << " ns), median "
              << result.stats.median << " cycles
";

    if (verbose) {
        std::cout << "p95: " << result.stats.p95
                  << " | p99: " << result.stats.p99
                  << " | min/max: " << result.stats.min
                  << "/" << result.stats.max << " cycles
";
    }

    std::cout << "samples: " << result.stats.count << "

";
}

bool Reporter::write_csv(
    const std::string& filename,
    const std::vector<MeasurementResult>& latency,
    const std::vector<BandwidthResult>& bandwidth,
    const SharedMemoryResult* shared_memory,
    double tsc_hz) {
    std::ofstream file(filename);
    if (!file) return false;

    file << "Type,Label,Samples,MeanCycles,MedianCycles,P95Cycles,"
            "P99Cycles,StdDevCycles,MinCycles,MaxCycles,MeanNanoseconds,"
            "ReadGiBps,WriteGiBps,CopyGiBps,Allocator,Bytes
";

    for (const auto& result : latency) {
        const auto& s = result.stats;
        file << "latency,"" << result.label << "","
             << s.count << ','
             << s.mean << ','
             << s.median << ','
             << s.p95 << ','
             << s.p99 << ','
             << s.stddev << ','
             << s.min << ','
             << s.max << ','
             << cycles_to_ns(s.mean, tsc_hz)
             << ",,,,,
";
    }

    for (const auto& result : bandwidth) {
        file << "bandwidth,"" << result.allocator
             << "",,,,,,,,,,"
             << result.read_gb_s << ','
             << result.write_gb_s << ','
             << result.copy_gb_s << ",""
             << result.allocator << "","
             << result.size_bytes << '
';
    }

    if (shared_memory && shared_memory->success) {
        const auto& s = shared_memory->stats;

        file << "shared_memory,"Cross-process round trip","
             << s.count << ','
             << s.mean << ','
             << s.median << ','
             << s.p95 << ','
             << s.p99 << ','
             << s.stddev << ','
             << s.min << ','
             << s.max << ','
             << cycles_to_ns(s.mean, tsc_hz)
             << ",,,,,
";
    }

    return true;
}

bool Reporter::append_history(
    const std::string& filename,
    const std::vector<MeasurementResult>& latency,
    const std::vector<BandwidthResult>& bandwidth) {
    std::ofstream file(filename, std::ios::app);
    if (!file) return false;

    file << timestamp() << '
';

    for (const auto& result : latency) {
        file << "latency,"" << result.label
             << ""," << result.stats.median << '
';
    }

    for (const auto& result : bandwidth) {
        file << "bandwidth," << result.allocator << ','
             << result.read_gb_s << ','
             << result.write_gb_s << ','
             << result.copy_gb_s << '
';
    }

    file << "----
";
    return true;
}
