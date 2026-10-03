#pragma once

#include <cstddef>

namespace Config {

constexpr int DEFAULT_ITERATIONS = 1000;
constexpr int DEFAULT_WARMUP = 150;
constexpr int DEFAULT_ROUNDS = 5;

constexpr int DEFAULT_SHM_ITERATIONS = 2000;
constexpr int DEFAULT_SHM_WARMUP = 200;
constexpr int SHM_SPIN_ITERATIONS = 2'000'000;
constexpr int SHM_TIMEOUT_MS = 1000;

constexpr std::size_t CACHE_LINE_SIZE = 64;
constexpr std::size_t MIN_BUFFER_SIZE = 32ULL * 1024 * 1024;
constexpr std::size_t DEFAULT_STRIDE = 4096;
constexpr std::size_t LARGE_PAGE_SIZE = 2ULL * 1024 * 1024;
constexpr std::size_t DEFAULT_BANDWIDTH_SIZE = 64ULL * 1024 * 1024;

constexpr int TSC_CALIBRATION_MS = 50;
constexpr int TSC_CALIBRATION_ROUNDS = 3;

constexpr int BANDWIDTH_REPEATS = 5;

} // namespace Config
