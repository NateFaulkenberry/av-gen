#pragma once

// The diagnostic images (docs/quality-lab/artifact-detection.md §8, mandate §17, §32).
//
// **These are not presentation.** `docs/post-artifact-forensics.md` localised the anamorphic
// lattice to post stage 6 by capturing every intermediate target, after three plausible fixes had
// been shipped and reverted without localising anything, and its opening rule generalises exactly:
//
//     A post chain judged on its final frame cannot be debugged.
//
// The same is true of a quality vector judged on its pooled numbers. Every detector in the Lab emits
// an image, and every pooled number names the worst frame, because §32's question is "show me what
// caused this number" and the answer usually lives in three frames.
//
// Every image here is **absolute-scaled and says so in its own filename or in the report**: a
// heatmap normalised to its own maximum looks identical for a catastrophic frame and a clean one,
// which is how a diagnostic becomes decorative.

#include "capture/sequence.hpp"
#include "metrics/spatial.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::quality {

// |a - b| per channel, multiplied by `amplification` and clamped. The amplification is a stated
// constant rather than an auto-fit, for the reason in the header.
[[nodiscard]] Frame differenceImage(const Frame& a, const Frame& b, double amplification = 8.0);

// A single-channel map on a black-red-yellow-white ramp, with `fullScale` the value that reaches
// white. Used for the residual, the Laplacian and anything else per-pixel.
[[nodiscard]] Frame heatmapImage(const std::vector<float>& values, std::uint32_t width,
                                 std::uint32_t height, double fullScale);

// A mask, as white-on-black. The disocclusion mask and every AOV gate.
[[nodiscard]] Frame maskImage(const std::vector<std::uint8_t>& mask, std::uint32_t width,
                              std::uint32_t height);

// The per-pixel |spatial Laplacian| of luma, as a plane. `spatialLaplacian` in spatial.hpp pools
// this; the map is what localises a finding to "the grass beds" rather than "the frame".
[[nodiscard]] std::vector<float> laplacianPlane(const Frame& frame);

// The 8x8 tile grid of any per-pixel plane: the mean over each tile, and the worst tile's top-left
// pixel. artifact-detection.md §2 asks for this by name.
struct TileMap {
    std::uint32_t tilesX = 0;
    std::uint32_t tilesY = 0;
    std::uint32_t tileSize = 8;
    std::vector<double> means;
    std::uint32_t worstTileX = 0;
    std::uint32_t worstTileY = 0;
    double worstTileMean = 0.0;
};
[[nodiscard]] TileMap tileMap(const std::vector<float>& values, std::uint32_t width,
                              std::uint32_t height, std::uint32_t tileSize = 8);

} // namespace avgen::quality
