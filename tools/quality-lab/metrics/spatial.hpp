#pragma once

// Phase 2 of the Quality Lab: the full-reference spatial metrics (docs/quality-lab/metrics.md §2).
//
// Every function here is a pure function of two images and has no opinion about what its number
// means. That separation is the whole architecture: ADR-250 refuses a single score, and ADR-243 is
// the reason -- a correct detector ranked two remedies backwards against human judgement, so a metric
// that decides is a metric that can be confidently wrong. These compute; the report presents; a
// person judges.
//
// **Naming is deliberate.** Each function is named for what it COMPUTES, not for the artifact it is
// believed to indicate. `spatialLaplacian` is the mean absolute second spatial derivative, and
// whether that constitutes "aliasing" is an interpretation stated separately, with its limitations.
// The alternative -- a function called `aliasing()` -- makes the interpretation invisible and
// therefore unarguable.

#include <cstdint>
#include <vector>

namespace avgen::quality {

// An 8-bit RGBA frame, which is what every metric here consumes. Display-referred on purpose: VMAF,
// CAMBI, PSNR-HVS, MS-SSIM and CIEDE2000 are all *defined* on display-referred content, so a
// tone-mapped PNG is the correct input and not a compromise (ADR-251 §1).
struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool valid() const {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<std::size_t>(width) * height * 4;
    }
    [[nodiscard]] bool sameShapeAs(const Frame& other) const {
        return width == other.width && height == other.height;
    }
    [[nodiscard]] const std::uint8_t* pixel(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

// Rec.709 luma over the 8-bit channels, one plane. **The range is 0..255 and not 0..1**: the
// coefficients are applied to the bytes, so every metric below reports in luma STEPS, which is what
// their units say and what `spatialLaplacian`'s "0..255 luma" means. The comment here claimed 0..1
// until 2026-09-17, when a caller believed it and produced a table of luma differences in the tens
// of thousands.
[[nodiscard]] std::vector<float> luma(const Frame& frame);

// ---- the metrics -------------------------------------------------------------------------------

// 10*log10(MAX^2/MSE) over RGB. Infinity for identical frames.
//
// **Not a quality metric here, and the ladder proves it rather than asserting it**: the exposure arm
// shifts every pixel by 5%, which collapses PSNR while MS-SSIM barely moves. Its job is the alignment
// test -- if two renders that should be identical are not, this is what says so first and loudest.
[[nodiscard]] double psnr(const Frame& a, const Frame& b);

// Single-scale SSIM on luma, 8x8 windows, the standard C1/C2 stabilisers.
[[nodiscard]] double ssim(const Frame& a, const Frame& b);

// Multi-scale SSIM: SSIM at successive half-resolutions, combined with the Wang 2003 exponents.
//
// Reported instead of `ssim` because a single scale cannot tell a change in fine detail from a change
// in coarse structure, and those are different failures. It still responds identically to *more
// aliasing* and *more genuine detail*, which is why metrics.md forbids it deciding an AA comparison
// alone.
[[nodiscard]] double msSsim(const Frame& a, const Frame& b);

// Mean and 95th-percentile CIEDE2000 colour difference, via sRGB -> linear -> XYZ -> L*a*b*.
//
// Defined for surface colour under a reference illuminant and used here on tone-mapped emissive
// content, so the absolute value means little: it is **comparable between arms of one experiment**
// and not against any published threshold.
struct ColourDifference {
    double mean = 0.0;
    double p95 = 0.0;
};
[[nodiscard]] ColourDifference ciede2000(const Frame& a, const Frame& b);

// Mean |spatial Laplacian| of luma -- the measure ADR-243 found tracks the eye on the artifact
// viewers actually report, where the temporal detector did not.
//
// A property of ONE frame, so it is reported as a ratio between candidate and reference. Above 1 the
// candidate has more high-frequency spatial energy than the reference, which is what both aliasing
// and genuine extra detail look like; the ladder's aliasing and over-sharpen arms are both > 1, and
// distinguishing them is not this number's job.
[[nodiscard]] double spatialLaplacian(const Frame& frame);

// The fraction of pixels whose 5-bit quantised luma differs from a local median of the same --
// a cheap banding proxy standing in for CAMBI until libvmaf's is wired in.
//
// Named for what it computes. It rises on a quantised gradient and is near zero on a dithered or
// textured one, which is the behaviour the ladder's banding arm checks.
[[nodiscard]] double quantisationSteps(const Frame& frame);

} // namespace avgen::quality
