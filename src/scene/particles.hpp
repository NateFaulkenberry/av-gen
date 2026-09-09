#pragma once

// GPU particle system description (milestone 0.5, ADR-015). The CPU only holds settings; the
// simulation lives in compute shaders (rendering/particle_renderer). Every field here is a
// modulation target through registerParticleParameters().

#include "core/error.hpp"
#include "params/parameter_set.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// ---- lifetime curves (ADR-040) ------------------------------------------------------------
// Small keyframed curves over normalised age (0 = birth, 1 = death), linearly interpolated and
// clamped outside the first and last key. A curve with fewer than two keys is *empty* and the
// linear start-to-end ramp is used instead, so every scene authored before curves existed
// renders bit-identically. At most kMaxCurveKeys keys: they are packed into the particle
// uniforms and evaluated in the vertex shader (see shaders/particles.wgsl `curveAt`).
constexpr int kMaxCurveKeys = 8;

struct CurveKey {
    float t = 0.0f;
    float value = 0.0f;
};
struct ColorKey {
    float t = 0.0f;
    glm::vec3 color{1.0f};
};

// evaluate() is the CPU reference for the shader: the same clamped piecewise-linear result.
struct ParticleCurve {
    std::vector<CurveKey> keys; // ascending t; at most kMaxCurveKeys are uploaded
    [[nodiscard]] bool active() const { return keys.size() >= 2; }
    [[nodiscard]] float evaluate(float t) const;
};
struct ParticleColorCurve {
    std::vector<ColorKey> keys;
    [[nodiscard]] bool active() const { return keys.size() >= 2; }
    [[nodiscard]] glm::vec3 evaluate(float t) const;
};

// ---- trails (ADR-040) ---------------------------------------------------------------------
// A trail keeps kMaxTrailPoints - 1 previous positions per particle in a ring that is part of
// the simulation state, so two runs of the same frame sequence produce the same ribbon.
constexpr std::uint32_t kMaxTrailPoints = 32;
// 16 bytes per history point (a vec4 in a storage array). The budget is per system and is what
// validateParticleSystem() enforces: it makes trails usable for hero emitters (32 k x 32 points
// = 16 MiB) and refuses them for million-particle systems.
constexpr std::uint64_t kTrailBytesPerPoint = 16;
constexpr std::uint64_t kMaxTrailBytes = 64ull << 20;

// Spline: emits along the scene spline named `spline` (position = S(u) + jitter within extent.x).
enum class EmitterShape : std::uint8_t { Point, Sphere, Disc, Box, Spline };
enum class ParticleBlend : std::uint8_t { Additive, Alpha };

enum class FieldForceMode : std::uint8_t { Force, Velocity, Turbulence, Kill };
[[nodiscard]] const char* fieldForceModeName(FieldForceMode mode);
[[nodiscard]] std::optional<FieldForceMode> fieldForceModeFromName(std::string_view name);
struct FieldForce {
    std::string field;               // FieldSpec name in Scene::fields
    FieldForceMode mode = FieldForceMode::Force;
    bool enabled = true;
    float strength = 1.0f;
    float mix = 1.0f;                // Velocity mode blend
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // scalar-field direction
};
constexpr int kMaxFieldForces = 4;

struct ParticleSystem {
    std::string name = "particles";
    bool enabled = true;
    std::uint32_t capacity = 65536; // pool size; fixed after creation (renderer re-creates on change)
    std::uint32_t seed = 1;

    // Emitter
    EmitterShape shape = EmitterShape::Sphere;
    std::string spline;              // Spline shape: scene spline name
    glm::vec3 position{0.0f, 1.0f, 0.0f};
    glm::vec3 extent{0.5f};          // sphere radius (x), disc radius (x), box half extents
    float spawnRate = 2000.0f;       // particles per second (continuous)
    float burst = 0.0f;              // extra particles emitted this frame (impulse; reset by the caller)
    float lifetimeMin = 1.0f;
    float lifetimeMax = 3.0f;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread = 0.5f;             // 0 = along direction, 1 = full sphere
    float speedMin = 0.5f;
    float speedMax = 2.0f;

    // Forces
    glm::vec3 gravity{0.0f, -0.5f, 0.0f};
    float drag = 0.2f;               // per second
    float turbulence = 0.8f;         // curl-noise strength
    float turbulenceScale = 1.0f;    // noise frequency (1 / metres)
    float turbulenceSpeed = 0.3f;    // noise animation speed
    glm::vec3 attractorPosition{0.0f, 1.0f, 0.0f};
    float attractorStrength = 0.0f;  // > 0 pulls, < 0 pushes
    float attractorRadius = 3.0f;
    float orbit = 0.0f;              // tangential force around the attractor
    // Field forces (ADR-025): each entry samples a scene field at the particle position every
    // simulation step. Mode Velocity sets/adds the field vector to the velocity directly (scaled
    // by strength, blended by `mix` 0..1 = full replace), Force adds it as an acceleration,
    // Turbulence adds the field's vector scaled by strength like the built-in curl noise, and
    // Kill removes particles where the scalar sample >= 0.5. Scalar fields act along `axis`.
    // Emission spawns from a Field-shaped emitter when `emitField` is set (density-weighted, later).
    std::vector<FieldForce> fieldForces; // at most kMaxFieldForces

    // Appearance
    float sizeStart = 0.04f;         // world units
    float sizeEnd = 0.0f;
    glm::vec4 colorStart{1.0f, 0.6f, 0.2f, 1.0f};
    glm::vec4 colorEnd{0.4f, 0.1f, 1.0f, 0.0f};
    float emissive = 4.0f;           // HDR intensity multiplier
    ParticleBlend blend = ParticleBlend::Additive;
    float softness = 0.2f;           // depth fade distance (world units)

    // Lifetime curves (ADR-040). Empty (fewer than two keys) = the linear ramp above.
    ParticleCurve sizeCurve;         // world units; replaces mix(sizeStart, sizeEnd)
    ParticleColorCurve colorCurve;   // rgb; replaces mix(colorStart.rgb, colorEnd.rgb)
    ParticleCurve opacityCurve;      // alpha; replaces mix(colorStart.a, colorEnd.a)

    // ---- velocity-aligned stretching (ADR-040) -------------------------------------------
    // The billboard grows along the screen projection of the simulated velocity by
    // particleStretchLength(); its width stays `size`, so slow particles stay round and fast
    // ones read as streaks. Free in memory: no history, one vertex-shader branch.
    float velocityStretch = 0.0f;    // 0 = always round; 1 = one shutter's worth of travel
    float stretchMax = 0.25f;        // world-unit cap on the added length
    float stretchMin = 0.0f;         // added length below this is dropped (slow = round)

    // ---- trails / ribbons (ADR-040) -------------------------------------------------------
    // Opt in per system: memory is capacity * (trailLength - 1) * 16 bytes and
    // validateParticleSystem() refuses anything over kMaxTrailBytes.
    bool trailEnabled = false;
    std::uint32_t trailLength = 16;  // ribbon points including the live head; 2..kMaxTrailPoints
    std::uint32_t trailStride = 1;   // record a history point every N simulation steps
    float trailWidth = 1.0f;         // head half-width as a multiple of the particle size
    float trailTaper = 0.0f;         // tail half-width as a fraction of the head's
    float trailFade = 0.0f;          // tail alpha as a fraction of the head's
    glm::vec3 trailTint{1.0f};       // tail colour multiplier (head is the particle colour)

    // ---- atmosphere coupling (ADR-040) ----------------------------------------------------
    // fogCoupling scales how much of the volumetric transmittance to the particle's own depth
    // it receives (1 = fully in the fog, 0 = the old behaviour). volumeGlow > 0 injects the
    // system's emissive light into the volume march as one aggregate sphere, so sparks light
    // the dust around them; the coupling is one-directional (the volume never affects the sim).
    float fogCoupling = 1.0f;
    float volumeGlow = 0.0f;
};

// The length the billboard gains along its velocity, in world units. The shader computes exactly
// this (shaders/particles.wgsl `stretchLength`); tests check them against each other.
// `shutterSeconds` is the camera's open time: shutterAngle / 360 * frame duration (ADR-037).
[[nodiscard]] float particleStretchLength(float speed, float shutterSeconds, const ParticleSystem& s);

// History points actually kept per particle (trailLength - 1; 0 when trails are off).
[[nodiscard]] std::uint32_t trailHistoryPoints(const ParticleSystem& s);
// Bytes the history ring costs for this system (0 when trails are off).
[[nodiscard]] std::uint64_t trailMemoryBytes(const ParticleSystem& s);

// Rejects systems the renderer cannot honour: an out-of-range trail length or stride, a curve
// with too many or unsorted keys, and above all a trail buffer over kMaxTrailBytes.
[[nodiscard]] Result<void> validateParticleSystem(const ParticleSystem& s);

// Registers "particles/<name>/<field>" parameters for the modulatable fields and returns
// handles; applyParticleParameters() copies their finals back into the system each frame.
struct ParticleParameters {
    params::Parameter<float>* spawnRate = nullptr;
    params::Parameter<float>* burst = nullptr;
    params::Parameter<float>* lifetime = nullptr;      // scales lifetimeMin/Max
    params::Parameter<float>* speed = nullptr;         // scales speedMin/Max
    params::Parameter<float>* spread = nullptr;
    params::Parameter<glm::vec3>* position = nullptr;
    params::Parameter<float>* extent = nullptr;        // scales extent
    params::Parameter<glm::vec3>* gravity = nullptr;
    params::Parameter<float>* drag = nullptr;
    params::Parameter<float>* turbulence = nullptr;
    params::Parameter<float>* turbulenceScale = nullptr;
    params::Parameter<float>* turbulenceSpeed = nullptr;
    params::Parameter<glm::vec3>* attractorPosition = nullptr;
    params::Parameter<float>* attractorStrength = nullptr;
    params::Parameter<float>* orbit = nullptr;
    std::array<params::Parameter<float>*, kMaxFieldForces> fieldStrength{}; // fieldForce/<slot>/strength
    params::Parameter<float>* size = nullptr;          // scales sizeStart/End
    params::Parameter<glm::vec4>* colorStart = nullptr;
    params::Parameter<glm::vec4>* colorEnd = nullptr;
    params::Parameter<float>* emissive = nullptr;
    params::Parameter<float>* stretch = nullptr;    // scales velocityStretch (ADR-040)
    params::Parameter<float>* trailWidth = nullptr; // scales trailWidth (ADR-040)
    params::Parameter<bool>* enabled = nullptr;
};

ParticleParameters registerParticleParameters(params::ParameterSet& params, const ParticleSystem& system);
// Writes finals into `system` using `rest` (the values at registration) for the scaled fields.
void applyParticleParameters(const ParticleParameters& p, const ParticleSystem& rest, ParticleSystem& system);

} // namespace avgen::scene
