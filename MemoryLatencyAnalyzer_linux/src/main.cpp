#include "bandwidth_measurer.hpp"
#include "cpu_info.hpp"
#include "cpu_capability.hpp"
#include "defensive_lab.hpp"
#include "evaluation_lab.hpp"
#include "operational_lab.hpp"
#include "latency_measurer.hpp"
#include "platform_utils.hpp"
#include "pmu_counters.hpp"
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
        << "  --verbose              Include p95/p99/stddev/min/max\n"
        << "  --pmu                  Enable Linux perf_event_open PMU counters\n\n"        << "  --spectre              Run the self-contained Spectre V1 demo and exit\n"
        << "  --spectre-tries N      Attempts per leaked byte (default 999)\n"
        << "  --spectre-lab          Repeat the self-contained Spectre demo for reliability analysis\n"
        << "  --lab-runs N           Independent Spectre lab runs (default 5)\n"
        << "  --phase5-lab           Run the Phase 5 defensive lab and exit\n"
        << "  --phase5-runs N        Independent Phase 5 lab runs (default 5)\n"
        << "  --cpu-capability       Detect CPU and run compute-capacity benchmark\n"
        << "  --compute-seconds N    CPU benchmark duration in seconds (default 1)\n"
        << "  --evaluation-lab       Run stability, SNR, TVLA and control validation lab\n"
        << "  --evaluation-runs N    Evaluation runs (default 3)\n"
        << "  --trace-samples N      Timing samples per class (default 2048)\n"
        << "  --trace-average N      Samples averaged per block (default 4)\n"
        << "  --evaluation-report F  Write evaluation JSON report (default evaluation_lab_report.json)\n"
        << "  --operational-lab      Run the end-to-end defensive simulation lab\n"
        << "  --operational-runs N   Operational lab runs (default 3)\n"
        << "  --operational-report F Write operational JSON report (default operational_lab_report.json)\n"
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
    int spectre_tries = 999;
    int spectre_lab_runs = 5;
    int phase5_runs = 5;
    int compute_seconds = 1;
    int evaluation_runs = 3;
    int trace_samples = 2048;
    int trace_average = 4;
    int operational_runs = 3;

    std::size_t buffer_size = 0;
    std::size_t bandwidth_size =
        Config::DEFAULT_BANDWIDTH_SIZE;

    bool run_bandwidth = true;
    bool run_shared_memory = true;
    bool run_spectre = false;
    bool run_spectre_lab = false;
    bool run_phase5_lab = false;
    bool run_cpu_capability = false;
    bool run_evaluation_lab = false;
    bool run_operational_lab = false;
    bool quiet = false;
    bool verbose = false;
    bool enable_pmu = false;
    std::string csv_file;
    std::string evaluation_report = "evaluation_lab_report.json";
    std::string operational_report = "operational_lab_report.json";

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
        } else if (arg == "--pmu") {
            enable_pmu = true;
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
        } else if (arg == "--spectre-lab") {
            run_spectre_lab = true;
        } else if (arg == "--lab-runs") {
            if (!consume_int(argc, argv, i, spectre_lab_runs) || spectre_lab_runs <= 0) {
                std::cerr << "Invalid --lab-runs value\n";
                return 2;
            }
        } else if (arg == "--phase5-lab") {
            run_phase5_lab = true;
        } else if (arg == "--phase5-runs") {
            if (!consume_int(argc, argv, i, phase5_runs) || phase5_runs <= 0) {
                std::cerr << "Invalid --phase5-runs value\n";
                return 2;
            }
        } else if (arg == "--cpu-capability") {
            run_cpu_capability = true;
        } else if (arg == "--compute-seconds") {
            if (!consume_int(argc, argv, i, compute_seconds) || compute_seconds <= 0) {
                std::cerr << "Invalid --compute-seconds value\n";
                return 2;
            }
        } else if (arg == "--evaluation-lab") {
            run_evaluation_lab = true;
        } else if (arg == "--evaluation-runs") {
            if (!consume_int(argc, argv, i, evaluation_runs) || evaluation_runs <= 0) {
                std::cerr << "Invalid --evaluation-runs value\n";
                return 2;
            }
        } else if (arg == "--trace-samples") {
            if (!consume_int(argc, argv, i, trace_samples) || trace_samples <= 0) {
                std::cerr << "Invalid --trace-samples value\n";
                return 2;
            }
        } else if (arg == "--trace-average") {
            if (!consume_int(argc, argv, i, trace_average) || trace_average <= 0) {
                std::cerr << "Invalid --trace-average value\n";
                return 2;
            }
        } else if (arg == "--evaluation-report") {
            if (i + 1 >= argc) {
                std::cerr << "Missing --evaluation-report filename\n";
                return 2;
            }
            evaluation_report = argv[++i];
        } else if (arg == "--operational-lab") {
            run_operational_lab = true;
        } else if (arg == "--operational-runs") {
            if (!consume_int(argc, argv, i, operational_runs) || operational_runs <= 0) {
                std::cerr << "Invalid --operational-runs value\n";
                return 2;
            }
        } else if (arg == "--operational-report") {
            if (i + 1 >= argc) {
                std::cerr << "Missing --operational-report filename\n";
                return 2;
            }
            operational_report = argv[++i];
        } else if (arg == "--spectre-tries") {
            if (!consume_int(argc, argv, i, spectre_tries) || spectre_tries <= 0) {
                std::cerr << "Invalid --spectre-tries value\n";
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
        (run_shared_memory && shm_iterations <= 0) ||
        spectre_tries <= 0 ||
        (run_spectre_lab && spectre_lab_runs <= 0) ||
        (run_phase5_lab && phase5_runs <= 0) ||
        (run_cpu_capability && compute_seconds <= 0) ||
        (run_evaluation_lab && (evaluation_runs <= 0 || trace_samples <= 0 || trace_average <= 0)) ||
        (run_operational_lab && operational_runs <= 0)) {
        std::cerr << "Invalid numeric configuration\n";
        return 2;
    }

    if (run_operational_lab) {
        const auto lab =
            OperationalLab::run(
                operational_runs,
                operational_report);

        std::cout
            << "Operational Defensive Lab\n"
            << "Runs: " << lab.runs << '\n'
            << "Events generated: " << lab.events_generated << '\n'
            << "Detection rate: " << lab.detection_rate_percent << "%\n"
            << "Missed events: " << lab.missed_events << '\n'
            << "False-positive rate: "
            << lab.false_positive_rate_percent << "%\n"
            << "Recovery rate: " << lab.recovery_rate_percent << "%\n"
            << "Boundary: " << lab.reference_boundary << '\n'
            << "Real network: NOT PERFORMED\n"
            << "Real persistence: NOT PERFORMED\n"
            << "Real privilege change: NOT PERFORMED\n"
            << "External process access: NOT PERFORMED\n"
            << "Report: "
            << (lab.report_written
                    ? lab.report_path
                    : "FAILED")
            << '\n';

        return lab.report_written ? 0 : 1;
    }

    if (run_evaluation_lab) {
        const auto evaluation =
            EvaluationLab::run(
                evaluation_runs,
                trace_samples,
                static_cast<std::size_t>(trace_average),
                evaluation_report);

        std::cout
            << "Evaluation & Validation Lab\n"
            << "Aligned samples: " << evaluation.aligned_samples
            << " | alignment loss: " << evaluation.alignment_loss_percent << "%\n"
            << "Fixed mean: " << evaluation.fixed_metrics.mean
            << " | Random mean: " << evaluation.random_metrics.mean << '\n'
            << "Outliers rejected: "
            << evaluation.fixed_metrics.rejected_count
            << " / "
            << evaluation.random_metrics.rejected_count << '\n'
            << "SNR: " << evaluation.snr_linear
            << " (" << evaluation.snr_db << " dB)\n"
            << "Classification error: "
            << evaluation.classification_error_percent << "%\n"
            << "Capture recovery: "
            << evaluation.recovered_captures << "/"
            << evaluation.simulated_capture_faults << " ("
            << evaluation.recovery_rate_percent << "%)\n"
            << "TVLA |t|: " << evaluation.tvla.abs_t
            << " (threshold "
            << evaluation.tvla.threshold << ")"
            << (evaluation.leakage_detected
                    ? " LEAKAGE-SIGNAL-DETECTED"
                    : " no-threshold-crossing")
            << '\n'
            << "Repeatability CV: "
            << evaluation.repeatability_cv_percent << "% "
            << (evaluation.repeatability_ok ? "OK" : "OUTSIDE_CRITERIA")
            << '\n'
            << "Platform: " << evaluation.platform.brand
            << " | " << evaluation.platform.os_release
            << " | " << evaluation.platform.machine << '\n'
            << "Control contracts evaluated: "
            << evaluation.controls.size() << " (synthetic; not live EDR/DLP testing)\n"
            << "Report: "
            << (evaluation.report_written
                    ? evaluation.report_path
                    : "FAILED")
            << '\n';

        return evaluation.report_written ? 0 : 1;
    }

    if (run_cpu_capability) {
        const auto capability = CpuCapability::detect();
        const auto topology = CpuInfo::topology();
        const auto compute =
            CpuCapability::benchmark(
                static_cast<double>(compute_seconds));

        std::cout << "CPU Capability\n"
                  << "Vendor: " << capability.vendor << '\n'
                  << "Model: " << capability.brand << '\n'
                  << "Family/Model/Stepping: "
                  << capability.family << "/"
                  << capability.model << "/"
                  << capability.stepping << '\n'
                  << "Cores/Threads: "
                  << topology.physical_cores << "/"
                  << topology.logical_cpus << '\n'
                  << "Packages: " << topology.packages << '\n'
                  << "SMT: "
                  << (topology.smt_active ? "active" : "inactive")
                  << '\n'
                  << "Max frequency: ";

        if (capability.max_frequency_mhz > 0.0) {
            std::cout << capability.max_frequency_mhz << " MHz\n";
        } else {
            std::cout << "unavailable\n";
        }

        std::cout
            << "ISA: "
            << "SSE=" << (capability.sse ? "yes" : "no")
            << " SSE2=" << (capability.sse2 ? "yes" : "no")
            << " SSE4.2=" << (capability.sse4_2 ? "yes" : "no")
            << " AVX=" << (capability.avx ? "yes" : "no")
            << " AVX2=" << (capability.avx2 ? "yes" : "no")
            << " AVX-512F=" << (capability.avx512f ? "yes" : "no")
            << '\n'
            << "Compute benchmark: " << compute.seconds << " s\n"
            << "  FP throughput: "
            << compute.gflops << " GFLOP/s\n"
            << "  Integer throughput: "
            << compute.gintops << " GIntOps/s\n"
            << "\nNote: these are measured single-thread throughput values, not the CPU vendor's theoretical maximum.\n";

        return 0;
    }

    if (run_phase5_lab) {
        const auto lab =
            DefensiveLab::run(
                phase5_runs,
                spectre_tries);

        std::cout
            << "Phase 5 Defensive Lab\n"
            << "Runs: " << lab.runs
            << " | tries/byte: " << lab.tries_per_byte << '\n'
            << "Byte accuracy: "
            << lab.byte_accuracy_percent << "%\n"
            << "Exact recovery: "
            << lab.exact_recovery_rate_percent << "%\n"
            << "Simulated alerts: "
            << lab.simulated_alerts << '\n'
            << "Confirmed alerts: "
            << lab.simulated_confirmed_alerts << '\n'
            << "False positives: "
            << lab.simulated_false_positives << '\n'
            << "Network access: NOT PERFORMED\n"
            << "Persistence: NOT PERFORMED\n"
            << "External process access: NOT PERFORMED\n"
            << "Report: "
            << (lab.report_written
                    ? lab.report_path
                    : "FAILED")
            << '\n';

        return lab.report_written ? 0 : 1;
    }

    if (run_spectre_lab) {
        const auto lab =
            SpectreV1::run_reliability_lab(
                spectre_lab_runs,
                spectre_tries);

        const double byte_accuracy =
            lab.total_bytes == 0
                ? 0.0
                : (100.0 * static_cast<double>(lab.correct_bytes) /
                   static_cast<double>(lab.total_bytes));

        const double exact_rate =
            lab.runs == 0
                ? 0.0
                : (100.0 * static_cast<double>(lab.exact_matches) /
                   static_cast<double>(lab.runs));

        std::cout
            << "Spectre V1 Reliability Lab\n"
            << "Runs: " << lab.runs
            << " | tries/byte: " << lab.tries_per_byte << '\n'
            << "Exact recovery: "
            << exact_rate << "%\n"
            << "Byte accuracy: "
            << byte_accuracy << "%\n";

        if (!lab.per_byte_correct_runs.empty()) {
            std::cout << "Stable bytes (correct in every run): ";
            bool first = true;
            for (std::size_t i = 0;
                 i < lab.per_byte_correct_runs.size();
                 ++i) {
                if (lab.per_byte_correct_runs[i] == lab.runs) {
                    if (!first) std::cout << ',';
                    std::cout << i;
                    first = false;
                }
            }
            std::cout << '\n';
        }

        return 0;
    }

    if (run_spectre) {
        SpectreV1::run_demo(spectre_tries);
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

        std::unique_ptr<PmuCounters> pmu;
        if (enable_pmu) {
            std::string pmu_error;
            pmu = PmuCounters::create(pmu_error);
            if (!pmu) {
                std::cerr
                    << "[warning] PMU unavailable: "
                    << pmu_error << '\n';
            }
        }

        LatencyMeasurer latency(buffer_size, line_size);
        std::vector<MeasurementResult> latency_results;

        if (!quiet) {
            std::cout
                << "[1/4] Measuring cache and access latency...\n";
        }

        latency_results.push_back(
            latency.measure_hit(
                iterations, warmup, rounds, pmu.get()));

        latency_results.push_back(
            latency.measure_forced_miss(
                iterations, warmup, rounds, pmu.get()));

        latency_results.push_back(
            latency.measure_store(
                iterations, warmup, rounds, pmu.get()));

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
                rounds,
                pmu.get());

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
                            Config::BANDWIDTH_REPEATS,
                            pmu.get()));
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
                        Config::BANDWIDTH_REPEATS,
                        pmu.get()));
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
