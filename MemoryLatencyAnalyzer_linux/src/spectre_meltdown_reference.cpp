#include "spectre_meltdown_reference.hpp"

#include <iostream>

namespace {

const std::vector<VulnerabilityReference>& references() {
    static const std::vector<VulnerabilityReference> value{
        {
            "Spectre Variant 1",
            "CVE-2017-5753",
            "bounds-check bypass / speculative execution",
            true,
            "--spectre and --spectre-lab",
            "The project uses a self-contained toy victim and never accepts an external memory address."
        },
        {
            "Spectre Variant 2",
            "CVE-2017-5715",
            "branch target injection",
            false,
            "reference only",
            "No branch-target-injection exploit is implemented in this analyzer."
        },
        {
            "Meltdown",
            "CVE-2017-5754",
            "rogue data cache load / privilege-boundary bypass",
            false,
            "reference only",
            "No kernel or cross-process memory access is implemented; the project does not include a Meltdown exploit."
        }
    };

    return value;
}

} // namespace

std::vector<VulnerabilityReference>
SpectreMeltdownReference::entries() {
    return references();
}

std::string SpectreMeltdownReference::source_repository() noexcept {
    return "https://github.com/jarmouz/spectre_meltdown";
}

void SpectreMeltdownReference::print() {
    std::cout
        << "===== Spectre / Meltdown reference =====\n"
        << "Source repository: "
        << source_repository() << "\n\n";

    for (const auto& entry : references()) {
        std::cout
            << entry.name
            << " (" << entry.cve << ")\n"
            << "  class : " << entry.class_name << '\n'
            << "  local : "
            << (entry.locally_demonstrated ? "yes" : "no") << '\n'
            << "  mode  : " << entry.project_mode << '\n'
            << "  scope : " << entry.limitation << "\n\n";
    }

    std::cout
        << "Note: this mode is a vulnerability/reference catalog. "
           "It does not execute a Meltdown exploit or access external process/kernel memory.\n";
}
