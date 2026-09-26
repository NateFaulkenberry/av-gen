#include "scene/water_surface.hpp"

#include "scene/struct_hash.hpp"

#include <cmath>

namespace avgen::scene {

using detail::StructHash;

Result<void> WaterSettings::validate() const {
    if (!(shallow > 0.0f) || shallow > 10000.0f) {
        return fail("water: shallow must be in (0, 10000] metres");
    }
    if (roughness < 0.0f || roughness > 1.0f) {
        return fail("water: roughness must be in [0, 1]");
    }
    if (emissiveIntensity < 0.0f || emissiveIntensity > 1000.0f) {
        return fail("water: emissiveIntensity must be in [0, 1000]");
    }
    if (!(clarity > 0.0f) || clarity > 1000.0f) {
        return fail("water: clarity must be in (0, 1000] metres");
    }
    if (maxOpacity < 0.0f || maxOpacity > 1.0f) {
        return fail("water: maxOpacity must be in [0, 1]");
    }
    if (edgeFade < 0.0f || edgeFade > 100.0f) {
        return fail("water: edgeFade must be in [0, 100] metres");
    }
    if (fresnel < 0.0f || fresnel > 1.0f) {
        return fail("water: fresnel must be in [0, 1]");
    }
    if (reflection < 0.0f || reflection > 20.0f) {
        return fail("water: reflection must be in [0, 20]");
    }
    if (specular < 0.0f || specular > 100.0f) {
        return fail("water: specular must be in [0, 100]");
    }
    if (ripple < 0.0f || ripple > 20.0f) {
        return fail("water: ripple must be in [0, 20]");
    }
    if (!(rippleScale > 0.0f) || rippleScale > 100.0f) {
        return fail("water: rippleScale must be in (0, 100] cycles per metre");
    }
    if (rippleSpeed < 0.0f || rippleSpeed > 100.0f) {
        return fail("water: rippleSpeed must be in [0, 100]");
    }
    if (chop < 0.0f || chop > 4.0f) {
        return fail("water: chop must be in [0, 4]");
    }
    if (foam < 0.0f || foam > 10.0f) {
        return fail("water: foam must be in [0, 10]");
    }
    if (foamWidth < 0.0f || foamWidth > 100.0f) {
        return fail("water: foamWidth must be in [0, 100] metres");
    }
    if (refraction < 0.0f || refraction > 20.0f) {
        return fail("water: refraction must be in [0, 20] metres");
    }
    if (glow < 0.0f || glow > 100.0f) {
        return fail("water: glow must be in [0, 100]");
    }
    if (!(glowScale > 0.0f) || glowScale > 100.0f) {
        return fail("water: glowScale must be in (0, 100] cycles per metre");
    }
    if (glowCoverage < 0.0f || glowCoverage > 1.0f) {
        return fail("water: glowCoverage must be in [0, 1]");
    }
    if (glowDepth < 0.0f || glowDepth > 1000.0f) {
        return fail("water: glowDepth must be in [0, 1000] metres");
    }
    if (sparkle < 0.0f || sparkle > 100.0f) {
        return fail("water: sparkle must be in [0, 100]");
    }
    if (swell < 0.0f || swell > 100.0f) {
        return fail("water: swell must be in [0, 100] metres");
    }
    // ADR-916. Written as `!(inside)` so a NaN fails too.
    if (!(tears >= 0.0f && tears <= 20.0f)) {
        return fail("water: tears must be in [0, 20]");
    }
    if (!(tearShear >= 0.0f && tearShear <= 8.0f)) {
        return fail("water: tearShear must be in [0, 8] metres");
    }
    if (!(tearCoverage >= 0.0f && tearCoverage <= 1.0f)) {
        return fail("water: tearCoverage must be in [0, 1]");
    }
    if (!(tearCell >= 0.05f && tearCell <= 50.0f)) {
        return fail("water: tearCell must be in [0.05, 50] metres");
    }
    // The seam field is sampled at the lattice's corners, so seams closer than two cells apart would be
    // sampled below their own frequency and break up into noise.
    if (!(tearSpacing >= 2.0f * tearCell && tearSpacing <= 2000.0f)) {
        return fail("water: tearSpacing must be at least twice tearCell and at most 2000 metres");
    }
    if (!(tearStretch >= 0.25f && tearStretch <= 20.0f)) {
        return fail("water: tearStretch must be in [0.25, 20]");
    }
    if (!(std::fabs(tearAngle) <= 100.0f)) {
        return fail("water: tearDirection's angle must be a finite number of radians");
    }
    if (!(tearDrift >= -20.0f && tearDrift <= 20.0f)) {
        return fail("water: tearDrift must be in [-20, 20] metres per second");
    }
    if (!(tearWind >= 0.0f && tearWind <= 1.0f)) {
        return fail("water: tearWind must be in [0, 1]");
    }
    return {};
}

std::uint64_t WaterSettings::structuralHash() const {
    StructHash h;
    h.boolean(enabled);
    h.f32(shallow);
    // Only the fields that change the *mesh* are structural; colours are material uniforms.
    return h.value();
}

} // namespace avgen::scene
