#include "cpu_info.hpp"
#include <cpuid.h>
#include <x86intrin.h>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace {
    // معادل __cpuidex ویندوز: leaf و subleaf می‌گیرد و eax/ebx/ecx/edx را در
    // یک آرایه‌ی چهارتایی برمی‌گرداند (همان قرارداد ترتیبی که کد بقیه‌ی فایل
    // انتظار دارد).
    void cpuid_count(unsigned leaf, unsigned subleaf, int out[4]) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        __cpuid_count(leaf, subleaf, a, b, c, d);
        out[0] = static_cast<int>(a);
        out[1] = static_cast<int>(b);
        out[2] = static_cast<int>(c);
        out[3] = static_cast<int>(d);
    }
}

std::vector<CacheLevelInfo> CpuInfo::detect_caches() {
    std::vector<CacheLevelInfo> result;

    // subleaf‌های leaf=4 لزوماً معادل level نیستند (مثلاً معمولاً subleaf 0=L1D,
    // 1=L1I, 2=L2, 3=L3). به همین دلیل subleaf را پشت‌سرهم افزایش می‌دهیم و
    // سطح واقعی کش را از خودِ cpuInfo[0] (بیت‌های 5-7) می‌خوانیم.
    for (int subleaf = 0; subleaf < 8; ++subleaf) {
        int cpuInfo[4] = {};
        cpuid_count(4, subleaf, cpuInfo);

        int cache_type = cpuInfo[0] & 0x1F;
        if (cache_type == 0) break; // دیگر subleaf معتبری وجود ندارد

        int actual_level = (cpuInfo[0] >> 5) & 0x7;
        if (actual_level < 1 || actual_level > 3) continue;

        // فقط کش‌های Data/Unified را نگه می‌داریم (Instruction cache برای
        // اندازه‌گیری latency دسترسی به داده مرتبط نیست)
        if (cache_type == 2) continue;

        int ways       = ((cpuInfo[1] >> 22) & 0x3FF) + 1;
        int partitions = ((cpuInfo[1] >> 12) & 0x3FF) + 1;
        int line_size  = (cpuInfo[1] & 0xFFF) + 1;
        int sets       = cpuInfo[2] + 1;

        size_t size = static_cast<size_t>(ways) * partitions * line_size * sets;

        CacheLevelInfo info;
        info.level = actual_level;
        info.size_bytes = size;
        info.line_size = line_size;
        info.type = (cache_type == 1) ? "Data" : "Unified";

        result.push_back(info);
    }

    if (result.empty()) {
        int cpuInfo[4] = {};
        cpuid_count(0x80000006, 0, cpuInfo);
        unsigned int l3_kb = (cpuInfo[2] >> 18) & 0x3FFF;
        if (l3_kb > 0 && l3_kb < 256 * 1024) {
            result.push_back({3, static_cast<size_t>(l3_kb) * 1024, 64, "Unified (approx)"});
        }
    }
    return result;
}

size_t CpuInfo::get_l3_size() {
    for (const auto& c : detect_caches()) {
        if (c.level == 3) return c.size_bytes;
    }
    return 16 * 1024 * 1024;
}

double CpuInfo::estimate_cpu_freq_ghz() {
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t tsc_start = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    uint64_t tsc_end = __rdtsc();
    auto end = std::chrono::high_resolution_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    return ((tsc_end - tsc_start) / sec) / 1e9;
}

bool CpuInfo::has_hyperthreading() {
    int cpuInfo[4] = {};
    cpuid_count(1, 0, cpuInfo);
    return (cpuInfo[3] & (1 << 28)) != 0;
}

int CpuInfo::logical_cores() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
}

int CpuInfo::physical_cores() {
    int logical = logical_cores();
    return has_hyperthreading() ? logical / 2 : logical;
}
