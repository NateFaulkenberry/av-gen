#pragma once

// Surface waves (ADR-207, ADR-702): a spatial phenomenon propagating through the world.
//
// ADR-702: this is the PAYLOAD and the technique of two effect types, `GroundPulse` and
// `TravelBeam` -- not a system. Until ADR-702 it was a separate `worldEffects` list with its own
// registrar, serialiser and panel; now its instances are ordinary `EffectInstance`s in the one list,
// attached to an owner, and their parameters, JSON and panel rows come from the registry like every
// other type's. What is left here is what is genuinely the waves' own: the propagation model, the
// endpoint resolution and the GPU record.
//
// A wave has a **source**, a **propagation geometry**, a **motion**, an **appearance**, a
// **lifetime** and -- because every number in it is an ordinary parameter -- whatever modulation
// somebody routes into it. That sentence is the whole model, and the two effects this file shipped
// for are two settings of it: a beam that travels ahead of a camera on its way to the next hero, and
// a ripple that spreads from a mushroom's base while the camera is holding it.
//
// Three decisions are load-bearing.
//
// **Resolution is a pure function of time.** `resolveWaves` takes the effect list and a
// context -- the second on the transport clock, the camera's pose and velocity, where the nodes are,
// which heroes exist, and the shot schedule the director baked -- and returns records ready for the
// GPU. It reads no frame counter, no wall clock and no previous frame, which is what keeps an
// offline render of second N byte-identical to a realtime playthrough of second N (ADR-091). The
// camera's *velocity* is the one thing a naive implementation would take as a frame-to-frame
// difference; here it is a finite difference in timeline seconds over a fixed step, so it is the
// same vector at 30 fps and at 120.
//
// **Nothing here iterates the scene.** At most `kMaxGpuWaves` records reach the GPU and every
// per-surface question is asked per fragment, in `shaders/wave_effects.wgsl`, from data already
// resident in the frame block. A wave front therefore lands on the ground where the ground is and on
// a leaf where the leaf is, with no projection, no bounds test and no per-object cost at all.
//
// **Two propagation kinds are two distance metrics, not two effects.** `DirectionalWave` measures
// along an axis; `RadialWave` measures a radius in the ground plane. A ring, a shockwave, a cone and
// a beam are each one of those two with a different mask, which is why the extension point is the
// mask and why this enum is short.

#include "core/error.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/hero.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// How many effects reach the GPU at once. Chosen the way `spatial::kMaxGpuFields` chose sixteen:
// large enough that nothing real hits it, small enough that the per-fragment loop has a bound
// anybody can reason about. The ninth active wave is dropped and its status says so (`EffectStatus::Dropped`), which the
// Effects panel shows on its card.
inline constexpr std::size_t kMaxGpuWaves = 8;

// ---- propagation -------------------------------------------------------------------------------

// The distance metric the wave travels along. See the header: the two below are the two metrics, and
// the kinds a future effect would want are masks over them.
enum class PropagationKind : std::uint8_t {
    DirectionalWave, // distance measured along an axis from the origin: a front sweeping the world
    RadialWave,      // distance measured as a radius in the ground plane: a ripple
};
[[nodiscard]] const char* propagationKindName(PropagationKind k);
[[nodiscard]] std::optional<PropagationKind> propagationKindFromName(std::string_view name);

// Where an effect gets its origin. A world effect must never need hard-coded coordinates, which is
// the whole reason this enum exists rather than a `glm::vec3 position` on its own.
enum class SourceKind : std::uint8_t {
    World,      // an authored world position; `position` is the answer
    Node,       // a scene node, by name: the effect rides whatever that node's transform is
    // A hero, by name (ADR-072). Resolves to the *node* of that name when the scene has one -- a
    // hero is one object (ADR-107) and its transform is where that object stands -- and to
    // `HeroPoint::position`, which is the centre of its bounds, when it does not.
    Hero,
    Camera,     // the active camera's eye
    FocusHero,  // whichever hero the shot schedule is spotlighting *now* -- no name, no wiring
    // ADR-702: whatever the effect instance is ATTACHED to. An entity-owned pulse rides its owner
    // (resolved as a hero of that name, else a node), a camera-owned one the camera; a World-owned
    // effect has no position to lend and never activates with this source.
    Owner,
};
[[nodiscard]] const char* sourceKindName(SourceKind k);
[[nodiscard]] std::optional<SourceKind> sourceKindFromName(std::string_view name);

// How a directional effect decides which way it is going.
enum class DirectionMode : std::uint8_t {
    Explicit,       // the authored world direction, and nothing else
    SourceForward,  // the source's own forward axis (a node's yaw; the camera's view axis)
    CameraForward,
    CameraVelocity, // where the camera is actually going, from the baked camera track
    SourceToTarget,
    CameraToTarget,
    Blended,        // a weighted sum of camera forward, camera velocity and camera -> target
};
[[nodiscard]] const char* directionModeName(DirectionMode m);
[[nodiscard]] std::optional<DirectionMode> directionModeFromName(std::string_view name);


// ---- the authored effect -----------------------------------------------------------------------

struct EffectEndpoint {
    SourceKind kind = SourceKind::World;
    std::string name;             // Node / Hero
    glm::vec3 position{0.0f};     // World, and the fallback when a name resolves to nothing
    // Metres to drop the resolved origin by, so a wave can start at the base of a thing whose
    // transform is somewhere up its stem. Applied after resolution, on the Y axis only.
    float groundOffset = 0.0f;
    [[nodiscard]] Result<void> validate() const;
};

struct Propagation {
    PropagationKind kind = PropagationKind::RadialWave;
    // Explicit, because the default must not require anything else to exist: a `SourceToTarget`
    // default makes the *minimal* effect -- a name and a source -- invalid, which is a file that
    // refuses to open for stating nothing.
    DirectionMode direction = DirectionMode::Explicit;
    glm::vec3 explicitDirection{0.0f, 0.0f, -1.0f};
    // Blended weights. Normalised after the sum, so they are ratios rather than magnitudes.
    float forwardWeight = 1.0f;
    float velocityWeight = 0.6f;
    float targetWeight = 1.2f;

    float speed = 60.0f;      // metres per second the front travels
    float range = 220.0f;     // metres; past this the front is gone
    float frontWidth = 14.0f; // metres of leading edge: how sharp the arrival is
    float trailLength = 46.0f;// metres behind the front the effect persists over
    float falloff = 1.6f;     // exponent on the trail; > 1 fades faster near the tail
    float startOffset = 0.0f; // metres ahead of the origin the front starts

    // How far off the ground plane the effect reaches, and how much that grows with distance. A
    // ripple crossing a valley wall needs the band to open out, or it clips at the first slope.
    float verticalExtent = 18.0f;
    float verticalGrowth = 0.22f;

    // Secondary ripples inside the trail (RadialWave). 0 is one clean ring.
    float ringCount = 2.0f;

    // Lateral falloff for a DirectionalWave, in metres from the axis. 0 means the front is a plane
    // rather than a beam, which is the right default for "the world lights up ahead of the camera".
    float beamRadius = 0.0f;

    [[nodiscard]] Result<void> validate() const;
};

struct Appearance {
    glm::vec3 color{0.10f, 0.85f, 1.0f};
    float intensity = 2.4f;        // HDR: this is scene-linear radiance, and bloom will see it
    glm::vec3 edgeColor{0.75f, 1.0f, 0.95f};
    float edgeIntensity = 3.2f;    // the leading edge, which is what makes a front read as a front
    float width = 1.0f;            // one multiplier over every width in Propagation

    // Rainbow is procedural: a cosine palette over the travelled distance, so it is a hue *ramp
    // through the wave* rather than a list of colours somebody typed. Temporally stable by
    // construction -- it is a function of position and of the front's distance, not of noise.
    bool rainbow = false;
    float rainbowSpeed = 0.35f;    // cycles per second the ramp slides through the wave
    float rainbowScale = 0.025f;   // cycles per metre; under ~2 turns inside the band it is a stripe
    float rainbowSaturation = 0.85f;
    float rainbowBrightness = 1.0f;

    [[nodiscard]] Result<void> validate() const;
};


// How much of the effect a surface takes. See ADR-207's consequences: these are the classes the
// shader can tell apart for free -- the procedural scatter against everything else, split by how
// ground-facing the surface is -- not a named category per asset.
struct MaterialResponse {
    float ground = 0.85f;    // near-horizontal, upward-facing: terrain
    float foliage = 1.35f;   // the procedural scatter: vegetation, rocks, mushroom caps
    float surface = 1.0f;    // everything else authored: props, heroes, characters
    float emissive = 0.9f;   // how much the wave amplifies emission the surface already had
    [[nodiscard]] Result<void> validate() const;
};

// The payload of a surface-wave effect instance. Its identity, enable, owner, order, style,
// activation and timing live on the instance (`world/effects/effect_instance.hpp`), shared with
// every other type.
struct WaveEffect {
    EffectEndpoint source;
    bool hasTarget = false;
    EffectEndpoint target;

    Propagation propagation;
    Appearance appearance;
    Sparkle sparkle;
    MaterialResponse response;

    [[nodiscard]] Result<void> validate() const;
};

// The endpoint halves of the payload that are not parameters -- a kind and a NAME -- as JSON. The
// numbers are registry rows and are walked by the registry like every other type's.
[[nodiscard]] nlohmann::json waveEndpointToJson(const EffectEndpoint& e);
[[nodiscard]] Result<EffectEndpoint> waveEndpointFromJson(const nlohmann::json& j);

// ---- presets (§18) -----------------------------------------------------------------------------
//
// A style configures the underlying parameters and then gets out of the way; nothing reads `style`
// at runtime. Separate lists for beams and pulses because the two answer different questions --
// "what is travelling" against "what is spreading" -- and a single list would offer Water as a beam.

[[nodiscard]] std::span<const std::string_view> beamStyleNames();
[[nodiscard]] std::span<const std::string_view> pulseStyleNames();
// Applies a style's appearance, sparkle and propagation shape, leaving source, target and
// propagation kind alone. False when the name is not a style.
bool applyBeamStyle(WaveEffect& wave, std::string_view style);
bool applyPulseStyle(WaveEffect& wave, std::string_view style);

// ---- resolution --------------------------------------------------------------------------------

struct EffectInstance;
enum class EffectStatus : std::uint8_t;

// Mirrors `Wave` in shaders/wave_effects.wgsl. 144 bytes; see ADR-207 for why it lives in
// FrameUniforms rather than in a buffer of its own.
struct WaveGpu {
    glm::vec4 originKind{0.0f};   // xyz = origin (world), w = PropagationKind
    glm::vec4 axisFront{0.0f};    // xyz = unit axis (directional), w = how far the front has travelled
    glm::vec4 shape{0.0f};        // x = front width, y = trail length, z = range, w = falloff exponent
    glm::vec4 vertical{0.0f};     // x = vertical half-extent, y = growth per metre, z = ring count,
                                  // w = beam radius (0 = a plane rather than a beam)
    glm::vec4 color{0.0f};        // rgb = core radiance (intensity and envelope folded in),
                                  // w = the metres at which sparkle has faded out (its whole AA)
    glm::vec4 edge{0.0f};         // rgb = edge radiance, w = rainbow amount 0..1
    glm::vec4 rainbow{0.0f};      // x = cycles per metre, y = phase, z = saturation, w = brightness
    glm::vec4 sparkle{0.0f};      // x = cells per metre (0 = off), y = size, z = intensity,
                                  // w = twinkle phase
    glm::vec4 response{0.0f};     // x = ground, y = foliage, z = surface, w = emissive amplification
};
static_assert(sizeof(WaveGpu) == 144);

// What one frame hands the renderer. A plain array so nothing allocates and so `scene::Scene` can
// hold it by value the way it holds `post`.
struct WaveFrame {
    std::uint32_t count = 0;
    WaveGpu effects[kMaxGpuWaves]{};
};

// The intermediate an effect resolves to, before packing. Exposed because every interesting
// question -- did it activate, where did it end up pointing, how far has the front got -- is
// answerable here without a GPU, which is what the tests ask.
struct ResolvedWave {
    const EffectInstance* effect = nullptr;
    glm::vec3 origin{0.0f};
    glm::vec3 axis{0.0f, 0.0f, -1.0f};
    float frontDistance = 0.0f; // metres the front has travelled since this pass started
    float envelope = 0.0f;      // 0..1: delay, fade in, lifetime and fade out, multiplied together
    double elapsed = 0.0;       // seconds since this pass started, for the phase lanes
    glm::vec3 color{0.0f};      // the colour actually used: the hero's accent when it had one
};

// Resolves ONE wave-type instance. Nothing when it is not alive this frame: disabled, outside its
// activation window, faded to zero, its front between passes, or a focus/owner source with nothing
// to stand on. Pure: the same instance, context and second always give the same record.
[[nodiscard]] std::optional<ResolvedWave> resolveWave(const EffectInstance& effect, const EffectContext& context);

// Every wave-type instance in `effects` (instances of other types are skipped), walked in `order`
// -- indices into `effects`, the evaluation order `effectEvaluationOrder` computes; empty means list
// order. Returns how many were written. An instance that is alive once `out` is full is DROPPED,
// and says so: when `status` is non-empty (indexed like `effects`) every wave-type instance's entry
// is written -- Disabled, Dormant, Drawn or Dropped -- and no other entry is touched.
std::size_t resolveWaves(std::span<const EffectInstance> effects, const EffectContext& context,
                         std::span<ResolvedWave> out, std::span<const std::uint32_t> order = {},
                         std::span<EffectStatus> status = {});

// Packs a resolved effect for the GPU. Pure, so a test can read every lane.
[[nodiscard]] WaveGpu packWave(const ResolvedWave& resolved);

// Both steps, into the frame block the renderer reads.
void buildWaveFrame(std::span<const EffectInstance> effects, const EffectContext& context, WaveFrame& out,
                    std::span<const std::uint32_t> order = {}, std::span<EffectStatus> status = {});

} // namespace avgen::world

