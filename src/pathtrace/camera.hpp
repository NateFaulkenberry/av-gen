#pragma once

// Primary ray generation (spec sections 14-15).
//
// The one thing this file must get right is agreeing with `scene::Camera`, whose matrices are
// `glm::lookAtRH` and `glm::perspectiveRH_ZO` (src/scene/scene.cpp). Right-handed, +Y up, metres,
// looking down -Z, depth 0..1. A ray generator that disagrees with the rasteriser's projection by
// a sign or a half-pixel produces an image that looks plausible and is wrong, which is why
// `test_pathtrace_camera.cpp` checks these rays against the projection matrix itself rather than
// against a hand-derived expectation.
//
// Pixel convention: (0,0) is the TOP-LEFT pixel and `px`/`py` are continuous coordinates inside the
// image, so the centre of pixel (x,y) is (x + 0.5, y + 0.5). That matches the EXR the tracer writes
// and the readback the rest of the engine does.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::pathtrace {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // normalised
};

// The camera's world-space basis, computed once per render rather than per pixel.
struct CameraBasis {
    glm::vec3 origin{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float tanHalfFovY = 0.5f;
    float aspect = 1.0f;
};

[[nodiscard]] CameraBasis cameraBasis(const scene::Camera& camera, std::uint32_t width, std::uint32_t height);

// `px`, `py` in [0, width] x [0, height], top-left origin.
[[nodiscard]] Ray generateRay(const CameraBasis& basis, float px, float py, std::uint32_t width,
                              std::uint32_t height);

// The inverse of `generateRay`: where a world point lands in pixel coordinates. Returns false if
// the point is behind the camera, where a projection is meaningless rather than merely off-screen.
[[nodiscard]] bool projectToPixel(const CameraBasis& basis, const glm::vec3& world,
                                  std::uint32_t width, std::uint32_t height, glm::vec2& outPixel);

} // namespace avgen::pathtrace
