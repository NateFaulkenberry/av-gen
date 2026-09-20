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
// ADR-370: the silhouette a particle draws with.
enum class ParticleShape : std::uint8_t { Round, Leaf };
enum class ParticleBlend : std::uint8_t { Additive, Alpha };

// ADR-520: what a particle does when it reaches the collision height.
//
// `None` is the default and is what every particle in the repository did before this existed:
// nothing, it falls through the ground and lives out its lifetime underneath it.
enum class CollisionResponse : std::uint8_t {
    None,   // fall through (the old behaviour)
    Kill,   // die on contact
    Bounce, // reflect, keeping `collisionRestitution` of the normal speed -- spray, hail, grit
    Splash, // a SECOND LIFE: the particle is reborn at the impact point as a ground-aligned
            // expanding ring, which is what a drop landing actually leaves behind. See the note
            // on `splashSize` for why this is one particle and not a burst of them.
};
[[nodiscard]] const char* collisionResponseName(CollisionResponse mode);
[[nodiscard]] std::optional<CollisionResponse> collisionResponseFromName(std::string_view name);

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
    // ADR-370: what a particle looks like. `Round` is the soft dot this system has always drawn and
    // is the default, so nothing that does not ask changes. `Leaf` draws an oriented, tumbling card
    // with a pointed-ellipse silhouette -- because the brief's falling leaves must not read as
    // "generic glowing dots", and a round additive blob is exactly that.
    ParticleShape shape2d = ParticleShape::Round;
    float tumbleRate = 2.4f;  // radians per second the card spins about its long axis
    float leafAspect = 0.42f; // half-width over half-length; below 1 the card is longer than wide
    float twoSided = 0.45f;   // how much darker the card is edge-on, 0..1
    // ADR-370: how much of ADR-055's wind field this system catches. 0 is off and is the default,
    // so no existing particle system starts drifting.
    float windInfluence = 0.0f;
    // ADR-367: how far in front of a surface the billboard has fully faded in, in world units.
    // 0 is off and is the default, and "off" has to be the default because until ADR-367 this value
    // was read by no shader at all: every system in the repository was rendering as though it were
    // 0, so 0 is the only value that reproduces what those scenes already look like. The old 0.2
    // default was never a behaviour, it was a number the writer baked into every scene it saved.
    float softness = 0.0f;

    // ---- ADR-520: the air the camera carries ------------------------------------------------
    // Weather is not a box of particles somewhere in the world; it is everywhere, and what the
    // camera can see of it is a small moving slice. `volumeFollow` moves the emitter (and the wrap
    // bounds below) with the camera, per axis, 0..1 -- so rain authored as a 60 m box follows the
    // shot instead of being left behind by it, and a 40 k pool spends all of itself on what is
    // actually in frame. All zero is off and is what every existing system does.
    glm::vec3 volumeFollow{0.0f};
    // A particle that leaves the box (half extents `extent`, centred on the emitter) re-enters on
    // the opposite face instead of travelling on. This is what makes a *steady* fall possible: with
    // wrapping, lifetime stops being the thing that recycles a particle, so rain does not have to
    // fade in and out at the top and bottom of its life to hide the respawn. Pure function of
    // position, so it costs determinism nothing.
    bool volumeWrap = false;

    // ---- ADR-520: the ground ------------------------------------------------------------------
    CollisionResponse collision = CollisionResponse::None;
    float collisionHeight = 0.0f;     // world Y of the plane a particle collides with
    float collisionRestitution = 0.3f; // Bounce: the share of the normal speed kept
    // Splash: the reborn ring. One particle, not a burst, because the pool is fixed (ADR-015) and
    // spending eight slots on every impact would mean a downpour with an eighth of the drops. An
    // expanding ground ring is also what the eye actually reads as "something landed there" --
    // three additive dots flying up is not.
    float splashLifetime = 0.45f;   // seconds the ring lives
    float splashSize = 6.0f;        // the ring's final radius as a multiple of the drop's size
    float ringThickness = 0.22f;    // ring wall as a fraction of its radius; 1 = a filled disc

    // ---- ADR-520: layered scale ---------------------------------------------------------------
    // The per-particle size multiplier used to be a hard-coded mix(0.7, 1.3, r): uniform, and
    // uniform randomness is the brief's §82 failure by name. `sizeSkew` is the exponent on the
    // uniform sample, so > 1 gives many small and few large -- which is what a real population of
    // drops, flakes, motes and embers looks like. variance 0.3 / skew 1 is exactly the old
    // expression, so nothing that does not ask changes.
    float sizeVariance = 0.3f;
    float sizeSkew = 1.0f;
    // ...and scale, in a falling population, means SPEED. A raindrop's terminal velocity goes with
    // the square root of its radius; a big flake flutters down slower than a small dense one. With
    // one `drag` for the whole system every particle reaches the same terminal velocity and falls
    // in lockstep, which is the "identical particle motion" the quality bar names -- and no amount
    // of turbulence hides it, because turbulence perturbs a speed it does not vary.
    //
    // `dragSizeBias` blends the system's drag towards `drag / size`, so a particle drawn large by
    // `sizeVariance` is also drawn fast, and the two variations are correlated the way they are in
    // the world rather than independent the way two random numbers are. 0 is off and is exactly
    // what every existing system does.
    float dragSizeBias = 0.0f;

    // ---- ADR-520: the flash -------------------------------------------------------------------
    // A per-particle brightness oscillation. `pulseSync` is the whole reason this is one feature
    // and not two: at 0 every particle blinks on its own phase, which is an ember bed; at 1 they
    // all blink together, which is a firefly chorus. `pulseSharpness` is the exponent that turns
    // the sine into a brief flash. Rate 0 is off.
    float pulseRate = 0.0f;
    float pulseDepth = 0.8f;
    float pulseSync = 0.0f;
    float pulseSharpness = 1.0f;

    // ---- ADR-520: clustering and hovering -----------------------------------------------------
    // Spawn positions drawn from `clusterCount` centres rather than uniformly over the emitter.
    // A cluster centre is a pure function of its index, so it is the same place every frame and
    // the clustering persists instead of dissolving into the uniform field it was drawn from.
    // 0 = off (uniform), which is what every existing system does.
    std::uint32_t clusterCount = 0;
    float clusterRadius = 1.0f;
    // The dart-and-hover cycle. While paused a particle is heavily damped, so it drifts almost
    // still; between pauses it moves at whatever the forces give it. Rate 0 is off.
    float pauseRate = 0.0f;
    float pauseFraction = 0.5f;

    // ---- ADR-520: catching the light ----------------------------------------------------------
    // Dust is not visible because it is bright; it is visible because it is BETWEEN you and a
    // light. `scatterStrength` multiplies the particle by a Henyey-Greenstein phase function of
    // the angle between the view ray and the key light, so the same motes are nearly invisible
    // across the light and blaze when you look into it. 0 is off.
    float scatterStrength = 0.0f;
    float scatterAnisotropy = 0.72f; // HG g; 0 isotropic, -> 1 sharply forward

    // ---- ADR-520: where it is allowed to exist ------------------------------------------------
    // A scalar FieldSpec sampled at the spawn position: the spawn survives with probability equal
    // to the sample. Soft-edged by construction, which is what a weather front needs -- a Kill
    // force gives a hard wall of rain with nothing in front of it, and a front arrives gradually.
    // Empty = no mask. A name the scene does not define is reported by validateParticleSystem's
    // caller, never silently ignored.
    std::string emitMaskField;

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
    // Absolute, and seeded from the scene -- not a multiplier. See the note on
    // `particleExtentFromRadius` below for why this one is not in the `lifetime`/`speed`/`size`
    // family it used to sit in.
    params::Parameter<glm::vec3>* extent = nullptr;
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
    params::Parameter<float>* softness = nullptr; // ADR-367
    params::Parameter<float>* windInfluence = nullptr; // ADR-370
    params::Parameter<float>* tumbleRate = nullptr; // ADR-370
    params::Parameter<float>* leafAspect = nullptr;
    params::Parameter<float>* twoSided = nullptr;
    params::Parameter<float>* stretch = nullptr;    // scales velocityStretch (ADR-040)
    params::Parameter<float>* trailWidth = nullptr; // scales trailWidth (ADR-040)
    // ADR-520. All absolute, not multipliers: each of these is a thing an artist points at in the
    // picture ("make them blink faster", "make the motes catch more light"), and a multiplier over
    // a rest value is not what that request means.
    params::Parameter<float>* collisionHeight = nullptr;
    params::Parameter<float>* splashSize = nullptr;
    params::Parameter<float>* sizeVariance = nullptr;
    params::Parameter<float>* sizeSkew = nullptr;
    params::Parameter<float>* dragSizeBias = nullptr;
    params::Parameter<float>* pulseRate = nullptr;
    params::Parameter<float>* pulseDepth = nullptr;
    params::Parameter<float>* pulseSync = nullptr;
    params::Parameter<float>* pulseSharpness = nullptr;
    params::Parameter<float>* clusterRadius = nullptr;
    params::Parameter<float>* pauseRate = nullptr;
    params::Parameter<float>* pauseFraction = nullptr;
    params::Parameter<float>* scatterStrength = nullptr;
    params::Parameter<float>* scatterAnisotropy = nullptr;
    params::Parameter<bool>* enabled = nullptr;
};

// The extent a radius typed in metres means, keeping the emitter's authored proportions.
//
// `extent` is one field with three meanings (sphere radius, disc radius, box half-extents), and a
// panel offers one number for it. Writing that number into all three components would turn a
// 30 x 18 x 4 box of dust into a cube the first time anybody touched the slider, so the other two
// follow `x` in the ratio the scene authored them in. The proportions are authorship; the size is
// what the person at the slider is changing.
//
// Here rather than in the panel because the panel is not testable (ADR-182 wants an arm that can
// fail, and an ImGui call is not one); this is the whole of what the control does to a number.
[[nodiscard]] glm::vec3 particleExtentFromRadius(const glm::vec3& authored, float radius);

ParticleParameters registerParticleParameters(params::ParameterSet& params, const ParticleSystem& system);
// Writes finals into `system` using `rest` (the values at registration) for the scaled fields.
void applyParticleParameters(const ParticleParameters& p, const ParticleSystem& rest, ParticleSystem& system);

} // namespace avgen::scene
