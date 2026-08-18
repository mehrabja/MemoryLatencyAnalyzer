#pragma once
#include "latency_measurer.hpp"
#include "bandwidth_measurer.hpp"
#include <vector>
#include <string>

class Reporter {
public:
    static void print_console(const std::vector<MeasurementResult>& results,
                              double cpu_freq_ghz,
                              bool verbose);

    static void print_bandwidth(const std::vector<BandwidthResult>& results,
                                double cpu_freq_ghz);

    static void write_csv(const std::string& filename,
                          const std::vector<MeasurementResult>& results,
                          double cpu_freq_ghz);

    static void append_history(const std::string& filename,
                               const std::vector<MeasurementResult>& results);
};