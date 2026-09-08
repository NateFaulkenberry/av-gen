#include "analysis/fft.hpp"

#include <kiss_fftr.h>

#include <cassert>
#include <cmath>
#include <vector>

namespace avgen::analysis {

struct RealFFT::Impl {
    explicit Impl(std::size_t n)
        : cfg(kiss_fftr_alloc(static_cast<int>(n), 0, nullptr, nullptr))
        , bins(n / 2 + 1) {}
    ~Impl() {
        if (cfg != nullptr) {
            kiss_fftr_free(cfg);
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    kiss_fftr_cfg cfg = nullptr;
    std::vector<kiss_fft_cpx> bins; // scratch for magnitude()
};

RealFFT::RealFFT(std::size_t size)
    : impl_(std::make_unique<Impl>(size))
    , size_(size) {
    assert(size >= 2 && size % 2 == 0 && "RealFFT size must be even");
}

RealFFT::~RealFFT() = default;
RealFFT::RealFFT(RealFFT&&) noexcept = default;
RealFFT& RealFFT::operator=(RealFFT&&) noexcept = default;

void RealFFT::forward(std::span<const float> input, std::span<std::complex<float>> output) {
    assert(input.size() == size_ && output.size() == binCount());
    static_assert(sizeof(kiss_fft_cpx) == sizeof(std::complex<float>));
    static_assert(alignof(kiss_fft_cpx) == alignof(std::complex<float>));
    // std::complex<float> is layout-compatible with {float r; float i;} (array-oriented access,
    // [complex.numbers.general]), so the output span can receive KissFFT's result directly.
    kiss_fftr(impl_->cfg, input.data(), reinterpret_cast<kiss_fft_cpx*>(output.data()));
}

void RealFFT::magnitude(std::span<const float> input, std::span<float> outMagnitude) {
    assert(input.size() == size_ && outMagnitude.size() == binCount());
    kiss_fftr(impl_->cfg, input.data(), impl_->bins.data());
    const std::size_t bins = binCount();
    const float n = static_cast<float>(size_);
    const float interiorScale = 2.0f / n;
    const float edgeScale = 1.0f / n;
    for (std::size_t k = 0; k < bins; ++k) {
        const kiss_fft_cpx& c = impl_->bins[k];
        const float scale = (k == 0 || k == bins - 1) ? edgeScale : interiorScale;
        outMagnitude[k] = std::sqrt(c.r * c.r + c.i * c.i) * scale;
    }
}

} // namespace avgen::analysis
