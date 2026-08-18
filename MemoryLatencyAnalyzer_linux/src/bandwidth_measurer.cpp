#include "bandwidth_measurer.hpp"
#include "timer.hpp"
#include "settings.hpp"
#include <sys/mman.h>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000 // در صورت نبود در هدرهای سیستم (kernel های خیلی قدیمی)
#endif

// چون munmap برخلاف VirtualFree به سایز نیاز دارد، سایز واقعی تخصیص را هم
// برمی‌گردانیم (برای LargePage این سایز ممکن است بزرگ‌تر از سایز درخواستی و
// هم‌ترازشده با LARGE_PAGE_SIZE باشد).
static char* allocate_buffer(size_t size, const std::string& method, size_t& out_alloc_size) {
    out_alloc_size = size;
    if (method == "malloc") {
        return static_cast<char*>(malloc(size));
    }
    if (method == "LargePage") {
        size_t aligned = (size + Config::LARGE_PAGE_SIZE - 1) & ~(Config::LARGE_PAGE_SIZE - 1);
        out_alloc_size = aligned;
        // معادل VirtualAlloc(..., MEM_LARGE_PAGES, ...): نیاز به Huge Pages
        // از قبل رزرو شده در سیستم دارد (/proc/sys/vm/nr_hugepages)؛ در غیر
        // این صورت mmap با MAP_FAILED برمی‌گردد، درست مثل شکست بدون
        // Administrator روی ویندوز.
        void* p = mmap(nullptr, aligned, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        return (p == MAP_FAILED) ? nullptr : static_cast<char*>(p);
    }
    // mmap معمولی (معادل VirtualAlloc ساده روی ویندوز)
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : static_cast<char*>(p);
}

static void free_buffer(char* p, const std::string& method, size_t alloc_size) {
    if (!p) return;
    if (method == "malloc") {
        free(p);
    } else {
        munmap(p, alloc_size);
    }
}

BandwidthResult BandwidthMeasurer::measure(size_t size_bytes, const std::string& method) {
    size_t src_alloc_size = 0, dst_alloc_size = 0;
    char* src = allocate_buffer(size_bytes, method, src_alloc_size);
    char* dst = allocate_buffer(size_bytes, method, dst_alloc_size);

    if (!src || !dst) {
        free_buffer(src, method, src_alloc_size);
        free_buffer(dst, method, dst_alloc_size);
        throw std::runtime_error("Allocation failed: " + method);
    }

    memset(src, 0xAA, size_bytes);

    const int repeats = 6;
    uint64_t total_read = 0, total_write = 0, total_copy = 0;

    for (int r = 0; r < repeats; ++r) {
        // Write
        Timer::serialize();
        uint64_t t0 = Timer::rdtsc();
        memset(dst, 0x55, size_bytes);
        Timer::serialize();
        total_write += Timer::rdtsc() - t0;

        // Read
        volatile char sink = 0;
        Timer::serialize();
        t0 = Timer::rdtsc();
        for (size_t i = 0; i < size_bytes; i += 64) {
            sink ^= src[i];
        }
        Timer::serialize();
        total_read += Timer::rdtsc() - t0;

        // Copy
        Timer::serialize();
        t0 = Timer::rdtsc();
        memcpy(dst, src, size_bytes);
        Timer::serialize();
        total_copy += Timer::rdtsc() - t0;
    }

    free_buffer(src, method, src_alloc_size);
    free_buffer(dst, method, dst_alloc_size);

    BandwidthResult res;
    res.allocator    = method;
    res.size_bytes   = size_bytes;
    res.read_cycles  = static_cast<double>(total_read)  / repeats;
    res.write_cycles = static_cast<double>(total_write) / repeats;
    res.copy_cycles  = static_cast<double>(total_copy)  / repeats;
    return res;
}