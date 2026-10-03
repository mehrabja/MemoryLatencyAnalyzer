#include "pmu_counters.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/perf_event.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace {

int perf_event_open(
    struct perf_event_attr* attr,
    pid_t pid,
    int cpu,
    int group_fd,
    unsigned long flags) {
    return static_cast<int>(
        syscall(
            __NR_perf_event_open,
            attr,
            pid,
            cpu,
            group_fd,
            flags));
}

std::string errno_message(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

bool is_cache_event_valid(
    std::uint64_t cache,
    std::uint64_t operation,
    std::uint64_t result) {
    return cache <= PERF_COUNT_HW_CACHE_MAX &&
           operation <= PERF_COUNT_HW_CACHE_OP_MAX &&
           result <= PERF_COUNT_HW_CACHE_RESULT_MAX;
}

} // namespace

std::uint64_t PmuCounters::cache_config(
    std::uint64_t cache,
    std::uint64_t operation,
    std::uint64_t result) noexcept {
    return cache |
           (operation << 8U) |
           (result << 16U);
}

std::unique_ptr<PmuCounters> PmuCounters::create(
    std::string& error) {
    auto counters = std::unique_ptr<PmuCounters>(
        new PmuCounters());

    if (!counters->open_events()) {
        error = counters->last_error();
        return nullptr;
    }

    return counters;
}

PmuCounters::~PmuCounters() {
    close_events();
}

bool PmuCounters::open_event(
    const std::string& name,
    std::uint32_t type,
    std::uint64_t config,
    int group_fd) {
    perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = type;
    attr.config = config;
    attr.disabled = group_fd < 0 ? 1U : 0U;
    attr.exclude_kernel = 1U;
    attr.exclude_hv = 1U;
    attr.read_format =
        PERF_FORMAT_GROUP |
        PERF_FORMAT_ID |
        PERF_FORMAT_TOTAL_TIME_ENABLED |
        PERF_FORMAT_TOTAL_TIME_RUNNING;

    const int fd =
        perf_event_open(&attr, 0, -1, group_fd, 0);
    if (fd < 0) {
        return false;
    }

    std::uint64_t id = 0;
    if (ioctl(fd, PERF_EVENT_IOC_ID, &id) != 0) {
        close(fd);
        last_error_ = errno_message("PERF_EVENT_IOC_ID");
        return false;
    }

    events_.push_back(Event{name, fd, id});
    if (group_fd < 0) {
        leader_fd_ = fd;
    }
    return true;
}

bool PmuCounters::open_events() {
    if (!open_event(
            "cpu-cycles",
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            -1)) {
        last_error_ =
            errno_message(
                "perf_event_open(cpu-cycles)");
        return false;
    }

    struct EventSpec {
        const char* name;
        std::uint32_t type;
        std::uint64_t config;
    };

    const std::vector<EventSpec> optional_events{
        {
            "instructions-retired",
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_INSTRUCTIONS
        },
        {
            "L1D-load-misses",
            PERF_TYPE_HW_CACHE,
            is_cache_event_valid(
                PERF_COUNT_HW_CACHE_L1D,
                PERF_COUNT_HW_CACHE_OP_READ,
                PERF_COUNT_HW_CACHE_RESULT_MISS)
                ? cache_config(
                      PERF_COUNT_HW_CACHE_L1D,
                      PERF_COUNT_HW_CACHE_OP_READ,
                      PERF_COUNT_HW_CACHE_RESULT_MISS)
                : 0U
        },
        {
            "LLC-loads",
            PERF_TYPE_HW_CACHE,
            is_cache_event_valid(
                PERF_COUNT_HW_CACHE_LL,
                PERF_COUNT_HW_CACHE_OP_READ,
                PERF_COUNT_HW_CACHE_RESULT_ACCESS)
                ? cache_config(
                      PERF_COUNT_HW_CACHE_LL,
                      PERF_COUNT_HW_CACHE_OP_READ,
                      PERF_COUNT_HW_CACHE_RESULT_ACCESS)
                : 0U
        },
        {
            "LLC-load-misses",
            PERF_TYPE_HW_CACHE,
            is_cache_event_valid(
                PERF_COUNT_HW_CACHE_LL,
                PERF_COUNT_HW_CACHE_OP_READ,
                PERF_COUNT_HW_CACHE_RESULT_MISS)
                ? cache_config(
                      PERF_COUNT_HW_CACHE_LL,
                      PERF_COUNT_HW_CACHE_OP_READ,
                      PERF_COUNT_HW_CACHE_RESULT_MISS)
                : 0U
        },
        {
            "stalled-cycles-backend",
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_BACKEND
        }
    };

    for (const auto& spec : optional_events) {
        if (spec.config == 0U) continue;

        if (!open_event(
                spec.name,
                spec.type,
                spec.config,
                leader_fd_)) {
            // Individual generic/cache mappings vary by CPU. Keep the
            // working events instead of failing the complete PMU feature.
            last_error_ =
                errno_message(
                    ("perf_event_open(" +
                     std::string(spec.name) +
                     ")").c_str());
        }
    }

    return !events_.empty();
}

bool PmuCounters::start() {
    if (leader_fd_ < 0) {
        last_error_ = "PMU group is not open";
        return false;
    }

    if (ioctl(
            leader_fd_,
            PERF_EVENT_IOC_RESET,
            PERF_IOC_FLAG_GROUP) != 0) {
        last_error_ = errno_message(
            "PERF_EVENT_IOC_RESET");
        return false;
    }

    if (ioctl(
            leader_fd_,
            PERF_EVENT_IOC_ENABLE,
            PERF_IOC_FLAG_GROUP) != 0) {
        last_error_ = errno_message(
            "PERF_EVENT_IOC_ENABLE");
        return false;
    }

    running_ = true;
    return true;
}

bool PmuCounters::stop() {
    if (leader_fd_ < 0 || !running_) return true;

    if (ioctl(
            leader_fd_,
            PERF_EVENT_IOC_DISABLE,
            PERF_IOC_FLAG_GROUP) != 0) {
        last_error_ = errno_message(
            "PERF_EVENT_IOC_DISABLE");
        running_ = false;
        return false;
    }

    running_ = false;
    return true;
}

PmuSnapshot PmuCounters::snapshot() const {
    PmuSnapshot snapshot;

    if (leader_fd_ < 0) {
        snapshot.error = "PMU group is not open";
        return snapshot;
    }

    const std::size_t count = events_.size();
    std::vector<std::uint64_t> data(
        3U + (2U * count),
        0U);

    const ssize_t bytes = read(
        leader_fd_,
        data.data(),
        data.size() * sizeof(std::uint64_t));

    const std::size_t expected =
        data.size() * sizeof(std::uint64_t);

    if (bytes != static_cast<ssize_t>(expected)) {
        snapshot.error =
            errno_message("read(perf_event group)");
        return snapshot;
    }

    snapshot.time_enabled = data[1];
    snapshot.time_running = data[2];

    if (snapshot.time_running == 0U) {
        snapshot.error = "PMU group was not scheduled";
        return snapshot;
    }

    snapshot.valid = true;
    snapshot.counters.reserve(count);

    const long double scale =
        snapshot.time_enabled == snapshot.time_running
            ? 1.0L
            : static_cast<long double>(
                  snapshot.time_enabled) /
              static_cast<long double>(
                  snapshot.time_running);

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t raw = data[3U + i];
        const std::uint64_t id = data[3U + count + i];

        auto event_it = std::find_if(
            events_.begin(),
            events_.end(),
            [id](const Event& event) {
                return event.id == id;
            });

        if (event_it == events_.end()) continue;

        const long double scaled =
            static_cast<long double>(raw) * scale;

        snapshot.counters.push_back(
            PmuCounterValue{
                event_it->name,
                raw,
                static_cast<std::uint64_t>(scaled)
            });
    }

    return snapshot;
}

void PmuCounters::close_events() noexcept {
    for (const auto& event : events_) {
        if (event.fd >= 0) {
            close(event.fd);
        }
    }
    events_.clear();
    leader_fd_ = -1;
    running_ = false;
}
