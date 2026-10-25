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
//
// ADR-1662 adds a third metric, `PathWave`: distance measured ALONG a terrain feature's polyline (a
// river's own course), so a front follows the bends the water follows. The CPU walks the polyline once
// per front per frame and hands the GPU an eight-point chain round the front; the shader projects onto
// that chain. It also lights the water from under its reflection, disturbs the water's normal behind the
// front, and lights the land beside the path by facet orientation (see `PathShape`).

#include "core/error.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/hero.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// How many wave FRONTS reach the GPU at once. Chosen the way `spatial::kMaxGpuFields` chose sixteen:
// large enough that nothing real hits it, small enough that the per-fragment loop has a bound
// anybody can reason about -- and the loop runs over the live count, so an empty slot costs nothing.
// A front that does not fit is dropped: an instance whose NEWEST front does not fit is
// `EffectStatus::Dropped` on the Effects panel's card.
//
// ADR-981: 16, from 8. A trigger-activated instance now keeps each earlier front alive for its own
// lifetime (`resolveWaveFronts`), so one hero pulse fired every bar with a 4.5 s life is up to three
// fronts, and Glowmere Valley 3 holds two such heroes on screen with a travel beam over them.
inline constexpr std::size_t kMaxGpuWaves = 16;
// ADR-981: the most fronts ONE instance keeps alive at once (the newest and up to three before it).
inline constexpr std::size_t kMaxWaveFronts = 4;

// ---- propagation -------------------------------------------------------------------------------

// The distance metric the wave travels along. See the header: the two below are the two metrics, and
// the kinds a future effect would want are masks over them.
enum class PropagationKind : std::uint8_t {
    DirectionalWave, // distance measured along an axis from the origin: a front sweeping the world
    RadialWave,      // distance measured as a radius in the ground plane: a ripple
    PathWave,        // ADR-1662: distance measured along a terrain feature's polyline: light down a river
    LightBall,       // ADR-1663: a ball of light thrown along a camera-relative arc, lighting what it passes
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

// ADR-1662: the path a `PathWave` travels and how its light is shaped. The path is a terrain node's
// feature by name -- the very polyline the water's flow and streaks are built from, so the light cannot
// drift from the water -- and the front travels along it AWAY from the camera, starting where the
// source (normally the camera) projects onto it.
struct PathShape {
    std::string node;          // the terrain node
    std::string feature;       // its feature (a river)
    // How the front moves: it covers `range` metres in range / speed seconds, eased out by `ease`
    // (0 = constant speed, 1 = starts at twice the mean speed and stops), then holds there while the
    // instance's own fade-out takes it away.
    float ease = 0.5f;
    float wakeGrowth = 0.25f;  // the wake is trailLength + wakeGrowth x metres travelled (perspective)
    float coreWidth = 6.0f;    // metres: the core's 1/e half-width across the path
    float coreOffset = 0.0f;   // 0 = one core on the centreline; > 0 = twin filaments this far either side
    float haloWidth = 35.0f;   // metres: the soft halo round the core
    float reach = 0.0f;        // metres across the path the land is lit to (0 = the land is not lit)
    float reachHeight = 90.0f; // metres above the path's level the land light reaches
    float land = 0.0f;         // the land light's share of the core radiance (facets lit by orientation)
    float bed = 0.0f;          // the same below the path's level (a riverbed seen through the water), lit from the head
    float embed = 0.625f;      // on water: the share of the light drawn under the reflection (the rest on top)
    float disturb = 0.0f;      // on water: the wake's ripple slope behind the front (0 = the mirror untouched)
    float nearFade = 0.0f;     // metres from the camera inside which the light fades in (0 = off)
    // ADR-1663 (pass 4): the lane -- metres across the path from its centreline (+ = right, looking downstream),
    // plus up to +-laneJitter metres chosen per hit (a hash of the hit's launch time: seek equals play) -- and the
    // splash: rings spreading from where the front launched (`splash` is their slope; 0 = none), and the wake's
    // ripples scaled up near the camera (`nearWake` times at the camera, fading to 1x by 700 m; 0 = 1x everywhere).
    float lane = 0.0f;
    float laneJitter = 0.0f;
    float splash = 0.0f;
    float nearWake = 0.0f;
    // ADR-1666: the splash throws light -- a flash at the launch and a bright ring running out with the splash's rings,
    // in the wave's colour, this many times the splash (0: none; the splash only ripples the water, as in ADR-1663)
    float splashGlow = 0.0f;
    // ADR-1666: how far the light reaches under the water, in metres -- down (it fades from half this depth) and across
    // (at least this wide); 0 keeps ADR-1663's 40 m
    float bedReach = 0.0f;
};

// ADR-1663: a LightBall's flight. Launch and target are offsets in the camera's own frame (right, up, forward, in
// metres), taken from the camera of the current frame, so a ball thrown "from the near left bank into the canyon"
// is that wherever the camera is. The ball flies between them in `travel` seconds, eased by the propagation's
// `path.ease`, bowed up by `arc` metres at mid-flight; then holds while the instance fades it.
struct BallShape {
    glm::vec3 launch{-70.0f, -60.0f, 220.0f};
    glm::vec3 target{60.0f, 140.0f, 2400.0f};
    float travel = 1.2f;       // seconds
    float arc = 120.0f;        // metres of upward bow at mid-flight
    float lightRadius = 260.0f;// metres: where the light on surfaces has fallen to half
    float range = 1200.0f;     // metres: past this the light is cut (faded from 70%)
    float coreSize = 7.0f;     // metres: the visible ball's radius
    float haloSize = 60.0f;    // metres: its glow
    // ADR-1663 (pass 4, the owner: "big glowing vibrant fireballs"): a corona round the core whose edge flickers, a
    // short flaming trail behind it, and a per-hit spread of the target to the right/left (a hash of the hit's launch
    // second: seek equals play). All 0: the plain ball above.
    glm::vec3 coronaColor{1.0f, 0.42f, 0.1f};
    float coronaIntensity = 0.0f;
    float coronaSize = 80.0f;  // metres
    float flicker = 0.0f;      // 0..1: how much the corona's edge flickers
    float trail = 0.0f;        // metres of flaming trail behind the ball
    float spread = 0.0f;       // metres: each hit's target moves up to +- this to the right
    // ADR-1665 (pass 5, the owner: "make the fireball look more like a realistic fireball"): how the visible ball is
    // drawn. 0: ADR-1663's glow (core, corona, trail; every value below ignored, byte-identical). 1: a flame billboard --
    // domain-warped fbm tongues swept back from the flight, coloured by a blackbody ramp (white-yellow core, orange, deep
    // red), with a sooty rim. 2: volumetric-lite -- a short emission-absorption march through a teardrop of 3D
    // domain-warped noise, hot inside, cooling to soot at its edge. `smoke` darkens the rim and leaves a smoke trail (it
    // dims the aurora behind it); `embers` sheds glowing sparks along the trail.
    int look = 0;
    float smoke = 0.0f;        // 0..1
    float embers = 0.0f;       // 0..1
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

    PathShape path; // ADR-1662: PathWave only
    BallShape ball; // ADR-1663: LightBall only

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

// Mirrors `Wave` in shaders/common.wgsl. 208 bytes (ADR-1662: 144 + the path chain); see ADR-207 for
// why it lives in FrameUniforms rather than in a buffer of its own.
//
// A PathWave (kind 2) re-reads the lanes it has no use for (ADR-1662; ADR-1663 added the lane, the splash and `extra`):
//   originKind  xyz = the front's point on the path (at the source's height), w = 2
//   axisFront   xyz = the path's unit tangent at the front (downstream), w = metres travelled
//   shape       x = leading-edge width, y = wake length, z = near fade (m), w = wake falloff exponent
//   vertical    x = seconds since launch (ADR-1663; the light reaches 40 m below the level), y = metres above it the
//               land light reaches, z = land reach across the path (m), w = halo width (m)
//   color       rgb = core radiance (envelope folded in), w = core half-width (m)
//   edge        rgb = leading-edge radiance (envelope folded in), w = twin offset (m; 0 = one core)
//   rainbow     x = embed share, y = disturbance slope (envelope folded in), z = land share, w = bed share
//   sparkle     x = 0, y = the front's speed now (m/s), z = its mean speed (m/s), w = 0
//   path        eight xz points round the front, the first `lead` ahead of it, the second AT it, the rest
//               back along the wake (on the hit's lane); every other kind leaves them zero.
//   extra       xy = the launch point (xz, on the lane), z = the splash's slope (envelope folded in),
//               w = near-wake gain
// A LightBall (kind 3, ADR-1663):
//   originKind  xyz = the ball, w = 3;   axisFront xyz = its direction of flight
//   color       rgb = the light it casts (intensity and envelope folded in), w = the light's half radius (m)
//   edge        rgb = the visible core's radiance (edge colour x edge intensity x envelope), w = core radius (m)
//   shape       x = halo radius (m), y = range (m);   response as every kind
//   rainbow     rgb = the corona's radiance (envelope folded in), w = its radius (m)
//   vertical    x = trail length (m), y = flicker 0..1, z = transport seconds (mod 4096) for the flicker, w = 0
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
    glm::vec4 path[4]{};          // ADR-1662: PathWave's chain, two xz points per vec4; zero otherwise
    glm::vec4 extra{0.0f};        // ADR-1663: PathWave's launch point, splash and near-wake gain; zero otherwise
};
static_assert(sizeof(WaveGpu) == 224);
inline constexpr std::size_t kPathChainPoints = 8;

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
    // ADR-1662, PathWave only: the chain round the front (`kPathChainPoints` xz points, see WaveGpu), the
    // wake's length, the front's speed now and its mean, and the transport second.
    std::array<glm::vec2, kPathChainPoints> path{};
    glm::vec2 launch{0.0f};     // ADR-1663: where the front launched (xz, on its lane), for the splash
    float wake = 0.0f;
    float frontSpeed = 0.0f;
    float meanSpeed = 0.0f;
    double seconds = 0.0;
    double launchSecond = 0.0;  // ADR-1665: a light ball's launch (its window's start), its look's seed
};

// ADR-1662: a polyline's geometry for a PathWave, exposed for the tests. `arcOf` projects a point onto
// the polyline (in xz) and returns its arc length and the unit tangent there; `pointAt` walks to an arc
// length (wrapping on a closed polyline, clamping on an open one). A polyline whose last point is within
// a metre of its first is closed.
struct PathGeometry {
    std::span<const glm::vec3> points;
    glm::mat4 transform{1.0f};
    [[nodiscard]] bool closed() const;
    [[nodiscard]] float length() const;
    [[nodiscard]] float arcOf(const glm::vec3& p, glm::vec2* tangent = nullptr) const;
    [[nodiscard]] glm::vec2 pointAt(float arc, glm::vec2* tangent = nullptr) const;
};

// Resolves ONE wave-type instance's NEWEST front. Nothing when it is not alive this frame: disabled,
// outside its activation window, faded to zero, its front between passes, or a focus/owner source
// with nothing to stand on. Pure: the same instance, context and second always give the same record.
[[nodiscard]] std::optional<ResolvedWave> resolveWave(const EffectInstance& effect, const EffectContext& context);

// ADR-981: every front of ONE instance that is alive this frame, newest first, up to `out.size()`
// (at most `kMaxWaveFronts` are asked for). A `Trigger` activation with a lifetime gives each event
// its own front, living its own lifetime with its own envelope: the next trigger no longer cuts the
// last ring off mid-flight, it starts a new one beside it. Every other activation -- and a Trigger
// with no lifetime, whose window is "until the next one" by definition -- has the one front
// `resolveWave` gives. The newest front is exactly `resolveWave`'s. Pure, like it.
std::size_t resolveWaveFronts(const EffectInstance& effect, const EffectContext& context, std::span<ResolvedWave> out);

// Every live front of every wave-type instance in `effects` (instances of other types are skipped),
// walked in `order` -- indices into `effects`, the evaluation order `effectEvaluationOrder` computes;
// empty means list order. Each instance's NEWEST front first, in that order, then the earlier fronts
// (ADR-981), so an old ring never takes a slot a new pulse needs. Returns how many were written. An
// instance whose newest front is alive once `out` is full is DROPPED, and says so: when `status` is
// non-empty (indexed like `effects`) every wave-type instance's entry is written -- Disabled, Dormant,
// Drawn or Dropped -- and no other entry is touched.
std::size_t resolveWaves(std::span<const EffectInstance> effects, const EffectContext& context,
                         std::span<ResolvedWave> out, std::span<const std::uint32_t> order = {},
                         std::span<EffectStatus> status = {});

// Packs a resolved effect for the GPU. Pure, so a test can read every lane.
[[nodiscard]] WaveGpu packWave(const ResolvedWave& resolved);

// Both steps, into the frame block the renderer reads.
void buildWaveFrame(std::span<const EffectInstance> effects, const EffectContext& context, WaveFrame& out,
                    std::span<const std::uint32_t> order = {}, std::span<EffectStatus> status = {});

} // namespace avgen::world

