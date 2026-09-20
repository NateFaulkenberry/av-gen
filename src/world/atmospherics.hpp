#pragma once

// Atmospheric effects (ADR-230): a celestial phenomenon in the sky, rather than a wave travelling
// through the ground.
//
// ADR-207 gave the engine one answer for "a spatial phenomenon with a source, a propagation
// geometry, a speed, an appearance and a lifetime": resolve it on the CPU once per frame into a
// fixed-size record and evaluate it per fragment in the shared *surface* shader. That answer is
// right for a beam crossing a valley and wrong for a comet, for one reason that is not negotiable:
// **the surface shader only runs where there is a surface.** The sky has none. A comet, an aurora,
// a meteor shower and a lightning sheet are all things you see *instead of* geometry, and none of
// them can be an additive term on a fragment that was never shaded.
//
// So this file shares ADR-207's *model* and replaces its *rendering path*. It reuses
// `world::Activation`, `world::Timing` and `world::Sparkle` outright -- lifecycle, gating on the
// director's cut, delay/fade/lifetime/repeat and the twinkle controls are the same questions with
// the same answers -- and adds two `kind`s whose geometry is a **view ray**, evaluated in
// `shaders/atmosphere_fx.wgsl` from a direction-space draw that sits at the far plane.
//
// Four decisions are load-bearing.
//
// **Resolution is a pure function of the transport second**, exactly as ADR-207 requires.
// `resolveAtmosphericEffects` reads no frame counter, no wall clock and no previous frame. A
// comet's position is `C(a(t))` for an arc length `a` that is a closed-form function of `t`, so an
// offline render of second N is byte-identical to a realtime playthrough of second N (ADR-091).
//
// **A comet is a curve in the world, not a streak on the screen.** Its trajectory is authored in
// sky coordinates -- an azimuth and elevation to launch from, one to fly to, and a distance -- and
// flown as a **great-circle arc at that distance**, bowed by an optional lift and curvature.
//
// The arc matters, and a straight chord between the two points does not work. A chord between two
// points on a sphere passes through the interior, so a comet on one dives towards the anchor and
// its *apparent* elevation swings far outside the two numbers that were authored: the first
// implementation launched at 31 degrees, aimed at 5, and passed overhead at 49, which put it out of
// frame. On an arc the distance is constant, so a camera near the anchor sees the azimuth and
// elevation interpolate between exactly what was typed -- which is the difference between a control
// and a suggestion.
//
// The shader integrates the view ray against that curve, so the comet has real parallax: crossing
// Glowmere's valley moves it against the stars, which is the difference between a celestial object
// and a texture sliding across the sky.
//
// **An aurora is a set of vertical cylinders, not a plane.** The ray is intersected with K shells
// at stepped radii; the hit's azimuth and height index a curtain whose top is driven by the audio
// spectrum. Stepping the radii is what gives the layers parallax against each other, and putting
// the base at a world height is what makes terrain silhouette it -- the sky draw is depth-tested,
// so a ridge in front of the aurora occludes it with no horizon seam to author.
//
// **Audio reaches it the way audio reaches everything else.** Scalar response -- height, brightness,
// flow speed, beat pulse -- is a `ModRoute` onto `atmos/<name>/<property>`, because that is the rule
// `world/effect_params.hpp` states and a second reaction system is a second thing to debug. The one
// deliberate exception is the **spectrum vector**: a curtain whose shape is a frequency spectrum
// needs sixteen numbers across the sky, and a scalar route cannot carry a vector. Those sixteen
// bins ride in `AtmosphericContext::spectrum`, filled by the engine from the same
// `analysis::AnalysisFrame` every other consumer reads, and are documented as the exception rather
// than smuggled in as a second analyzer.

#include "core/error.hpp"
#include "world/effects.hpp"
#include "world/world_effects/field_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// How many of each reach the GPU at once. Comets are six because a "burst" in §3.5 of the brief is
// a handful launched together and six is what a sky holds before they stop reading as events;
// auroras are two because the only reason to have a second one is to cross-fade between two colour
// presets, which §7 asks for by name. Both are per-fragment loop bounds, so both are small on
// purpose. See ADR-230's revisit triggers before raising either.
inline constexpr std::size_t kMaxGpuComets = 6;
inline constexpr std::size_t kMaxGpuAuroras = 2;

// The number of spectrum bins an aurora's curtain shape is drawn from. Sixteen is the smallest
// count that still reads as a *spectrum* rather than as five bands smoothed: it survives the
// mirroring across the sky (below) with eight distinct features per side, which is about what a
// curtain can show before the folds hide it.
inline constexpr std::size_t kAuroraBands = 16;

// ---- the kinds ---------------------------------------------------------------------------------

enum class AtmosphereKind : std::uint8_t {
    Comet,  // a bright head on a world-space curve, with a trail integrated along the view ray
    Aurora, // curtains on vertical shells, rising from a world height, shaped by the spectrum
    // ADR-387. A cosmic vortex is a sky phenomenon a scene AUTHORS, so it belongs in this family
    // for the same reasons the other two do: instances with names, an enable, a table of parameters
    // under `atmos/<name>/`, serialisation in `atmosphericEffects`, a row in the World Effects
    // panel, and modulation, all for free. That it is drawn by the volumetric pass rather than by
    // `atmosphere_fx.wgsl` is a rendering detail -- this family is about authoring, not about which
    // pass rasterises the result.
    Vortex,
};
[[nodiscard]] const char* atmosphereKindName(AtmosphereKind k);
[[nodiscard]] std::optional<AtmosphereKind> atmosphereKindFromName(std::string_view name);

// What the effect is anchored to. A comet anchored to the World has honest parallax and can leave
// frame; one anchored to the Camera keeps its place in the sky however far the camera travels,
// which is what a music video wants when the shot has to contain the event. Both are pure functions
// of time, because the camera is a baked track (ADR-075).
enum class SkyAnchor : std::uint8_t {
    World,  // a fixed world point; the effect has full translation parallax
    Camera, // the camera's eye; the effect keeps its bearing and distance, and still has rotation parallax
};
[[nodiscard]] const char* skyAnchorName(SkyAnchor a);
[[nodiscard]] std::optional<SkyAnchor> skyAnchorFromName(std::string_view name);

// §6: how much of the phenomenon lands on the ground below. Deliberately three words rather than a
// float, because the question an artist asks is "should this light the valley" and the honest
// answers are no, a little, and yes. Each maps to a multiplier over the authored illumination.
enum class GroundGlow : std::uint8_t { Off, Subtle, Strong };
[[nodiscard]] const char* groundGlowName(GroundGlow g);
[[nodiscard]] std::optional<GroundGlow> groundGlowFromName(std::string_view name);

// ---- shared pieces -----------------------------------------------------------------------------

// Procedural hue cycling, shared by both kinds. The same construction ADR-207 chose and for the
// same reason: a cosine palette over a *distance* is a hue ramp through the phenomenon rather than
// a list of colours somebody typed, and because it is a function of position and of a phase that
// came from the transport clock, it cannot shimmer. `scale` is cycles per unit of the kind's own
// parameter -- metres along a comet's tail, turns of azimuth across an aurora.
struct SkyRainbow {
    bool enabled = false;
    float speed = 0.22f;      // cycles per second the ramp slides through the phenomenon
    float scale = 1.1f;       // cycles across it; under ~1 the whole thing is one hue at a time
    float hueOffset = 0.0f;   // where the ramp starts, so two comets can be different colours
    float saturation = 0.85f;
    float brightness = 1.0f;
    [[nodiscard]] Result<void> validate() const;
};

// §6. `color` is a radiance, `intensity` scales it, and the three-word `GroundGlow` scales that
// again -- so switching Strong -> Subtle does not lose the colour somebody chose.
struct GroundIllumination {
    GroundGlow mode = GroundGlow::Off;
    glm::vec3 color{0.35f, 0.85f, 1.0f};
    float intensity = 0.6f;
    float radius = 220.0f;   // metres; a comet's pool of light on the ground. Ignored by an aurora,
                             // whose illumination is hemispheric by construction.
    float falloff = 2.0f;    // exponent on the radial fade
    [[nodiscard]] Result<void> validate() const;
};

// ---- comets ------------------------------------------------------------------------------------

// Where a comet flies. Authored in sky coordinates because that is the frame an artist composes in:
// "come in high on the left, exit low behind the ridge" is two azimuth/elevation pairs, and the
// same numbers give the same picture in any scene, whereas a world coordinate is a number you have
// to re-derive every time the terrain changes.
//
// Azimuth is degrees clockwise from +Z (so 0 looks along +Z, 90 along +X); elevation is degrees
// above the horizon. The two directions are taken from the anchor at `distance` metres, which puts
// both ends on a sphere -- and the bow below lifts the middle off the chord.
struct CometPath {
    SkyAnchor anchor = SkyAnchor::World;
    glm::vec3 anchorPosition{0.0f}; // SkyAnchor::World; ignored for Camera

    float startAzimuth = -70.0f;
    float startElevation = 34.0f;
    float endAzimuth = 55.0f;
    float endElevation = 9.0f;
    float distance = 2600.0f;   // metres from the anchor: how far away the comet flies, and so how
                                // much translation parallax it has

    float travelSeconds = 7.0f; // how long the crossing takes, which is what a sequencer authors
    float speedScale = 1.0f;    // a multiplier over that, so a route can drive speed
    // 0 = constant speed. Above 0 the comet accelerates through the crossing (it covers the second
    // half faster), below 0 it decelerates. Applied as a reparameterisation of progress, so the
    // start and end points do not move.
    float acceleration = 0.0f;

    // Both bows are metres at the midpoint, perpendicular to the great circle: `curvature` out of
    // its plane, `arcLift` towards the zenith. They are applied to the *direction* and renormalised,
    // so a bowed comet is still at `distance` and still behaves like a thing in the sky.
    float curvature = 0.0f;
    float arcLift = 180.0f;
    [[nodiscard]] Result<void> validate() const;
};

// What a comet looks like. Every radiance here is scene-linear and unclamped: Glowmere's bloom
// threshold is 1.0 in exposed units, so a core intensity of 18 is what makes a comet read as a
// light source rather than as a bright patch.
struct CometAppearance {
    glm::vec3 coreColor{0.55f, 1.0f, 1.0f};
    float coreIntensity = 18.0f;
    float headSize = 26.0f;       // metres of the emissive core's radius

    glm::vec3 haloColor{0.20f, 0.75f, 1.0f};
    float haloIntensity = 2.2f;
    float haloSize = 150.0f;      // metres; the soft atmospheric glow around the head

    glm::vec3 tailColor{0.45f, 0.35f, 1.0f};
    float tailIntensity = 4.5f;
    float tailLength = 900.0f;    // metres of trail behind the head
    float tailWidth = 70.0f;      // metres at the far end; the near end is the head's radius
    float tailFalloff = 1.9f;     // exponent on the fade towards the tail's end

    // Flowing wisps: the trail is displaced by world-anchored noise, which is what stops it reading
    // as a swept cone. World-anchored on purpose -- the displacement is a function of the *world
    // position* of the trail point, so it does not crawl when the camera moves.
    float wispAmount = 55.0f;     // metres of lateral displacement at the tail's end
    float wispScale = 0.0024f;    // cycles per metre of the displacement noise
    float flowSpeed = 0.35f;      // how fast the wisps drift, in cycles per second

    [[nodiscard]] Result<void> validate() const;
};

struct Comet {
    CometPath path;
    CometAppearance appearance;
    // Glowing fragments shed along the trail. Cells are indexed by **absolute arc length from the
    // launch point**, so a fragment sits at a fixed world position and is left behind as the head
    // moves on -- which is both what a comet actually does and the only indexing that cannot crawl.
    Sparkle sparkle;
    SkyRainbow rainbow;
    [[nodiscard]] Result<void> validate() const;
};

// ---- aurora ------------------------------------------------------------------------------------

struct AuroraShape {
    SkyAnchor anchor = SkyAnchor::Camera; // an aurora surrounds the viewer; Camera is the honest default
    glm::vec3 anchorPosition{0.0f};

    float curtainCount = 3.0f;   // shells, 1..5. Each is one more ray/cylinder solve per pixel.
    float radius = 5200.0f;      // metres to the nearest shell
    float layerSpacing = 0.34f;  // each further shell is this much further out, as a fraction
    float baseHeight = -40.0f;   // world Y the curtains stand on. Below the valley floor, so the
                                 // bases are hidden behind terrain rather than ending in mid-air.
    float curtainHeight = 2600.0f; // metres from the base to a full-height curtain's top

    float waveAmplitude = 0.30f; // how much the top undulates, as a fraction of the height
    float waveScale = 2.4f;      // undulations around the full turn of azimuth
    float turbulence = 0.45f;    // fine distortion of the folds
    float complexity = 26.0f;    // vertical ray structure: striations per turn of azimuth
    float flowSpeed = 0.055f;    // how fast the whole curtain drifts, in turns per second
    float driftSpeed = 0.13f;    // how fast the internal folds churn
    float verticalSpeed = 0.07f; // how fast the fold pattern climbs
    [[nodiscard]] Result<void> validate() const;
};

struct AuroraAppearance {
    // Bottom, middle and top. Three rather than two because an aurora's signature is a *vertical*
    // hue ramp -- green at the base through cyan to violet at the fading top -- and interpolating
    // two colours cannot produce it.
    glm::vec3 lowColor{0.15f, 1.0f, 0.55f};
    glm::vec3 midColor{0.20f, 0.85f, 1.0f};
    glm::vec3 topColor{0.65f, 0.30f, 1.0f};

    float intensity = 2.6f;
    float emission = 1.0f;       // how much of it reaches the bloom mask; 1 = all of it
    float opacity = 0.85f;       // how solid a curtain is against the stars behind it
    float edgeBrightness = 2.4f; // the bright lower rim, which is what makes a curtain read as one
    float filaments = 0.9f;      // fine luminous threads, the thing high frequencies drive
    float sparkle = 0.5f;        // glinting points within the curtain, on the same high band
    float horizonGlow = 0.55f;   // the broad wash above the horizon the curtains stand in
    [[nodiscard]] Result<void> validate() const;
};

// §4.2. These are **depths on an existing signal**, not an analyzer: each one scales how much the
// band already in the frame block moves its feature. The mapping the brief proposes is the default
// and every number is a parameter, which is the whole of "make them adjustable".
struct AuroraAudio {
    float bass = 0.85f;      // base movement and overall height
    float lowMid = 0.55f;    // curtain width and wave amplitude
    float mid = 0.45f;       // internal folds, shape complexity
    float high = 0.70f;      // fine filaments and edge motion
    float beat = 0.35f;      // the pulse on each beat
    float sensitivity = 1.0f;// one multiplier over all of them
    // How much of the curtain's height comes from the *spectrum* rather than from a flat base.
    // 0 is an aurora that ignores the music's shape entirely and still answers its level through
    // the routes above; 1 is a full visualiser.
    float spectrumShape = 0.75f;
    [[nodiscard]] Result<void> validate() const;
};

struct Aurora {
    AuroraShape shape;
    AuroraAppearance appearance;
    AuroraAudio audio;
    SkyRainbow rainbow;
    [[nodiscard]] Result<void> validate() const;
};

// ---- the authored effect -----------------------------------------------------------------------

// One struct with both payloads rather than a variant: an effect changes kind when somebody picks a
// different preset, and a variant would throw away the settings of the kind they left. Both are
// small, both round-trip, and only the one `kind` names is ever read.
// ADR-387: the cosmic vortex as an authored instance rather than a singleton on `Environment`.
// Every field here was `Environment::Vortex` and means exactly what it did, which is what keeps
// §10's "the Tree of Life must look the same" true by construction rather than by re-tuning.
struct Vortex {
    glm::vec3 center{0.0f};       // world space
    float radius = 0.0f;          // metres; 0 is off and is the default
    float thickness = 120.0f;     // vertical half-extent of the wall
    float swirl = 3.2f;           // radians of shear per unit radius
    float rotationSpeed = 0.035f; // radians per second
    float density = 0.45f;        // extinction PER METRE (ADR-374)
    float innerVoid = 0.18f;
    float contrast = 1.9f;
    float turbulence = 0.6f;
    float turbulenceScale = 2.1f;
    float breathAmount = 0.05f;
    float breathSpeed = 0.18f;
    // ADR-389: what makes it read as SMOKE rather than as noise. The three octaves used to be
    // independent fBMs summed at fixed rates, and uncorrelated detail sitting on top of a spiral is
    // exactly what the eye calls grain. Smoke reads as smoke because the fine detail is ADVECTED by
    // the coarse flow -- dragged into the sheets and curls of the big structure.
    float smokeWarp = 0.0f;   // domain-warp amount; the one that does the work
    float smokeBillow = 0.0f; // 0 wispy fBM, 1 rounded billowing masses
    float detail = 0.2f;      // the fine octave's weight; was a hardcoded 0.2
    float emission = 1.0f;        // emissive density PER METRE (ADR-374)
    float filaments = 0.9f;
    float spill = 2.5f;           // surface irradiance on what floats above it (ADR-379)
    // ADR-388, at the owner's request: how much of the SCENE's light this medium scatters.
    //
    // 0 is off and is the default, and the default is load-bearing rather than cautious. ADR-371
    // measured what 1 does: at the shipped density, with the key light at intensity 22 over a 2.6 km
    // march, the frame came back at mean luminance 131 of 255 **with the vortex's own emission set
    // to zero** -- an even wash, the flat haze ADR-358 refused to build. So the funnel is
    // self-luminous by construction and this is the controlled way back in, for the one thing that
    // needs it: an upward spotlight whose beam should read INSIDE the funnel rather than stopping
    // at its edge.
    float scattering = 0.0f;
    float cometResponse = 0.0f;   // ADR-381
    float cometReach = 6.0f;
    float funnelDepth = 0.0f;     // metres the throat descends; 0 keeps the flat slab
    float throat = 0.25f;
    float throatDensity = 0.6f;
    glm::vec3 colorDeep{0.020f, 0.016f, 0.075f};
    glm::vec3 colorMid{0.050f, 0.085f, 0.230f};
    glm::vec3 colorAccent{0.090f, 0.320f, 0.420f};
    [[nodiscard]] bool active() const { return radius > 0.0f; }
    [[nodiscard]] Result<void> validate() const;
};

struct AtmosphericEffect {
    std::string name;
    bool enabled = true;
    std::string style; // the preset it was made from, for the UI; changes nothing on its own

    AtmosphereKind kind = AtmosphereKind::Comet;
    Comet comet;
    Aurora aurora;
    Vortex vortex;

    GroundIllumination ground;
    Activation activation = Activation::Always; // ADR-207's, unchanged
    Timing timing;                              // ADR-207's, unchanged

    // §68, one field many subscribers. Which spatial field this effect's motion answers to, and how
    // much. Empty and 0 by default, so every scene written before this existed renders the frame it
    // rendered before -- and `fields::FieldBus::unresolved` makes a name that resolves to nothing a
    // reported problem rather than a still picture.
    //
    // The reason this is one member on the shared struct rather than three per-kind ones is the
    // whole of ADR-387's argument: the question "what is the air doing where this effect is" has
    // one answer, and a comet, an aurora and a funnel that each asked it privately are what this
    // replaces. What each kind DOES with the answer is per-kind and lives in the resolver.
    fields::Subscription flow;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<AtmosphericEffect> fromJson(const nlohmann::json& j);
};

// Refuses duplicate names, for the reason `validateWorldEffects` does: a name is half of a
// parameter path.
[[nodiscard]] Result<void> validateAtmosphericEffects(std::span<const AtmosphericEffect> effects);

// ---- presets (§10) -----------------------------------------------------------------------------
//
// A style configures the underlying parameters and then gets out of the way; nothing reads `style`
// at runtime. Separate lists per kind because "Rainbow Cosmic" is a comet and "Glowmere
// Bioluminescence" is an aurora, and one list would offer each to the other.

[[nodiscard]] std::span<const std::string_view> cometStyleNames();
[[nodiscard]] std::span<const std::string_view> auroraStyleNames();
[[nodiscard]] std::span<const std::string_view> vortexStyleNames();
// Applies a style's appearance and shape, leaving name, activation, timing and ground illumination
// alone. False when the name is not a style of that kind.
bool applyCometStyle(AtmosphericEffect& effect, std::string_view style);
bool applyAuroraStyle(AtmosphericEffect& effect, std::string_view style);
// ADR-387. A vortex style leaves `center` alone: where the funnel is in the world is a placement
// decision the scene made, and a preset that moved it would silently unanchor it from the island.
bool applyVortexStyle(AtmosphericEffect& effect, std::string_view style);

// Ready-made effects, so "add a comet" is one call from the UI and one line in a test rather than a
// page of field assignments that can drift from the shipped scene's.
[[nodiscard]] AtmosphericEffect bioluminescentComet(std::string name = "Bioluminescent Comet");
[[nodiscard]] AtmosphericEffect glowmereAurora(std::string name = "Glowmere Aurora");
[[nodiscard]] AtmosphericEffect cosmicVortex(std::string name = "Cosmic Vortex");

// ---- resolution --------------------------------------------------------------------------------

// What an atmospheric effect needs to know about this frame. A superset of nothing: the shot spans
// and the transport second come straight from ADR-207's context, because `Activation` is ADR-207's.
struct AtmosphericContext {
    double seconds = 0.0;          // the transport clock, and the only clock
    glm::vec3 cameraPosition{0.0f};
    std::span<const ShotSpan> shots;
    // §4.2's exception, documented in this file's header: `kAuroraBands` normalised magnitudes,
    // low to high. Empty is legal and means "no music" -- the curtain falls back to its flat base
    // height, which is what an aurora should look like in silence.
    std::span<const float> spectrum;
    // §68. The scene's published fields. Null is legal and means "no field layer this frame": every
    // subscription then resolves to nothing, which is the same picture as no subscription and is
    // what every call site written before the bus existed gets.
    //
    // A raw pointer rather than a reference because the bus is rebuilt each frame by the engine and
    // a context is a value that tests construct without one; a `span`-shaped borrow with no
    // ownership, exactly as `shots` and `spectrum` are.
    const fields::FieldBus* fieldBus = nullptr;
};

// The intermediate an effect resolves to, before packing. Exposed because every interesting
// question -- did it activate, where is the head, how far has it flown -- is answerable here with
// no GPU, which is what the tests ask.
struct ResolvedAtmospheric {
    const AtmosphericEffect* effect = nullptr;
    glm::vec3 anchor{0.0f};
    float envelope = 0.0f;     // 0..1: delay, fade in, lifetime and fade out, multiplied together
    double elapsed = 0.0;      // seconds since this pass started, for the phase lanes

    // Comet only. The arc is `dir0 -> dir1` through `omega` radians at `distance` metres, so
    // `pathLength` is a true arc length and speeds and tail lengths in metres mean what they say.
    glm::vec3 dir0{0.0f, 0.0f, 1.0f}; // unit direction from the anchor to the launch point
    glm::vec3 dir1{0.0f, 0.0f, 1.0f}; // ...and to the destination
    float omega = 0.0f;               // radians between them
    float distance = 0.0f;            // metres from the anchor the comet flies at
    float liftAmount = 0.0f;          // midpoint bow towards the zenith, as a fraction of `distance`
    float curveAmount = 0.0f;         // midpoint bow out of the arc's plane, likewise
    glm::vec3 launch{0.0f};           // the crossing's start, in world space (for tests and the ground track)
    glm::vec3 destination{0.0f};
    float pathLength = 0.0f;          // metres of arc
    float travelled = 0.0f;           // metres flown so far, after the acceleration reparameterisation

    // §68: what the subscribed field was doing at this effect's anchor, this frame, and how much of
    // it the effect asked for. Resolved on the CPU once per effect per frame and folded into the
    // numbers the GPU structs already carry -- ADR-055's rule that a transfer function is evaluated
    // once per draw rather than once per fragment, applied to a field instead of to a plant.
    //
    // It is sampled at the ANCHOR rather than at the head, and that is a decision rather than a
    // convenience: the anchor is a pure function of (activation, camera), so the sample is a pure
    // function of time. Sampling at the head would make the comet's own motion an input to the
    // field that drives it, which is a feedback loop an integrator would have to close -- and
    // ADR-091's two-tier determinism has no integrators in it.
    fields::FlowSample flow;
    float flowInfluence = 0.0f; // 0 when unsubscribed, when the name is dead, or when set to 0
};

// §68. What one effect's subscription answers at `anchor` this frame, and how much of it the effect
// asked for. Free functions rather than members because the vortex has no `ResolvedAtmospheric` to
// hang them on -- it is a static field the march samples, not a trajectory -- and because a thing
// that is a pure function of its arguments is a thing a test can ask a question of without building
// a frame.
struct EffectFlow {
    fields::FlowSample sample;
    // 0 when the effect is unsubscribed, when it named a field the bus does not publish, or when
    // the artist set the influence to 0. Those three are deliberately one number downstream: a
    // subscriber's behaviour must not depend on WHY there is no field, only on there being none.
    // Which of the three it was is a question for the report, not for the picture.
    float influence = 0.0f;
    [[nodiscard]] bool active() const { return influence != 0.0f; }
};
[[nodiscard]] EffectFlow resolveEffectFlow(const AtmosphericEffect& effect, const glm::vec3& anchor,
                                           const AtmosphericContext& ctx);

// The two numbers every kind derives from a flow. They are here, shared, rather than open-coded in
// three packers, because "each effect has its own isolated wind handling" is the thing §68 is
// against and three private derivations of one field would be that again one layer down.
//
// `flowAmplitude` is a multiplier on whatever lateral motion the kind already has: 1 in still air,
// more in wind and more again inside a gust front. Unitless, and therefore valid for either
// `FlowUnits` -- which is why the atmospheric family reads `strength` and `gust` rather than `flow`,
// and can subscribe to a wind or a vortex without knowing which it got.
[[nodiscard]] float flowAmplitude(const fields::FlowSample& sample, float influence);
// `flowOffset` is a SPATIAL phase offset in radians, wrapped to [0, tau). Two effects in different
// parts of the sky subscribed to one field get different offsets, which is the property that makes
// this a field rather than a shared clock -- a shared clock would move them in lockstep, and moving
// in lockstep is the tell ADR-055 was written to remove from the meadow.
[[nodiscard]] float flowOffset(const fields::FlowSample& sample, float influence);

// Mirrors `Comet` in shaders/atmosphere_fx.wgsl. 144 bytes, the same size ADR-207 chose, for the
// same reason: it is what nine vec4s cost and nine is what the lanes need.
struct CometGpu {
    glm::vec4 anchorTravel{0.0f}; // xyz = anchor (world), w = arc length travelled (m)
    glm::vec4 dir0Tail{0.0f};     // xyz = unit direction to the launch point, w = tail length (m)
    glm::vec4 dir1Path{0.0f};     // xyz = unit direction to the destination, w = arc length (m)
    glm::vec4 arc{0.0f};          // x = distance (m), y = omega (rad), z = lift, w = curvature
                                  // (both bows as fractions of the distance)
    glm::vec4 core{0.0f};         // rgb = core radiance (envelope folded in), w = head radius (m)
    glm::vec4 halo{0.0f};         // rgb = halo radiance, w = halo radius (m)
    glm::vec4 tail{0.0f};         // rgb = tail radiance, w = tail falloff exponent
    glm::vec4 shape{0.0f};        // x = tail width (m), y = wisp amount (m), z = wisp scale (1/m),
                                  // w = flow phase
    glm::vec4 sparkle{0.0f};      // x = fragments per metre (0 = off), y = fragment radius (m),
                                  // z = intensity, w = twinkle phase
    glm::vec4 rainbow{0.0f};      // x = cycles per metre, y = phase, z = saturation,
                                  // w = brightness (0 = rainbow off)
};
static_assert(sizeof(CometGpu) == 160);

// Mirrors `Aurora` in shaders/atmosphere_fx.wgsl. The four band vectors are spelled out rather than
// declared as an array because an array inside a struct inside an array is the one uniform layout
// shape that differs between backends often enough to be worth not finding out about.
struct AuroraGpu {
    glm::vec4 config{0.0f};  // x = curtain shells, y = nearest radius (m), z = base height (world Y),
                             // w = curtain height (m)
    glm::vec4 shape{0.0f};   // x = wave amplitude, y = wave scale, z = turbulence, w = complexity
    glm::vec4 flow{0.0f};    // x = flow phase, y = drift phase, z = vertical phase, w = layer spacing
    glm::vec4 low{0.0f};     // rgb = base radiance (intensity and envelope folded in), w = opacity
    glm::vec4 mid{0.0f};     // rgb = mid radiance, w = bloom weight
    glm::vec4 top{0.0f};     // rgb = top radiance, w = edge brightness
    glm::vec4 detail{0.0f};  // x = filaments, y = sparkle, z = spectrum shape amount, w = horizon glow
    glm::vec4 audio{0.0f};   // x = bass, y = lowMid, z = mid, w = high -- depths, already sensitised
    glm::vec4 audio2{0.0f};  // x = beat depth, y = rainbow amount, z = rainbow scale, w = rainbow phase
    glm::vec4 anchor{0.0f};  // xyz = shell centre (world), w = rainbow saturation
    glm::vec4 band0{0.0f};   // the spectrum, low to high: bins 0..3
    glm::vec4 band1{0.0f};   // bins 4..7
    glm::vec4 band2{0.0f};   // bins 8..11
    glm::vec4 band3{0.0f};   // bins 12..15
};
static_assert(sizeof(AuroraGpu) == 224);

// The ground illumination every live effect contributes, summed. One block rather than one per
// effect because §6 asks for *cinematic* light rather than correct light, and a sum of coloured
// washes is what that is -- while a per-effect list would put a loop in the surface shader for a
// term that is a single add.
struct SkyGroundGpu {
    glm::vec4 ambient{0.0f}; // rgb = hemispheric radiance from the auroras, w = 0
    glm::vec4 point{0.0f};   // xyz = the brightest comet's ground track point, w = radius (m)
    glm::vec4 pointColor{0.0f}; // rgb = its radiance, w = falloff exponent
};
static_assert(sizeof(SkyGroundGpu) == 48);

// What one frame hands the renderer. A plain aggregate so nothing allocates and `scene::Scene` can
// hold it by value beside `worldEffects`.
struct AtmosphericFrame {
    std::uint32_t cometCount = 0;
    std::uint32_t auroraCount = 0;
    // ADR-387: the live vortex, if a scene authored one. ONE, not an array: the volumetric march
    // evaluates three fBMs per sample inside it, and ADR-374 measured the single vortex at +5.5 ms
    // of a 13.5 ms frame -- the most expensive term in the scene. A second is a deliberate future
    // decision rather than an oversight, and the resolve counts what it dropped so the UI can say
    // so instead of silently ignoring it.
    bool hasVortex = false;
    Vortex vortex{};
    // Samples the shader marches down each comet's tail. A quality control (§12): lowering it
    // costs smoothness and not brightness, because the accumulation is normalised by the count and
    // the sample width has a floor of the sample spacing.
    std::uint32_t cometSteps = 24;
    CometGpu comets[kMaxGpuComets]{};
    AuroraGpu auroras[kMaxGpuAuroras]{};
    SkyGroundGpu ground{};

    // True when anything at all is live. The renderer skips the whole sky-layer draw on false, so
    // a scene with no atmospheric effects pays for none of this -- not even a uniform branch.
    [[nodiscard]] bool any() const { return cometCount > 0 || auroraCount > 0; }
};

// Resolves every enabled effect. Returns how many of each were written; effects past the caps are
// dropped (the caller warns). An effect whose activation is closed resolves with `envelope == 0`
// and is **not** written, so a scene full of dormant comets costs the GPU nothing.
struct AtmosphericCounts {
    std::size_t comets = 0;
    std::size_t auroras = 0;
    std::size_t vortices = 0; // ADR-387; at most one is used, the rest count as dropped
    std::size_t dropped = 0;
};
// §68 added `vortices`. A vortex has no trajectory, so before this it resolved into a count and
// nothing else, and `buildAtmosphericFrame` found its payload by walking `effects` a second time.
// That was fine while the payload was the authored struct copied through, and stopped being fine
// the moment the resolve had something to SAY about a vortex -- what the field it subscribes to is
// doing where it stands. Writing it into a record the caller already holds is how the other two
// kinds work, and a second walk that re-derives what the first walk knew is how the two drift.
//
// Defaulted, so every caller written before this compiles and behaves exactly as it did: an empty
// span means the vortex is counted and not recorded, which is what used to happen.
AtmosphericCounts resolveAtmosphericEffects(std::span<const AtmosphericEffect> effects,
                                            const AtmosphericContext& context,
                                            std::span<ResolvedAtmospheric> comets,
                                            std::span<ResolvedAtmospheric> auroras,
                                            std::span<ResolvedAtmospheric> vortices = {});

// Packing, pure so a test can read every lane.
[[nodiscard]] CometGpu packComet(const ResolvedAtmospheric& resolved);
[[nodiscard]] AuroraGpu packAurora(const ResolvedAtmospheric& resolved, std::span<const float> spectrum);

// Every step, into the frame block the renderer reads.
void buildAtmosphericFrame(std::span<const AtmosphericEffect> effects, const AtmosphericContext& context,
                           AtmosphericFrame& out);

// Where a comet is at this instant, in world space. The shader computes the same curve from the
// packed lanes; this is the CPU half, used for the ground track and by the tests that check a
// trajectory is a pure function of its parameters and the transport second.
[[nodiscard]] glm::vec3 cometPositionAt(const ResolvedAtmospheric& resolved, float arcLength);

} // namespace avgen::world
