#pragma once

// What the renderer says about the frame it just drew (renderer forensics, Phases 2.1 and 3.1).
//
// These live apart from `scene_renderer.hpp` for one reason: nothing in them needs a device. They
// are names, numbers and matrices, so anything that *reads* a frame's diagnosis -- a capture, a
// diff, the transform history -- can be built and checked without WebGPU in the link line. The
// renderer includes this header; the header does not know the renderer exists.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace avgen::rendering {

struct RenderObjectDiagnostic {
    std::string name;
    std::size_t entityIndex = 0;
    std::uint32_t objectSlot = std::numeric_limits<std::uint32_t>::max();
    // What this object is *made of*, which the transform cannot say (Phase 2.1). A mesh or material
    // swapped for another produces a wrong picture with every matrix identical, and a capture that
    // carried only the geometry's placement would compare as unchanged. The material is a
    // fingerprint rather than the struct: the question a diff asks is "is this the same surface",
    // and one number answers it without the snapshot growing a copy of every material field.
    scene::MeshId mesh = scene::kInvalidMesh;
    std::uint64_t materialHash = 0;
    glm::vec3 worldPosition{0.0f};
    glm::mat4 worldMatrix{1.0f};
    glm::vec3 worldBoundsMin{0.0f};
    glm::vec3 worldBoundsMax{0.0f};
    std::array<float, 6> frustumMargins{};
    std::uint32_t rigIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t jointCount = 0;
    std::uint64_t paletteVersion = 0;
    double paletteTime = -1.0;
    std::string cullReason;
    bool visible = false;
    bool cameraCulled = false;
    bool submitted = false;
    bool finite = true;
};

struct RendererDiagnosticFrame {
    std::uint64_t frameIndex = 0;
    std::uint64_t stateHash = 0;
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    // The numbers the projection was built *from* (Phase 3.1). A matrix that is wrong tells you it
    // is wrong and nothing else; these say which input made it so, and a capture carrying them can
    // be read by a person rather than only diffed by a machine. `fovY` is the *effective* one --
    // the lens decides it unless `camera/lens/useExplicitFov` is set -- which is exactly the kind of
    // indirection a reader should not have to reconstruct from a matrix.
    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float aspect = 0.0f;
    float fovY = 0.0f;
    std::uint32_t viewportWidth = 0;
    std::uint32_t viewportHeight = 0;
    std::vector<RenderObjectDiagnostic> objects;
};

} // namespace avgen::rendering
