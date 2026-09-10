#pragma once

// Picking in the viewport (ADR-068).
//
// The scene pass already writes an identifier target -- low 16 bits object id, high 16 bits
// material id (ADR-035) -- and a linear-depth target holding view-space distance. Between them they
// answer both questions a click asks: *what* is under the cursor, and *where in the world* is the
// surface it landed on. Nothing new has to be rendered to support selection, and nothing is added
// to the per-frame cost: the reads happen on click.
//
// The reconstruction maths is separated from the two texture reads on purpose. Turning a pixel and
// a depth into a world position is the part that can be wrong in a way nobody notices -- off by a
// half-texel, or by a flipped Y, or reconstructing a direction that is normalised when the depth
// assumed it was not -- and it is the part that can be tested without a GPU at all.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {
class Context;
}

namespace avgen::app {

// Everything needed to turn a pixel into a ray, taken from the frame the click landed on.
struct PickView {
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
    glm::uvec2 size{1u, 1u};   // the render target's size in pixels
};

struct PickResult {
    bool hit = false;              // false when the click landed on the sky
    std::uint32_t objectId = 0;    // the scene entity index the identifier target recorded
    std::uint32_t materialId = 0;
    glm::vec3 position{0.0f};      // world space, on the surface under the cursor
    float distance = 0.0f;         // along the camera's forward axis, in metres
};

// The world-space ray through the centre of `pixel`. Normalised.
[[nodiscard]] glm::vec3 rayThroughPixel(const PickView& view, glm::uvec2 pixel);

// Where a surface at `linearDepth` under `pixel` sits in the world. `linearDepth` is what the
// linear-depth target stores: distance along the camera's *forward axis*, not along the ray. The
// difference is a cosine that grows toward the edges of a wide frame, so dividing by it is not
// optional -- skipping it places clicks correctly in the middle of the screen and increasingly
// short of the surface toward the corners, which reads as "picking is a bit off" rather than as a
// missing term.
[[nodiscard]] glm::vec3 worldPositionAt(const PickView& view, glm::uvec2 pixel, float linearDepth);

// Reads the identifier and linear-depth targets at one pixel. Blocking, and meant to be: it runs on
// a click, not on a frame. `ids` and `linearDepth` are the scene renderer's own targets.
[[nodiscard]] Result<PickResult> pickAt(gpu::Context& context, const wgpu::Texture& ids,
                                        const wgpu::Texture& linearDepth, const PickView& view,
                                        glm::uvec2 pixel);

// The distance the linear-depth target uses for "nothing was drawn here". Matches
// shaders/linear_depth.wgsl; anything at or beyond it is sky.
inline constexpr float kPickFarDistance = 1.0e6f;

} // namespace avgen::app
