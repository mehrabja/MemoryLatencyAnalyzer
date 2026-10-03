#pragma once

#include <cstddef>
#include <vector>

struct NumaNodeInfo {
    int id = -1;
    std::vector<int> cpus;
};

class NumaSupport {
public:
    static bool available() noexcept;
    static std::vector<NumaNodeInfo> nodes();
    static int node_for_cpu(int cpu) noexcept;
    static int distance(int from_node, int to_node) noexcept;

    static void* allocate_on_node(std::size_t bytes, int node) noexcept;
    static void free_on_node(void* ptr, std::size_t bytes) noexcept;
};
