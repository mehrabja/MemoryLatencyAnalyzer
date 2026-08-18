#pragma once
#include <cstdint>
#include <x86intrin.h>

class Timer {
public:
    static inline uint64_t rdtsc() {
        return __rdtsc();
    }

    static inline void serialize() {
        _mm_lfence();
    }

    static inline void clflush(const volatile void* addr) {
        _mm_clflush(const_cast<const void*>(addr));
        _mm_mfence();
    }
};
