#include "numa_support.hpp"

#include <cassert>
#include <cstddef>

int main() {
    if (!NumaSupport::available()) {
        return 77;
    }

    const auto nodes = NumaSupport::nodes();
    if (nodes.empty()) {
        return 77;
    }

    for (const auto& node : nodes) {
        if (!node.cpus.empty()) {
            assert(NumaSupport::node_for_cpu(node.cpus.front()) == node.id);
            const int self_distance = NumaSupport::distance(node.id, node.id);
            assert(self_distance >= 0);

            constexpr std::size_t bytes = 64U * 1024U;
            void* ptr = NumaSupport::allocate_on_node(bytes, node.id);
            if (!ptr) {
                return 77;
            }
            NumaSupport::free_on_node(ptr, bytes);
            return 0;
        }
    }

    return 77;
}
