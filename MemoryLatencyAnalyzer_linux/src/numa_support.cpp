#include "numa_support.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(MLA_HAS_NUMA) && MLA_HAS_NUMA
#include <numa.h>
#endif

namespace {

std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;

    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        if (end == begin) return {};

        const std::string token = text.substr(begin, end - begin);
        const std::size_t dash = token.find('-');

        try {
            if (dash == std::string::npos) {
                cpus.push_back(std::stoi(token));
            } else {
                const int first = std::stoi(token.substr(0, dash));
                const int last = std::stoi(token.substr(dash + 1));
                if (first > last) return {};

                for (int cpu = first; cpu <= last; ++cpu) {
                    cpus.push_back(cpu);
                }
            }
        } catch (...) {
            return {};
        }

        if (comma == std::string::npos) break;
        begin = comma + 1;
    }

    return cpus;
}

bool read_text(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    if (!file) return false;

    std::getline(file, value);
    return static_cast<bool>(file) || !value.empty();
}

bool parse_node_id(
    const std::filesystem::path& path,
    int& node_id) {
    const std::string name = path.filename().string();
    if (name.rfind("node", 0) != 0 || name.size() <= 4U) {
        return false;
    }

    try {
        node_id = std::stoi(name.substr(4));
    } catch (...) {
        return false;
    }

    return node_id >= 0;
}

} // namespace

bool NumaSupport::available() noexcept {
#if defined(MLA_HAS_NUMA) && MLA_HAS_NUMA
    return numa_available() >= 0;
#else
    return false;
#endif
}

std::vector<NumaNodeInfo> NumaSupport::nodes() {
    std::vector<NumaNodeInfo> result;

    std::error_code ec;
    const std::filesystem::path root("/sys/devices/system/node");

    if (!std::filesystem::is_directory(root, ec)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) continue;

        int node_id = -1;
        if (!parse_node_id(entry.path(), node_id)) continue;

        std::string cpulist;
        (void)read_text(entry.path() / "cpulist", cpulist);

        NumaNodeInfo node;
        node.id = node_id;
        node.cpus = parse_cpu_list(cpulist);
        result.push_back(std::move(node));
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const NumaNodeInfo& lhs, const NumaNodeInfo& rhs) {
            return lhs.id < rhs.id;
        });

    return result;
}

int NumaSupport::node_for_cpu(int cpu) noexcept {
    if (cpu < 0) return -1;

    const auto all_nodes = nodes();
    for (const auto& node : all_nodes) {
        if (std::find(node.cpus.begin(), node.cpus.end(), cpu) != node.cpus.end()) {
            return node.id;
        }
    }

    return -1;
}

int NumaSupport::distance(int from_node, int to_node) noexcept {
    if (from_node < 0 || to_node < 0) return -1;

    std::ifstream file(
        "/sys/devices/system/node/node" +
        std::to_string(from_node) +
        "/distance");

    if (!file) return -1;

    std::vector<int> values;
    int value = 0;
    while (file >> value) {
        values.push_back(value);
    }

    const auto all_nodes = nodes();
    const auto it = std::find_if(
        all_nodes.begin(),
        all_nodes.end(),
        [to_node](const NumaNodeInfo& node) {
            return node.id == to_node;
        });

    if (it == all_nodes.end()) return -1;

    const std::size_t index =
        static_cast<std::size_t>(
            std::distance(all_nodes.begin(), it));

    return index < values.size() ? values[index] : -1;
}

void* NumaSupport::allocate_on_node(
    std::size_t bytes,
    int node) noexcept {
#if defined(MLA_HAS_NUMA) && MLA_HAS_NUMA
    if (bytes == 0U || node < 0 || !available()) {
        return nullptr;
    }

    return numa_alloc_onnode(bytes, node);
#else
    (void)bytes;
    (void)node;
    return nullptr;
#endif
}

void NumaSupport::free_on_node(
    void* ptr,
    std::size_t bytes) noexcept {
#if defined(MLA_HAS_NUMA) && MLA_HAS_NUMA
    if (ptr && bytes != 0U) {
        numa_free(ptr, bytes);
    }
#else
    (void)ptr;
    (void)bytes;
#endif
}
