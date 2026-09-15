#pragma once

// World effects (ADR-207): a spatial phenomenon propagating through the world.
//
// A world effect has a **source**, a **propagation geometry**, a **motion**, an **appearance**, a
// **lifetime** and -- because every number in it is an ordinary parameter -- whatever modulation
// somebody routes into it. That sentence is the whole model, and the two effects this file shipped
// for are two settings of it: a beam that travels ahead of a camera on its way to the next hero, and
// a ripple that spreads from a mushroom's base while the camera is holding it.
//
// Three decisions are load-bearing.
//
// **Resolution is a pure function of time.** `resolveWorldEffects` takes the authored set and a
// context -- the second on the transport clock, the camera's pose and velocity, where the nodes are,
// which heroes exist, and the shot schedule the director baked -- and returns records ready for the
// GPU. It reads no frame counter, no wall clock and no previous frame, which is what keeps an
// offline render of second N byte-identical to a realtime playthrough of second N (ADR-091). The
// camera's *velocity* is the one thing a naive implementation would take as a frame-to-frame
// difference; here it is a finite difference in timeline seconds over a fixed step, so it is the
// same vector at 30 fps and at 120.
//
// **Nothing here iterates the scene.** At most `kMaxGpuWorldEffects` records reach the GPU and every
// per-surface question is asked per fragment, in `shaders/world_effects.wgsl`, from data already
// resident in the frame block. A wave front therefore lands on the ground where the ground is and on
// a leaf where the leaf is, with no projection, no bounds test and no per-object cost at all.
//
// **Two propagation kinds are two distance metrics, not two effects.** `DirectionalWave` measures
// along an axis; `RadialWave` measures a radius in the ground plane. A ring, a shockwave, a cone and
// a beam are each one of those two with a different mask, which is why the extension point is the
// mask and why this enum is short.

#include "core/error.hpp"
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
// anybody can reason about. The ninth active effect is dropped with a warning rather than silently.
inline constexpr std::size_t kMaxGpuWorldEffects = 8;

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

// When the effect exists at all. §15 of the brief: an effect that is permanently on is scenery, and
// the two shipped effects are both *events* -- one belongs to a camera move, one to a held subject.
enum class Activation : std::uint8_t {
    // The window is open for the whole timeline. Note what that does and does not mean: the *front*
    // still makes one pass from t = 0 and is then past its range, so a permanently-visible effect is
    // one with a `repeatSeconds`, not one with `Always`.
    Always,
    Window,       // an authored [start, start + seconds) on the transport clock
    CameraTravel, // while the director's cut says the camera is travelling between subjects
    HeroFocus,    // while the director's cut is spotlighting this effect's source
};
[[nodiscard]] const char* activationName(Activation a);
[[nodiscard]] std::optional<Activation> activationFromName(std::string_view name);

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

// Procedural sparkle around the leading edge. Cells are static in world space and each cell's
// brightness is a smooth function of how near the front is, so the pattern does not crawl when the
// camera moves -- which is the failure mode §8 names. The distance fade is the anti-aliasing: a cell
// smaller than a pixel is faded out rather than sampled.
struct Sparkle {
    bool enabled = false;
    float density = 1.1f;     // cells per metre; coarse cells read as blobs, not as sparkle
    float size = 0.22f;       // 0..1 of a cell
    float intensity = 1.6f;
    float speed = 0.6f;       // twinkle rate, in cycles per second, off the effect's own clock
    float fadeDistance = 85.0f; // metres at which sparkle is gone, so it cannot alias at range
    std::uint32_t seed = 1;
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

struct Timing {
    double delay = 0.0;      // seconds after activation before the front starts
    double lifetime = 0.0;   // seconds the effect lives; 0 = as long as its activation lasts
    double fadeIn = 0.35;
    double fadeOut = 0.9;
    double windowStart = 0.0;    // Activation::Window
    double windowSeconds = 6.0;  // Activation::Window
    // Restart the front every this many seconds while the activation holds. 0 = one pass. What makes
    // a hero pulse a *pulse* rather than a single expanding ring -- and what a beat route modulates
    // when somebody wants one ring per bar.
    double repeatSeconds = 0.0;
    [[nodiscard]] Result<void> validate() const;
};

struct WorldEffect {
    std::string name;
    bool enabled = true;
    std::string style;  // the preset it was made from, for the UI; changes nothing on its own

    EffectEndpoint source;
    bool hasTarget = false;
    EffectEndpoint target;

    Propagation propagation;
    Appearance appearance;
    Sparkle sparkle;
    MaterialResponse response;
    Activation activation = Activation::Always;
    Timing timing;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<WorldEffect> fromJson(const nlohmann::json& j);
};

// Refuses duplicate names: a parameter path is `worldfx/<name>/...`, and two effects of one name is
// two things writing one path.
[[nodiscard]] Result<void> validateWorldEffects(std::span<const WorldEffect> effects);

// ---- presets (§18) -----------------------------------------------------------------------------
//
// A style configures the underlying parameters and then gets out of the way; nothing reads `style`
// at runtime. Named separately for beams and pulses because the two answer different questions --
// "what is travelling" against "what is spreading" -- and a single list would offer Water as a beam.

[[nodiscard]] std::span<const std::string_view> beamStyleNames();
[[nodiscard]] std::span<const std::string_view> pulseStyleNames();
// Applies a style's appearance, sparkle and propagation shape to `effect`, leaving its name, source,
// target, activation and timing alone. False when the name is not a style.
bool applyBeamStyle(WorldEffect& effect, std::string_view style);
bool applyPulseStyle(WorldEffect& effect, std::string_view style);

// The two effects the brief is about, ready to drop into a scene: a camera travel beam and a hero
// mushroom ground pulse. Exposed because "add the camera beam" should be one call from the UI and
// one line in a test, not a page of field assignments that can drift from the shipped scene's.
[[nodiscard]] WorldEffect cameraTravelBeam(std::string name = "Camera Travel Beam");
[[nodiscard]] WorldEffect heroGroundPulse(std::string name = "Hero Mushroom Pulse");

// ---- resolution --------------------------------------------------------------------------------

// One span of the director's cut, flattened to what an effect needs to know. Baked from an
// `app::Sequence` when the camera is directed (ADR-075) and empty otherwise, in which case
// `CameraTravel` and `HeroFocus` effects simply never activate -- which is the honest answer for a
// camera nobody is directing.
struct ShotSpan {
    double start = 0.0;
    double end = 0.0;
    bool travel = false;     // the camera is moving from one subject to another
    // The camera has landed: this shot is not travelling and it is about something. Deliberately a
    // geometric fact rather than the director's own `Spotlight::emphasis`, which is how much of the
    // *film* a subject owns and is zero for a whole intro. An effect gated on "the camera is on this
    // hero" wants the former; `emphasis` below is there for anything that wants the latter.
    bool spotlight = false;
    float emphasis = 0.0f;   // 0..1, the director's own weighting of this subject
    std::string subject;     // who the shot is about
    glm::vec3 subjectPosition{0.0f};
    float subjectRadius = 1.0f;
    std::string handoff;     // for a travel shot, who it is going to
    glm::vec3 handoffPosition{0.0f};
};

// Where the world's nodes are. An interface rather than a std::function so resolution allocates
// nothing: the engine counts allocations per frame and a lambda capture in this path would show up.
class WorldEffectScene {
public:
    virtual ~WorldEffectScene() = default;
    // World position of the node named `name`, or false when there is no such node.
    [[nodiscard]] virtual bool nodePosition(std::string_view name, glm::vec3& out) const = 0;
    // The node's forward axis in world space, for DirectionMode::SourceForward. Optional: a scene
    // that cannot answer returns false and the direction falls back to the next mode in the chain.
    [[nodiscard]] virtual bool nodeForward(std::string_view name, glm::vec3& out) const { (void)name; (void)out; return false; }
};

struct WorldEffectContext {
    double seconds = 0.0;             // the transport clock, and the only clock
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraTarget{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
    // Metres per second, as a finite difference **in timeline seconds**. See the header: taking it
    // from the frame delta would make the beam point somewhere different at 30 fps than at 120.
    glm::vec3 cameraVelocity{0.0f};
    std::span<const ShotSpan> shots;
    std::span<const HeroPoint> heroes;
    const WorldEffectScene* scene = nullptr;
};

// Mirrors `WorldEffect` in shaders/world_effects.wgsl. 144 bytes; see ADR-207 for why it lives in
// FrameUniforms rather than in a buffer of its own.
struct WorldEffectGpu {
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
static_assert(sizeof(WorldEffectGpu) == 144);

// What one frame hands the renderer. A plain array so nothing allocates and so `scene::Scene` can
// hold it by value the way it holds `post`.
struct WorldEffectFrame {
    std::uint32_t count = 0;
    WorldEffectGpu effects[kMaxGpuWorldEffects]{};
};

// The intermediate an effect resolves to, before packing. Exposed because every interesting
// question -- did it activate, where did it end up pointing, how far has the front got -- is
// answerable here without a GPU, which is what the tests ask.
struct ResolvedEffect {
    const WorldEffect* effect = nullptr;
    glm::vec3 origin{0.0f};
    glm::vec3 axis{0.0f, 0.0f, -1.0f};
    float frontDistance = 0.0f; // metres the front has travelled since this pass started
    float envelope = 0.0f;      // 0..1: delay, fade in, lifetime and fade out, multiplied together
    double elapsed = 0.0;       // seconds since this pass started, for the phase lanes
    glm::vec3 color{0.0f};      // the colour actually used: the hero's accent when it had one
};

// Resolves every enabled effect. Returns how many were written; effects past `kMaxGpuWorldEffects`
// active at once are dropped (the caller warns). An effect whose activation is closed resolves with
// `envelope == 0` and is **not** written, so a scene full of dormant effects costs the GPU nothing.
std::size_t resolveWorldEffects(std::span<const WorldEffect> effects, const WorldEffectContext& context,
                                std::span<ResolvedEffect> out);

// Packs a resolved effect for the GPU. Pure, so a test can read every lane.
[[nodiscard]] WorldEffectGpu packWorldEffect(const ResolvedEffect& resolved);

// Both steps, into the frame block the renderer reads.
void buildWorldEffectFrame(std::span<const WorldEffect> effects, const WorldEffectContext& context,
                           WorldEffectFrame& out);

} // namespace avgen::world
