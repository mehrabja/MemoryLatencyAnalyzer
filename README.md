# Memory Microarchitecture & Performance Analyzer

**MemoryLatencyAnalyzer** is a Linux/x86 microbenchmark and systems-performance analysis tool written in C++17. It measures several aspects of the CPU memory subsystem rather than memory latency alone:

- CPU cache hierarchy discovery (L1/L2/L3)
- Cache-hit and forced cache-miss load latency
- Load/store microbenchmarks
- Memory access behavior under different strides
- Read, write, and copy throughput
- Throughput comparison across `mmap`, `malloc`, and 2 MiB Huge Pages
- Cross-process shared-memory round-trip latency and cache-coherence traffic
- Statistical summaries and CSV/history output
- A self-contained educational Spectre Variant 1 side-channel demonstration

The implementation is intentionally close to the hardware: it uses x86 `RDTSC`, `LFENCE`, `CLFLUSH`, `CPUID`, CPU affinity, POSIX shared memory, `mmap`, and Huge Pages.

> **Scope:** this project is a low-level learning and experimentation tool. Its results are workload- and platform-dependent and should not be interpreted as universal specifications for a CPU or DRAM subsystem.

---

## What this project actually measures

The name "MemoryLatencyAnalyzer" is slightly narrower than the implementation. The executable is better thought of as a **CPU/memory microarchitecture laboratory**.

### 1. CPU and cache discovery

The program queries x86 `CPUID leaf 4` to discover cache properties such as:

- cache level
- cache type (data or unified)
- cache line size
- associativity (ways)
- set count
- calculated capacity

If the standard cache enumeration is unavailable, the code has a fallback L3-size query.

It also:

- estimates the TSC rate against wall-clock time
- checks the CPUID Hyper-Threading capability bit
- reports the online logical CPU count

The current implementation treats `logical_cores / 2` as the physical-core count when Hyper-Threading is reported, so that value should be considered an approximation rather than a complete CPU-topology discovery mechanism.

### 2. Cache-hit latency

A buffer is allocated with `mmap`. Its default size is approximately:

`max(32 MiB, 2.5 × detected L3 capacity)`

A location is warmed up by repeatedly reading it, then each measurement performs a serialized timestamped load.

Conceptually:

```text
warm cache line
      │
      ▼
 RDTSC / load / RDTSC
      │
      ▼
 measured cycles
```

The implementation records the mean, sample standard deviation, minimum, and maximum.

### 3. Forced cache-miss latency

For the miss test, cache lines are explicitly evicted with x86 `CLFLUSH` before the timed load.

The simplified sequence is:

```text
memory address
     │
     ├── CLFLUSH
     ▼
cache line evicted
     │
     ├── serialized RDTSC
     ▼
     load
     │
     ├── serialized RDTSC
     ▼
miss latency in TSC cycles
```

This is a **controlled flush-induced miss experiment**. It should not be interpreted as a direct measurement of "DRAM latency" in every possible workload, because real applications may be affected by cache state, hardware prefetching, TLB state, memory-level parallelism, and other microarchitectural effects.

### 4. Load vs. store

The load benchmark currently reuses the cache-hit measurement path.

The store benchmark times:

```cpp
buffer_[0] = value;
```

on a cache-resident location.

Therefore, the reported store number is best interpreted as the observed cost of a small CPU store under this setup, not the time required for the modified data to reach DRAM. Store buffers, write-back behavior, and cache coherence all separate those concepts.

### 5. Stride experiments

The executable tests several strides:

```text
64 B
256 B
1 KiB
4 KiB
16 KiB
```

The goal is to expose how access distance changes the observed behavior of the memory hierarchy.

An important implementation detail is that the miss benchmark still uses `CLFLUSH` before individual measurements. Consequently, these results are not a pure "natural cache miss-rate vs. stride" experiment; they are a controlled access-latency experiment in which stride can interact with cache lines, pages, TLB behavior, and hardware prefetching.

### 6. Memory bandwidth / throughput

The bandwidth module allocates two large buffers and measures:

- sequential reads
- writes using `memset`
- copies using `memcpy`

The default benchmark size from `main.cpp` is 64 MiB.

Three allocation modes are attempted:

1. **`mmap`** — anonymous private virtual memory
2. **`malloc`** — heap allocation
3. **`LargePage`** — Linux `MAP_HUGETLB`, using 2 MiB Huge Pages

The measured cycles are converted to a throughput estimate in GB/s using the measured TSC rate.

The Huge Page path requires pre-reserved Huge Pages on the host. When allocation fails, that mode is skipped.

### 7. Cross-process shared memory

The project also contains a separate experiment for inter-process communication.

The parent process creates a POSIX shared-memory object with:

```text
shm_open + ftruncate + mmap
```

It then uses `fork()` to create a consumer process.

The two processes perform a ping-pong protocol:

```text
Producer                              Consumer
   │                                     │
   │ request.ready = 1                   │
   ├────────────────────────────────────>│
   │                                     │
   │                         response=1   │
   │<────────────────────────────────────┤
   │                                     │
   └────── measure the round trip ───────┘
```

The synchronization slots are aligned to 64-byte boundaries to reduce false sharing between control variables.

The parent is pinned to logical CPU 0 and the consumer attempts to use logical CPU 1. This makes the experiment useful for observing cross-core cache-coherence and synchronization effects in addition to the cost of the shared-memory protocol itself.

This measurement is therefore a **shared-memory round-trip latency**, not a pure DRAM latency measurement.

### 8. Spectre Variant 1 demonstration

The project contains an educational Spectre Variant 1 demonstration that follows the classic bounds-check bypass + cache side-channel pattern.

The victim function conceptually does:

```text
if (x < array1_size)
    array2[array1[x] * 4096]
```

The attack code:

1. flushes the probe array from cache
2. repeatedly trains the branch predictor with an in-bounds index
3. supplies an out-of-bounds malicious index
4. relies on speculative execution
5. reloads the 256 probe locations
6. counts cache hits
7. selects the byte value with the strongest score

Run it explicitly with:

```bash
./latency_analyzer --spectre
```

The demo uses an in-process hard-coded string:

```text
The Magic Words are Squeamish Ossifrage.
```

It is intended as a **local educational demonstration of a microarchitectural side channel**, not as a general-purpose Spectre exploit against arbitrary processes.

---

## Measurement methodology

### Cycle timing

The project uses the x86 Time Stamp Counter through `__rdtsc()`.

Timing is surrounded by `LFENCE`-based serialization:

```text
LFENCE
RDTSC
operation
LFENCE
RDTSC
```

This reduces instruction reordering around the timed region, but the timing sequence itself has non-zero overhead. For very small operations, that overhead can be a significant fraction of the observed value.

### CPU affinity

The main process attempts to pin its current thread to logical CPU 0 with:

```cpp
pthread_setaffinity_np(...)
```

It also attempts to increase the process priority with `setpriority(..., -10)`. Higher priority may require additional privileges; failure is intentionally treated as best-effort.

The shared-memory consumer attempts to run on logical CPU 1.

### Warm-up

Most latency measurements perform warm-up iterations before collecting samples. This reduces some first-touch and cold-start effects, but it does not eliminate all OS and hardware variability.

### Statistics

For each measurement series the code reports:

- arithmetic mean
- sample standard deviation
- minimum
- maximum

The current program then averages the per-round statistics for some results. In particular, averaging standard deviations, minima, and maxima across independent rounds is not equivalent to recomputing those statistics from all raw samples. For rigorous benchmarking, raw samples should be retained and aggregated centrally.

---

## Command-line interface

```text
Usage: latency_analyzer [options]

  --rounds N
      Number of full latency rounds (default: 5)

  --iterations N
      Iterations per measurement (default: 1000)

  --warmup N
      Warmup iterations (default: 150)

  --shm-iterations N
      Shared-memory ping-pong iterations (default: 2000)

  --no-shared-memory
      Skip the cross-process shared-memory test

  --csv FILE
      Write latency results to a CSV file

  --quiet
      Reduce console output

  --verbose
      Print standard deviation, minimum, and maximum

  --spectre
      Run the Spectre Variant 1 demonstration and exit

  --help
      Show command-line help
```

Examples:

```bash
# Default benchmark
./latency_analyzer

# More repetitions, no shared-memory test
./latency_analyzer --rounds 10 --iterations 5000 --no-shared-memory

# Detailed statistics
./latency_analyzer --verbose

# Save latency measurements
./latency_analyzer --csv results.csv

# Run only the educational Spectre demo
./latency_analyzer --spectre
```

---

## Build

The implementation currently targets Linux and C++17.

### Requirements

- Linux
- x86/x86-64 CPU with the required timing/cache instructions
- CMake 3.15+
- C++17-compatible compiler
- pthread support

Build:

```bash
cd MemoryLatencyAnalyzer_linux
cmake -S . -B build
cmake --build build
```

The resulting executable is:

```text
build/latency_analyzer
```

The project is compiled with:

```text
-O0 -march=native
```

The disabled compiler optimization is deliberate for a low-level experiment, although it also means this is not equivalent to measuring a normally optimized application workload.

---

## Project structure

```text
MemoryLatencyAnalyzer/
├── README.md
└── MemoryLatencyAnalyzer_linux/
    ├── CMakeLists.txt
    ├── config/
    │   └── settings.hpp
    ├── include/
    │   ├── bandwidth_measurer.hpp
    │   ├── cpu_info.hpp
    │   ├── latency_measurer.hpp
    │   ├── platform_utils.hpp
    │   ├── reporter.hpp
    │   ├── shared_memory_measurer.hpp
    │   ├── spectre_v1.hpp
    │   ├── statistics.hpp
    │   └── timer.hpp
    └── src/
        ├── bandwidth_measurer.cpp
        ├── cpu_info.cpp
        ├── latency_measurer.cpp
        ├── main.cpp
        ├── platform_utils.cpp
        ├── reporter.cpp
        ├── shared_memory_measurer.cpp
        ├── spectre_v1.cpp
        └── statistics.cpp
```

### Component responsibilities

| Component | Responsibility |
|---|---|
| `main.cpp` | CLI parsing and benchmark orchestration |
| `latency_measurer.cpp` | Hit/miss/load/store/stride latency measurements |
| `bandwidth_measurer.cpp` | Read/write/copy throughput |
| `shared_memory_measurer.cpp` | POSIX shared-memory cross-process ping-pong |
| `spectre_v1.cpp` | Spectre Variant 1 educational demo |
| `cpu_info.cpp` | Cache and CPU information via CPUID/TSC |
| `statistics.cpp` | Mean/stddev/min/max |
| `reporter.cpp` | Console, CSV, and history output |
| `platform_utils.cpp` | CPU affinity and best-effort priority adjustment |
| `timer.hpp` | `RDTSC`, `LFENCE`, and `CLFLUSH` primitives |

---

## Interpreting the results

A useful mental model is:

```text
Cache hit
  └── very close to the core/cache hierarchy

Forced cache miss
  └── cache eviction + demand load
      └── may involve lower cache levels and/or DRAM

Stride experiments
  └── access pattern + cache/TLB/prefetch interactions

Bandwidth
  └── sustained throughput of the benchmarked access pattern

Shared-memory round trip
  └── process communication + synchronization + cache coherence

Spectre demo
  └── speculative execution + cache side channel
```

Do not compare results across machines without controlling, or at least recording, factors such as:

- CPU microarchitecture
- core and thread topology
- CPU frequency / turbo behavior
- power-management state
- memory configuration and channels
- NUMA placement
- background system load
- kernel and scheduler behavior
- compiler and libc implementation
- virtualization / cloud environment

For reproducible studies, run multiple independent trials, retain raw samples, record the complete machine configuration, and avoid drawing conclusions from a single aggregate number.

---

## Known limitations and engineering notes

This repository is intentionally lightweight, so the current implementation has several limitations worth knowing before using it as a scientific benchmark:

1. **TSC frequency is used as the GHz conversion factor.** The code estimates the TSC rate over wall-clock time; this is not necessarily the instantaneous core frequency reported by the CPU.

2. **Timer overhead is not explicitly calibrated.** The measured region includes the cost of the timestamping/serialization sequence.

3. **Round statistics are aggregated approximately.** Per-round minima/maxima/stddev values are averaged instead of recomputed from all raw observations.

4. **Load/store tests are microbenchmarks, not end-to-end memory-access latencies.** In particular, the store test does not measure write-back completion to DRAM.

5. **Bandwidth numbers are implementation-specific throughput estimates.** `memset` and `memcpy` depend on compiler/libc and CPU-specific implementations.

6. **CPU topology detection is simplified.** Hyper-Threading detection and physical-core estimation are not a substitute for full topology parsing.

7. **Cross-process synchronization uses volatile shared flags.** This is intentionally simple, but a more rigorous implementation would use explicit atomic/inter-process synchronization semantics and document the memory-ordering assumptions.

8. **The benchmark is x86-specific.** It uses `RDTSC`, `LFENCE`, `CLFLUSH`, and x86 CPUID facilities.

These limitations do not make the project useless; they define it correctly as a **hardware-oriented educational and exploratory microbenchmark** rather than a standards-grade benchmark suite.

---

## Why this project is useful

The value of this project is not a single "memory latency" number. It provides a compact way to experiment with relationships between:

- cache hierarchy
- locality and stride
- cache eviction
- timing primitives
- CPU affinity
- memory allocation mechanisms
- Huge Pages
- cache coherence
- process synchronization
- speculative execution
- cache-based side channels

In other words, it connects high-level performance behavior to mechanisms that are normally hidden below application code.

---

## License

No project license has been selected yet. Until a license is added to the repository, reuse and redistribution should not be assumed to be permitted beyond the rights granted by applicable law.

---

## Author

**Mehrab Jalilian (مهراب جلیلیان)**

AI Engineer & Researcher

- GitHub: https://github.com/mehrabJA
- Website: https://mehrabjalilian.site/
- LinkedIn: https://www.linkedin.com/in/mehrab-jalilian
