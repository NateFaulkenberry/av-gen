#include "scene/water_surface.hpp"

#include "scene/struct_hash.hpp"

namespace avgen::scene {

using detail::StructHash;

Result<void> WaterSettings::validate() const {
    if (!(shallow > 0.0f) || shallow > 10000.0f) {
        return fail("water: shallow must be in (0, 10000] metres");
    }
    if (roughness < 0.0f || roughness > 1.0f) {
        return fail("water: roughness must be in [0, 1]");
    }
    if (shoreFade < 0.0f || shoreFade > 1000.0f) {
        return fail("water: shoreFade must be in [0, 1000]");
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
    return {};
}

std::uint64_t WaterSettings::structuralHash() const {
    StructHash h;
    h.boolean(enabled);
    h.f32(shallow);
    h.f32(shoreFade);
    // Only the fields that change the *mesh* are structural; colours are material uniforms.
    return h.value();
}

} // namespace avgen::scene
