#pragma once

// Signed distance fields from glyph coverage (ADR-081).
//
// A glyph is rasterised once, at `scale` times the atlas resolution, into an 8-bit coverage
// bitmap. This turns that bitmap into a single-channel SDF at 1x: exact Euclidean distances
// (Felzenszwalb's separable transform) measured on the high-resolution image, then averaged down.
//
// Averaging *distances* rather than coverage is the whole point. Downsampled coverage is an
// antialiased bitmap and looks soft the moment it is magnified; downsampled distance is still a
// distance field, so the shader can reconstruct a hard edge at any size. It is also what keeps
// corners: a corner in a distance field averaged from 4x4 samples is off by a quarter of a texel,
// where a corner in a 1x field is off by a whole one.
//
// Deterministic: integer input, no floating-point ordering hazards beyond a fixed loop order, the
// same bytes on every run. Offline renders and live playback share the atlas byte for byte.

#include <cstdint>
#include <vector>

namespace avgen::comp {

// The distance the 0..255 range encodes, either side of the edge, in 1x texels. 0.5 is the edge.
inline constexpr float kSdfSpreadTexels = 6.0f;
// Em size of a glyph in the atlas, in 1x texels. Text is scaled from here by the shader, so this
// is a quality/memory knob and not a limit on how large text can be drawn.
inline constexpr std::uint32_t kSdfEmTexels = 64;
// How much finer the coverage raster is than the field it produces.
inline constexpr std::uint32_t kSdfRasterScale = 4;

// Exact squared Euclidean distance transform of one row/column (Felzenszwalb & Huttenlocher).
// `f` holds the sampled function; the result replaces it. Exposed for its own test.
void distanceTransform1d(std::vector<float>& f, std::vector<float>& scratch, std::vector<int>& v,
                         std::vector<float>& z);

struct SdfBitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> texels; // width*height, 0.5 (128) on the outline
    [[nodiscard]] bool empty() const { return width == 0 || height == 0; }
    [[nodiscard]] float sample(std::uint32_t x, std::uint32_t y) const {
        return static_cast<float>(texels[y * width + x]) / 255.0f;
    }
};

// `coverage` is `width` x `height` 8-bit alpha at `scale` times the output resolution. The output
// is width/scale x height/scale. A scale that does not divide the input exactly is rounded down.
[[nodiscard]] SdfBitmap buildSdf(const std::uint8_t* coverage, std::uint32_t width, std::uint32_t height,
                                 std::uint32_t scale, float spreadTexels = kSdfSpreadTexels);

} // namespace avgen::comp
