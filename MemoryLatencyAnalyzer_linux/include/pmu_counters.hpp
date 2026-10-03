#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct PmuCounterValue {
    std::string name;
    std::uint64_t raw_count = 0;
    std::uint64_t scaled_count = 0;
};

struct PmuSnapshot {
    bool valid = false;
    std::string error;
    std::uint64_t time_enabled = 0;
    std::uint64_t time_running = 0;
    std::vector<PmuCounterValue> counters;
};

class PmuCounters {
public:
    static std::unique_ptr<PmuCounters> create(
        std::string& error);

    ~PmuCounters();

    PmuCounters(const PmuCounters&) = delete;
    PmuCounters& operator=(const PmuCounters&) = delete;

    bool start();
    bool stop();
    PmuSnapshot snapshot() const;

    const std::string& last_error() const noexcept {
        return last_error_;
    }

private:
    PmuCounters() = default;

    struct Event {
        std::string name;
        int fd = -1;
        std::uint64_t id = 0;
    };

    int leader_fd_ = -1;
    std::vector<Event> events_;
    mutable std::string last_error_;
    bool running_ = false;

    bool open_events();
    bool open_event(
        const std::string& name,
        std::uint32_t type,
        std::uint64_t config,
        int group_fd);
    static std::uint64_t cache_config(
        std::uint64_t cache,
        std::uint64_t operation,
        std::uint64_t result) noexcept;
    void close_events() noexcept;
};
