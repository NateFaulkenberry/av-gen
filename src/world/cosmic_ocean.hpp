#pragma once

// The Cosmic Ocean (ADR-390): a layered, animated, procedural celestial environment, authored as an
// `AtmosphericEffect` and evaluated per view ray in `shaders/cosmic_ocean.wgsl`.
//
// This file is the *data*: what an artist authors, what a frame packs, and what the parameter
// tables register. It deliberately knows nothing about rendering and nothing about
// `AtmosphericEffect` -- `world/atmospherics.hpp` includes this one, not the other way round -- so
// that the whole model can be constructed, validated, round-tripped and walked by a test with no
// GPU and no scene.
//
// Five decisions are load-bearing.
//
// **Nothing here is an object.** There is no star, planet, mote or galaxy anywhere in this file,
// on the CPU, or in a buffer. Every celestial body is a *hash of the cell the view ray lands in*,
// evaluated in the fragment shader and forgotten. That is the brief's §23 ("do NOT create thousands
// of individual Scene entities") satisfied by there being nothing to create, and it is why
// `Planets::density` can be a modulation target at all: ADR-015's particle pool has to be destroyed
// and recreated to change capacity, and a hash has no capacity.
//
// **Depth and parallax are one mechanism, not ten.** Every stratum carries a `depth` in metres and
// a `parallax` in 0..1, and the shader intersects the view ray with a shell of that radius about
// the effect's `center` from an eye interpolated by that fraction:
//
//     E = center + (cameraPos - center) * parallax
//     P = the point where the ray from E along rd meets |P - center| = depth
//
// `parallax = 0` puts the eye at the centre, so the stratum is sampled by direction alone: camera
// rotation reveals it and camera translation does nothing. `parallax = 1` is the true geometric
// parallax of something `depth` metres away. The brief's §11 conceptual defaults -- nebula 0.02,
// distant stars 0.05, planets 0.18, dust 0.45, near particles 0.80 -- *are* this number.
//
// That one mechanism is also what answers §26. Nothing is ever sampled at a world coordinate that
// grows without bound: every field is indexed by a unit direction plus a bounded offset, so there
// is no wrap to author, no seam to hide, and no precision cliff at the floating island's scale.
//
// **Every quantity here is dimensionless or a metre, and none of them is per metre.** No layer
// marches. ADR-374, ADR-379 and ADR-381 each cost a day to the same mistake -- a density or an
// emission that had to be per metre and was not -- and the reason this file cannot join them is
// that its densities are *coverage fractions of a shell*, integrated analytically rather than
// accumulated along a ray. Where a comment says "density" it means "how much of this stratum is
// not empty", in 0..1, and a reader who converts it to an extinction coefficient has misread it.
//
// **Every phase is packed on the CPU from the transport second.** `packCosmicOcean` takes
// `seconds` and turns each speed into a phase; the shader receives phases and never a clock. So an
// offline render of second N is identical to a realtime playthrough of second N (ADR-091), a seek
// cannot desynchronise anything, and §18's "occasional celestial events" are
// `floor(t / period)` hashed rather than an accumulator that would have to be rewound.
//
// **Every field in this struct is read by the shader.** That is a rule and not an observation. The
// failure this codebase keeps finding -- ADR-385's `bloomLevels`, ADR-350's family, and the
// `nodes/cosmos-*/emissiveBoost` that ADR-390 measured as inert while three presets were setting it
// -- is a parameter that registers, serialises, modulates and reaches nothing. A field added here
// without a reader in `cosmic_ocean.wgsl` is that bug, pre-written.

#include "core/error.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace avgen::world {

// ---- the strata --------------------------------------------------------------------------------

// What every layer has, whatever it is made of. Split out because §10's depth strata are the same
// question nine times, and nine copies of four fields is how two of them end up meaning subtly
// different things.
struct CosmicStratum {
    float depth = 40000.0f;  // metres from `center` to this shell
    float parallax = 0.05f;  // 0 = sampled by direction alone; 1 = true parallax at `depth`
    float density = 0.5f;    // 0..1 coverage; what "how much of this is here" means for the kind
    float brightness = 1.0f; // linear scale on the stratum's radiance
};

// §4. A nebula is a domain-warped fBM on the stratum's shell, two-coloured by height in the field.
struct CosmicNebula {
    CosmicStratum stratum{};
    float scale = 1.6f;      // cycles across the sky at the lowest octave
    float detail = 4.0f;     // octaves, 1..6; the quality tier scales this
    float turbulence = 0.55f;// how much the higher octaves fight the lower ones
    float warp = 0.45f;      // domain warp amount -- what stops it reading as blobs
    float flowSpeed = 0.012f;// how fast the field drifts, in cycles per second
    float evolveSpeed = 0.03f; // how fast it changes shape rather than position
    float softness = 0.0f;   // §12: drops top octaves and widens the lowest. Distance, cheaply.
    float contrast = 1.25f;  // exponent on the coverage mask; higher = emptier sky, sharper edges
    float colorMix = 0.45f;  // where between the nebula's two colours the field's midpoint sits
    float shimmer = 0.25f;   // §21: a high-frequency emissive sparkle inside the densest parts
};

// §7. A star stratum. Four of these, differing only in numbers -- which is the brief's point: the
// depth comes from the layers disagreeing, not from four different techniques.
struct CosmicStars {
    CosmicStratum stratum{};
    float size = 1.0f;         // angular size multiplier; a star below a pixel is faded, not drawn
    float sizeVariance = 0.6f; // 0..1 spread of per-star size
    float twinkle = 0.35f;     // §8: depth of the temporal variation, 0 = steady
    float twinkleSpeed = 0.7f; // cycles per second of the *slowest* star; each has its own phase
    float colorVariation = 0.35f; // 0..1 spread around the star tint, blue-white to amber
    float drift = 0.0f;        // slow bulk rotation of the whole stratum, radians per second
    float glint = 0.06f;       // probability a given star also throws a brief four-point glint
};

// §5, §6. Distant celestial bodies, analytically intersected. Not billboards, not geometry.
struct CosmicPlanets {
    CosmicStratum stratum{};
    float scale = 1.0f;         // angular radius multiplier
    float scaleVariance = 0.7f; // 0..1 spread; the brief asks for worlds that are not all one size
    float clustering = 0.25f;   // 0 = evenly spread over the cells; 1 = clumped into groups
    float atmosphereGlow = 0.6f;// the rim of scattered light at the limb
    float terminator = 0.18f;   // softness of the day/night boundary, as a fraction of the radius
    float nightSide = 0.06f;    // how much the unlit hemisphere still emits
    float cloudBands = 0.45f;   // 0 = bare worlds; 1 = every gas giant banded
    float rings = 0.3f;         // probability a given planet has them
    float ringSize = 1.9f;      // outer radius as a multiple of the planet's
    float ringBrightness = 0.7f;
    float colorVariation = 0.5f;// 0..1 spread around the planet tint
    float rotationSpeed = 0.01f;// radians per second of surface rotation
    float driftSpeed = 0.004f;  // radians per second of bulk drift across the sky
    float driftDirection = 25.0f; // degrees; which way the drift goes
};

// §9. The near field, and the reason the camera feels *inside* the environment rather than in front
// of it. Sampled from a 3D lattice about the camera rather than a shell, because at this distance
// a shell would read as a dome.
struct CosmicDust {
    CosmicStratum stratum{};
    float size = 1.0f;
    float sizeVariance = 0.7f;
    float turbulence = 0.35f;  // how much the drift curls rather than translating
    float driftSpeed = 0.9f;   // metres per second
    float fadeDistance = 0.55f;// fraction of `depth` over which motes fade in and out
};

// §19. Sparse, mostly tiny, a few worth looking at.
struct CosmicGalaxies {
    CosmicStratum stratum{};
    float scale = 1.0f;
    float spiral = 0.65f;       // 0 = elliptical and diffuse; 1 = clear arms
    float coreBrightness = 2.2f;// the bulge, which is what makes one read as a galaxy at all
    float inclination = 0.6f;   // 0..1 how far from face-on the population averages
    float rotationSpeed = 0.0f; // radians per second; usually 0, because they are galaxies
};

// §13. Atmospheric perspective: what distance does to colour and contrast.
struct CosmicAtmosphere {
    float haze = 0.35f;              // broad luminous fog filling the volume between the strata
    float density = 0.5f;            // how quickly that fog accumulates with depth
    float brightnessFalloff = 0.45f; // how much of a distant stratum's brightness is lost
    float saturationFalloff = 0.35f; // ...and of its saturation
    float contrastFalloff = 0.40f;   // ...and of its contrast
    float scattering = 0.55f;        // forward-scattering bias around bright bodies
    float glow = 0.5f;               // §20: the halo radius multiplier on every emissive thing
};

// §14, §15. Colour is a system, not a swatch. Three independent timescales, because the brief asks
// for minutes, tens of seconds and seconds and one rate cannot be all three.
struct CosmicColor {
    glm::vec3 primary{0.09f, 0.22f, 0.62f};
    glm::vec3 secondary{0.36f, 0.14f, 0.72f};
    glm::vec3 accent{0.10f, 0.62f, 0.72f};
    glm::vec3 deepSpace{0.008f, 0.011f, 0.032f};
    glm::vec3 nebulaTint{0.30f, 0.45f, 1.00f};
    glm::vec3 planetTint{0.72f, 0.74f, 0.88f};
    glm::vec3 starTint{0.86f, 0.90f, 1.00f};
    glm::vec3 atmosphereTint{0.16f, 0.30f, 0.58f};
    float blend = 0.5f;        // where between primary and secondary the environment sits
    float hueDrift = 0.06f;    // how far the whole palette wanders, in turns
    float saturation = 1.0f;
    float slowSpeed = 0.004f;  // turns per second -- minutes
    float mediumSpeed = 0.03f; // tens of seconds
    float fastSpeed = 0.35f;   // §15's second-scale micro variation
    float fastAmount = 0.06f;  // how much of the fast rate actually reaches the picture
};

// §16, §17. The whole ocean moving as one.
struct CosmicFlow {
    float speed = 1.0f;       // global multiplier over every other motion in the effect
    float directionAzimuth = 30.0f; // degrees
    float directionElevation = 8.0f;
    float strength = 0.35f;   // how far the flow displaces what it carries
    float turbulence = 0.3f;
    float curl = 0.45f;       // 0 = straight cosmic wind; 1 = eddies and spirals
    float scale = 0.8f;       // size of the flow field's features
    float evolveSpeed = 0.02f;// how fast the flow field itself changes
};

// §18. Sparse, stateless, and deterministic: event `i` is `floor(t / period)` and everything about
// it is a hash of `i`. No accumulator, so a seek cannot desynchronise it and an offline render of
// second N is a realtime frame at second N.
struct CosmicEvents {
    float shootingStars = 0.45f; // events per minute
    float comets = 0.08f;
    float flares = 0.2f;
    float pulses = 0.3f;         // a nebula or planet brightening
    float probability = 0.7f;    // fraction of scheduled slots that actually fire
    float intensity = 1.0f;
    float size = 1.0f;
    float speed = 1.0f;
    float lifetime = 1.4f;       // seconds
    glm::vec3 color{0.75f, 0.90f, 1.00f};
};

// §33. Keep the region behind the subject calm without switching anything off.
struct CosmicMask {
    float amount = 0.0f;         // 0 = no mask at all, which is the default
    float azimuth = 0.0f;        // degrees; the direction the mask is centred on
    float elevation = 10.0f;
    float innerAngle = 14.0f;    // degrees; fully suppressed inside this
    float outerAngle = 42.0f;    // degrees; unaffected outside this
    float suppression = 0.7f;    // how much is removed at the centre, 0..1
    float horizonBias = 0.0f;    // -1 emphasises below the horizon, +1 above, 0 neither
};

// §23's quality ladder, as sample counts. A tier scales these; it never removes a control, which is
// the rule `QualitySettings::particleSpawnScale` states and this obeys.
struct CosmicQuality {
    float nebulaOctaves = 4.0f; // 1..6, the ceiling `CosmicNebula::detail` is clamped to
    float starStrata = 4.0f;    // 1..4; how many of the four star layers are evaluated
    float planetCells = 3.0f;   // the n of the n x n cell neighbourhood searched, odd, 1..5
    float dustCells = 3.0f;     // likewise, in three dimensions; 0 turns dust off
};

// ---- the authored effect -----------------------------------------------------------------------

struct CosmicOcean {
    // ---- master (§28) ----
    bool enabled = true;      // distinct from the effect's own `enabled`: this is the ocean's
    float intensity = 1.0f;   // one number over everything, and the thing a route reaches for first
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float exposure = 0.0f;    // stops; separate from brightness because it is applied in log space
    float globalScale = 1.0f; // multiplies every stratum's `depth`, so the whole ocean can breathe
    float seed = 1.0f;        // §34: change it, get a different universe; keep it, get this one

    // Where the strata are concentric about. Defaults to the world origin, which for the Tree of
    // Life is the island; a scene that wanted the ocean centred elsewhere says so here.
    glm::vec3 center{0.0f};

    CosmicNebula nebulaFar{};
    CosmicNebula nebulaMid{};
    CosmicStars starsUltra{};
    CosmicStars starsFar{};
    CosmicStars starsMid{};
    CosmicStars starsNear{};
    CosmicPlanets planets{};
    CosmicDust dust{};
    CosmicGalaxies galaxies{};
    CosmicAtmosphere atmosphere{};
    CosmicColor color{};
    CosmicFlow flow{};
    CosmicEvents events{};
    CosmicMask mask{};
    CosmicQuality quality{};

    [[nodiscard]] bool active() const { return enabled && intensity > 0.0f; }
    [[nodiscard]] Result<void> validate() const;
};

// The §30 default: deep blue-black, subtle violet/cyan nebula, sparse bright stars, several distant
// planets, soft dust, restrained haze, modest parallax, gentle drift. Returned by value rather than
// being the struct's member defaults, because the member defaults have to be *neutral* (a field
// that round-trips to itself) and the default *look* has to be beautiful, and those are not the
// same numbers.
[[nodiscard]] CosmicOcean defaultCosmicOcean();

// §14's five palettes, and one more. A style writes the underlying parameters and then gets out of
// the way: nothing reads the style name at runtime, exactly as ADR-230 requires.
[[nodiscard]] std::span<const std::string_view> cosmicOceanStyleNames();
bool applyCosmicOceanStyle(CosmicOcean& ocean, std::string_view style);

// ---- the GPU block -----------------------------------------------------------------------------

// Mirrors `CosmicOcean` in shaders/cosmic_ocean.wgsl. Forty-six vec4 lanes, 736 bytes; every one is
// commented at its declaration there and here, because a lane whose meaning lives in only one of
// the two files is a lane that will be read wrong. It is appended to the end of `FrameUniforms`,
// which is the pattern that block has used four times: no offset above it moves, so every other
// pass's view of the block stays byte-identical.
struct CosmicOceanGpu {
    glm::vec4 master{0.0f};      // x intensity, y brightness, z contrast, w saturation
    // w is the *palette* saturation, not the master one in `master.w`: the master grades the whole
    // composited result and this one desaturates the palette the nebulae and dust are tinted from,
    // so a style can be muted without flattening the stars. It lives here rather than beside the
    // colours because every colour lane's w is already spoken for -- and it is the field the
    // "every leaf moves the block" test caught being packed nowhere at all.
    glm::vec4 master2{0.0f};     // x exposure gain (already exp2'd), y global scale, z seed, w palette saturation
    glm::vec4 center{0.0f};      // xyz centre (world), w mask amount
    glm::vec4 mask{0.0f};        // xyz unit direction the mask is centred on, w cos(inner)
    glm::vec4 mask2{0.0f};       // x cos(outer), y suppression, z horizon bias, w 0
    glm::vec4 colorDeep{0.0f};   // rgb deep-space radiance, w hue-drift phase (turns)
    glm::vec4 colorPrimary{0.0f};// rgb, w palette blend
    glm::vec4 colorSecondary{0.0f}; // rgb, w medium-evolution phase
    glm::vec4 colorAccent{0.0f}; // rgb, w fast-evolution phase
    glm::vec4 colorAtmos{0.0f};  // rgb atmospheric tint, w fast-evolution amount

    glm::vec4 nebFar0{0.0f};     // x depth, y parallax, z density, w scale
    glm::vec4 nebFar1{0.0f};     // x octaves, y turbulence, z warp, w brightness
    glm::vec4 nebFar2{0.0f};     // x flow phase, y softness, z contrast, w colour mix
    glm::vec4 nebFar3{0.0f};     // x evolve phase, y shimmer, z domain-warp octaves,
                                 // w 1 when the nebulae come from the low-resolution buffer (ADR-450)
    glm::vec4 nebMid0{0.0f};
    glm::vec4 nebMid1{0.0f};
    glm::vec4 nebMid2{0.0f};
    glm::vec4 nebMid3{0.0f};
    glm::vec4 nebulaTint{0.0f};  // rgb shared nebula tint, w 0

    glm::vec4 starUltra{0.0f};   // x depth, y parallax, z density, w brightness
    glm::vec4 starFar{0.0f};
    glm::vec4 starMid{0.0f};
    glm::vec4 starNear{0.0f};
    glm::vec4 starShape{0.0f};   // x size, y size variance, z colour variation, w drift phase
    glm::vec4 starTwinkle{0.0f}; // x amount, y phase, z glint probability, w strata count
    glm::vec4 starTint{0.0f};    // rgb, w 0

    glm::vec4 planet0{0.0f};     // x depth, y parallax, z density, w brightness
    glm::vec4 planet1{0.0f};     // x scale, y scale variance, z clustering, w colour variation
    glm::vec4 planet2{0.0f};     // x atmosphere glow, y terminator, z night side, w cloud bands
    glm::vec4 planet3{0.0f};     // x rings, y ring size, z ring brightness, w cell count
    glm::vec4 planet4{0.0f};     // x rotation phase, y drift phase, z drift dir x, w drift dir y
    glm::vec4 planetTint{0.0f};  // rgb, w 0

    glm::vec4 dust0{0.0f};       // x depth, y parallax, z density, w brightness
    glm::vec4 dust1{0.0f};       // x size, y size variance, z turbulence, w drift phase
    glm::vec4 dust2{0.0f};       // x fade distance, y cell count, zw 0

    glm::vec4 galaxy0{0.0f};     // x depth, y parallax, z density, w brightness
    glm::vec4 galaxy1{0.0f};     // x scale, y spiral, z core brightness, w inclination
    glm::vec4 galaxy2{0.0f};     // x rotation phase, yzw 0

    glm::vec4 atmos0{0.0f};      // x haze, y density, z brightness falloff, w saturation falloff
    glm::vec4 atmos1{0.0f};      // x contrast falloff, y scattering, z glow, w 0

    glm::vec4 flow0{0.0f};       // xyz unit flow direction, w strength
    glm::vec4 flow1{0.0f};       // x turbulence, y curl, z scale, w evolve phase

    glm::vec4 event0{0.0f};      // x shooting-star period (s), y comet period, z flare period, w pulse period
    glm::vec4 event1{0.0f};      // x probability, y intensity, z size, w speed
    glm::vec4 event2{0.0f};      // x lifetime (s), y transport second, zw 0
    glm::vec4 eventColor{0.0f};  // rgb, w 0
};
static_assert(sizeof(CosmicOceanGpu) == 16 * 46);

// Packs the authored ocean for one frame. Pure: the same arguments give the same bytes, which is
// what lets a test read every lane and what makes an offline render of second N identical to a
// realtime frame at second N. `envelope` is the effect's lifecycle fade, folded into `intensity`.
// `quality` scales the sample counts, and is the only argument a renderer supplies.
struct CosmicQualityScale {
    float octaveScale = 1.0f; // multiplier on the nebula's octave ceiling
    float sampleScale = 1.0f; // multiplier on the planet and dust cell neighbourhoods
    // ADR-450. The fraction of the frame's resolution the nebulae are evaluated at. 1.0 evaluates
    // them in the main draw as everything else is; below 1.0 they are rendered once into a small
    // offscreen pair and sampled back, and this value only reaches the pack so that the shader
    // knows which of the two it is looking at.
    float nebulaScale = 1.0f;
};
[[nodiscard]] CosmicOceanGpu packCosmicOcean(const CosmicOcean& ocean, float envelope, double seconds,
                                             CosmicQualityScale quality = {});

// ---- the parameter tables ----------------------------------------------------------------------
//
// Declared here, over `CosmicOcean`, rather than in `atmospheric_params.cpp` over
// `AtmosphericEffect`. The rows belong with the struct they describe -- a field added above and
// forgotten below is the drift the round-trip test in `test_cosmic_ocean.cpp` exists to catch --
// and keeping them here is also what stops a hundred and ten rows landing in a file another agent
// is editing. `atmospheric_params.cpp` bridges these to its own `FloatField` with a templated
// adapter; see ADR-390's Sequencing section.

struct CosmicFloatField {
    const char* leaf;
    float lo, hi, slo, shi;
    float (*get)(const CosmicOcean&);
    void (*set)(CosmicOcean&, float);
};

struct CosmicColorField {
    const char* leaf;
    glm::vec3 (*get)(const CosmicOcean&);
    void (*set)(CosmicOcean&, glm::vec3);
};

struct CosmicBoolField {
    const char* leaf;
    bool (*get)(const CosmicOcean&);
    void (*set)(CosmicOcean&, bool);
};

[[nodiscard]] std::span<const CosmicFloatField> cosmicFloatFields();
[[nodiscard]] std::span<const CosmicColorField> cosmicColorFields();
[[nodiscard]] std::span<const CosmicBoolField> cosmicBoolFields();

// Belt and braces after a route has driven a final anywhere inside a hard range: the counts the
// shader uses as loop bounds have to be integers in range whatever a modulation curve did.
void sanitiseCosmicOcean(CosmicOcean& ocean);

} // namespace avgen::world
