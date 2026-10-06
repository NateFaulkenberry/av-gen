#pragma once

// ADR-1143: a material's thin film and anisotropy as the one ObjectUniforms lane the lit shader reads.
// Pure, so a CPU test can pin the packing the shader decodes.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <algorithm>

namespace avgen::rendering {

// x = film thickness in nm (0 = off), y = film ior (0 while the film is off),
// z = anisotropy strength in -1..1 (0 = off), w = anisotropy rotation in radians (0 while off).
// A material with neither on packs to exactly zero -- the same bytes as a material from before
// ADR-1143 -- whatever its ior or rotation say, which is what makes "absent" and "explicitly zero"
// the same draw.
[[nodiscard]] inline glm::vec4 packOptics(const scene::Material& m) {
    glm::vec4 out(0.0f);
    if (m.thinFilm.enabled()) {
        out.x = std::min(m.thinFilm.thickness, 2000.0f);
        out.y = std::clamp(m.thinFilm.ior, 1.0f, 5.0f);
    }
    if (m.anisotropy.enabled()) {
        out.z = std::clamp(m.anisotropy.strength, -1.0f, 1.0f);
        out.w = m.anisotropy.rotation;
    }
    return out;
}

} // namespace avgen::rendering
