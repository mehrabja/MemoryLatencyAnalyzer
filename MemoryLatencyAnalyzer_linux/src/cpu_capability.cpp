#include "cpu_capability.hpp"

#include <cpuid.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>

namespace {

void cpuid(unsigned leaf, unsigned subleaf, unsigned out[4]) {
    __cpuid_count(leaf, subleaf, out[0], out[1], out[2], out[3]);
}

unsigned max_basic_leaf() {
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) return 0;
    return eax;
}

unsigned max_extended_leaf() {
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    if (!__get_cpuid(0x80000000U, &eax, &ebx, &ecx, &edx)) return 0;
    return eax;
}

std::string read_brand() {
    if (max_extended_leaf() < 0x80000004U) return "Unknown";

    char brand[49] = {};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned regs[4] = {};
        cpuid(0x80000002U + i, 0, regs);
        std::memcpy(brand + i * 16, regs, 16);
    }

    std::string value(brand);
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value.empty() ? "Unknown" : value;
}

double max_frequency_mhz() {
    std::ifstream file(
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    long long khz = 0;
    if (file >> khz && khz > 0) {
        return static_cast<double>(khz) / 1000.0;
    }

    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
        if (line.rfind("cpu MHz", 0) != 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        try {
            return std::stod(line.substr(colon + 1));
        } catch (...) {
            return 0.0;
        }
    }

    return 0.0;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
CpuComputeResult run_compute(double seconds) {
    using clock = std::chrono::steady_clock;
    volatile double f0 = 1.000001;
    volatile double f1 = 1.000003;
    volatile std::uint64_t i0 = 0x9E3779B97F4A7C15ULL;
    volatile std::uint64_t i1 = 0xD1B54A32D192ED03ULL;

    constexpr std::size_t kBatch = 8;
    constexpr double kFlopsPerBatch = 16.0;
    constexpr double kIntOpsPerBatch = 16.0;

    std::uint64_t batches = 0;
    const auto start = clock::now();
    const auto deadline =
        start + std::chrono::duration<double>(seconds);

    while (clock::now() < deadline) {
        for (std::size_t j = 0; j < kBatch; ++j) {
            f0 = f0 * f1 + 1.0000001;
            f1 = f1 * f0 + 1.0000003;
            i0 = i0 * 2862933555777941757ULL + i1;
            i1 = i1 * 3202034522624059733ULL + i0;
        }
        ++batches;
    }

    const auto end = clock::now();
    const double elapsed =
        std::chrono::duration<double>(end - start).count();

    if (elapsed <= 0.0) return {};

    const double batch_count = static_cast<double>(batches);
    return CpuComputeResult{
        elapsed,
        (batch_count * kFlopsPerBatch) / elapsed / 1e9,
        (batch_count * kIntOpsPerBatch) / elapsed / 1e9
    };
}

} // namespace

CpuCapabilityInfo CpuCapability::detect() {
    CpuCapabilityInfo info;

    unsigned regs[4] = {};
    cpuid(0, 0, regs);

    char vendor[13] = {};
    std::memcpy(vendor + 0, &regs[1], 4);
    std::memcpy(vendor + 4, &regs[3], 4);
    std::memcpy(vendor + 8, &regs[2], 4);
    info.vendor = vendor;

    info.brand = read_brand();
    info.max_frequency_mhz = max_frequency_mhz();

    if (max_basic_leaf() >= 1U) {
        cpuid(1, 0, regs);

        const unsigned base_family = (regs[0] >> 8U) & 0xFU;
        const unsigned base_model = (regs[0] >> 4U) & 0xFU;
        const unsigned ext_family = (regs[0] >> 20U) & 0xFFU;
        const unsigned ext_model = (regs[0] >> 16U) & 0xFU;

        info.family =
            base_family == 0xFU ? base_family + ext_family : base_family;
        info.model =
            (base_family == 0x6U || base_family == 0xFU)
                ? ((ext_model << 4U) | base_model)
                : base_model;
        info.stepping = regs[0] & 0xFU;

        info.sse = (regs[3] & (1U << 25U)) != 0;
        info.sse2 = (regs[3] & (1U << 26U)) != 0;
        info.sse4_2 = (regs[2] & (1U << 20U)) != 0;
        info.avx = (regs[2] & (1U << 28U)) != 0;
    }

#if defined(__GNUC__) || defined(__clang__)
    info.avx2 = __builtin_cpu_supports("avx2");
    info.avx512f = __builtin_cpu_supports("avx512f");
#endif

    return info;
}

CpuComputeResult CpuCapability::benchmark(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return {};
    }
    return run_compute(seconds);
}
