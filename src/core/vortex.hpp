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
    // ADR-389: the smoke controls. `smokeWarp` advects the finer octaves through a low-frequency
    // flow, which is the difference between detail that sits on the spiral and detail carried by
    // it; `smokeBillow` blends toward rounded masses; `detail` is the fine octave's weight, which
    // was a hardcoded 0.2.
    float smokeWarp = 0.0f;
    float smokeBillow = 0.0f;
    float detail = 0.2f;

    // ---- the macro structure (Vortex 2.0 brief §7-§11) -------------------------------------
    //
    // Why these exist, stated once and here because it is the load-bearing finding of the audit:
    // BEFORE them, this field's envelope was `voidMask * rim * vert` -- monotone in radius,
    // *completely uniform in angle*, and smooth in height. It contained no structure whatever.
    // Every feature anybody has ever seen in this effect came out of the three fBMs below, which
    // is why the owner reads it as "procedural noise / stippled particles": structurally that is
    // exactly what it is, noise draped on a smooth cone. No step count and no band-limit can fix
    // that, because there is nothing underneath to resolve -- which is also why ADR-389's strict
    // Nyquist clamp came back a flat teal wash rather than a coarse cyclone.
    //
    // Everything below is ANALYTIC: trigonometry and smoothsteps, no noise, a handful of ALU per
    // sample. That matters twice over. It is band-limited by construction, so it survives the
    // 125-metre sample spacing the march actually has; and it costs nothing, so it does not move
    // the measurement the architecture decision turns on.
    //
    // All of it defaults off (`bandArms` and `eyeWallGain` at 0, `eyeWallWidth` at the 0.22
    // ADR-374 hardcoded), so a scene that
    // does not ask for a cyclone gets byte-for-byte ADR-389's funnel.

    // §8: the eye. NOT a new radius -- `innerVoid` above already is one, and adding a second
    // would have left whichever of the two was not in charge as a slider that silently does
    // nothing. The per-field reachability probe in the parity test caught exactly that on the
    // first attempt (`changing innerVoid moved 0 of 160 GPU samples`) and it is the reason this
    // is written the way it is: the eye is `innerVoid` given a WALL, not a second control.
    //
    // §9: the wall itself. `eyeWallWidth` is how far, as a fraction of the mouth radius, the
    // density takes to climb out of the eye -- it replaces a hardcoded 0.22, and 0.22 is its
    // default, so nothing moves until somebody asks. `eyeWallGain` is how much denser the crest
    // of the ring is than the body of the storm; a hurricane's silhouette from orbit IS that
    // ring, and without it the densest place in frame is wherever the noise happens to peak.
    float eyeWallWidth = 0.22f;
    float eyeWallGain = 0.0f;
    // §10/§11: logarithmic spiral bands, in the ENVELOPE rather than in the noise. `bandArms` is
    // the primary arm count and 0 is off. `bandPitchDegrees` is the spiral's pitch angle -- the
    // angle the arm makes with the circle it crosses, which is the quantity a meteorologist and an
    // artist both name; real rainbands run 10 to 25 degrees. `bandDepth` is the contrast, and
    // `bandHarmonic` the weight of the two finer nested scales §11 asks for (3x and 7x the arm
    // count, at a third and a ninth of the depth).
    //
    // ADR-389's family rule, obeyed on purpose: the band term is written `1 + depth * cos(...)`
    // and not `mix(1, 0.5 + 0.5 cos(...), depth)`, so its mean over angle is EXACTLY 1 whatever
    // the depth. `density` and `emission` are per-metre coefficients calibrated against the mean
    // of this field, and a band function with mean 0.5 would have halved the medium under them.
    float bandArms = 0.0f;
    float bandPitchDegrees = 18.0f;
    float bandDepth = 0.0f;
    float bandHarmonic = 0.0f;
    // §53, the owner's second failure test, as a control rather than as a code edit: the weight of
    // the whole fBM stack against a flat field. At 0 the density IS the macro envelope, which is
    // both the diagnostic §5 demands before any detail is added and the arm §53 will be judged
    // on. One parameter, so the test and the tool are the same thing.
    float cloudNoise = 1.0f;

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
    glm::vec4 v6{0.0f}; // ADR-389: smokeWarp, smokeBillow, detail, 0
    glm::vec4 v7{0.0f}; // §9/§53: eyeWallWidth, eyeWallGain, cloudNoise, 0
    glm::vec4 v8{0.0f}; // §10/§11: bandArms, band cotangent, bandDepth, bandHarmonic
};
static_assert(sizeof(VortexUniforms) == 128);

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
    // The envelope alone, without the filament noise: the smooth funnel wall -- now including the
    // eye, the eye wall and the spiral bands, because those are macro STRUCTURE and not detail.
    // Cheap for a consumer that wants containment rather than appearance (a spawn test, a debug
    // overlay), and it is exactly what §5's diagnostic render shows.
    float envelope = 0.0f;
};

// `p` is a world position; `t` is render time in seconds. Pure: the same arguments give the same
// answer on the CPU and on the GPU, at any frame rate, in any order.
[[nodiscard]] VortexSample sampleVortex(const VortexUniforms& v, const glm::vec3& p, float t);

// The shape alone, which is what the volumetric march wants and all it wants. Separate because it
// skips the velocity trigonometry entirely, and the march evaluates this per step per pixel: the
// one place in this system where a few multiplies are worth a second entry point.
// `filterWidth` is the world-space distance between the caller's samples, and 0 means "a point
// sample, not an integral". ADR-389: an octave whose period falls below twice that spacing cannot
// be resolved and contributes aliasing rather than detail, so it is faded out. Not a knob -- the
// right answer changes with `volumeSteps` and `volumeMaxDistance`, which change between tiers.
[[nodiscard]] float vortexShape(const VortexUniforms& v, const glm::vec3& p, float t,
                                float filterWidth = 0.0f);

} // namespace avgen::vortex
