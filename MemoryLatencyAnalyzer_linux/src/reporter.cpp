#include "reporter.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

const PmuCounterValue* find_pmu(
    const PmuSnapshot& snapshot,
    const std::string& name) {
    const auto it = std::find_if(
        snapshot.counters.begin(),
        snapshot.counters.end(),
        [&name](const PmuCounterValue& value) {
            return value.name == name;
        });
    return it == snapshot.counters.end() ? nullptr : &(*it);
}

void print_pmu(
    const PmuSnapshot& snapshot,
    std::size_t sample_count,
    double mean_tsc_cycles,
    bool show_tsc_ratio) {
    if (!snapshot.valid) {
        std::cout
            << "  PMU: unavailable"
            << (snapshot.error.empty() ? "" : " (" + snapshot.error + ")")
            << '\n';
        return;
    }

    std::cout
        << "  PMU raw counts:";
    for (const auto& counter : snapshot.counters) {
        std::cout
            << ' ' << counter.name
            << '=' << counter.raw_count;
    }
    std::cout << '\n';

    std::cout
        << "  PMU scaled counts:";
    for (const auto& counter : snapshot.counters) {
        std::cout
            << ' ' << counter.name
            << '=' << counter.scaled_count;
    }
    std::cout << '\n';

    const auto* cycles = find_pmu(snapshot, "cpu-cycles");
    const auto* instructions =
        find_pmu(snapshot, "instructions-retired");
    const auto* l1d_misses =
        find_pmu(snapshot, "L1D-load-misses");
    const auto* llc_refs =
        find_pmu(snapshot, "LLC-loads");
    const auto* llc_misses =
        find_pmu(snapshot, "LLC-load-misses");
    const auto* stalled =
        find_pmu(snapshot, "stalled-cycles-backend");

    if (instructions && instructions->scaled_count != 0U) {
        if (l1d_misses) {
            std::cout
                << "  L1D misses/KI: "
                << (1000.0 *
                    static_cast<double>(l1d_misses->scaled_count) /
                    static_cast<double>(instructions->scaled_count));
        } else {
            std::cout << "  L1D misses/KI: N/A";
        }

        if (llc_misses) {
            std::cout
                << " | LLC misses/KI: "
                << (1000.0 *
                    static_cast<double>(llc_misses->scaled_count) /
                    static_cast<double>(instructions->scaled_count));
        } else {
            std::cout << " | LLC misses/KI: N/A";
        }
        std::cout << '\n';
    }

    if (llc_refs && llc_refs->scaled_count != 0U && llc_misses) {
        std::cout
            << "  LLC miss rate: "
            << (100.0 *
                static_cast<double>(llc_misses->scaled_count) /
                static_cast<double>(llc_refs->scaled_count))
            << "%\n";
    } else {
        std::cout << "  LLC miss rate: N/A\n";
    }

    if (cycles && instructions && instructions->scaled_count != 0U) {
        std::cout
            << "  cycles/instruction: "
            << (static_cast<double>(cycles->scaled_count) /
                static_cast<double>(instructions->scaled_count))
            << '\n';
    } else {
        std::cout << "  cycles/instruction: N/A\n";
    }

    if (cycles && stalled && cycles->scaled_count != 0U) {
        std::cout
            << "  backend stalled-cycle fraction: "
            << (static_cast<double>(stalled->scaled_count) /
                static_cast<double>(cycles->scaled_count))
            << '\n';
    } else {
        std::cout << "  backend stalled-cycle fraction: N/A\n";
    }

    if (show_tsc_ratio &&
        cycles &&
        sample_count != 0U &&
        mean_tsc_cycles > 0.0) {
        const double expected_cycles =
            mean_tsc_cycles *
            static_cast<double>(sample_count);
        std::cout
            << "  PMU CPU-cycles / TSC-reference cycles: "
            << (static_cast<double>(cycles->scaled_count) /
                expected_cycles)
            << '\n';
    }
}

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
    std::cout
        << "CPU: " << cpu_model << '\n'
        << "Logical CPUs allowed: " << allowed_cpus.size() << '\n'
        << "Physical cores: " << topology.physical_cores
        << " | Packages: " << topology.packages
        << " | SMT active: "
        << (topology.smt_active ? "yes" : "no") << '\n'
        << "TSC rate: " << std::fixed << std::setprecision(3)
        << (tsc_hz / 1e9) << " GHz\n"
        << "Timing overhead: " << timer_overhead_cycles
        << " cycles\n";

    std::cout << "Cache hierarchy:\n";
    for (const auto& cache : caches) {
        std::cout
            << "  L" << cache.level << ' '
            << cache.type << ": "
            << (static_cast<double>(cache.size_bytes) / 1024.0)
            << " KiB, line " << cache.line_size << " B\n";
    }

    std::cout << '\n';
}

void Reporter::print_latency(
    const std::vector<MeasurementResult>& results,
    double tsc_hz,
    bool verbose) {
    std::cout << "===== Latency / access cost =====\n";

    for (const auto& result : results) {
        const auto& s = result.stats;

        std::cout
            << result.label << '\n'
            << "  mean   : "
            << std::fixed << std::setprecision(2)
            << s.mean << " cycles ("
            << cycles_to_ns(s.mean, tsc_hz) << " ns)\n"
            << "  median : "
            << s.median << " cycles ("
            << cycles_to_ns(s.median, tsc_hz) << " ns)\n";

        if (verbose) {
            std::cout
                << "  p95    : " << s.p95 << " cycles\n"
                << "  p99    : " << s.p99 << " cycles\n"
                << "  stddev : " << s.stddev << " cycles\n"
                << "  min/max: " << s.min << " / "
                << s.max << " cycles\n";

            if (result.pmu) {
                print_pmu(
                    *result.pmu,
                    s.count,
                    s.mean,
                    true);
            }
        }

        std::cout
            << "  samples: " << s.count << "\n\n";
    }
}

void Reporter::print_bandwidth(
    const std::vector<BandwidthResult>& results,
    bool verbose) {
    if (results.empty()) return;

    std::cout << "===== Bandwidth =====\n";

    for (const auto& result : results) {
        std::cout
            << result.allocator
            << " (" << std::fixed << std::setprecision(2)
            << (static_cast<double>(result.size_bytes) /
                (1024.0 * 1024.0))
            << " MiB)\n"
            << "  read : " << result.read_gb_s << " GiB/s\n"
            << "  write: " << result.write_gb_s << " GiB/s\n"
            << "  copy : " << result.copy_gb_s << " GiB/s\n";

        if (verbose) {
            if (result.pmu_read) {
                std::cout << "  PMU read region:\n";
                print_pmu(*result.pmu_read, 0U, 0.0, false);
            }
            if (result.pmu_write) {
                std::cout << "  PMU write region:\n";
                print_pmu(*result.pmu_write, 0U, 0.0, false);
            }
            if (result.pmu_copy) {
                std::cout << "  PMU copy region:\n";
                print_pmu(*result.pmu_copy, 0U, 0.0, false);
            }
    
            }

        std::cout << '\n';
    }
}

void Reporter::print_shared_memory(
    const SharedMemoryResult& result,
    double tsc_hz,
    bool verbose) {
    if (!result.success) {
        std::cout
            << "Shared memory test: "
            << result.error_message << '\n';
        return;
    }

    std::cout
        << "===== Cross-process shared memory =====\n"
        << "producer CPU: " << result.producer_cpu
        << " | consumer CPU: " << result.consumer_cpu << '\n'
        << "round trip: " << std::fixed << std::setprecision(2)
        << result.stats.mean << " cycles ("
        << cycles_to_ns(result.stats.mean, tsc_hz)
        << " ns), median "
        << result.stats.median << " cycles\n";

    if (verbose) {
        std::cout
            << "p95: " << result.stats.p95
            << " | p99: " << result.stats.p99
            << " | min/max: " << result.stats.min
            << "/" << result.stats.max << " cycles\n";
    }

    std::cout
        << "samples: " << result.stats.count << "\n\n";
}

bool Reporter::write_csv(
    const std::string& filename,
    const std::vector<MeasurementResult>& latency,
    const std::vector<BandwidthResult>& bandwidth,
    const SharedMemoryResult* shared_memory,
    double tsc_hz) {
    std::ofstream file(filename);
    if (!file) return false;

    file
        << "Type,Label,Samples,MeanCycles,MedianCycles,P95Cycles,"
           "P99Cycles,StdDevCycles,MinCycles,MaxCycles,MeanNanoseconds,"
           "ReadGiBps,WriteGiBps,CopyGiBps,Allocator,Bytes\n";

    for (const auto& result : latency) {
        const auto& s = result.stats;

        file
            << "latency,\"" << result.label << "\","
            << s.count << ','
            << s.mean << ','
            << s.median << ','
            << s.p95 << ','
            << s.p99 << ','
            << s.stddev << ','
            << s.min << ','
            << s.max << ','
            << cycles_to_ns(s.mean, tsc_hz)
            << ",,,,,\n";
    }

    for (const auto& result : bandwidth) {
        file
            << "bandwidth,\"" << result.allocator
            << "\",,,,,,,,,,"
            << result.read_gb_s << ','
            << result.write_gb_s << ','
            << result.copy_gb_s << ",\""
            << result.allocator << "\","
            << result.size_bytes << '\n';
    }

    if (shared_memory && shared_memory->success) {
        const auto& s = shared_memory->stats;

        file
            << "shared_memory,\"Cross-process round trip\","
            << s.count << ','
            << s.mean << ','
            << s.median << ','
            << s.p95 << ','
            << s.p99 << ','
            << s.stddev << ','
            << s.min << ','
            << s.max << ','
            << cycles_to_ns(s.mean, tsc_hz)
            << ",,,,,\n";
    }

    return true;
}

bool Reporter::append_history(
    const std::string& filename,
    const std::vector<MeasurementResult>& latency,
    const std::vector<BandwidthResult>& bandwidth) {
    std::ofstream file(filename, std::ios::app);
    if (!file) return false;

    file << timestamp() << '\n';

    for (const auto& result : latency) {
        file
            << "latency,\"" << result.label
            << "\"," << result.stats.median << '\n';
    }

    for (const auto& result : bandwidth) {
        file
            << "bandwidth," << result.allocator << ','
            << result.read_gb_s << ','
            << result.write_gb_s << ','
            << result.copy_gb_s << '\n';
    }

    file << "----\n";
    return true;
}
