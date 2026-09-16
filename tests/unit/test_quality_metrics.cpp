// The Quality Lab's §34 distortion ladder — the gate the architecture sets for Phase 2.
//
// Each arm takes one base image, applies exactly one known distortion, and asserts **both halves**:
// the metric that must move, and the metrics that must *not*. The second half is what makes this a
// validation rather than a smoke test. A metric that rises for every distortion is not detecting
// anything; it is detecting that something changed, which the file size already told us.
//
// This exists because of ADR-243, where a correct detector ranked two remedies backwards against a
// human viewer. The defence is not a better detector -- it is knowing, before trusting a number, what
// that number does and does not respond to. Every limitation in docs/quality-lab/metrics.md is a
// claim, and the claims that can be checked on synthetic input are checked here.

#include "metrics/spatial.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 96;

// A base image with the three things the ladder needs to distinguish: a smooth gradient (where
// banding shows and texture does not), fine high-contrast detail (where blur and aliasing show), and
// saturated colour (where a colour shift shows).
quality::Frame baseImage() {
    quality::Frame f;
    f.width = kW;
    f.height = kH;
    f.rgba.assign(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
            if (y < kH / 2) {
                // A smooth horizontal gradient over the full range, so 5-bit quantisation has
                // somewhere to make stairs.
                const auto v = static_cast<std::uint8_t>((x * 255) / (kW - 1));
                p[0] = v;
                p[1] = v;
                p[2] = static_cast<std::uint8_t>(200);
            } else {
                // Fine vertical bars, one pixel wide: the thinnest thing a renderer has to resolve,
                // and what blur destroys and aliasing mangles.
                const bool on = (x % 2) == 0;
                p[0] = on ? 240 : 20;
                p[1] = on ? 60 : 200;
                p[2] = on ? 40 : 90;
            }
            p[3] = 255;
        }
    }
    return f;
}

quality::Frame blurred(const quality::Frame& src, double sigma) {
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> kernel;
    double total = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-(i * i) / (2.0 * sigma * sigma));
        kernel.push_back(w);
        total += w;
    }
    for (double& w : kernel) {
        w /= total;
    }
    quality::Frame out = src;
    // Separable, two passes.
    for (int axis = 0; axis < 2; ++axis) {
        const quality::Frame in = out;
        for (std::uint32_t y = 0; y < src.height; ++y) {
            for (std::uint32_t x = 0; x < src.width; ++x) {
                double acc[3] = {0.0, 0.0, 0.0};
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = axis == 0 ? std::clamp(static_cast<int>(x) + k, 0,
                                                          static_cast<int>(src.width) - 1)
                                             : static_cast<int>(x);
                    const int sy = axis == 1 ? std::clamp(static_cast<int>(y) + k, 0,
                                                          static_cast<int>(src.height) - 1)
                                             : static_cast<int>(y);
                    const std::uint8_t* p =
                        in.pixel(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy));
                    const double w = kernel[static_cast<std::size_t>(k + radius)];
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += w * p[c];
                    }
                }
                std::uint8_t* q = out.rgba.data() + (static_cast<std::size_t>(y) * src.width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    q[c] = static_cast<std::uint8_t>(std::clamp(acc[c], 0.0, 255.0));
                }
            }
        }
    }
    return out;
}

// A diagonal edge, with and without antialiasing.
//
// **The first version of this arm downsampled one-pixel bars and it was wrong**, in a way worth
// recording. Bars one pixel wide sit exactly at Nyquist, so sampling every other column lands on the
// same phase every time and the region comes back FLAT -- mean |Laplacian| fell from 57.9 to 3.2.
// That is total signal loss, not aliasing, and a metric that rose for it would be responding to
// something else entirely.
//
// What ADR-243 actually measured is the staircase on an unfiltered edge: turning FXAA off raised the
// spatial measure by 31% on grass, and the reviewer agreed it looked worse. So the arm is an edge
// with coverage-based antialiasing against the same edge snapped to whole pixels -- "AA on" against
// "AA off", which is the comparison the metric exists to make.
quality::Frame diagonalEdge(bool antialiased) {
    quality::Frame f;
    f.width = kW;
    f.height = kH;
    f.rgba.assign(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            // Signed distance to a shallow diagonal, so the edge crosses many pixels per row and the
            // staircase has somewhere to show.
            const double d = static_cast<double>(y) - (0.35 * static_cast<double>(x) + 12.0);
            double coverage = antialiased ? std::clamp(0.5 - d, 0.0, 1.0) : (d < 0.0 ? 1.0 : 0.0);
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
            const auto v = static_cast<std::uint8_t>(std::clamp(coverage * 235.0 + 10.0, 0.0, 255.0));
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
    return f;
}

quality::Frame quantised(const quality::Frame& src, int bits) {
    quality::Frame out = src;
    const int levels = 1 << bits;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const int v = out.rgba[i + c] * (levels - 1) / 255;
            out.rgba[i + c] = static_cast<std::uint8_t>(v * 255 / (levels - 1));
        }
    }
    return out;
}

quality::Frame exposed(const quality::Frame& src, double scale) {
    quality::Frame out = src;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            out.rgba[i + c] =
                static_cast<std::uint8_t>(std::clamp(out.rgba[i + c] * scale, 0.0, 255.0));
        }
    }
    return out;
}

// A shift in the green/red balance only: a colour change that leaves luma structure alone, which is
// what makes it the arm that separates a colour metric from a structural one.
quality::Frame colourShifted(const quality::Frame& src, int amount) {
    quality::Frame out = src;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i + 0] = static_cast<std::uint8_t>(std::clamp(out.rgba[i + 0] + amount, 0, 255));
        out.rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(out.rgba[i + 1] - amount, 0, 255));
    }
    return out;
}

} // namespace

TEST_CASE("identity: every metric reports no difference", "[quality][ladder]") {
    const quality::Frame a = baseImage();
    // The arm that would catch a metric wired to the wrong buffer, a stale cache, or an accidental
    // in-place modification. If this fails nothing else in the file means anything.
    CHECK(std::isinf(quality::psnr(a, a)));
    CHECK(quality::msSsim(a, a) == Approx(1.0).margin(1e-9));
    CHECK(quality::ssim(a, a) == Approx(1.0).margin(1e-9));
    CHECK(quality::ciede2000(a, a).mean == Approx(0.0).margin(1e-9));
    CHECK(quality::ciede2000(a, a).p95 == Approx(0.0).margin(1e-9));
}

TEST_CASE("blur: detail falls, and banding does not appear", "[quality][ladder]") {
    const quality::Frame base = baseImage();
    const quality::Frame slight = blurred(base, 0.5);
    const quality::Frame severe = blurred(base, 3.0);

    const double baseDetail = quality::spatialLaplacian(base);
    const double slightDetail = quality::spatialLaplacian(slight);
    const double severeDetail = quality::spatialLaplacian(severe);
    INFO("laplacian base " << baseDetail << " slight " << slightDetail << " severe " << severeDetail);

    // Must move: detail retention falls, and further for the heavier blur.
    CHECK(slightDetail < baseDetail);
    CHECK(severeDetail < slightDetail);
    CHECK(quality::msSsim(base, severe) < quality::msSsim(base, slight));

    // Must NOT move: blurring cannot create banding. A banding metric that rises here is responding
    // to "the image changed", which is the failure this half of the ladder exists to catch.
    CHECK(quality::quantisationSteps(severe) <= quality::quantisationSteps(base) + 0.01);
}

TEST_CASE("aliasing: an unfiltered edge has MORE high-frequency energy", "[quality][ladder]") {
    const quality::Frame filtered = diagonalEdge(true);
    const quality::Frame jagged = diagonalEdge(false);

    // The direction is the point. Aliasing does not look like blur to this metric -- it looks like
    // its opposite, because an unfiltered edge puts a full-contrast step where a filtered one puts a
    // ramp. A detector that reported "less detail" for both could not tell an AA improvement from an
    // AA regression, which is exactly the confusion ADR-243 documents.
    const double smooth = quality::spatialLaplacian(filtered);
    const double stairs = quality::spatialLaplacian(jagged);
    INFO("laplacian filtered " << smooth << " jagged " << stairs);
    CHECK(stairs > smooth);

    // And it is a real structural difference, so MS-SSIM sees it too -- but note it cannot say WHICH
    // is better, only that they differ. That is the division of labour metrics.md describes.
    CHECK(quality::msSsim(filtered, jagged) < 1.0);

    // Must NOT move: an edge treatment is not a colour change. If CIEDE2000 moved sharply here it
    // would be tracking luminance structure, and its claim to measure something the others cannot
    // would be false.
    const quality::ColourDifference delta = quality::ciede2000(filtered, jagged);
    INFO("dE00 mean " << delta.mean);
    CHECK(delta.mean < 8.0);
}

TEST_CASE("banding: the quantisation metric rises and detail does not", "[quality][ladder]") {
    const quality::Frame base = baseImage();
    const quality::Frame banded = quantised(base, 5);

    const double baseSteps = quality::quantisationSteps(base);
    const double bandedSteps = quality::quantisationSteps(banded);
    INFO("steps base " << baseSteps << " banded " << bandedSteps);
    CHECK(bandedSteps > baseSteps);

    // Must NOT move much: quantising a gradient does not sharpen or soften the bars below it. A
    // large change here would mean the metric is reading the whole frame rather than the gradient.
    const double ratio = quality::spatialLaplacian(banded) / quality::spatialLaplacian(base);
    INFO("laplacian ratio " << ratio);
    CHECK(ratio == Approx(1.0).margin(0.35));
}

TEST_CASE("colour shift: the colour metric moves and the structural ones do not",
          "[quality][ladder]") {
    const quality::Frame base = baseImage();
    const quality::Frame shifted = colourShifted(base, 12);

    const quality::ColourDifference delta = quality::ciede2000(base, shifted);
    INFO("dE00 mean " << delta.mean << " p95 " << delta.p95);
    CHECK(delta.mean > 1.0);

    // Must NOT move: the structure is untouched. This is the arm that proves CIEDE2000 is measuring
    // something the others cannot -- if MS-SSIM moved as much, the colour metric would be redundant
    // and metrics.md's claim that it "provides information unavailable from our other metrics"
    // would be false.
    const double structural = quality::spatialLaplacian(shifted) / quality::spatialLaplacian(base);
    INFO("laplacian ratio " << structural);
    CHECK(structural == Approx(1.0).margin(0.1));
}

TEST_CASE("exposure: PSNR collapses while structure barely moves", "[quality][ladder]") {
    const quality::Frame base = baseImage();
    const quality::Frame brighter = exposed(base, 1.05);

    // **This arm is why PSNR is not a quality metric here, demonstrated rather than asserted.**
    // A 5% exposure change is a grade, not a defect; PSNR reports it as a large error because every
    // pixel moved, and MS-SSIM correctly reports that the picture is the same picture.
    const double p = quality::psnr(base, brighter);
    const double ms = quality::msSsim(base, brighter);
    INFO("psnr " << p << " dB, msSsim " << ms);
    CHECK(p < 45.0);          // a "poor" PSNR by any published rule of thumb
    CHECK(ms > 0.95);         // and a picture a viewer would call identical in structure

    // The two disagreeing is the finding, not a problem with either -- which is the pairing rule
    // ADR-243 bought: pair every metric with one that measures a different failure, and when they
    // disagree, the disagreement is the result.
    CHECK(quality::psnr(base, base) > p);
}
