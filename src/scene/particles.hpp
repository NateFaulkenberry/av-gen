#pragma once

// GPU particle system description (milestone 0.5, ADR-015). The CPU only holds settings; the
// simulation lives in compute shaders (rendering/particle_renderer). Every field here is a
// modulation target through registerParticleParameters().

#include "params/parameter_set.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

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
};

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
    params::Parameter<bool>* enabled = nullptr;
};

ParticleParameters registerParticleParameters(params::ParameterSet& params, const ParticleSystem& system);
// Writes finals into `system` using `rest` (the values at registration) for the scaled fields.
void applyParticleParameters(const ParticleParameters& p, const ParticleSystem& rest, ParticleSystem& system);

} // namespace avgen::scene
