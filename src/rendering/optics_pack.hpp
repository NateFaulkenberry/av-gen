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

// ADR-1603: the refraction lanes. A material with refraction off packs to exactly zero (the bytes of a material from
// before ADR-1603), whatever its other fields say. ADR-1640: lane0.w is the dispersion, 0 (the single read) when the
// material authors none or the ladder has taken it away (`dispersionAllowed`, QualitySettings::refractionDispersion),
// so a material without dispersion packs the bytes it packed before ADR-1640.
struct PackedRefraction {
    glm::vec4 lane0{0.0f}; // x = strength, y = opacity (a glass's ior, ADR-1819), z = thickness (m), w = dispersion
    glm::vec4 lane1{0.0f}; // rgb = absorption per metre, w = ADR-1819's transmission (0 = the ADR-1603 mix)
};
[[nodiscard]] inline PackedRefraction packRefraction(const scene::Material& m, bool dispersionAllowed) {
    PackedRefraction out;
    if (m.refraction.enabled()) {
        const float dispersion = dispersionAllowed ? std::clamp(m.refraction.dispersion, 0.0f, 2.0f) : 0.0f;
        out.lane0 = glm::vec4(std::min(m.refraction.strength, 0.5f), std::clamp(m.refraction.opacity, 0.0f, 1.0f),
                              std::max(m.refraction.thickness, 0.0f), dispersion);
        if (m.refraction.transmission > 0.0f) {
            // ADR-1819: a glass has no use for `opacity` (its diffuse share is 1 - transmission), so y carries its ior.
            out.lane0.y = std::clamp(m.refraction.ior, 1.0f, 3.0f);
        }
        out.lane1 = glm::vec4(glm::max(m.refraction.absorption, glm::vec3(0.0f)),
                              std::clamp(m.refraction.transmission, 0.0f, 1.0f)); // ADR-1819
    }
    return out;
}

} // namespace avgen::rendering
