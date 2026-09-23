# MemoryLatencyAnalyzer

**MemoryLatencyAnalyzer** is a Linux/C++17 tool for measuring memory latency, cache hierarchy behavior, memory bandwidth, and related system-performance characteristics.

> Developed by [Mehrab Jalilian](https://github.com/mehrabJA).

## What it measures

- CPU and memory-system characteristics
- Cache hierarchy and access latency
- Memory latency
- Sequential and random memory-access behavior
- Memory bandwidth
- Statistical summaries and reports

The implementation is organized around dedicated measurement and reporting components, including latency measurement, bandwidth measurement, CPU information, shared-memory measurement, statistics, and reporting.

## Build

The Linux implementation is under the [MemoryLatencyAnalyzer_linux](./MemoryLatencyAnalyzer_linux/) directory.

Requirements:

- Linux
- CMake 3.15+
- C++17-compatible compiler

Build with CMake:

~~~bash
cd MemoryLatencyAnalyzer_linux
cmake -S . -B build
cmake --build build
~~~

The resulting executable is named `latency_analyzer`.

## Project structure

~~~text
MemoryLatencyAnalyzer_linux/
├── include/     # C++ headers
├── src/         # measurement and analysis code
├── config/      # project configuration
└── CMakeLists.txt
~~~

## Why this project

Memory performance is affected by cache locality, access patterns, CPU behavior, and system-level contention. MemoryLatencyAnalyzer is intended as a lightweight way to inspect these effects from a Linux system.

## Related topics

Memory latency · cache performance · memory bandwidth · CPU performance · Linux systems programming · C++17 · performance analysis · benchmarking

## Author

**Mehrab Jalilian (مهراب جلیلیان)**
AI Engineer & Researcher

- GitHub: https://github.com/mehrabJA
- Website: https://mehrabjalilian.site/
- LinkedIn: https://www.linkedin.com/in/mehrab-jalilian

## License

Add the project's license here when one is selected.
