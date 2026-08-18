#pragma once
#include <cstddef>

namespace Config {
    constexpr int    DEFAULT_ITERATIONS  = 1000;
    constexpr int    DEFAULT_WARMUP      = 150;
    constexpr int    DEFAULT_ROUNDS      = 5;
    constexpr int    DEFAULT_SHM_ITERATIONS = 2000;
    constexpr int    DEFAULT_SHM_WARMUP      = 200;
    constexpr size_t CACHE_LINE_SIZE     = 64;
    constexpr size_t MIN_BUFFER_SIZE     = 32 * 1024 * 1024;
    constexpr size_t DEFAULT_STRIDE      = 4096;
    constexpr size_t LARGE_PAGE_SIZE     = 2 * 1024 * 1024; // 2 MB
}