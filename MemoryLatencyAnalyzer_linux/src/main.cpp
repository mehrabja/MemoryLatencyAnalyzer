#include "latency_measurer.hpp"
#include "bandwidth_measurer.hpp"
#include "cpu_info.hpp"
#include "reporter.hpp"
#include "settings.hpp"
#include "shared_memory_measurer.hpp"
#include "platform_utils.hpp"
#include "spectre_v1.hpp"   // <---- در بالای فایل، کنار سایر includeها
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

void print_help() {
    std::cout << "Usage: latency_analyzer [options]\n"
              << "  --rounds N         Number of full rounds (default 5)\n"
              << "  --iterations N     Iterations per measurement (default 1000)\n"
              << "  --warmup N         Warmup iterations (default 150)\n"
              << "  --shm-iterations N Round-trips for the shared-memory IPC test (default 2000)\n"
              << "  --no-shared-memory Skip the cross-process shared memory test\n"
              << "  --csv FILE         Write results to CSV file\n"
              << "  --quiet            Less output\n"
              << "  --verbose          More detailed output\n"              << "  --spectre          Run Spectre Variant 1 demo and exit\n"              << "  --help             Show this help\n";
}

int main(int argc, char* argv[]) {
    // نقش مخفیِ consumer: این پروسه توسط خودِ برنامه (به‌عنوان producer)
    // اجرا می‌شود تا برای تست حافظه‌ی مشترک بین دو پروسه استفاده شود.
    // کاربر معمولی هرگز این فلگ را دستی نمی‌زند.
    if (argc >= 3 && strcmp(argv[1], "--shm-consumer") == 0) {
        return SharedMemoryMeasurer::run_consumer(argv[2]);
    }

    PlatformUtils::pin_current_thread(0); // پین به هسته‌ی منطقی 0 + تلاش برای اولویت بالا

    int rounds      = Config::DEFAULT_ROUNDS;
    int iterations  = Config::DEFAULT_ITERATIONS;
    int warmup      = Config::DEFAULT_WARMUP;
    int shm_iterations = Config::DEFAULT_SHM_ITERATIONS;
    bool run_shared_memory = true;
    bool run_spectre = false;
    bool verbose    = false;
    bool quiet      = false;
    std::string csv_file;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc)
            rounds = atoi(argv[++i]);
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
            iterations = atoi(argv[++i]);
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
            warmup = atoi(argv[++i]);
        else if (strcmp(argv[i], "--shm-iterations") == 0 && i + 1 < argc)
            shm_iterations = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-shared-memory") == 0)
            run_shared_memory = false;
        else if (strcmp(argv[i], "--spectre") == 0)
            run_spectre = true;
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            csv_file = argv[++i];
        else if (strcmp(argv[i], "--verbose") == 0)
            verbose = true;
        else if (strcmp(argv[i], "--quiet") == 0)
            quiet = true;
    }

    if (rounds < 1) {
        std::cerr << "Error: --rounds باید حداقل 1 باشد.\n";
        return 1;
    }
    if (iterations < 1) {
        std::cerr << "Error: --iterations باید حداقل 1 باشد.\n";
        return 1;
    }
    if (warmup < 0) {
        std::cerr << "Error: --warmup نمی‌تواند منفی باشد.\n";
        return 1;
    }
    if (run_shared_memory && shm_iterations < 1) {
        std::cerr << "Error: --shm-iterations باید حداقل 1 باشد.\n";
        return 1;
    }

    try {
        if (run_spectre) {
            SpectreV1::run_demo(999);
            return 0;
        }

        if (!quiet) {
            std::cout << "=============================================\n";
            std::cout << "   Memory Latency & Bandwidth Analyzer\n";
            std::cout << "   (Pure Performance Measurement)\n";
            std::cout << "=============================================\n\n";
        }

        // اطلاعات کش
        auto caches = CpuInfo::detect_caches();
        if (!quiet) {
            std::cout << "Cache hierarchy:\n";
            for (const auto& c : caches) {
                std::cout << "  L" << c.level << " (" << c.type << "): "
                          << (c.size_bytes / 1024.0) << " KB, line "
                          << c.line_size << " B\n";
            }
            std::cout << "\n";
        }

        // هشدار Hyper-Threading
        if (CpuInfo::has_hyperthreading() && !quiet) {
            std::cout << "[!] Hyper-Threading فعال است.\n"
                      << "    برای دقت بیشتر affinity روی یک هسته تنظیم شده.\n\n";
        }

        double freq = CpuInfo::estimate_cpu_freq_ghz();
        if (!quiet) {
            std::cout << "Estimated CPU frequency: " << freq << " GHz\n\n";
        }

        LatencyMeasurer measurer;

        std::vector<MeasurementResult> all_results;
        std::vector<MeasurementResult> hit_rounds;
        std::vector<MeasurementResult> miss_rounds;

        // چند دور latency
        for (int r = 0; r < rounds; ++r) {
            if (!quiet) std::cout << "Round " << (r + 1) << "/" << rounds << " ...\n";

            hit_rounds.push_back(measurer.measure_hit(iterations, warmup));
            miss_rounds.push_back(measurer.measure_miss(iterations, warmup, Config::DEFAULT_STRIDE));
        }

        auto average_rounds = [](const std::vector<MeasurementResult>& rounds, const std::string& label) {
            MeasurementResult avg{};
            avg.label = label;
            for (const auto& r : rounds) {
                avg.mean_cycles   += r.mean_cycles;
                avg.stddev_cycles += r.stddev_cycles;
                avg.min_cycles    += r.min_cycles;
                avg.max_cycles    += r.max_cycles;
            }
            size_t n = rounds.size();
            avg.mean_cycles   /= n;
            avg.stddev_cycles /= n;
            avg.min_cycles    /= static_cast<uint64_t>(n);
            avg.max_cycles    /= static_cast<uint64_t>(n);
            return avg;
        };

        all_results.push_back(average_rounds(hit_rounds, "Cache Hit (averaged)"));
        all_results.push_back(average_rounds(miss_rounds, "Cache Miss (averaged)"));

        // Load vs Store
        all_results.push_back(measurer.measure_load(iterations, warmup));
        all_results.push_back(measurer.measure_store(iterations, warmup));

        // Strideهای مختلف
        std::vector<size_t> strides = {64, 256, 1024, 4096, 16384};
        auto stride_results = measurer.measure_strides(strides, iterations / 2, warmup / 2);
        all_results.insert(all_results.end(), stride_results.begin(), stride_results.end());

        // Bandwidth
        if (!quiet) std::cout << "\n[+] Measuring bandwidth...\n";
        std::vector<BandwidthResult> bw_results;

        try {
            bw_results.push_back(BandwidthMeasurer::measure(64 * 1024 * 1024, "mmap"));
            bw_results.push_back(BandwidthMeasurer::measure(64 * 1024 * 1024, "malloc"));

            try {
                bw_results.push_back(BandwidthMeasurer::measure(64 * 1024 * 1024, "LargePage"));
            } catch (...) {
                if (!quiet) {
                    std::cout << "  Large Pages در دسترس نیست (نیاز به رزرو hugepage در سیستم، مثلاً:"
                              << " echo 64 | sudo tee /proc/sys/vm/nr_hugepages)\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Bandwidth error: " << e.what() << "\n";
        }

        // حافظه‌ی مشترک بین دو پروسه (Shared Memory / cross-process IPC)
        if (run_shared_memory) {
            if (!quiet) std::cout << "\n[+] Measuring cross-process shared memory latency...\n";
            SharedMemoryResult shm = SharedMemoryMeasurer::measure_cross_process(shm_iterations, Config::DEFAULT_SHM_WARMUP);
            if (shm.success) {
                MeasurementResult shm_as_result{
                    shm.mean_cycles,
                    shm.stddev_cycles,
                    shm.min_cycles,
                    shm.max_cycles,
                    "Shared Memory Round-Trip (2 processes, " + std::to_string(shm.iterations) + " ping-pongs)"
                };
                all_results.push_back(shm_as_result);
            } else if (!quiet) {
                std::cout << "  تست حافظه‌ی مشترک ناموفق بود: " << shm.error_message << "\n";
            }
        }

        // چاپ نتایج
        Reporter::print_console(all_results, freq, verbose);
        Reporter::print_bandwidth(bw_results, freq);

        // ذخیره فایل
        if (!csv_file.empty()) {
            Reporter::write_csv(csv_file, all_results, freq);
            Reporter::append_history("history.csv", all_results);
            if (!quiet) {
                std::cout << "\nنتایج در فایل ذخیره شد: " << csv_file << "\n";
                std::cout << "تاریخچه در فایل history.csv ذخیره شد.\n";
            }
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}