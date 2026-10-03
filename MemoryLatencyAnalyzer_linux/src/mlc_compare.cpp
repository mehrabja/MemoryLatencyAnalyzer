#include "mlc_compare.hpp"

#include "platform_utils.hpp"
#include "statistics.hpp"
#include "timer.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <immintrin.h>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kMiB =
    static_cast<std::size_t>(1024U * 1024U);
constexpr std::size_t kCacheLine = 64U;

struct alignas(64) Node {
    std::uintptr_t next = 0;
    std::uint8_t padding[56]{};
};
static_assert(sizeof(Node) == kCacheLine);

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

struct Worker {
    std::thread thread;
    void* memory = nullptr;
    std::size_t bytes = 0;
};

class RingBuffer {
public:
    explicit RingBuffer(std::size_t bytes) {
        size_ = std::max(
            bytes,
            static_cast<std::size_t>(64U * kMiB));
        size_ = (size_ / kCacheLine) * kCacheLine;

        void* memory = mmap(
            nullptr,
            size_,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        if (memory == MAP_FAILED) {
            throw std::runtime_error("mmap failed for MLC comparison ring");
        }

        buffer_ = static_cast<Node*>(memory);
        count_ = size_ / kCacheLine;

        std::vector<std::size_t> order(count_);
        for (std::size_t i = 0; i < count_; ++i) {
            order[i] = i;
        }

        std::mt19937_64 rng(0x4D4C435F434F4D50ULL);
        std::shuffle(order.begin(), order.end(), rng);

        for (std::size_t i = 0; i < count_; ++i) {
            const std::size_t current = order[i];
            const std::size_t next =
                order[(i + 1U) % count_];
            buffer_[current].next =
                reinterpret_cast<std::uintptr_t>(
                    &buffer_[next]);
        }

        // First touch before timing, one cache line per 4 KiB page.
        for (std::size_t i = 0; i < count_; i += 64U) {
            buffer_[i].padding[0] = static_cast<std::uint8_t>(i);
        }
    }

    ~RingBuffer() {
        if (buffer_) {
            munmap(buffer_, size_);
        }
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    Node* first() const noexcept {
        return buffer_;
    }

private:
    Node* buffer_ = nullptr;
    std::size_t size_ = 0;
    std::size_t count_ = 0;
};

ProcessResult run_process(
    const std::string& program,
    const std::vector<std::string>& args) {
    int pipe_fds[2]{-1, -1};
    if (pipe(pipe_fds) != 0) {
        return {-1, std::strerror(errno)};
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {-1, std::strerror(errno)};
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        (void)dup2(pipe_fds[1], STDOUT_FILENO);
        (void)dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 2U);
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(program.c_str(), argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);

    std::string output;
    char buffer[4096];
    for (;;) {
        const ssize_t count =
            read(pipe_fds[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(pipe_fds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return {-1, output + "\nwaitpid failed: " +
                        std::strerror(errno)};
    }

    if (WIFEXITED(status)) {
        return {WEXITSTATUS(status), output};
    }

    if (WIFSIGNALED(status)) {
        return {128 + WTERMSIG(status), output};
    }

    return {-1, output};
}

std::string parse_version(std::string_view output) {
    for (std::size_t i = 0; i + 1U < output.size(); ++i) {
        if (output[i] != 'v' &&
            output[i] != 'V') {
            continue;
        }

        std::size_t end = i + 1U;
        bool has_digit = false;
        while (end < output.size()) {
            const char ch = output[end];
            if ((ch >= '0' && ch <= '9') || ch == '.') {
                has_digit = has_digit || (ch >= '0' && ch <= '9');
                ++end;
            } else {
                break;
            }
        }

        if (has_digit) {
            return std::string(output.substr(i, end - i));
        }
    }

    return "unknown";
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

std::uint64_t pause_cycles(std::uint64_t cycles) {
    for (std::uint64_t i = 0; i < cycles; ++i) {
        _mm_pause();
    }
    return cycles;
}

double measure_ring_latency(
    RingBuffer& ring,
    std::size_t samples,
    std::uint64_t injection_delay,
    std::uint64_t timer_overhead_cycles) {
    volatile Node* current = ring.first();
    std::vector<double> values;
    values.reserve(samples);

    for (std::size_t i = 0; i < samples; ++i) {
        Timer::compiler_barrier();
        const std::uint64_t t0 =
            Timer::read_tsc_start();
        current = reinterpret_cast<volatile Node*>(current->next);
        Timer::compiler_barrier();
        const std::uint64_t t1 =
            Timer::read_tsc_end();
        Timer::compiler_barrier();

        const std::uint64_t elapsed =
            t1 - t0 > timer_overhead_cycles
                ? t1 - t0 - timer_overhead_cycles
                : 0U;
        values.push_back(
            static_cast<double>(elapsed));

        if (injection_delay != 0U) {
            (void)pause_cycles(injection_delay);
        }
    }

    return median(std::move(values));
}

void load_worker(
    std::size_t bytes,
    int cpu,
    std::atomic<bool>& stop,
    volatile std::uint64_t& sink) {
    bytes = std::max(bytes, kMiB);
    bytes = (bytes / sizeof(std::uint64_t)) *
            sizeof(std::uint64_t);

    void* memory = mmap(
        nullptr,
        bytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (memory == MAP_FAILED) return;

    if (!PlatformUtils::pin_current_thread(cpu)) {
        // Continue without pinning rather than changing benchmark semantics.
    }

    auto* data =
        static_cast<volatile std::uint64_t*>(memory);

    for (std::size_t i = 0;
         i < bytes / sizeof(std::uint64_t);
         i += 8U) {
        data[i] = static_cast<std::uint64_t>(i);
    }

    while (!stop.load(std::memory_order_relaxed)) {
        std::uint64_t local = 0;
        for (std::size_t i = 0;
             i < bytes / sizeof(std::uint64_t);
             i += 8U) {
            local ^= data[i];
        }
        sink ^= local;
    }

    munmap(memory, bytes);
}

std::vector<int> worker_cpus(
    const std::vector<int>& allowed_cpus,
    int primary_cpu) {
    std::vector<int> cpus;
    for (const int cpu : allowed_cpus) {
        if (cpu != primary_cpu) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

std::string csv_escape(const std::string& value) {
    bool quote = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' ||
            ch == '\n' || ch == '\r') {
            quote = true;
            break;
        }
    }

    if (!quote) return value;

    std::string escaped{"""};
    for (const char ch : value) {
        if (ch == '"') escaped += """";
        else escaped += ch;
    }
    escaped += '"';
    return escaped;
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8U);
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped += '\\';
            escaped += ch;
        } else if (ch == '\n') {
            escaped += "\\n";
        } else if (ch == '\r') {
            escaped += "\\r";
        } else if (ch == '\t') {
            escaped += "\\t";
        } else {
            escaped += ch;
        }
    }
    return escaped;
}

bool write_reports(
    const MlcComparisonReport& report,
    const std::string& csv_path,
    const std::string& json_path) {
    std::ofstream csv(csv_path);
    if (!csv) return false;

    csv
        << "scenario,delay,metric,ours,mlc,absolute_delta,"
           "percent_delta,status\n";

    csv << std::fixed << std::setprecision(6);
    for (const auto& point : report.results) {
        csv
            << csv_escape(point.scenario) << ','
            << point.delay_cycles << ",latency_ns,"
            << point.ours_ns << ','
            << point.mlc_ns << ','
            << point.absolute_delta_ns << ',';
        if (std::isfinite(point.percent_delta)) {
            csv << point.percent_delta;
        } else {
            csv << "NA";
        }
        csv << ',' << csv_escape(point.status) << '\n';
    }

    std::ofstream json(json_path);
    if (!json) return false;

    json
        << "{\n"
        << "  \"mlc_comparison\": {\n"
        << "    \"available\": "
        << (report.available ? "true" : "false") << ",\n"
        << "    \"binary\": \""
        << json_escape(report.binary) << "\",\n"
        << "    \"version\": \""
        << json_escape(report.version) << "\",\n"
        << "    \"diagnostic\": \""
        << json_escape(report.diagnostic) << "\",\n"
        << "    \"prefetcher_control\": "
           "\"disabled_by_-e\",\n"
        << "    \"results\": [\n";

    for (std::size_t i = 0;
         i < report.results.size(); ++i) {
        const auto& point = report.results[i];
        json
            << "      {\"scenario\": \""
            << json_escape(point.scenario)
            << "\", \"delay_cycles\": "
            << point.delay_cycles
            << ", \"ours_ns\": "
            << point.ours_ns
            << ", \"mlc_ns\": "
            << point.mlc_ns
            << ", \"absolute_delta_ns\": "
            << point.absolute_delta_ns
            << ", \"percent_delta\": ";
        if (std::isfinite(point.percent_delta)) {
            json << point.percent_delta;
        } else {
            json << "null";
        }
        json
            << ", \"status\": \""
            << json_escape(point.status)
            << "\"}";
        if (i + 1U < report.results.size()) {
            json << ',';
        }
        json << '\n';
    }

    json
        << "    ]\n"
        << "  }\n"
        << "}\n";

    return true;
}

} // namespace

std::string MlcComparison::find_mlc() {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return {};

    std::string path(path_env);
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t separator = path.find(':', begin);
        const std::size_t end =
            separator == std::string::npos
                ? path.size()
                : separator;
        const std::filesystem::path candidate =
            std::filesystem::path(
                path.substr(begin, end - begin)) /
            "mlc";

        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }

        if (separator == std::string::npos) break;
        begin = separator + 1U;
    }

    return {};
}

std::optional<double> MlcComparison::parse_idle_latency_ns(
    std::string_view output) {
    std::istringstream stream{std::string(output)};
    std::string line;
    while (std::getline(stream, line)) {
        const auto marker = line.find("Each iteration took");
        if (marker == std::string::npos) continue;

        std::istringstream line_stream(
            line.substr(marker + 19U));
        double value = 0.0;
        std::string unit;
        if (line_stream >> value >> unit &&
            unit == "ns") {
            return value;
        }

        const auto paren = line.find('(');
        if (paren != std::string::npos) {
            line_stream.clear();
            line_stream.str(line.substr(paren + 1U));
            if (line_stream >> value >> unit &&
                unit == "ns") {
                return value;
            }
        }
    }

    return std::nullopt;
}

std::optional<double> MlcComparison::parse_loaded_latency_ns(
    std::string_view output,
    std::uint64_t delay_cycles) {
    std::istringstream stream{std::string(output)};
    std::string line;

    while (std::getline(stream, line)) {
        std::istringstream row(line);
        std::uint64_t delay = 0;
        double latency = 0.0;
        double bandwidth = 0.0;

        if (!(row >> delay >> latency >> bandwidth)) {
            continue;
        }

        if (delay == delay_cycles) {
            return latency;
        }
    }

    return std::nullopt;
}

MlcComparisonReport MlcComparison::run(
    const std::vector<int>& allowed_cpus,
    int primary_cpu,
    std::size_t buffer_bytes,
    std::size_t total_load_bytes,
    int iterations,
    int rounds,
    const std::vector<std::uint64_t>& delays_cycles,
    const std::string& csv_path,
    const std::string& json_path) {
    MlcComparisonReport report;
    report.binary = find_mlc();

    if (report.binary.empty()) {
        report.diagnostic =
            "Intel MLC executable not found in PATH";
    } else {
        report.available = true;
        const auto version =
            run_process(report.binary, {"--version"});
        report.version = parse_version(version.output);

        try {
            RingBuffer idle_ring(buffer_bytes);
            const std::size_t samples =
                static_cast<std::size_t>(
                    std::max(1, iterations)) *
                static_cast<std::size_t>(
                    std::max(1, rounds));
            const std::uint64_t overhead =
                Timer::measure_overhead_cycles(2000);
            const double tsc_hz =
                Timer::calibrate_tsc_hz(
                    Config::TSC_CALIBRATION_ROUNDS,
                    Config::TSC_CALIBRATION_MS);

            const double ours_idle =
                measure_ring_latency(
                    idle_ring,
                    samples,
                    0U,
                    overhead);

            const auto mlc_idle =
                run_process(
                    report.binary,
                    {"-e", "--idle_latency"});
            const auto parsed_idle =
                parse_idle_latency_ns(
                    mlc_idle.output);

            MlcComparisonPoint point;
            point.scenario = "idle";
            point.ours_ns =
                ours_idle *
                1e9 /
                tsc_hz;
            if (parsed_idle) {
                point.delay_cycles = 0U;
                point.mlc_ns = *parsed_idle;
                point.absolute_delta_ns =
                    point.ours_ns - point.mlc_ns;
                point.percent_delta =
                    point.mlc_ns == 0.0
                        ? std::numeric_limits<double>::quiet_NaN()
                        : 100.0 *
                          point.absolute_delta_ns /
                          point.mlc_ns;
                point.status =
                    mlc_idle.exit_code == 0
                        ? "ok"
                        : "mlc-command-warning";
            } else {
                point.status = "mlc-parse-failed";
            }
            report.results.push_back(point);

            const auto cpus =
                worker_cpus(allowed_cpus, primary_cpu);
            std::atomic<bool> stop{false};
            volatile std::uint64_t sink = 0;
            std::vector<Worker> workers;

            if (!cpus.empty()) {
                const std::size_t per_worker =
                    std::max(
                        1U * kMiB,
                        total_load_bytes / cpus.size());

                workers.reserve(cpus.size());
                for (const int cpu : cpus) {
                    Worker worker;
                    worker.thread = std::thread(
                        load_worker,
                        per_worker,
                        cpu,
                        std::ref(stop),
                        std::ref(sink));
                    workers.push_back(std::move(worker));
                }
            }

            RingBuffer loaded_ring(buffer_bytes);
            for (const auto delay : delays_cycles) {
                const double ours_cycles =
                    measure_ring_latency(
                        loaded_ring,
                        samples,
                        delay,
                        overhead);
                MlcComparisonPoint loaded;
                loaded.scenario = "loaded";
                loaded.delay_cycles = delay;
                loaded.ours_ns =
                    ours_cycles *
                    1e9 /
                    Timer::calibrate_tsc_hz(
                        Config::TSC_CALIBRATION_ROUNDS,
                        Config::TSC_CALIBRATION_MS);

                const std::string delay_arg =
                    "-d" + std::to_string(delay);
                const auto mlc_loaded =
                    run_process(
                        report.binary,
                        {"-e", "--loaded_latency", delay_arg});
                const auto parsed_loaded =
                    parse_loaded_latency_ns(
                        mlc_loaded.output, delay);

                if (parsed_loaded) {
                    loaded.mlc_ns = *parsed_loaded;
                    loaded.absolute_delta_ns =
                        loaded.ours_ns - loaded.mlc_ns;
                    loaded.percent_delta =
                        loaded.mlc_ns == 0.0
                            ? std::numeric_limits<double>::quiet_NaN()
                            : 100.0 *
                              loaded.absolute_delta_ns /
                              loaded.mlc_ns;
                    loaded.status =
                        mlc_loaded.exit_code == 0
                            ? "ok"
                            : "mlc-command-warning";
                } else {
                    loaded.status =
                        mlc_loaded.exit_code == 0
                            ? "mlc-parse-failed"
                            : "mlc-command-failed";
                }

                report.results.push_back(loaded);
            }

            stop.store(true, std::memory_order_relaxed);
            for (auto& worker : workers) {
                if (worker.thread.joinable()) {
                    worker.thread.join();
                }
            }

            (void)sink;
        } catch (const std::exception& ex) {
            report.diagnostic =
                std::string("MLC comparison setup/measurement failed: ") +
                ex.what();
        }
    }

    report.csv_path = csv_path;
    report.json_path = json_path;
    report.report_written =
        write_reports(report, csv_path, json_path);

    return report;
}
