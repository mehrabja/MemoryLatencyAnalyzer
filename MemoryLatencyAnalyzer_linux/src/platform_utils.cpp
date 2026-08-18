#include "platform_utils.hpp"
#include <sched.h>
#include <sys/resource.h>
#include <pthread.h>

void PlatformUtils::pin_current_thread(int logical_core_index) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(logical_core_index, &cpuset);
    // best-effort: اگر سیستم به اندازه‌ی کافی هسته نداشته باشد یا اجازه
    // ندهد، این فراخوانی صرفاً شکست می‌خورد و نادیده گرفته می‌شود.
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // بالا بردن اولویت زمان‌بندی؛ بدون CAP_SYS_NICE / root معمولاً شکست
    // می‌خورد و بی‌سروصدا نادیده گرفته می‌شود (مثل نسخه‌ی ویندوز).
    setpriority(PRIO_PROCESS, 0, -10);
}
