#include "analysis/window.hpp"

#include <cmath>
#include <numbers>

namespace avgen::analysis {

std::vector<float> hannWindow(std::size_t n) {
    std::vector<float> w(n, 1.0f);
    if (n == 0) {
        return w;
    }
    // Periodic form (denominator n, not n - 1): the correct choice for STFT overlap-add.
    const double step = 2.0 * std::numbers::pi / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = static_cast<float>(0.5 - 0.5 * std::cos(step * static_cast<double>(i)));
    }
    return w;
}

float coherentGain(const std::vector<float>& window) {
    if (window.empty()) {
        return 1.0f;
    }
    double sum = 0.0;
    for (const float v : window) {
        sum += static_cast<double>(v);
    }
    return static_cast<float>(sum / static_cast<double>(window.size()));
}

} // namespace avgen::analysis
