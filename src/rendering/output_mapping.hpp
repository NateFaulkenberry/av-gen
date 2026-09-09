#pragma once

// Output mapping (milestone 1.2): how one final frame lands on one output. GPU-free so the
// homography, blend weights and JSON are unit-testable; rendering/output_mapper.hpp draws it.
//
// Target space is the output's normalised [0,1]^2 with y down (top-left origin, like texture
// coordinates). The source quad's corners are placed in that space (TL, TR, BR, BL); the
// projective transform between the unit square and that quad is a homography computed here and
// inverted on the GPU per pixel (perspective-correct). Everything outside the quad is black.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>

namespace avgen::rendering {

struct OutputCrop {
    float x = 0.0f; // in 0..1 of the source, top-left origin
    float y = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
};

struct OutputBlend {
    float left = 0.0f; // widths in 0..1 of the (warped) source quad's own axes
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

struct OutputMapping {
    OutputCrop crop;
    // Target-space positions of the source quad's corners: TL, TR, BR, BL.
    std::array<glm::vec2, 4> corners = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    OutputBlend blend;
    float blendGamma = 2.2f;  // soft edge: weight = pow(distance / width, blendGamma)
    float brightness = 1.0f;  // multiplier on the (encoded) colour
    float gamma = 1.0f;       // out = pow(in, 1 / gamma); > 1 lifts midtones
    bool flipX = false;
    bool flipY = false;

    static OutputMapping identity() { return {}; }
    // True when drawing this mapping is a plain copy of the source (the mapper's fast path).
    [[nodiscard]] bool isIdentity() const;
    // Both corner orderings must be convex and non-degenerate; crop inside 0..1 with positive size.
    [[nodiscard]] Result<void> validate() const;

    [[nodiscard]] nlohmann::json toJson() const;
    static Result<OutputMapping> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

bool operator==(const OutputCrop& a, const OutputCrop& b);
bool operator==(const OutputBlend& a, const OutputBlend& b);
bool operator==(const OutputMapping& a, const OutputMapping& b);

// Homography H (column-major, glm convention) with H * (u, v, 1) ~ the target position of the
// unit-square point (u, v): (0,0) -> corners[0], (1,0) -> corners[1], (1,1) -> corners[2],
// (0,1) -> corners[3]. Affine quads give a matrix with a bottom row of (0, 0, 1).
[[nodiscard]] glm::mat3 homographyFromCorners(const std::array<glm::vec2, 4>& corners);
// Inverse of homographyFromCorners, normalised so that w > 0 at the quad centre (the shader
// rejects w <= 0 as "outside").
[[nodiscard]] glm::mat3 inverseHomography(const std::array<glm::vec2, 4>& corners);
// Applies a homography with the perspective divide.
[[nodiscard]] glm::vec2 projectPoint(const glm::mat3& h, glm::vec2 p);

// Soft-edge weight for a point `distance` (0..1) from an edge with blend `width`: 1 when the
// width is zero or the point is beyond it, else pow(distance / width, gamma).
[[nodiscard]] float blendWeight(float distance, float width, float gamma);
// Product of the four edge weights at quad coordinates uv (0..1, top-left origin).
[[nodiscard]] float blendWeightAt(glm::vec2 uv, const OutputBlend& blend, float gamma);

} // namespace avgen::rendering
