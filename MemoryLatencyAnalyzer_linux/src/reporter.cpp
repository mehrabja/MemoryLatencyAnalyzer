#include "reporter.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <ctime>

void Reporter::print_console(const std::vector<MeasurementResult>& results,
                             double cpu_freq_ghz,
                             bool verbose)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n================= نتایج Latency =================\n";
    std::cout << "فرکانس تقریبی پردازنده: " << cpu_freq_ghz << " GHz\n\n";

    for (const auto& r : results) {
        double ns = r.mean_cycles / cpu_freq_ghz;
        std::cout << r.label << ":\n";
        std::cout << "  میانگین      : " << std::setw(10) << r.mean_cycles << " سیکل"
                  << "   (" << ns << " ns)\n";
        if (verbose) {
            std::cout << "  انحراف معیار : " << std::setw(10) << r.stddev_cycles << " سیکل\n";
            std::cout << "  حداقل       : " << std::setw(10) << r.min_cycles << " سیکل\n";
            std::cout << "  حداکثر       : " << std::setw(10) << r.max_cycles << " سیکل\n";
        }
        std::cout << "\n";
    }
    std::cout << "==================================================\n";
}

void Reporter::print_bandwidth(const std::vector<BandwidthResult>& results,
                               double cpu_freq_ghz)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n================= نتایج Bandwidth =================\n";

    for (const auto& r : results) {
        // تبدیل سیکل به GB/s با استفاده از سایز واقعیِ بافر اندازه‌گیری‌شده
        const double size_gb = static_cast<double>(r.size_bytes) / (1024.0 * 1024.0 * 1024.0);
        double read_gb_s  = (size_gb * cpu_freq_ghz * 1e9) / r.read_cycles;
        double write_gb_s = (size_gb * cpu_freq_ghz * 1e9) / r.write_cycles;
        double copy_gb_s  = (size_gb * cpu_freq_ghz * 1e9) / r.copy_cycles;

        std::cout << "Allocator: " << r.allocator << "\n";
        std::cout << "  Read  : " << read_gb_s  << " GB/s\n";
        std::cout << "  Write : " << write_gb_s << " GB/s\n";
        std::cout << "  Copy  : " << copy_gb_s  << " GB/s\n\n";
    }
    std::cout << "===================================================\n";
}

void Reporter::write_csv(const std::string& filename,
                         const std::vector<MeasurementResult>& results,
                         double cpu_freq_ghz)
{
    std::ofstream file(filename);
    if (!file) return;

    file << "Label,MeanCycles,StdDevCycles,MinCycles,MaxCycles,MeanNanoseconds\n";
    for (const auto& r : results) {
        double ns = r.mean_cycles / cpu_freq_ghz;
        file << "\"" << r.label << "\","
             << r.mean_cycles << ","
             << r.stddev_cycles << ","
             << r.min_cycles << ","
             << r.max_cycles << ","
             << ns << "\n";
    }
}

void Reporter::append_history(const std::string& filename,
                              const std::vector<MeasurementResult>& results)
{
    std::ofstream file(filename, std::ios::app);
    if (!file) return;

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    file << std::ctime(&t);
    for (const auto& r : results) {
        file << r.label << "," << r.mean_cycles << "\n";
    }
    file << "----\n";
}