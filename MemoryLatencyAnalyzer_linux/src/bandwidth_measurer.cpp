#include "bandwidth_measurer.hpp"

#include "settings.hpp"
#include "timer.hpp"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <sys/mman.h>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

namespace {

std::optional<PmuSnapshot> stop_pmu(
    PmuCounters* pmu,
    bool started) {
    if (!pmu || !started) return std::nullopt;
    (void)pmu->stop();
    return pmu->snapshot();
}

struct Allocation {
    char* ptr = nullptr;
    std::size_t size = 0;
    std::string method;
};

Allocation allocate_buffer(std::size_t requested, const std::string& method) {
    Allocation result;
    result.method = method;
    result.size = requested;

    if (method == "malloc") {
        result.ptr = static_cast<char*>(std::malloc(requested));
        return result;
    }

    if (method == "LargePage") {
        result.size =
            (requested + Config::LARGE_PAGE_SIZE - 1) &
            ~(Config::LARGE_PAGE_SIZE - 1);

        void* memory = mmap(nullptr, result.size,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                            -1, 0);
        result.ptr = memory == MAP_FAILED
                         ? nullptr
                         : static_cast<char*>(memory);
        return result;
    }

    if (method == "mmap") {
        void* memory = mmap(nullptr, requested,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS,
                            -1, 0);
        result.ptr = memory == MAP_FAILED
                         ? nullptr
                         : static_cast<char*>(memory);
        return result;
    }

    throw std::invalid_argument(
        "Unknown bandwidth allocation method: " + method);
}

void release_buffer(Allocation& allocation) {
    if (!allocation.ptr) return;

    if (allocation.method == "malloc") {
        std::free(allocation.ptr);
    } else {
        munmap(allocation.ptr, allocation.size);
    }

    allocation.ptr = nullptr;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double gib_per_second(std::size_t bytes, std::uint64_t elapsed_ns) {
    if (elapsed_ns == 0) return 0.0;

    const double gib =
        static_cast<double>(bytes) /
        (1024.0 * 1024.0 * 1024.0);
    const double seconds =
        static_cast<double>(elapsed_ns) * 1e-9;

    return gib / seconds;
}

} // namespace

BandwidthResult BandwidthMeasurer::measure(
    std::size_t size_bytes,
    const std::string& method,
    int repeats,
    PmuCounters* pmu) {
    if (size_bytes == 0 || repeats <= 0) {
        throw std::invalid_argument(
            "Bandwidth size and repeats must be positive");
    }

    Allocation src = allocate_buffer(size_bytes, method);
    Allocation dst = allocate_buffer(size_bytes, method);

    if (!src.ptr || !dst.ptr) {
        release_buffer(src);
        release_buffer(dst);
        throw std::runtime_error(
            "Allocation failed for bandwidth method: " + method);
    }

    // First-touch is deliberately outside the timed region.
    std::memset(src.ptr, 0xAA, size_bytes);
    std::memset(dst.ptr, 0x55, size_bytes);

    volatile unsigned char checksum = 0;

    std::vector<double> read_rates;
    std::vector<double> write_rates;
    std::vector<double> copy_rates;
    read_rates.reserve(static_cast<std::size_t>(repeats));
    write_rates.reserve(static_cast<std::size_t>(repeats));
    copy_rates.reserve(static_cast<std::size_t>(repeats));

    std::optional<PmuSnapshot> pmu_read;
    std::optional<PmuSnapshot> pmu_write;
    std::optional<PmuSnapshot> pmu_copy;

    bool pmu_started = pmu != nullptr && pmu->start();
    for (int r = 0; r < repeats; ++r) {
        const std::uint64_t t0 = Timer::monotonic_raw_ns();

        for (std::size_t i = 0;
             i < size_bytes;
             i += Config::CACHE_LINE_SIZE) {
            checksum ^= static_cast<unsigned char>(src.ptr[i]);
        }

        const std::uint64_t read_ns =
            Timer::monotonic_raw_ns() - t0;
        read_rates.push_back(gib_per_second(size_bytes, read_ns));
    }
    pmu_read = stop_pmu(pmu, pmu_started);

    pmu_started = pmu != nullptr && pmu->start();
    for (int r = 0; r < repeats; ++r) {
        const std::uint64_t t0 = Timer::monotonic_raw_ns();
        std::memset(dst.ptr, 0x55, size_bytes);
        const std::uint64_t write_ns =
            Timer::monotonic_raw_ns() - t0;
        write_rates.push_back(gib_per_second(size_bytes, write_ns));
    }
    pmu_write = stop_pmu(pmu, pmu_started);

    pmu_started = pmu != nullptr && pmu->start();
    for (int r = 0; r < repeats; ++r) {
        const std::uint64_t t0 = Timer::monotonic_raw_ns();
        std::memcpy(dst.ptr, src.ptr, size_bytes);
        const std::uint64_t copy_ns =
            Timer::monotonic_raw_ns() - t0;
        copy_rates.push_back(gib_per_second(size_bytes, copy_ns));
    }
    pmu_copy = stop_pmu(pmu, pmu_started);

    (void)checksum;

    BandwidthResult result{
        method,
        size_bytes,
        median(std::move(read_rates)),
        median(std::move(write_rates)),
        median(std::move(copy_rates)),
        std::move(pmu_read),
        std::move(pmu_write),
        std::move(pmu_copy)
    };

    release_buffer(src);
    release_buffer(dst);
    return result;
}
