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
    │   ├── defensive_lab.hpp
    │   ├── cpu_info.hpp
    │   ├── latency_measurer.hpp
    │   ├── platform_utils.hpp
    │   ├── reporter.hpp
    │   ├── shared_memory_measurer.hpp
    │   ├── spectre_v1.hpp
    │   ├── statistics.hpp
    │   └── timer.hpp
    ├── src/
    │   ├── bandwidth_measurer.cpp
    │   ├── defensive_lab.cpp
    │   ├── cpu_info.cpp
    │   ├── latency_measurer.cpp
    │   ├── main.cpp
    │   ├── platform_utils.cpp
    │   ├── reporter.cpp
    │   ├── shared_memory_measurer.cpp
    │   ├── spectre_v1.cpp
    │   ├── statistics.cpp
    │   └── timer.cpp
    └── tests/statistics_test.cpp
~~~

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
