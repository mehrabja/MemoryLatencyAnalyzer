#include "shared_memory_measurer.hpp"

#include "platform_utils.hpp"
#include "settings.hpp"
#include "timer.hpp"

#include <atomic>
#include <cstdint>
#include <new>
#include <sstream>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

namespace {

struct alignas(64) SharedFlag {
    std::atomic<std::uint32_t> value;
    char padding[64 - sizeof(std::atomic<std::uint32_t>)];
};

static_assert(sizeof(std::atomic<std::uint32_t>) <= 64,
              "Unexpected uint32 atomic size");
static_assert(sizeof(SharedFlag) == 64,
              "SharedFlag must occupy one cache line");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "Shared-memory test requires lock-free uint32 atomics");

struct alignas(64) SharedControl {
    SharedFlag request;
    SharedFlag response;
    SharedFlag consumer_ready;
    SharedFlag stop;
};

std::string unique_name() {
    std::ostringstream oss;
    oss << "/mla_shm_" << static_cast<unsigned long>(getpid()) << "_"
        << static_cast<unsigned long>(Timer::monotonic_raw_ns());
    return oss.str();
}

bool wait_for_value(
    const std::atomic<std::uint32_t>& flag,
    std::uint32_t expected,
    const std::atomic<std::uint32_t>* stop_flag = nullptr) {
    for (int i = 0; i < Config::SHM_SPIN_ITERATIONS; ++i) {
        if (flag.load(std::memory_order_acquire) == expected) {
            return true;
        }
        if (stop_flag &&
            stop_flag->load(std::memory_order_acquire) != 0) {
            return false;
        }
        _mm_pause();
    }

    const std::uint64_t deadline =
        Timer::monotonic_raw_ns() +
        static_cast<std::uint64_t>(Config::SHM_TIMEOUT_MS) * 1'000'000ULL;

    while (Timer::monotonic_raw_ns() < deadline) {
        if (flag.load(std::memory_order_acquire) == expected) {
            return true;
        }
        if (stop_flag &&
            stop_flag->load(std::memory_order_acquire) != 0) {
            return false;
        }
        usleep(1000);
    }

    return false;
}

void consumer_loop(SharedControl* control, int cpu) {
    if (!PlatformUtils::pin_current_thread(cpu)) {
        _exit(2);
    }

    control->consumer_ready.value.store(
        1, std::memory_order_release);

    for (;;) {
        if (control->stop.value.load(std::memory_order_acquire) != 0) {
            break;
        }

        if (!wait_for_value(
                control->request.value, 1,
                &control->stop.value)) {
            break;
        }

        control->request.value.store(
            0, std::memory_order_release);
        control->response.value.store(
            1, std::memory_order_release);
    }
}

bool wait_or_kill(pid_t pid, int timeout_ms) {
    constexpr int step_ms = 10;

    for (int elapsed = 0; elapsed < timeout_ms; elapsed += step_ms) {
        const pid_t status = waitpid(pid, nullptr, WNOHANG);

        if (status == pid) return true;
        if (status < 0) return false;

        usleep(static_cast<useconds_t>(step_ms * 1000));
    }

    if (kill(pid, SIGKILL) != 0) {
        return false;
    }

    return waitpid(pid, nullptr, 0) == pid;
}

} // namespace

SharedMemoryResult SharedMemoryMeasurer::measure_cross_process(
    int iterations,
    int warmup,
    int producer_cpu,
    int consumer_cpu) {
    SharedMemoryResult result;
    result.iterations = iterations;
    result.producer_cpu = producer_cpu;
    result.consumer_cpu = consumer_cpu;

    if (iterations <= 0 || warmup < 0) {
        result.error_message =
            "Invalid shared-memory iteration configuration";
        return result;
    }

    if (producer_cpu < 0 || consumer_cpu < 0) {
        result.error_message = "Invalid CPU affinity selection";
        return result;
    }

    const std::string name = unique_name();
    const int fd = shm_open(
        name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);

    if (fd < 0) {
        result.error_message = "shm_open failed";
        return result;
    }

    if (ftruncate(
            fd, static_cast<off_t>(sizeof(SharedControl))) != 0) {
        close(fd);
        shm_unlink(name.c_str());
        result.error_message = "ftruncate failed";
        return result;
    }

    void* view = mmap(
        nullptr,
        sizeof(SharedControl),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0);
    close(fd);

    if (view == MAP_FAILED) {
        shm_unlink(name.c_str());
        result.error_message = "mmap failed";
        return result;
    }

    auto* control = new (view) SharedControl{};
    control->request.value.store(0, std::memory_order_relaxed);
    control->response.value.store(0, std::memory_order_relaxed);
    control->consumer_ready.value.store(
        0, std::memory_order_relaxed);
    control->stop.value.store(0, std::memory_order_relaxed);

    const pid_t child = fork();

    if (child < 0) {
        munmap(view, sizeof(SharedControl));
        shm_unlink(name.c_str());
        result.error_message = "fork failed";
        return result;
    }

    if (child == 0) {
        consumer_loop(control, consumer_cpu);
        _exit(0);
    }

    if (!wait_for_value(control->consumer_ready.value, 1)) {
        control->stop.value.store(
            1, std::memory_order_release);
        (void)wait_or_kill(child, 500);
        munmap(view, sizeof(SharedControl));
        shm_unlink(name.c_str());
        result.error_message =
            "Consumer did not become ready";
        return result;
    }

    for (int i = 0; i < warmup; ++i) {
        control->request.value.store(
            1, std::memory_order_release);

        if (!wait_for_value(
                control->response.value, 1,
                &control->stop.value)) {
            control->stop.value.store(
                1, std::memory_order_release);
            (void)wait_or_kill(child, 500);
            munmap(view, sizeof(SharedControl));
            shm_unlink(name.c_str());
            result.error_message =
                "Consumer stopped during warmup";
            return result;
        }

        control->response.value.store(
            0, std::memory_order_release);
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(static_cast<std::size_t>(iterations));

    for (int i = 0; i < iterations; ++i) {
        Timer::compiler_barrier();
        const std::uint64_t start = Timer::read_tsc_start();

        control->request.value.store(
            1, std::memory_order_release);

        if (!wait_for_value(
                control->response.value, 1,
                &control->stop.value)) {
            result.error_message =
                "Consumer stopped during measurement";
            break;
        }

        Timer::compiler_barrier();
        const std::uint64_t end = Timer::read_tsc_end();
        Timer::compiler_barrier();

        control->response.value.store(
            0, std::memory_order_release);

        samples.push_back(end - start);
    }

    control->stop.value.store(
        1, std::memory_order_release);
    (void)wait_or_kill(child, 1000);

    munmap(view, sizeof(SharedControl));
    shm_unlink(name.c_str());

    if (samples.empty()) {
        if (result.error_message.empty()) {
            result.error_message =
                "No successful shared-memory samples";
        }
        return result;
    }

    result.stats = Statistics::summarize(samples);
    result.iterations =
        static_cast<int>(samples.size());
    result.success = true;
    return result;
}
