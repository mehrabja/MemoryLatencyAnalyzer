#pragma once

#include <vector>

namespace PlatformUtils {

std::vector<int> allowed_cpus();

bool pin_current_thread(int logical_cpu);

bool raise_priority_best_effort(int nice_value = -10);

} // namespace PlatformUtils
