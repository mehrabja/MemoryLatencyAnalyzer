#include "bandwidth_measurer.hpp"
#include "cpu_info.hpp"
#include "latency_measurer.hpp"
#include "platform_utils.hpp"
#include "reporter.hpp"
#include "settings.hpp"
#include "shared_memory_measurer.hpp"
#include "spectre_v1.hpp"
#include "timer.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_help() {
    std::cout
        << "Usage: latency_analyzer [options]\n\n"
        << "Latency:\n"
        << "  --rounds N             Independent measurement rounds (default 5)\n"
        << "  --iterations N         Samples per round (default 1000)\n"
        << "  --warmup N             Warmup samples per round (default 150)\n"
        << "  --buffer-mib N         Latency buffer size in MiB (default: auto)\n\n"
        << "Bandwidth:\n"
        << "  --bandwidth-mib N      Streaming buffer size in MiB (default 64)\n"
        << "  --no-bandwidth         Skip bandwidth measurements\n\n"
        << "Shared memory:\n"
        << "  --shm-iterations N     Cross-process round trips (default 2000)\n"
        << "  --no-shared-memory     Skip the shared-memory test\n\n"
        << "Output:\n"
        << "  --csv FILE             Write latency, bandwidth and shared-memory results\n"
        << "  --quiet                Minimal output\n"
        << "  --verbose              Include p95/p99/stddev/min/max\n\n"
        << "Other:\n"
        << "  --spectre              Run the self-contained Spectre V1 demo and exit\n"
        << "  --help                 Show this help\n";
}

bool parse_int(std::string_view text, int& out) {
    if (text.empty()) return false;

    int value = 0;
    const auto [ptr, ec] = std::from_chars(
        text.data(), text.data() + text.size(), value);

    if (ec != std::errc{} ||
        ptr != text.data() + text.size()) {
        return false;
    }

    out = value;
    return true;
}

bool consume_int(
    int argc,
    char* argv[],
    int& index,
    int& target) {
    if (index + 1 >= argc) return false;
    ++index;
    return parse_int(argv[index], target);
}

bool consume_size_mib(
    int argc,
    char* argv[],
    int& index,
    std::size_t& target) {
    int value = 0;
    if (!consume_int(argc, argv, index, value) ||
        value <= 0) {
        return false;
    }

    const auto mib = static_cast<std::size_t>(value);
    target = mib * static_cast<std::size_t>(1024) *
             static_cast<std::size_t>(1024);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    int rounds = Config::DEFAULT_ROUNDS;
    int iterations = Config::DEFAULT_ITERATIONS;
    int warmup = Config::DEFAULT_WARMUP;
    int shm_iterations = Config::DEFAULT_SHM_ITERATIONS;

    std::size_t buffer_size = 0;
    std::size_t bandwidth_size =
        Config::DEFAULT_BANDWIDTH_SIZE;

    bool run_bandwidth = true;
    bool run_shared_memory = true;
    bool run_spectre = false;
    bool quiet = false;
    bool verbose = false;
    std::string csv_file;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help") {
            print_help();
            return 0;
        }
        if (arg == "--spectre") {
            run_spectre = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--no-bandwidth") {
            run_bandwidth = false;
        } else if (arg == "--no-shared-memory") {
            run_shared_memory = false;
        } else if (arg == "--rounds") {
            if (!consume_int(argc, argv, i, rounds)) {
                std::cerr << "Invalid --rounds value\n";
                return 2;
            }
        } else if (arg == "--iterations") {
            if (!consume_int(argc, argv, i, iterations)) {
                std::cerr << "Invalid --iterations value\n";
                return 2;
            }
        } else if (arg == "--warmup") {
            if (!consume_int(argc, argv, i, warmup)) {
                std::cerr << "Invalid --warmup value\n";
                return 2;
            }
        } else if (arg == "--shm-iterations") {
            if (!consume_int(argc, argv, i, shm_iterations)) {
                std::cerr << "Invalid --shm-iterations value\n";
                return 2;
            }
        } else if (arg == "--buffer-mib") {
            if (!consume_size_mib(argc, argv, i, buffer_size)) {
                std::cerr << "Invalid --buffer-mib value\n";
                return 2;
            }
        } else if (arg == "--bandwidth-mib") {
            if (!consume_size_mib(
                    argc, argv, i, bandwidth_size)) {
                std::cerr << "Invalid --bandwidth-mib value\n";
                return 2;
            }
        } else if (arg == "--csv") {
            if (i + 1 >= argc) {
                std::cerr << "Missing --csv filename\n";
                return 2;
            }
            csv_file = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            return 2;
        }
    }

    if (rounds <= 0 ||
        iterations <= 0 ||
        warmup < 0 ||
        (run_shared_memory && shm_iterations <= 0)) {
        std::cerr << "Invalid numeric configuration\n";
        return 2;
    }

    if (run_spectre) {
        SpectreV1::run_demo();
        return 0;
    }

    try {
        const auto allowed_cpus =
            PlatformUtils::allowed_cpus();

        if (allowed_cpus.empty()) {
            throw std::runtime_error(
                "Unable to determine CPUs allowed to this process");
        }

        const int primary_cpu = allowed_cpus.front();

        if (!PlatformUtils::pin_current_thread(primary_cpu) &&
            !quiet) {
            std::cerr << "[warning] CPU affinity could not be set\n";
        }

        (void)PlatformUtils::raise_priority_best_effort();

        const auto caches = CpuInfo::detect_caches();
        const auto topology = CpuInfo::topology();
        const std::size_t line_size =
            CpuInfo::cache_line_size();

        const double tsc_hz = Timer::calibrate_tsc_hz(
            Config::TSC_CALIBRATION_ROUNDS,
            Config::TSC_CALIBRATION_MS);

        const std::size_t timer_overhead =
            Timer::measure_overhead_cycles(2000);

        if (!quiet) {
            std::cout
                << "Memory Microarchitecture & Performance Analyzer\n\n";

            Reporter::print_system_info(
                CpuInfo::model_name(),
                topology,
                caches,
                tsc_hz,
                timer_overhead,
                allowed_cpus);
        }

        LatencyMeasurer latency(buffer_size, line_size);
        std::vector<MeasurementResult> latency_results;

        if (!quiet) {
            std::cout
                << "[1/4] Measuring cache and access latency...\n";
        }

        latency_results.push_back(
            latency.measure_hit(
                iterations, warmup, rounds));

        latency_results.push_back(
            latency.measure_forced_miss(
                iterations, warmup, rounds));

        latency_results.push_back(
            latency.measure_store(
                iterations, warmup, rounds));

        const std::vector<std::size_t> strides{
            64, 256, 1024, 4096, 16384
        };

        const int stride_iterations =
            std::max(1, iterations / 2);
        const int stride_warmup = warmup / 2;

        const auto stride_results =
            latency.measure_strides(
                strides,
                stride_iterations,
                stride_warmup,
                rounds);

        latency_results.insert(
            latency_results.end(),
            stride_results.begin(),
            stride_results.end());

        std::vector<BandwidthResult> bandwidth_results;

        if (run_bandwidth) {
            if (!quiet) {
                std::cout
                    << "[2/4] Measuring bandwidth...\n";
            }

            const std::vector<std::string> methods{
                "mmap", "malloc"
            };

            for (const auto& method : methods) {
                try {
                    bandwidth_results.push_back(
                        BandwidthMeasurer::measure(
                            bandwidth_size,
                            method,
                            Config::BANDWIDTH_REPEATS));
                } catch (const std::exception& ex) {
                    if (!quiet) {
                        std::cerr << "  "
                                  << method << ": "
                                  << ex.what() << '\n';
                    }
                }
            }

            try {
                bandwidth_results.push_back(
                    BandwidthMeasurer::measure(
                        bandwidth_size,
                        "LargePage",
                        Config::BANDWIDTH_REPEATS));
            } catch (const std::exception&) {
                if (!quiet) {
                    std::cout
                        << "  LargePage: unavailable; "
                           "requires pre-reserved 2 MiB Huge Pages.\n";
                }
            }
        }

        SharedMemoryResult shared_memory;

        if (run_shared_memory) {
            if (!quiet) {
                std::cout
                    << "[3/4] Measuring cross-process shared memory...\n";
            }

            if (allowed_cpus.size() < 2) {
                shared_memory.error_message =
                    "Only one CPU is available to this process";
            } else {
                const int secondary_cpu =
                    CpuInfo::choose_distinct_cpu(
                        primary_cpu, allowed_cpus);

                shared_memory =
                    SharedMemoryMeasurer::measure_cross_process(
                        shm_iterations,
                        Config::DEFAULT_SHM_WARMUP,
                        primary_cpu,
                        secondary_cpu);
            }
        }

        if (!quiet) {
            std::cout << "[4/4] Reporting...\n\n";
            Reporter::print_latency(
                latency_results, tsc_hz, verbose);
            Reporter::print_bandwidth(
                bandwidth_results);

            if (run_shared_memory) {
                Reporter::print_shared_memory(
                    shared_memory, tsc_hz, verbose);
            }
        }

        if (!csv_file.empty()) {
            if (!Reporter::write_csv(
                    csv_file,
                    latency_results,
                    bandwidth_results,
                    run_shared_memory
                        ? &shared_memory
                        : nullptr,
                    tsc_hz)) {
                std::cerr
                    << "Failed to write CSV: "
                    << csv_file << '\n';
                return 1;
            }

            (void)Reporter::append_history(
                "history.csv",
                latency_results,
                bandwidth_results);

            if (!quiet) {
                std::cout
                    << "Results written to "
                    << csv_file
                    << " (history: history.csv)\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
