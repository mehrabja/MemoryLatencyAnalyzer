#include "cpu_info.hpp"

#include <algorithm>
#include <cpuid.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <unistd.h>

namespace {

void cpuid_count(unsigned leaf, unsigned subleaf, unsigned out[4]) {
    __cpuid_count(leaf, subleaf, out[0], out[1], out[2], out[3]);
}

unsigned max_basic_leaf() {
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;

    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        return 0;
    }

    return eax;
}

std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;
    std::stringstream ss(text);
    std::string item;

    while (std::getline(ss, item, ',')) {
        const auto dash = item.find('-');

        try {
            if (dash == std::string::npos) {
                cpus.push_back(std::stoi(item));
                continue;
            }

            const int begin = std::stoi(item.substr(0, dash));
            const int end = std::stoi(item.substr(dash + 1));
            if (begin > end) return {};

            for (int cpu = begin; cpu <= end; ++cpu) {
                cpus.push_back(cpu);
            }
        } catch (...) {
            return {};
        }
    }

    return cpus;
}

bool read_int_file(const std::string& path, int& value) {
    std::ifstream file(path);
    if (!file) return false;
    file >> value;
    return static_cast<bool>(file);
}

std::pair<int, int> cpu_topology_pair(int cpu) {
    int package_id = 0;
    int core_id = cpu;

    (void)read_int_file(
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
            "/topology/physical_package_id",
        package_id);

    (void)read_int_file(
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
            "/topology/core_id",
        core_id);

    return {package_id, core_id};
}

} // namespace

std::string CpuInfo::model_name() {
    std::ifstream file("/proc/cpuinfo");
    std::string line;

    while (std::getline(file, line)) {
        if (line.rfind("model name", 0) != 0) continue;

        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        return value;
    }

    return "Unknown x86 CPU";
}

std::vector<CacheLevelInfo> CpuInfo::detect_caches() {
    std::vector<CacheLevelInfo> result;
    if (max_basic_leaf() < 4) return result;

    for (unsigned subleaf = 0; subleaf < 32; ++subleaf) {
        unsigned info[4] = {};
        cpuid_count(4, subleaf, info);

        const unsigned cache_type = info[0] & 0x1F;
        if (cache_type == 0) break;

        const int level = static_cast<int>((info[0] >> 5) & 0x7);
        if (level < 1 || level > 3) continue;

        // 1=data, 2=instruction, 3=unified.
        if (cache_type == 2) continue;

        const std::size_t line_size =
            static_cast<std::size_t>((info[1] & 0xFFF) + 1U);
        const std::size_t partitions =
            static_cast<std::size_t>(((info[1] >> 12) & 0x3FF) + 1U);
        const std::size_t ways =
            static_cast<std::size_t>(((info[1] >> 22) & 0x3FF) + 1U);
        const std::size_t sets =
            static_cast<std::size_t>(info[2]) + 1U;

        result.push_back(CacheLevelInfo{
            level,
            line_size * partitions * ways * sets,
            line_size,
            cache_type == 1 ? "Data" : "Unified"
        });
    }

    std::sort(
        result.begin(), result.end(),
        [](const CacheLevelInfo& a, const CacheLevelInfo& b) {
            if (a.level != b.level) return a.level < b.level;
            return a.type < b.type;
        });

    return result;
}

std::size_t CpuInfo::cache_line_size() {
    const auto caches = detect_caches();
    for (const auto& cache : caches) {
        if (cache.line_size != 0) return cache.line_size;
    }
    return 64;
}

CpuTopologyInfo CpuInfo::topology() {
    CpuTopologyInfo info;

    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    info.logical_cpus = online > 0 ? static_cast<int>(online) : 1;

    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        info.smt_capable = (edx & (1U << 28)) != 0;
    }

    std::ifstream online_file("/sys/devices/system/cpu/online");
    std::set<std::pair<int, int>> cores;
    std::set<int> packages;

    if (online_file) {
        std::string text;
        std::getline(online_file, text);

        for (const int cpu : parse_cpu_list(text)) {
            const auto [package_id, core_id] = cpu_topology_pair(cpu);
            cores.insert({package_id, core_id});
            packages.insert(package_id);
        }
    }

    if (!cores.empty()) {
        info.physical_cores = static_cast<int>(cores.size());
        info.packages = static_cast<int>(packages.size());
    } else {
        info.physical_cores = info.logical_cpus;
        info.packages = 1;
    }

    info.smt_active = info.logical_cpus > info.physical_cores;
    return info;
}

bool CpuInfo::has_hyperthreading() {
    return topology().smt_active;
}

int CpuInfo::logical_cores() {
    return topology().logical_cpus;
}

int CpuInfo::physical_cores() {
    return topology().physical_cores;
}

int CpuInfo::choose_distinct_cpu(
    int primary_cpu,
    const std::vector<int>& allowed_cpus) {
    if (allowed_cpus.size() < 2) return primary_cpu;

    const auto primary = cpu_topology_pair(primary_cpu);

    for (const int cpu : allowed_cpus) {
        if (cpu == primary_cpu) continue;
        if (cpu_topology_pair(cpu) != primary) return cpu;
    }

    return allowed_cpus[1];
}
