#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VulnerabilityReference {
    std::string name;
    std::string cve;
    std::string class_name;
    bool locally_demonstrated = false;
    std::string project_mode;
    std::string limitation;
};

class SpectreMeltdownReference {
public:
    static std::vector<VulnerabilityReference> entries();
    static void print();
    static std::string source_repository() noexcept;
};
