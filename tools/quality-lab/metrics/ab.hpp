#pragma once

// ADR-1110: ORIGINAL vs OPTIMIZED -- the objective differences the live optimizer reports (docs/live-optimizer/00-brief.md
// 5.4: pixel, structural, edge, luminance, temporal), each also inside the hero regions.
//
// The Lab's rule holds here too (ADR-250): every function is named for what it computes and none decides. The live
// optimizer ranks its candidates by one of these numbers and says which; a person judges. Nothing here claims two
// frames are "the same": the optimizer compares every number against the self-difference floor (ORIGINAL rendered
// twice) and the strongest thing it may say is "inside the floor".
//
// **Matched luminance** (the grain-metric exposure confound): a change that only moves the exposure moves every pixel
// and every pixel metric with it, so SSIM and the pixel difference are also reported after the OPTIMIZED frame is
// scaled to the ORIGINAL's mean luma. A lever whose whole effect is a brightness shift shows a large raw difference
// and a small matched one, and the luminance delta says how large the shift was.

#include "metrics/spatial.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::quality {

// A screen box in pixels, [x0, x1) x [y0, y1). The live optimizer writes one per on-screen hero entity.
struct Region {
    std::string name;
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct AbFrame {
    // pixel
    double meanAbsDiff = 0.0;      // mean |dRGB| over the three channels, 0..255 steps
    double changedFraction = 0.0;  // pixels whose largest channel difference exceeds 8 steps
    double psnr = 0.0;             // infinity for identical frames
    // structural
    double ssim = 1.0;
    double msSsim = 1.0;
    // edge (Sobel magnitude of luma)
    double edgeDifference = 0.0; // mean |G_a - G_b| / mean G_a
    double edgeStrengthRatio = 1.0; // mean G_b / mean G_a: below 1 the optimized frame is softer
    double edgesLost = 0.0;      // of the original's edge pixels (G_a above its 90th percentile), the fraction where
                                 // G_b fell under half that threshold
    double edgesAdded = 0.0;     // the converse, as a fraction of the original's edge-pixel count
    // luminance
    double lumaOriginal = 0.0;   // mean luma, 0..255
    double lumaOptimized = 0.0;
    double lumaDelta = 0.0;      // optimized - original
    double ssimMatched = 1.0;    // SSIM after the optimized frame is scaled to the original's mean luma
    double meanAbsDiffMatched = 0.0;
    // temporal (only when both previous frames were given)
    bool haveTemporal = false;
    double temporalDifference = 0.0;  // mean | |L_a(t)-L_a(t-1)| - |L_b(t)-L_b(t-1)| |, luma steps
    double temporalActivityRatio = 1.0; // mean frame-to-frame change of b over a's
    // inside the regions (only when regions were given and at least one is on the frame)
    bool haveRegions = false;
    double regionCoverage = 0.0;  // the fraction of the frame the regions cover (overlaps counted once)
    double regionMeanAbsDiff = 0.0;
    double regionLumaDelta = 0.0;
    double regionSsim = 1.0;      // area-weighted over the regions at least 8 px on a side
    double regionSsimMatched = 1.0;
};

// `previousA` / `previousB` may be null (no temporal numbers). Frames must share a shape; a mismatch returns a
// default-constructed result with `ssim` 0 so it cannot pass for "unchanged".
[[nodiscard]] AbFrame abCompare(const Frame& a, const Frame& b, const std::vector<Region>& regions = {},
                                const Frame* previousA = nullptr, const Frame* previousB = nullptr);

// A frame scaled so its mean luma matches `meanLuma` (channels clamped). What "matched luminance" applies.
[[nodiscard]] Frame scaledToLuma(const Frame& frame, double meanLuma);

// The Sobel gradient magnitude of luma, one value per pixel (borders 0).
[[nodiscard]] std::vector<float> sobelMagnitude(const Frame& frame);

// Pooled over a sequence: the mean, and the worst frame (lowest for SSIM-like, highest for differences).
struct AbPooled {
    double mean = 0.0;
    double worst = 0.0;
};
struct AbSequence {
    std::size_t frames = 0;
    std::vector<AbFrame> perFrame;
    AbPooled meanAbsDiff, changedFraction, psnr, ssim, msSsim, edgeDifference, edgeStrengthRatio, edgesLost,
        edgesAdded, lumaDelta, ssimMatched, meanAbsDiffMatched, temporalDifference, temporalActivityRatio,
        regionMeanAbsDiff, regionLumaDelta, regionSsim, regionSsimMatched;
    double lumaOriginal = 0.0, lumaOptimized = 0.0; // means
    bool haveTemporal = false;
    bool haveRegions = false;
    double regionCoverage = 0.0; // mean
};
[[nodiscard]] AbSequence abCompareSequence(const std::vector<Frame>& a, const std::vector<Frame>& b,
                                           const std::vector<Region>& regions = {});

} // namespace avgen::quality
