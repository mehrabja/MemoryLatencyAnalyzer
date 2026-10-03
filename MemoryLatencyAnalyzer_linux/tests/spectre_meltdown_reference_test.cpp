#include "spectre_meltdown_reference.hpp"

#include <cassert>
#include <string>

int main() {
    const auto entries = SpectreMeltdownReference::entries();
    assert(entries.size() == 3U);

    assert(entries[0].cve == "CVE-2017-5753");
    assert(entries[0].locally_demonstrated);
    assert(entries[1].cve == "CVE-2017-5715");
    assert(!entries[1].locally_demonstrated);
    assert(entries[2].cve == "CVE-2017-5754");
    assert(!entries[2].locally_demonstrated);

    assert(
        SpectreMeltdownReference::source_repository() ==
        "https://github.com/jarmouz/spectre_meltdown");

    return 0;
}
