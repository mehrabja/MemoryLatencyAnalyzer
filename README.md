# Memory Microarchitecture & Performance Analyzer

MemoryLatencyAnalyzer is a Linux/x86-64 microbenchmark for studying the CPU memory subsystem and related microarchitectural effects.

It works close to the hardware with RDTSCP, LFENCE, CLFLUSH, CPUID, Linux CPU affinity, mmap, POSIX shared memory, lock-free atomics, and optional Huge Pages.

> Scope: this is an educational and experimental microbenchmark. Results depend on the machine and environment and should primarily be used for controlled comparisons.

---

## What it measures

- L1-hot dependent load latency
- Controlled cache-miss latency using randomized pointer chasing and CLFLUSH
- Hot-store issue throughput
- Dependent pointer-chase behavior at several memory strides
- Read, write, and copy throughput
- Allocator comparison: mmap, malloc, optional 2 MiB Huge Pages
- Cross-process shared-memory round-trip latency
- Cache-coherence and synchronization effects
- Mean, median, p95, p99, standard deviation, minimum, maximum
- Stability evaluation with sample alignment, block averaging, MAD outlier rejection, SNR, classification error, and Welch t-test/TVLA
- Platform fingerprinting for repeatability across CPU/OS architectures
- CSV export and historical logging
- A self-contained educational Spectre Variant 1 demonstration

The repository is therefore better described as a CPU/memory microarchitecture laboratory than as a simple memory-latency calculator.

---

## Measurement methodology

### CPU and cache discovery

The analyzer queries x86 CPUID leaf 4 and reports cache level, data/unified type, cache-line size, associativity, set count, and calculated capacity.

Linux CPU topology is read from the system CPU topology files when available. Physical cores are therefore not inferred with the simplistic "logical CPUs divided by two" rule.

The program also reports CPU model, logical CPUs visible to the process, physical cores, package count, and whether SMT is active.

### TSC timing

Latency uses ordered RDTSCP reads together with LFENCE and compiler barriers.

For very small operations, the timestamping sequence itself is significant. The benchmark therefore:

1. Measures the timing primitive's own median overhead.
2. Times a batch of dependent operations.
3. Subtracts the calibrated overhead once per batch.
4. Divides by the number of operations.
5. Keeps fractional cycles instead of truncating integer division.

The TSC rate is calibrated against CLOCK_MONOTONIC_RAW.

Important: the reported GHz value is the TSC rate, not the instantaneous CPU core or turbo frequency.

### Cache-hit latency

The hit benchmark uses a dependent pointer chain whose node points back to itself.

~~~text
load address A
    ↓
load address A
    ↓
load address A
    ↓
...
~~~

Because each load depends on the previous load's result, the CPU cannot simply overlap many independent requests.

The target is warmed before measurement, so this approximates the latency of a hot, dependent load.

### Forced cache-miss latency

A deterministic pseudo-random ring is built across cache-line-aligned nodes.

Before a timed batch, the exact nodes required by that batch are flushed. The flush list is taken from a precomputed index order, so preparing the experiment does not accidentally read and re-warm the lines being flushed.

~~~text
CLFLUSH required nodes
        ↓
MFENCE
        ↓
dependent load → dependent load → ...
        ↓
average cycles per load
~~~

This is a controlled flush-induced demand-load experiment. It is not a universal DRAM-latency constant.

### Store benchmark

The store test performs a batch of stores to a small set of hot cache lines and reports cycles per store.

This is intentionally called store issue throughput. A CPU store can be accepted into the store machinery while the modified line remains in the cache hierarchy; this is not the same thing as "time until RAM has received the data."

### Stride experiments

Dependent pointer chains are built with these strides:

~~~text
64 B
256 B
1 KiB
4 KiB
16 KiB
~~~

Stride tests do not flush every access. They show how access distance interacts with cache locality, cache capacity, TLB behavior, page boundaries, prefetching, and dependent-load latency.

---

## Bandwidth

The bandwidth module measures:

- Read: one byte sampled per 64-byte cache line
- Write: memset
- Copy: memcpy

The source and destination buffers are touched before timing so first-touch page faults are not mixed into the timed region.

Allocation modes are:

- mmap
- malloc
- LargePage using Linux MAP_HUGETLB

LargePage uses 2 MiB Huge Pages and is skipped when the host has no suitable reserved Huge Pages.

Bandwidth uses CLOCK_MONOTONIC_RAW and is reported in GiB/s, so the calculation does not depend on an assumed CPU core frequency.

These numbers are benchmark-specific throughput measurements. libc implementations, cache state, memory placement, and the underlying CPU all affect them.

---

## Cross-process shared memory

The shared-memory test creates a POSIX shared-memory region and maps it into a parent and child process created with fork.

The processes perform a ping-pong:

~~~text
Producer                         Consumer

request = 1  ─────────────────►
                               response = 1
              ◄────────────────
~~~

Each synchronization flag is isolated on its own 64-byte cache line to reduce false sharing.

The test uses lock-free uint32 atomics with acquire/release ordering. The consumer is placed on a different physical core when the process's allowed CPUs and Linux topology make that possible.

The measured quantity is a cross-process round trip. It therefore includes synchronization, scheduling, cache-coherence, and inter-process communication effects. It is not direct DRAM latency.

---

## Spectre Variant 1 demonstration

The optional Spectre mode is a local, self-contained educational demonstration. The current implementation is a more robust Phase-2-style research PoC: it calibrates the cache threshold, keeps the bounds value dynamic, uses repeated evidence per byte, reports confidence, and retries uncertain bytes. It remains intentionally confined to its own in-process demo data.

It contains:

1. A bounds-checked victim function
2. Branch-predictor training
3. A logically out-of-bounds index
4. A cache-based probe array
5. Timed reloads of 256 possible byte values

The secret is placed inside the demonstration's own data array, so the example does not attack another process.

Run it with:

~~~bash
./latency_analyzer --spectre
./latency_analyzer --evaluation-lab --evaluation-runs 5 --trace-samples 4096 --trace-average 4
./latency_analyzer --spectre-lab --lab-runs 5 --spectre-tries 999
~~~

A successful leak is not guaranteed. CPU design, operating-system mitigations, virtualization, compiler behavior, and other environmental factors can prevent the side channel from being observable.

---

## Statistics

Each measurement keeps raw samples from all requested rounds and computes:

- Count
- Mean
- Median
- P95
- P99
- Sample standard deviation
- Minimum
- Maximum

This replaces the previous practice of averaging per-round minimums, maximums, and standard deviations. Those quantities are not correctly reconstructed by simply averaging the round summaries.

---

## CPU affinity and scheduling

The analyzer discovers the CPUs actually allowed to the process with sched_getaffinity.

It then attempts to:

- Pin the benchmark thread to one allowed CPU
- Increase scheduling priority as a best-effort optimization
- Select a different physical core for the shared-memory consumer when possible

Restricted containers or hosted environments may reject affinity or priority changes. The benchmark continues and reports that limitation instead of assuming CPU 0 or CPU 1 always exists.

---

## Command line

~~~text
Usage: latency_analyzer [options]

Latency:
  --rounds N             Independent measurement rounds (default 5)
  --iterations N         Samples per round (default 1000)
  --warmup N             Warmup samples (default 150)
  --buffer-mib N         Latency buffer size in MiB (default 32)

Bandwidth:
  --bandwidth-mib N      Bandwidth buffer size in MiB (default 64)
  --no-bandwidth         Skip bandwidth measurements
  --pmu                  Enable Linux perf_event_open PMU counters

Shared memory:
  --shm-iterations N     Cross-process round trips (default 2000)
  --no-shared-memory     Skip the shared-memory test

Output:
  --csv FILE             Write latency, bandwidth, and shared-memory results
  --quiet                Minimal output
  --verbose              Include p95/p99/stddev/min/max

Other:
  --spectre              Run the self-contained Spectre V1 demo and exit
  --spectre-tries N      Attempts per leaked byte (default 999)
  --spectre-lab          Repeat the self-contained Spectre demo for reliability analysis
  --lab-runs N           Independent Spectre lab runs (default 5)
  --phase5-lab           Run the Phase 5 defensive lab and exit
  --phase5-runs N        Independent Phase 5 lab runs (default 5)
  --cpu-capability       Detect CPU and run compute-capacity benchmark
  --compute-seconds N    CPU benchmark duration in seconds (default 1)
  --help                 Show this help
~~~

Examples:

~~~bash
./latency_analyzer
./latency_analyzer --rounds 10 --iterations 5000 --verbose
./latency_analyzer --buffer-mib 128 --bandwidth-mib 256
./latency_analyzer --no-bandwidth --no-shared-memory
./latency_analyzer --csv results.csv
./latency_analyzer --spectre
~~~

---

## Build

Requirements:

- Linux
- x86/x86-64 CPU
- CMake 3.15+
- C++17 compiler
- pthread support

Build and test:

~~~bash
cmake -S MemoryLatencyAnalyzer_linux -B MemoryLatencyAnalyzer_linux/build -DBUILD_TESTING=ON
cmake --build MemoryLatencyAnalyzer_linux/build --parallel
ctest --test-dir MemoryLatencyAnalyzer_linux/build --output-on-failure
~~~

The benchmark is compiled with optimization enabled:

~~~text
-O2 -march=native
~~~

and strict warnings:

~~~text
-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion -Wformat=2 -Wundef
~~~

The code does not depend on global -O0 behavior to remain measurable. Timed sections use explicit data dependencies, compiler barriers, volatile accesses where appropriate, and hardware ordering.

---

## Project structure

~~~text
MemoryLatencyAnalyzer/
├── .github/workflows/build.yml
├── .gitignore
├── README.md
└── MemoryLatencyAnalyzer_linux/
    ├── CMakeLists.txt
    ├── config/settings.hpp
    ├── include/
    │   ├── bandwidth_measurer.hpp
    │   ├── evaluation_lab.hpp
    │   ├── operational_lab.hpp
    │   ├── cpu_capability.hpp
    │   ├── cpu_info.hpp
    │   ├── defensive_lab.hpp
    │   ├── latency_measurer.hpp
    │   ├── platform_utils.hpp
    │   ├── reporter.hpp
    │   ├── shared_memory_measurer.hpp
    │   ├── spectre_v1.hpp
    │   ├── statistics.hpp
    │   └── timer.hpp
    ├── src/
    │   ├── bandwidth_measurer.cpp
    │   ├── evaluation_lab.cpp
    │   ├── operational_lab.cpp
    │   ├── cpu_capability.cpp
    │   ├── cpu_info.cpp
    │   ├── defensive_lab.cpp
    │   ├── latency_measurer.cpp
    │   ├── main.cpp
    │   ├── platform_utils.cpp
    │   ├── reporter.cpp
    │   ├── shared_memory_measurer.cpp
    │   ├── spectre_v1.cpp
    │   ├── statistics.cpp
    │   └── timer.cpp
    └── tests/
        ├── evaluation_test.cpp
        ├── operational_lab_test.cpp
        ├── pmu_test.cpp
        └── statistics_test.cpp

---

## What changed in the refactor

The implementation was tightened in the areas that matter most for a microbenchmark:

- Timer overhead is explicitly calibrated.
- Dependent pointer chasing is used instead of timing a single tiny load.
- Forced misses use precomputed random paths so the flush phase does not re-warm the path.
- Stride tests no longer flush every access.
- Fractional cycles are preserved.
- Raw samples across rounds are aggregated correctly.
- Percentiles are reported.
- Bandwidth uses wall-clock timing and excludes first-touch page faults.
- CPU affinity is based on the actual allowed CPU set.
- Physical-core selection is based on Linux topology information.
- Shared-memory coordination uses lock-free atomics with explicit memory ordering.
- CLI parsing rejects malformed numbers and unknown options.
- The build uses -O2 with strict warnings.
- CTest coverage was added for statistics.
- GitHub Actions now builds and tests the project on Ubuntu.
- The Spectre demo was made self-contained; its timing threshold is calibrated; the bounds check is dynamic; byte extraction reports confidence and retries uncertain observations.

---

## Interpreting results

Think about the outputs this way:

~~~text
L1-hot dependent load
  → approximate hot load latency under this benchmark

Forced miss
  → cache eviction + demand fetch

Stride
  → locality + cache + TLB + prefetch behavior

Bandwidth
  → throughput of the selected access implementation

Shared memory
  → process synchronization + coherence + round trip

Spectre demo
  → speculative execution + cache side channel
~~~

Do not treat one run as an intrinsic CPU specification.

For meaningful comparisons, keep the environment consistent and record CPU model, topology, memory configuration, NUMA placement, power-management state, kernel, libc/compiler, virtualization, and background load.

---

## Limitations

This is a serious low-level microbenchmark, but it is not a replacement for a complete benchmark suite.

Known boundaries include:

1. TSC calibration measures TSC rate, not instantaneous core frequency.
2. Residual timer and fence effects remain even after overhead calibration.
3. Forced misses are intentionally synthetic and use CLFLUSH.
4. Store results describe store issue throughput, not DRAM write-back completion.
5. Bandwidth results depend on libc and the selected access implementation.
6. Linux topology information can be incomplete in containers or unusual environments.
7. Cross-process atomics rely on lock-free x86/Linux behavior for the shared mapping.
8. Interrupts, scheduler activity, NUMA placement, and power management can affect results.
9. The implementation is intentionally x86-specific.

---

## Controlled Spectre reliability lab

The repository also includes a research harness for the built-in Spectre fixture:

~~~bash
./latency_analyzer --spectre-lab --lab-runs 5 --spectre-tries 999
~~~

It repeats the same in-process synthetic target and reports:

- exact recovery rate across independent runs
- aggregate byte accuracy
- bytes that were correct in every run

This is deliberately restricted to the program's own synthetic secret. It does not accept PIDs, executable paths, arbitrary addresses, or data from other processes.

---

## Evaluation & validation lab

The evaluation layer is a separate local harness for making measurements more repeatable and for documenting the experimental reference boundary. It is the implementation point for alignment, denoising, outlier control, SNR/error metrics, TVLA, recovery checks, platform fingerprints, and synthetic control evaluation.

Run:

~~~bash
./latency_analyzer --evaluation-lab
./latency_analyzer --evaluation-lab --evaluation-runs 5 --trace-samples 4096 --trace-average 4
~~~

The pipeline performs:

1. **Sample alignment** — paired classes are truncated to a common sample count before statistical comparison.
2. **Block averaging** — adjacent timing observations can be averaged to reduce short-term noise.
3. **MAD outlier rejection** — median absolute deviation is used instead of a mean/stddev rule so a few large excursions do not dominate the result.
4. **SNR** — reports the absolute difference between class means relative to pooled timing noise.
5. **Error rate** — evaluates a simple threshold classifier against the known fixed/random labels.
6. **TVLA** — runs a single-point Welch two-sample t-test and reports `|t|` against the API's reference threshold of 4.5.
7. **Repeatability** — computes the coefficient of variation across per-run medians.
8. **Recovery** — the lab injects one synthetic incomplete-capture fault, exercises the local retry/recovery path, and reports recovery rate.
9. **Platform fingerprint** — records CPU brand/vendor, core topology, machine architecture, and kernel release so results from other machines can be compared later.

### Default acceptance criteria

The lab records these as engineering defaults, not universal hardware or security standards:

- repeatability CV ≤ 10%
- classification error ≤ 5%
- capture recovery rate is reported; the built-in fault should recover to 100%
- TVLA reference threshold = 4.5

A TVLA threshold crossing is reported separately as `leakage_detected`; it does not automatically mean the measurement pipeline is unstable. The built-in timing fixture is intentionally data-dependent so the TVLA path can be exercised.

### Reference environment and ground truth

The reference boundary is explicit:

- **process boundary:** the lab is self-contained in one process
- **data boundary:** only synthetic fixed/random inputs are used
- **observation boundary:** local timing and local filesystem reporting only
- **ground truth:** the class label for every synthetic trace is known when the trace is collected
- **interfaces:** the lab exposes a small C++ API plus a JSON report containing metrics, platform metadata, and control contracts

The generated JSON report is suitable as a machine-readable record for later cross-platform comparisons.

### Operational control evaluation

The report contains synthetic scenarios for:

- EDR monitoring of suspicious execution
- DLP handling of a sensitive-data export
- microsegmentation blocking an unauthorized egress flow
- least-privilege denial of protected-resource access
- network monitoring of a simulated command-channel pattern
- persistence monitoring
- defense-evasion/stealth monitoring
- source-to-sink data-flow monitoring

These scenarios define expected control behavior and produce auditable evidence fields, but they do **not** connect to or claim to validate a real EDR, DLP, firewall, microsegmentation platform, IAM system, or SIEM. Live validation requires replaying equivalent authorized events through the target platform and comparing its telemetry with this ground truth.

### Phase/platform portability

Run the same evaluation command on each target CPU/OS environment and keep the resulting JSON reports. The platform fingerprint plus the measured SNR, TVLA, error rate, and repeatability metrics provide the basis for comparing architecture-dependent behavior. The project does not assume that one CPU's timing numbers transfer unchanged to another.

---

## Operational defensive lab

برای ارزیابی end-to-end، پروژه یک state machine کاملاً مصنوعی دارد:

~~~bash
./latency_analyzer --operational-lab
./latency_analyzer --operational-lab --operational-runs 5
~~~

در این آزمایشگاه رفتارهای زیر فقط به‌صورت event شبیه‌سازی می‌شوند:

- target access
- privilege-change attempt
- persistence attempt
- defense-evasion indicator
- command-and-control pattern
- sensitive-data collection
- data staging
- data transfer / exfiltration
- cleanup/recovery

خروجی شامل نرخ تشخیص، missed events، false-positive rate و recovery rate است و یک JSON قابل‌پردازش تولید می‌شود.

مرز مرجع:

- state و data فقط در حافظه هستند
- هیچ socket یا network connection ساخته نمی‌شود
- persistence واقعی ایجاد نمی‌شود
- privilege سیستم تغییر نمی‌کند
- subprocess یا process injection انجام نمی‌شود
- دسترسی به process یا memory خارجی وجود ندارد

این بخش برای سنجش قرارداد کنترلی و پایداری گزارش‌دهی است؛ عملکرد یک محصول واقعی EDR/DLP/firewall/SIEM را ادعا نمی‌کند.

---

## Optional Linux PMU counters

The analyzer can collect real hardware performance-monitoring counters through Linux `perf_event_open(2)`. Enable them explicitly:

~~~bash
./latency_analyzer --pmu --verbose --rounds 5 --iterations 1000
~~~

For latency tests, the PMU group is reset and enabled after setup and warmup, remains active over the sample-collection region, then is stopped before the result is reported. Each latency result has its own snapshot. Bandwidth read, write, and copy regions are measured with separate PMU snapshots.

The group requests these generic Linux events when the running kernel/CPU exposes them:

- CPU cycles
- retired instructions
- L1D read misses
- LLC read accesses
- LLC read misses
- backend stalled cycles

Generic cache-event mappings are implementation dependent. Unsupported events are omitted while the working PMU events remain active.

Verbose output reports raw counts, scaled counts when the group was multiplexed, and derived metrics such as misses per thousand instructions, LLC miss rate, cycles per instruction, and backend-stall fraction. For latency results it also compares PMU CPU-cycle counts with the accumulated TSC timing reference.

PMU access is controlled by the host's Linux perf security policy. A restricted `kernel.perf_event_paranoid` setting, container policy, missing permissions, or unsupported events can prevent access. In those cases the analyzer prints a diagnostic and continues without PMU measurements; the ordinary latency/bandwidth benchmark is not disabled.

## CPU capability profiler

A separate CPU profiling mode detects the processor identity and measures a short single-thread compute workload.

Run:

~~~bash
./latency_analyzer --cpu-capability
./latency_analyzer --cpu-capability --compute-seconds 3
~~~

It reports:

- CPU vendor and full brand/model string
- CPUID family, model, and stepping
- physical cores, logical threads, packages, and SMT state
- detected SSE/SSE2/SSE4.2/AVX/AVX2/AVX-512F support
- available maximum-frequency information
- measured floating-point throughput in GFLOP/s
- measured integer throughput in GIntOps/s

The compute numbers are measured single-thread throughput for this benchmark. They are not the vendor's theoretical peak and should be compared only under similar system conditions.

---

## Phase 5 defensive lab

The Phase 5 lab models an end-to-end attack lifecycle without implementing malware behavior:

~~~bash
./latency_analyzer --phase5-lab
./latency_analyzer --phase5-lab --phase5-runs 5 --spectre-tries 999
~~~

It combines the local Spectre reliability fixture with simulated:

- local collection
- local metrics-only export
- detector alerts and confirmed events
- network egress blocking
- persistence blocking

The lab writes `phase5_defensive_report.json` in the current working directory. The report contains only experiment metrics and event dispositions; the recovered test secret is not exported.

Safety boundary:

- no network connections are created
- no persistence mechanism is installed
- no process injection is performed
- no external process or arbitrary memory address is accessed

The detector statistics are synthetic lab measurements, not a production EDR or malware detector.

---

## Continuous integration

GitHub Actions builds the project on Ubuntu with CMake and runs the unit test suite.

Hardware-specific benchmark numbers are not used as CI assertions because hosted CI runners do not provide a stable microarchitectural environment.

---

## License

No project license has been selected yet. Until a license is added, reuse and redistribution should not be assumed to be permitted beyond the rights granted by applicable law.

## Author

**Mehrab Jalilian (مهراب جلیلیان)**

AI Engineer & Researcher

- GitHub: https://github.com/mehrabJA
- Website: https://mehrabjalilian.site/
- LinkedIn: https://www.linkedin.com/in/mehrab-jalilian
