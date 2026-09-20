#pragma once

// The vortex field (ADR-388): one deterministic, spatial, turning field that every part of the
// renderer can ask "what is the medium doing at this point, at this time".
//
// Built on ADR-055's precedent, deliberately and in the same shape, because the wind field already
// proved the pattern works: a pure function of (packed parameters, position, time), no state, no
// wall clock, no per-instance storage, with `shaders/vortex.wgsl` as the transliteration and a test
// that compares the two through the *packed* form so both sides start from bytes that are identical
// by construction.
//
// The reason it matters here is not tidiness. Before this, the vortex existed only as
// `vortexShape()` inside `volume.wgsl` -- so the only thing that could ask about it was the
// volumetric march. Particles approximated it with an attractor plus an orbit force (ADR-380),
// which is a different shape that happens to look similar, and anything else that wanted to know
// where the funnel was -- clouds, lightning, a debug overlay -- had to guess again. One sampler
// makes those the same question with one answer, and it is the only version of this where the
// determinism ADR-091 asks for is *checkable* rather than asserted.
//
// Units, and the trap this file is one call away from. `density` is an extinction coefficient PER
// METRE and `emission` is an emissive density PER METRE: they are integrated over the march's step
// length. Adding a radiance to that integral without the conversion has been wrong three times in
// this codebase (ADR-374's density, ADR-379's spill, ADR-381's comet-on-fog). `VortexSample` is
// deliberately unitless where it can be -- `density` here is the normalised SHAPE in 0..1, and the
// caller multiplies by the per-metre coefficient -- so the conversion happens where somebody can
// see both sides of it.

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::vortex {

inline constexpr float kTau = 6.28318530718f;

// ---- the field ---------------------------------------------------------------------------------

// Where the funnel is and how it turns, as authored. Metres, seconds and radians throughout.
//
// This is the GEOMETRY and MOTION half only. Colour, emission, filament contrast and the rest of
// the appearance stay on `world::Vortex`, because they are what the picture does with the field
// rather than part of the field: a particle asking "which way is the medium moving here" must not
// have to carry a colour ramp to find out.
struct VortexField {
    glm::vec3 center{0.0f};       // world space; the mouth's centre
    float radius = 0.0f;          // metres; 0 is off and is the default, and it is the gate
    float thickness = 120.0f;     // vertical half-extent of the wall at the mouth
    float funnelDepth = 0.0f;     // metres the throat descends; 0 keeps ADR-371's flat slab
    float throat = 0.25f;         // throat radius as a fraction of the mouth
    float throatDensity = 0.6f;   // how much of the wall's shape the throat keeps
    float swirl = 3.2f;           // radians of angular shear per unit radius
    float rotationSpeed = 0.035f; // radians per second
    float innerVoid = 0.18f;      // fraction of the radius that is dark centre
    float contrast = 1.9f;        // exponent on the filament noise
    float turbulence = 0.6f;
    float turbulenceScale = 2.1f;
    float breathAmount = 0.05f;
    float breathSpeed = 0.18f;

    [[nodiscard]] bool active() const { return radius > 0.0f; }
};

// The GPU-ready form. Laid out to match the five `vortexN` vec4s the volume pass already uploads,
// so the shader reads exactly the same numbers in exactly the same slots it did before this file
// existed -- which is what makes the byte-identity of the Tree of Life a property of the refactor
// rather than a thing to re-tune for.
struct VortexUniforms {
    glm::vec4 v0{0.0f}; // xyz = centre, w = radius
    glm::vec4 v1{0.0f}; // x = thickness, y = swirl, z = rotationSpeed, w = (unused here)
    glm::vec4 v2{0.0f}; // x = innerVoid, y = contrast, z = turbulence, w = turbulenceScale
    glm::vec4 v3{0.0f}; // x = breathAmount, y = breathSpeed, z/w = (appearance, unused here)
    glm::vec4 v4{0.0f}; // x = funnelDepth, y = throat, z = throatDensity, w = 0
};
static_assert(sizeof(VortexUniforms) == 80);

[[nodiscard]] VortexUniforms packVortex(const VortexField& field);

// ---- what the medium is doing at a point -------------------------------------------------------

// Everything a consumer has asked for so far, in one evaluation. Splitting it would mean sampling
// the noise twice for a particle that wants both where it is and which way to go.
struct VortexSample {
    // The normalised shape in 0..1: the funnel's envelope times its filament noise. This is what
    // `volume.wgsl` multiplies by the per-metre density and emission. Zero outside the funnel, and
    // exactly zero -- the envelope early-out is part of the function, not an optimisation the
    // caller may skip, because its absence is what ADR-374 measured as the cost.
    float density = 0.0f;
    // World-space velocity of the medium, m/s. The tangential term is the swirl, the radial term
    // draws inward, the vertical term descends the throat. This is what a particle integrates
    // instead of ADR-380's attractor-plus-orbit approximation.
    glm::vec3 velocity{0.0f};
    // Where in the funnel this is, for anything that wants to vary with position without
    // re-deriving the geometry: 0 at the axis, 1 at the mouth's rim (may exceed 1 just outside).
    float radialT = 0.0f;
    // 0 at the mouth, 1 at the throat. Above the mouth it is 0, which is a fact worth stating
    // because assuming otherwise is precisely ADR-374's upward-cylinder bug.
    float depthT = 0.0f;
    // The envelope alone, without the filament noise: the smooth funnel wall. Cheap for a consumer
    // that wants containment rather than appearance (a spawn test, a debug overlay).
    float envelope = 0.0f;
};

// `p` is a world position; `t` is render time in seconds. Pure: the same arguments give the same
// answer on the CPU and on the GPU, at any frame rate, in any order.
[[nodiscard]] VortexSample sampleVortex(const VortexUniforms& v, const glm::vec3& p, float t);

// The shape alone, which is what the volumetric march wants and all it wants. Separate because it
// skips the velocity trigonometry entirely, and the march evaluates this per step per pixel: the
// one place in this system where a few multiplies are worth a second entry point.
[[nodiscard]] float vortexShape(const VortexUniforms& v, const glm::vec3& p, float t);

} // namespace avgen::vortex
