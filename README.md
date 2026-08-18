# MemoryLatencyAnalyzer

A powerful Linux-based tool for analyzing and measuring memory subsystem performance, including cache latency, memory bandwidth, and system memory characteristics.

## Overview

MemoryLatencyAnalyzer is a lightweight, efficient utility designed to help developers and system administrators understand their system's memory hierarchy performance. Whether you're optimizing applications, debugging performance issues, or characterizing hardware, this tool provides detailed insights into:

- **Cache Latency**: L1, L2, and L3 cache access times
- **Memory Bandwidth**: Read/write throughput across different access patterns
- **Memory Latency**: Main memory access latency
- **Cache Hierarchy**: Complete memory subsystem profiling
- **Access Patterns**: Sequential and random memory access performance

## Features

✨ **Key Capabilities:**

- High-precision latency measurements using CPU cycle counters
- Minimal overhead design for accurate results
- Support for various memory access patterns
- Detailed performance metrics and statistics
- Real-time profiling capabilities
- Multi-threaded analysis support
- Hardware-aware optimization suggestions

## Requirements

- **OS**: Linux (kernel 2.6+)
- **Architecture**: x86_64, ARM64, or other POSIX-compliant systems
- **Compiler**: GCC 4.8+ or Clang 3.3+
- **Privileges**: Administrator/root access for certain measurements
- **Memory**: Minimal (< 100MB for most analyses)

## Installation

### From Source

```bash
git clone https://github.com/mehrabJA/MemoryLatencyAnalyzer.git
cd MemoryLatencyAnalyzer
make build
sudo make install
```

### Building

```bash
# Compile with optimizations
make all

# Build with debug symbols
make DEBUG=1

# Clean build artifacts
make clean
```

## Quick Start

### Basic Usage

```bash
# Measure overall memory latency
memory-latency-analyzer --measure

# Analyze cache hierarchy
memory-latency-analyzer --cache-hierarchy

# Test memory bandwidth
memory-latency-analyzer --bandwidth

# Profile specific access patterns
memory-latency-analyzer --pattern sequential
memory-latency-analyzer --pattern random
```

### Advanced Examples

```bash
# Detailed cache analysis with statistics
memory-latency-analyzer --cache-hierarchy --verbose --stats

# Bandwidth test with specific thread count
memory-latency-analyzer --bandwidth --threads 4

# Custom working set size
memory-latency-analyzer --measure --size 64M

# Generate detailed report
memory-latency-analyzer --measure --report output.txt

# Real-time monitoring
memory-latency-analyzer --monitor --interval 100ms
```

## Output Interpretation

### Latency Measurements

```
L1 Cache:    ~4-5 ns (hits)
L2 Cache:    ~10-20 ns
L3 Cache:    ~40-75 ns
Main Memory: ~100-300 ns
```

### Bandwidth Results

Results are reported in GB/s for different access patterns:

- **Sequential Read**: Theoretical maximum bandwidth
- **Sequential Write**: Memory write throughput
- **Random Access**: Cache-inefficient access patterns
- **Mixed Operations**: Combined read/write scenarios

## Command-Line Options

```
General Options:
  -h, --help              Show help message
  -v, --verbose           Enable verbose output
  --version               Display version information

Measurement Options:
  -m, --measure           Measure latency
  -b, --bandwidth         Measure bandwidth
  -c, --cache-hierarchy   Profile cache hierarchy
  --pattern               Access pattern (sequential, random, stride)
  --size                  Working set size (e.g., 1M, 64M, 1G)
  --iterations            Number of test iterations
  --threads               Number of threads to use

Output Options:
  --report FILE           Save results to file
  --json                  Output in JSON format
  --stats                 Include statistical analysis
  --verbose               Verbose output mode
  -q, --quiet             Minimal output
```

## Performance Tuning

### System Configuration

Before running measurements, consider:

```bash
# Disable CPU frequency scaling for consistent results
sudo cpupower frequency-set --governor performance

# Reduce background noise
sudo systemctl stop irqbalance
sudo nice -n 19 memory-latency-analyzer --measure
```

### Interpretation Tips

- **Spiky Results**: Indicates system interference; run with fewer background processes
- **Variable Latency**: Normal on systems with dynamic frequency scaling
- **Lower Bandwidth**: May indicate memory controller saturation or NUMA effects
- **Cache Discrepancies**: Check CPU model specs; some CPUs have asymmetric caches

## Output Examples

```
=== Memory Latency Analysis ===
System: Intel Core i7-9700K
Timestamp: 2024-01-15 14:32:45

Cache Hierarchy:
  L1 Instruction Cache: 32 KB    Latency: 4.2 ns
  L1 Data Cache:        32 KB    Latency: 4.1 ns
  L2 Cache:            256 KB    Latency: 12.5 ns
  L3 Cache:             12 MB    Latency: 42.3 ns
  Main Memory:          32 GB    Latency: 187.4 ns

Memory Bandwidth:
  Sequential Read:  42.3 GB/s
  Sequential Write: 38.1 GB/s
  Random Access:    2.1 GB/s
```

## Troubleshooting

### Permission Denied
```bash
# Ensure you have root/sudo access
sudo memory-latency-analyzer --measure
```

### Inconsistent Results
```bash
# Disable frequency scaling and reduce background load
sudo cpupower frequency-set --governor performance
sudo memory-latency-analyzer --measure --iterations 10 --stats
```

### High Variability
- Check for background processes: `top`, `htop`
- Disable Turbo Boost/hyper-threading if needed
- Run on isolated CPU cores if available

## Architecture

The tool is built with:

- **Core Engine**: Low-level memory access patterns in optimized assembly
- **Measurement Frontend**: User-friendly command-line interface
- **Analysis Module**: Statistical processing and result formatting
- **Hardware Abstraction**: Cross-platform support layer

## Supported Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| x86_64 Linux | ✅ Fully Supported | Primary platform |
| ARM64 Linux | ✅ Supported | Raspberry Pi 4+ recommended |
| RISC-V | 🟡 Experimental | Limited testing |
| PowerPC | 🟡 Experimental | Community contributions welcome |

## Contributing

We welcome contributions! Please:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Commit changes: `git commit -m 'Add amazing feature'`
4. Push to branch: `git push origin feature/amazing-feature`
5. Open a Pull Request

## Known Limitations

- NUMA systems may show varying results across nodes
- Hyper-threading can introduce measurement noise
- Results are CPU-specific; comparisons should use identical hardware
- Some CPU models have undocumented cache behavior
- Real-time results may vary with system load

## Performance Notes

- **Accuracy**: ±5-10% typical on unloaded systems
- **Overhead**: Minimal impact on system performance
- **Runtime**: Most tests complete in seconds
- **Memory Usage**: < 50MB for all modes

## License

[Add your license here - e.g., MIT, GPL, Apache 2.0]

## References

- Intel Memory Latency Checker: https://www.intel.com/content/www/us/en/download/736633/
- Linux Kernel Memory Management: https://www.kernel.org/doc/html/latest/vm/
- PAPI (Performance API): http://icl.utk.edu/papi/

## Support & Issues

- 📋 Report bugs: [Issues](https://github.com/mehrabJA/MemoryLatencyAnalyzer/issues)
- 💬 Discussions: [GitHub Discussions](https://github.com/mehrabJA/MemoryLatencyAnalyzer/discussions)
- 📧 Email: [Add contact email]

## Changelog

### Version 1.0.0
- Initial release
- Basic latency and bandwidth measurements
- Cache hierarchy profiling
- Command-line interface

## Acknowledgments

Special thanks to:
- Contributors and testers
- Open-source community
- Inspired by various memory profiling tools

---

**Made with ❤️ for performance enthusiasts and system engineers**
