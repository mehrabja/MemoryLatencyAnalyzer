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

    double f0 = 1.000001;
    double f1 = 1.000003;
    double f2 = 1.000005;
    double f3 = 1.000007;
    double f4 = 1.000009;
    double f5 = 1.000011;
    double f6 = 1.000013;
    double f7 = 1.000015;

    std::uint64_t i0 = 0x9E3779B97F4A7C15ULL;
    std::uint64_t i1 = 0xD1B54A32D192ED03ULL;
    std::uint64_t i2 = 0x94D049BB133111EBULL;
    std::uint64_t i3 = 0xBF58476D1CE4E5B9ULL;
    std::uint64_t i4 = 0x369DEA0F31A53F85ULL;
    std::uint64_t i5 = 0x2545F4914F6CDD1DULL;
    std::uint64_t i6 = 0xD2B74407B1CE6E93ULL;
    std::uint64_t i7 = 0xA4093822299F31D0ULL;

    constexpr std::size_t kRounds = 4;
    constexpr double kFlopsPerRound = 16.0;
    constexpr double kIntOpsPerRound = 16.0;

    std::uint64_t rounds = 0;
    const auto start = clock::now();
    const auto deadline =
        start + std::chrono::duration<double>(seconds);

    while (clock::now() < deadline) {
        for (std::size_t j = 0; j < kRounds; ++j) {
            f0 = f0 * 1.0000001 + 0.0000003;
            f1 = f1 * 1.0000002 + 0.0000005;
            f2 = f2 * 1.0000003 + 0.0000007;
            f3 = f3 * 1.0000004 + 0.0000009;
            f4 = f4 * 1.0000005 + 0.0000011;
            f5 = f5 * 1.0000006 + 0.0000013;
            f6 = f6 * 1.0000007 + 0.0000015;
            f7 = f7 * 1.0000008 + 0.0000017;

            i0 = i0 * 2862933555777941757ULL + i1;
            i1 = i1 * 3202034522624059733ULL + i2;
            i2 = i2 * 3935559000370003845ULL + i3;
            i3 = i3 * 2691343689449507681ULL + i4;
            i4 = i4 * 11400714819323198485ULL + i5;
            i5 = i5 * 7046029254386353131ULL + i6;
            i6 = i6 * 6364136223846793005ULL + i7;
            i7 = i7 * 1442695040888963407ULL + i0;
        }
        rounds += static_cast<std::uint64_t>(kRounds);
    }

    volatile double fp_sink = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7;
    volatile std::uint64_t int_sink =
        i0 ^ i1 ^ i2 ^ i3 ^ i4 ^ i5 ^ i6 ^ i7;
    (void)fp_sink;
    (void)int_sink;

    const auto end = clock::now();
    const double elapsed =
        std::chrono::duration<double>(end - start).count();

    if (elapsed <= 0.0) return {};

    const double round_count = static_cast<double>(rounds);
    return CpuComputeResult{
        elapsed,
        (round_count * kFlopsPerRound) / elapsed / 1e9,
        (round_count * kIntOpsPerRound) / elapsed / 1e9
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
