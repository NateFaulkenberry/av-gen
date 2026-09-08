#pragma once

// Real-to-complex FFT behind a small interface (ADR-004). KissFFT (scalar, portable, bit-identical
// across platforms) is the first implementation; SIMD backends can be added behind this class.

#include <complex>
#include <cstddef>
#include <memory>
#include <span>

namespace avgen::analysis {

class RealFFT {
public:
    explicit RealFFT(std::size_t size); // size must be even; power of two recommended
    ~RealFFT();
    RealFFT(RealFFT&&) noexcept;
    RealFFT& operator=(RealFFT&&) noexcept;
    RealFFT(const RealFFT&) = delete;
    RealFFT& operator=(const RealFFT&) = delete;

    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] std::size_t binCount() const { return size_ / 2 + 1; }

    // input.size() == size(); output.size() == binCount(). Unnormalised (like FFTW/KissFFT).
    void forward(std::span<const float> input, std::span<std::complex<float>> output);

    // Convenience: |X[k]| for each bin, scaled by 2/size so a full-scale sine reads ~1.0 at its bin
    // (with a rectangular window; apply the window's coherent gain otherwise).
    void magnitude(std::span<const float> input, std::span<float> outMagnitude);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::size_t size_;
};

} // namespace avgen::analysis
