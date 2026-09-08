#pragma once

#include <cstddef>
#include <vector>

namespace avgen::analysis {

// Periodic Hann window (for STFT use), length n.
std::vector<float> hannWindow(std::size_t n);

// Sum of window samples / n: the coherent gain used to normalise sine amplitude (Hann: 0.5).
float coherentGain(const std::vector<float>& window);

} // namespace avgen::analysis
