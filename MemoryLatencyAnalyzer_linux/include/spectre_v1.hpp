#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct SpectreResult {
    std::uint8_t guessed_value = 0;
    int score = 0;
    int second_score = 0;
    std::uint64_t threshold_cycles = 0;
    double confidence = 0.0;
    int attempts = 0;
    bool success = false;
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
};
