#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SpectreResult {
    std::uint8_t guessed_value = 0;
    int score = 0;
    int second_score = 0;
    std::uint64_t threshold_cycles = 0;
    double confidence = 0.0;
    int attempts = 0;
    bool success = false;
};

struct SpectreLabResult {
    int runs = 0;
    int tries_per_byte = 0;
    std::size_t secret_length = 0;
    int exact_matches = 0;
    std::size_t correct_bytes = 0;
    std::size_t total_bytes = 0;
    std::vector<int> per_byte_correct_runs;
};

class SpectreV1 {
public:
    // Self-contained local demonstration only. malicious_x is an offset
    // into this demo's own array; it is not an external memory address.
    static SpectreResult read_byte(
        std::size_t malicious_x,
        int tries = 999);

    static std::string read_string(
        std::size_t start_offset,
        std::size_t length,
        int tries = 999);

    static void run_demo(int tries = 999);

    // Research harness: repeats the local demo against the same built-in
    // target secret and reports reliability. It has no target-process input.
    static SpectreLabResult run_reliability_lab(
        int runs = 5,
        int tries_per_byte = 999);
};
