#include "pathtrace/camera.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::pathtrace {

CameraBasis cameraBasis(const scene::Camera& camera, std::uint32_t width, std::uint32_t height) {
    CameraBasis b;
    b.origin = camera.position;

    glm::vec3 forward = camera.target - camera.position;
    const float flen = glm::length(forward);
    // A camera whose target sits on its own position has no direction. Rather than emit NaNs into
    // every pixel, fall back to -Z, which is where a default camera looks.
    b.forward = flen > 1e-9f ? forward / flen : glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 up = camera.up;
    if (glm::length(up) < 1e-9f) up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(b.forward, up);
    if (glm::length(right) < 1e-6f) {
        // Looking straight up or down: `up` is parallel to `forward` and the cross degenerates.
        // glm::lookAtRH has the same singularity; pick a stable axis so the render is defined.
        right = glm::cross(b.forward, glm::vec3(0.0f, 0.0f, 1.0f));
        if (glm::length(right) < 1e-6f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    b.right = glm::normalize(right);
    b.up = glm::normalize(glm::cross(b.right, b.forward));

    b.tanHalfFovY = std::tan(0.5f * camera.effectiveFovY());
    b.aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    return b;
}

Ray generateRay(const CameraBasis& basis, float px, float py, std::uint32_t width, std::uint32_t height) {
    const float w = static_cast<float>(std::max(1u, width));
    const float h = static_cast<float>(std::max(1u, height));

    // Image plane coordinates in [-1, 1]. `sy` is negated relative to `px` because pixel rows run
    // downward while world +Y runs up.
    const float sx = (2.0f * px / w - 1.0f) * basis.aspect * basis.tanHalfFovY;
    const float sy = (1.0f - 2.0f * py / h) * basis.tanHalfFovY;

    Ray r;
    r.origin = basis.origin;
    r.direction = glm::normalize(basis.forward + sx * basis.right + sy * basis.up);
    return r;
}

} // namespace avgen::pathtrace
