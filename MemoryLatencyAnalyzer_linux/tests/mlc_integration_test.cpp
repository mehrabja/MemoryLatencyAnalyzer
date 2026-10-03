#include "mlc_compare.hpp"

#include <iostream>

int main() {
    if (MlcComparison::find_mlc().empty()) {
        std::cerr << "Intel MLC not installed; integration test skipped\n";
        return 77;
    }

    return 0;
}
