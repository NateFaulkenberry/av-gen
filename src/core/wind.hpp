#pragma once

// The wind field (ADR-055): one deterministic, spatial, travelling field that every part of the
// renderer can ask "what is the air doing at this point, at this time".
//
// It is not `sin(time)`. A meadow driven by a global clock moves in lockstep, which is the single
// most recognisable tell of fake wind. Here every temporal term is also a *spatial* term: gusts are
// travelling waves `sin(k.x - w t)` that cross the world at a real speed, so a front arrives at the
// near hedge before it reaches the far one, and two plants a hundred metres apart are never in
// phase. Regional variation decides which part of the valley is windier at all; turbulence turns
// the local direction; the flutter phase is spatial so neighbours rattle out of step.
//
// Everything is a pure function of (packed parameters, position, time). No state, no wall clock,
// no per-instance storage: the same frame index gives the same field, on the CPU and on the GPU.
// `shaders/wind.wgsl` is the transliteration and evaluates the same expressions in the same order;
// tests compare the two through the packed form, which is why `sampleWind` takes `WindUniforms`
// rather than `WindParams` -- both sides start from bytes that are identical by construction.
//
// The species side (`VegetationMotion` -> `MotionResponse`) is the other half: what a *plant* does
// with that field. It is resolved on the CPU, once per draw, into four numbers, so a vertex shader
// never evaluates a transfer function.

#include <glm/glm.hpp>

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace avgen::wind {

inline constexpr float kTau = 6.28318530718f;

// ---- the field -------------------------------------------------------------------------------

// What the air is doing over a whole world, as authored. Metres and seconds throughout.
struct WindParams {
    bool enabled = false;
    float direction = 0.0f; // radians about +Y; 0 blows towards +X, pi/2 towards +Z
    float speed = 0.0f;     // dimensionless strength of the steady flow (0 calm, 1 a fresh breeze)

    // Regional variation: which part of the map is windier, and over how many metres that changes.
    // Two long incommensurate travelling waves, so the pattern never repeats on any useful scale.
    float regionScale = 60.0f;  // metres between regional swings
    float regionAmount = 0.45f; // +-fraction of `speed` (0.45 => strength in [0.55, 1.45])
    float regionDrift = 0.05f;  // Hz the regional pattern itself moves at

    // Turbulence: a faster, smaller pattern that turns the local direction rather than its size.
    float turbulence = 0.3f;       // radians of local direction change
    float turbulenceScale = 14.0f; // metres
    float turbulenceSpeed = 1.1f;  // metres per second the pattern travels

    // Gusts: fronts that PROPAGATE downwind, which is what makes a meadow read as one ecosystem.
    float gustAmount = 0.9f;    // peak fraction of `speed` a front adds
    float gustScale = 34.0f;    // metres between fronts
    float gustSpeed = 9.0f;     // metres per second a front travels
    float gustSharpness = 3.0f; // 1 a smooth swell, 8 distinct fronts with calm between

    // Flutter: the fast rattle. Its *frequency* is a property of the plant, not of the air (a plant
    // rings at its own resonance when broadband turbulence excites it), so only the spatial scale
    // lives here; `MotionResponse::flutterOmega` carries the rate.
    float flutterScale = 2.2f; // metres between neighbours that flutter out of phase

    [[nodiscard]] bool active() const { return enabled && speed > 0.0f; }
};

// The GPU-ready form: what `FrameUniforms` carries and what both `sampleWind` implementations read.
// Wavenumbers are pre-divided here so neither side spends a divide per vertex.
struct WindUniforms {
    glm::vec4 dir{1.0f, 0.0f, 0.0f, 0.0f};    // xy = unit direction in XZ, z = speed, w = 1 when on
    glm::vec4 region{0.0f};                   // x = tau/regionScale, y = regionAmount,
                                              // z = regionDrift*tau, w = turbulence (radians)
    glm::vec4 gust{0.0f};                     // x = tau/gustScale, y = gustSpeed, z = gustAmount,
                                              // w = gustSharpness
    glm::vec4 turbulence{0.0f};               // x = tau/turbulenceScale, y = turbulenceSpeed,
                                              // z = tau/flutterScale, w = 0
};
static_assert(sizeof(WindUniforms) == 64);

[[nodiscard]] WindUniforms packWind(const WindParams& params);

// What the air is doing at one point at one time.
struct WindSample {
    glm::vec2 direction{1.0f, 0.0f}; // unit, in XZ, already turned by the turbulence
    float strength = 0.0f;           // >= 0; the steady flow, modulated regionally
    float gust = 0.0f;               // 0..gustAmount; the travelling front's envelope here and now
    float phase = 0.0f;              // spatial phase of the flutter term (radians)
};

// `p` is a world position (only xz are read: the field is columnar, which is what a plant rooted in
// the ground experiences). `t` is render time in seconds, already delayed by the caller's lag.
[[nodiscard]] WindSample sampleWind(const WindUniforms& w, const glm::vec3& p, float t);

// ---- what a plant does with it ---------------------------------------------------------------

// Per-species physical parameters. These are the plant, not the wind: the same field moves grass
// and mushrooms differently because these differ, which is the whole point of naming them.
struct VegetationMotion {
    float stiffness = 1.0f;          // resistance to bending; tip travel goes as 1/stiffness
    float mass = 0.02f;              // effective mass at the tip; with stiffness sets the resonance
    float damping = 0.3f;            // damping ratio zeta (0 rings forever, 1 critically damped)
    float windSensitivity = 0.0f;    // how much of the flow this thing catches (leaf area);
                                     // 0 by default, so nothing in any existing scene starts moving
    float bendLimit = 0.45f;         // ceiling on tip travel, as a fraction of the plant's height
    float tipAmplitude = 0.25f;      // tip travel at unit wind, as a fraction of the plant's height
    float gustResponse = 1.0f;       // how much of a gust it takes on top of the steady flow
    float bendCurve = 1.8f;          // exponent of the height profile (1 = shear, 3 = tip only)
    float amplitudeVariance = 0.35f; // per-instance spread of amplitude, +-this fraction

    [[nodiscard]] bool active() const { return windSensitivity > 0.0f && tipAmplitude > 0.0f; }
};

// The four numbers a vertex shader actually needs, plus the shape controls it passes through. Every
// transfer function is evaluated here, once per draw, so the shader is arithmetic and nothing else.
struct MotionResponse {
    float steadyGain = 0.0f;   // fraction of plant height of tip travel per unit strength
    float gustGain = 0.0f;     // ditto for the gust envelope (gustResponse already folded in)
    float flutterGain = 0.0f;  // ditto for the resonant flutter
    float swayDelay = 0.0f;    // seconds the sway lags the field (phase lag / gust frequency)
    float flutterOmega = 0.0f; // rad/s the plant rings at: its own resonance, not the wind's
    float bendCurve = 1.8f;
    float bendLimit = 0.45f;
    float amplitudeVariance = 0.35f;
};

// A driven damped harmonic oscillator, evaluated analytically at the two frequencies the field
// actually contains. `omega0 = sqrt(stiffness/mass)` is the plant's resonance; the dynamic gain at
// a driving frequency w is 1/sqrt((1-r^2)^2 + (2 zeta r)^2) with r = w/omega0, and the phase lag is
// atan2(2 zeta r, 1 - r^2). Static deflection goes as 1/stiffness. That is the entire model, and it
// is why a fourteen-metre tree ignores a gust front (r >> 1, gain -> 0) and keeps only the slow
// regional swing, while grass sits near resonance and leans into every front.
[[nodiscard]] MotionResponse motionResponse(const WindParams& wind, const VegetationMotion& plant);

// Amplitude gain and phase lag of the oscillator, exposed for tests and tools.
[[nodiscard]] float oscillatorGain(float omega, float omega0, float zeta);
[[nodiscard]] float oscillatorLag(float omega, float omega0, float zeta); // radians

// The Tier 0 deformation itself: the CPU reference for `windDisplacement` in shaders/wind.wgsl,
// which is a transliteration of it. `objectY` is the vertex's y in the space the deformer stack
// sees (after the source transform); `baseY` and `extentY` are the source mesh's bounds in that
// same space; `instanceScaleY` is the per-instance scale, so `extentY * instanceScaleY` is this
// specimen's world height. `random` is the instance's four hashed randoms.
//
// The properties this guarantees, and that tests check: displacement is exactly zero at the root,
// rises with height along a tunable curve, never exceeds `bendLimit` of the plant's height, and
// varies per instance while every instance in a region still leans the same way at the same time.
[[nodiscard]] glm::vec3 vegetationDisplacement(float objectY, float baseY, float extentY, float instanceScaleY,
                                               const WindSample& w, const MotionResponse& r,
                                               const glm::vec4& random, float tFlutter);

// ---- serialisation ---------------------------------------------------------------------------

[[nodiscard]] WindParams windFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json windToJson(const WindParams& params);
[[nodiscard]] VegetationMotion motionFromJson(const nlohmann::json& j, const VegetationMotion& base = {});
[[nodiscard]] nlohmann::json motionToJson(const VegetationMotion& motion);

} // namespace avgen::wind
