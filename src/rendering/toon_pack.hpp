#pragma once

// ADR-1071: a material's cel lighting as the three ObjectUniforms lanes the lit shader reads. Pure,
// so a CPU test can pin the packing the shader decodes.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::rendering {

// toon0 = (bands, softness, terminator, highlight strength)
// toon1 = (shadowColor x ambient, rim width)
// toon2 = (rimColor x rimIntensity, highlight size)
// All zero when the material's toon is off, which is the shader's gate.
[[nodiscard]] inline std::array<glm::vec4, 3> packToon(const scene::ToonShading& t) {
    if (!t.enabled()) {
        return {glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)};
    }
    const float bands = std::max(1.0f, std::round(t.bands));
    return {glm::vec4(bands, std::max(t.softness, 1e-3f), std::clamp(t.terminator, -1.0f, 0.99f),
                      std::max(t.specular, 0.0f)),
            glm::vec4(glm::max(t.shadowColor, glm::vec3(0.0f)) * std::max(t.ambient, 0.0f),
                      std::clamp(t.rimWidth, 0.0f, 1.0f)),
            glm::vec4(glm::max(t.rimColor, glm::vec3(0.0f)) * std::max(t.rimIntensity, 0.0f),
                      std::clamp(t.specularSize, 0.0f, 1.0f))};
}

} // namespace avgen::rendering
