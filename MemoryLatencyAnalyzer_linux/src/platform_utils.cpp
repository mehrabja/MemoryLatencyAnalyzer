#include "platform_utils.hpp"

#include <sched.h>
#include <sys/resource.h>

namespace PlatformUtils {

std::vector<int> allowed_cpus() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    if (sched_getaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        return {};
    }

    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(static_cast<unsigned>(cpu), &cpuset)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

bool pin_current_thread(int logical_cpu) {
    if (logical_cpu < 0 || logical_cpu >= CPU_SETSIZE) {
        return false;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<unsigned>(logical_cpu), &cpuset);

    return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
}

bool raise_priority_best_effort(int nice_value) {
    return setpriority(PRIO_PROCESS, 0, nice_value) == 0;
}

} // namespace PlatformUtils
