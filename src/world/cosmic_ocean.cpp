#include "world/cosmic_ocean.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::world {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// A phase, from a speed and the transport second, wrapped into 0..1.
//
// Wrapped rather than left to grow, and this is the one numerically interesting line in the file: a
// render an hour into a timeline has `seconds` around 3600, and `3600 * 0.004` is fine while
// `3600 * 35.0` is not -- a float's spacing at 126000 is 0.0078, so a fast phase would quantise
// into visible steps and then stop moving altogether. Wrapping keeps every phase in 0..1 where the
// spacing is 6e-8, so the slowest evolution and the fastest shimmer are equally smooth at any point
// in a song. The wrap is invisible because every consumer is periodic in it.
[[nodiscard]] float phase(double seconds, float speed) {
    const double turns = seconds * static_cast<double>(speed);
    return static_cast<float>(turns - std::floor(turns));
}

// A phase that is *not* periodic in its consumer -- a bulk rotation, where wrapping to 0..1 would
// jump. Kept in radians and wrapped at a full turn instead.
[[nodiscard]] float angularPhase(double seconds, float radiansPerSecond) {
    const double turns = seconds * static_cast<double>(radiansPerSecond) / (2.0 * kPi);
    return static_cast<float>((turns - std::floor(turns)) * 2.0 * kPi);
}

[[nodiscard]] glm::vec3 skyDirection(float azimuthDegrees, float elevationDegrees) {
    // The same convention `CometPath` uses: azimuth clockwise from +Z, elevation above the horizon.
    // One convention in the engine, so "come in high on the left" means the same thing everywhere.
    const float az = azimuthDegrees * kDegToRad;
    const float el = elevationDegrees * kDegToRad;
    const float c = std::cos(el);
    return glm::vec3(std::sin(az) * c, std::sin(el), std::cos(az) * c);
}

// Events per minute -> seconds between slots. Zero means never, which is a period the shader can
// use directly rather than a branch it has to take: an enormous period puts the next event past
// any timeline anybody will render.
[[nodiscard]] float eventPeriod(float perMinute) {
    return perMinute > 1.0e-4f ? 60.0f / perMinute : 1.0e9f;
}

[[nodiscard]] bool finite(float v) { return std::isfinite(v); }

[[nodiscard]] Result<void> checkStratum(const CosmicStratum& s, const char* what) {
    if (!finite(s.depth) || s.depth <= 0.0f) {
        return fail("cosmic ocean: {} depth must be a positive number of metres", what);
    }
    if (!finite(s.parallax) || s.parallax < 0.0f || s.parallax > 1.0f) {
        return fail("cosmic ocean: {} parallax must be in 0..1", what);
    }
    if (!finite(s.density) || s.density < 0.0f) {
        return fail("cosmic ocean: {} density must be >= 0", what);
    }
    if (!finite(s.brightness) || s.brightness < 0.0f) {
        return fail("cosmic ocean: {} brightness must be >= 0", what);
    }
    return {};
}

} // namespace

// ---- validation --------------------------------------------------------------------------------

Result<void> CosmicOcean::validate() const {
    if (!finite(intensity) || intensity < 0.0f) {
        return fail("cosmic ocean: intensity must be >= 0");
    }
    if (!finite(globalScale) || globalScale <= 0.0f) {
        return fail("cosmic ocean: global scale must be > 0");
    }
    if (!finite(seed)) {
        return fail("cosmic ocean: seed must be a finite number");
    }
    if (!finite(center.x) || !finite(center.y) || !finite(center.z)) {
        return fail("cosmic ocean: centre must be finite");
    }
    for (const auto& [neb, what] : std::array<std::pair<const CosmicNebula*, const char*>, 2>{
             {{&nebulaFar, "far nebula"}, {&nebulaMid, "mid nebula"}}}) {
        if (auto ok = checkStratum(neb->stratum, what); !ok) return ok;
        if (!finite(neb->detail) || neb->detail < 1.0f || neb->detail > 6.0f) {
            return fail("cosmic ocean: {} detail must be 1..6 octaves", what);
        }
        if (!finite(neb->scale) || neb->scale <= 0.0f) {
            return fail("cosmic ocean: {} scale must be > 0", what);
        }
    }
    for (const auto& [st, what] : std::array<std::pair<const CosmicStars*, const char*>, 4>{
             {{&starsUltra, "ultra-distant stars"},
              {&starsFar, "distant stars"},
              {&starsMid, "mid stars"},
              {&starsNear, "near stars"}}}) {
        if (auto ok = checkStratum(st->stratum, what); !ok) return ok;
        if (!finite(st->size) || st->size < 0.0f) {
            return fail("cosmic ocean: {} size must be >= 0", what);
        }
    }
    if (auto ok = checkStratum(planets.stratum, "planets"); !ok) return ok;
    if (auto ok = checkStratum(dust.stratum, "cosmic dust"); !ok) return ok;
    if (auto ok = checkStratum(galaxies.stratum, "galaxies"); !ok) return ok;
    if (!finite(planets.ringSize) || planets.ringSize < 1.0f) {
        return fail("cosmic ocean: planet ring size must be >= 1 (a multiple of the planet radius)");
    }
    if (!finite(quality.nebulaOctaves) || quality.nebulaOctaves < 1.0f || quality.nebulaOctaves > 6.0f) {
        return fail("cosmic ocean: nebula octave budget must be 1..6");
    }
    if (!finite(quality.planetCells) || quality.planetCells < 1.0f || quality.planetCells > 5.0f) {
        return fail("cosmic ocean: planet cell neighbourhood must be 1..5");
    }
    if (!finite(quality.dustCells) || quality.dustCells < 0.0f || quality.dustCells > 5.0f) {
        return fail("cosmic ocean: dust cell neighbourhood must be 0..5");
    }
    // Finiteness only. The ordering `outerAngle >= innerAngle` is deliberately NOT validated here,
    // and `effect_conformance` is what found out why.
    //
    // The two angles are independent registered parameters, each with its own hard range of 0..180.
    // Nothing stops a modulation route, a timeline key or a slider from putting the outer inside the
    // inner -- and `validate()` runs inside `fromJson`, so refusing that combination means the
    // engine can reach an authored state whose saved file will not load again. A control the
    // application does not keep is not a control (ADR-225/350), and a control it keeps but cannot
    // read back is worse: the failure lands on the next person to open the project.
    //
    // The ordering is resolved where the value is used instead, twice over:
    // `sanitiseCosmicOcean` pushes the outer past the inner after any route has driven a final, and
    // `packCosmicOcean` clamps both angles before the cosines reach the shader. So an inverted pair
    // is a degenerate mask rather than an unloadable file, which is the right shape for a soft
    // artistic constraint between two knobs.
    if (!finite(mask.innerAngle) || !finite(mask.outerAngle)) {
        return fail("cosmic ocean: the mask's angles must be finite");
    }
    if (!finite(events.lifetime) || events.lifetime <= 0.0f) {
        return fail("cosmic ocean: event lifetime must be > 0 seconds");
    }
    return {};
}

void sanitiseCosmicOcean(CosmicOcean& o) {
    // Loop bounds first: these reach the shader as counts and a modulation route can drive a final
    // anywhere inside the hard range. A non-integral or negative bound is a hang or a wrong picture.
    o.quality.nebulaOctaves = std::clamp(std::round(o.quality.nebulaOctaves), 1.0f, 6.0f);
    o.quality.starStrata = std::clamp(std::round(o.quality.starStrata), 1.0f, 4.0f);
    o.quality.planetCells = std::clamp(std::round(o.quality.planetCells), 1.0f, 5.0f);
    o.quality.dustCells = std::clamp(std::round(o.quality.dustCells), 0.0f, 5.0f);
    o.nebulaFar.detail = std::clamp(o.nebulaFar.detail, 1.0f, 6.0f);
    o.nebulaMid.detail = std::clamp(o.nebulaMid.detail, 1.0f, 6.0f);
    // Divisors and depths.
    o.globalScale = std::max(o.globalScale, 1.0e-3f);
    o.nebulaFar.scale = std::max(o.nebulaFar.scale, 1.0e-3f);
    o.nebulaMid.scale = std::max(o.nebulaMid.scale, 1.0e-3f);
    o.planets.ringSize = std::max(o.planets.ringSize, 1.0f);
    o.events.lifetime = std::max(o.events.lifetime, 1.0e-2f);
    o.dust.fadeDistance = std::clamp(o.dust.fadeDistance, 1.0e-3f, 1.0f);
    o.mask.outerAngle = std::max(o.mask.outerAngle, o.mask.innerAngle + 1.0e-3f);
    for (CosmicStratum* s : {&o.nebulaFar.stratum, &o.nebulaMid.stratum, &o.starsUltra.stratum,
                             &o.starsFar.stratum, &o.starsMid.stratum, &o.starsNear.stratum,
                             &o.planets.stratum, &o.dust.stratum, &o.galaxies.stratum}) {
        s->depth = std::max(s->depth, 1.0f);
        s->parallax = std::clamp(s->parallax, 0.0f, 1.0f);
        s->density = std::max(s->density, 0.0f);
        s->brightness = std::max(s->brightness, 0.0f);
    }
}

// ---- the default look (§30) ----------------------------------------------------------------------

CosmicOcean defaultCosmicOcean() {
    CosmicOcean o;
    // "Deep blue-black background, subtle violet/cyan nebula, sparse bright stars, several distant
    // planets, soft cosmic dust, extremely subtle atmospheric haze, modest parallax, gentle
    // planetary drift, slow nebula evolution, occasional tiny glints, faint distant galaxies,
    // restrained bloom." Every number below is one clause of that sentence.
    //
    // The amplitudes are the art direction, and they are the part that is easy to get wrong:
    // `glowmere-cosmos.wgsl` records that the Tree of Life's lit leaves sit near 0.5 and its
    // emissive specks above 1.0, so a background that is not two orders of magnitude below them
    // stops being a background. §32's visual hierarchy is enforced here, in the defaults, rather
    // than by asking an artist to discover it.

    o.color.deepSpace = glm::vec3(0.0055f, 0.0075f, 0.0210f);
    o.color.primary = glm::vec3(0.055f, 0.145f, 0.430f);
    o.color.secondary = glm::vec3(0.230f, 0.085f, 0.520f);
    o.color.accent = glm::vec3(0.070f, 0.430f, 0.520f);
    o.color.nebulaTint = glm::vec3(0.28f, 0.44f, 1.00f);
    o.color.atmosphereTint = glm::vec3(0.10f, 0.20f, 0.44f);
    o.color.blend = 0.42f;

    // Two nebula strata, both very faint and very large. The far one barely moves.
    o.nebulaFar.stratum = {400000.0f, 0.01f, 0.42f, 0.055f};
    o.nebulaFar.scale = 1.05f;
    o.nebulaFar.detail = 4.0f;
    o.nebulaFar.warp = 0.55f;
    o.nebulaFar.contrast = 1.45f;
    o.nebulaFar.flowSpeed = 0.0035f;
    o.nebulaFar.evolveSpeed = 0.010f;
    o.nebulaFar.softness = 0.45f;
    o.nebulaFar.shimmer = 0.10f;

    o.nebulaMid.stratum = {120000.0f, 0.03f, 0.34f, 0.085f};
    o.nebulaMid.scale = 2.4f;
    o.nebulaMid.detail = 4.0f;
    o.nebulaMid.warp = 0.40f;
    o.nebulaMid.contrast = 1.7f;
    o.nebulaMid.flowSpeed = 0.009f;
    o.nebulaMid.evolveSpeed = 0.022f;
    o.nebulaMid.colorMix = 0.62f;
    o.nebulaMid.shimmer = 0.30f;

    // Four star strata. Sparse and bright near, dense and faint far -- which is the physical
    // arrangement and also the one that reads as depth.
    o.starsUltra.stratum = {900000.0f, 0.0f, 0.85f, 0.16f};
    o.starsUltra.size = 0.7f;
    o.starsUltra.twinkle = 0.0f;
    o.starsUltra.glint = 0.0f;

    o.starsFar.stratum = {300000.0f, 0.02f, 0.45f, 0.55f};
    o.starsFar.size = 0.85f;
    o.starsFar.twinkle = 0.18f;
    o.starsFar.twinkleSpeed = 0.45f;
    o.starsFar.glint = 0.02f;

    o.starsMid.stratum = {90000.0f, 0.06f, 0.22f, 1.25f};
    o.starsMid.size = 1.0f;
    o.starsMid.twinkle = 0.32f;
    o.starsMid.twinkleSpeed = 0.7f;
    o.starsMid.glint = 0.05f;

    o.starsNear.stratum = {30000.0f, 0.14f, 0.08f, 2.60f};
    o.starsNear.size = 1.35f;
    o.starsNear.twinkle = 0.45f;
    o.starsNear.twinkleSpeed = 0.9f;
    o.starsNear.glint = 0.11f;

    // "Several distant planets" -- sparse on purpose. Density is the fraction of cells occupied, so
    // 0.1 over a 3x3 search is a handful in any given view rather than a sky full of marbles.
    o.planets.stratum = {18000.0f, 0.18f, 0.10f, 0.45f};
    o.planets.scale = 1.0f;
    o.planets.scaleVariance = 0.75f;
    o.planets.clustering = 0.3f;
    o.planets.atmosphereGlow = 0.55f;
    o.planets.terminator = 0.20f;
    o.planets.nightSide = 0.05f;
    o.planets.cloudBands = 0.5f;
    o.planets.rings = 0.28f;
    o.planets.rotationSpeed = 0.006f;
    o.planets.driftSpeed = 0.0025f;

    // Dust: near, strongly parallaxed, and dim enough to be felt rather than seen.
    o.dust.stratum = {900.0f, 0.80f, 0.30f, 0.20f};
    o.dust.size = 0.9f;
    o.dust.turbulence = 0.35f;
    o.dust.driftSpeed = 0.7f;

    // "Faint distant galaxies" -- a handful, mostly smudges.
    o.galaxies.stratum = {600000.0f, 0.0f, 0.05f, 0.30f};
    o.galaxies.scale = 1.0f;
    o.galaxies.spiral = 0.6f;
    o.galaxies.coreBrightness = 2.0f;

    o.atmosphere.haze = 0.22f;
    o.atmosphere.density = 0.45f;
    o.atmosphere.glow = 0.45f;

    o.flow.strength = 0.25f;
    o.flow.curl = 0.5f;

    o.events.shootingStars = 0.5f;
    o.events.comets = 0.05f;
    o.events.flares = 0.15f;
    o.events.pulses = 0.25f;
    o.events.intensity = 0.8f;

    return o;
}

// ---- styles (§14) --------------------------------------------------------------------------------

namespace {

constexpr std::string_view kStyles[] = {
    "Blue Cosmic Ocean", "Emerald Cosmic Ocean", "Magenta Nebula Ocean",
    "Golden Cosmic Ocean", "Alien Dream", "Deep Void",
};

// A palette is four colours and two numbers. Everything else a style could touch is composition,
// and the brief is explicit that these must *emerge from actual controls* rather than being presets
// with hidden state -- so a style writes exactly the fields an artist could have written by hand.
struct Palette {
    glm::vec3 deep, primary, secondary, accent, nebula, atmos;
    float blend, saturation;
};

[[nodiscard]] Palette paletteFor(std::string_view style) {
    if (style == "Emerald Cosmic Ocean") {
        return {{0.004f, 0.016f, 0.017f}, {0.030f, 0.290f, 0.250f}, {0.050f, 0.470f, 0.300f},
                {0.080f, 0.520f, 0.560f}, {0.20f, 0.80f, 0.62f},    {0.05f, 0.26f, 0.26f},
                0.45f, 1.0f};
    }
    if (style == "Magenta Nebula Ocean") {
        return {{0.018f, 0.005f, 0.026f}, {0.330f, 0.060f, 0.420f}, {0.620f, 0.090f, 0.420f},
                {0.720f, 0.240f, 0.520f}, {0.85f, 0.32f, 0.78f},    {0.30f, 0.09f, 0.36f},
                0.55f, 1.05f};
    }
    if (style == "Golden Cosmic Ocean") {
        return {{0.010f, 0.008f, 0.028f}, {0.090f, 0.070f, 0.360f}, {0.330f, 0.140f, 0.520f},
                {0.640f, 0.380f, 0.120f}, {0.78f, 0.52f, 0.28f},    {0.22f, 0.15f, 0.30f},
                0.50f, 1.0f};
    }
    if (style == "Alien Dream") {
        return {{0.004f, 0.020f, 0.022f}, {0.040f, 0.420f, 0.420f}, {0.340f, 0.100f, 0.560f},
                {0.380f, 0.640f, 0.120f}, {0.40f, 0.90f, 0.45f},    {0.10f, 0.30f, 0.34f},
                0.50f, 1.25f};
    }
    if (style == "Deep Void") {
        // The one that is mostly absence. Proof the same controls reach the other end of §40's
        // range: a dark deep-space void is this system with its amplitudes down, not a second mode.
        return {{0.0018f, 0.0022f, 0.0060f}, {0.020f, 0.040f, 0.120f}, {0.060f, 0.030f, 0.150f},
                {0.030f, 0.120f, 0.160f},    {0.12f, 0.18f, 0.40f},    {0.03f, 0.06f, 0.14f},
                0.40f, 0.85f};
    }
    // "Blue Cosmic Ocean", and the fallback: deep navy -> cyan -> violet.
    return {{0.0055f, 0.0075f, 0.0210f}, {0.055f, 0.145f, 0.430f}, {0.230f, 0.085f, 0.520f},
            {0.070f, 0.430f, 0.520f},    {0.28f, 0.44f, 1.00f},    {0.10f, 0.20f, 0.44f},
            0.42f, 1.0f};
}

} // namespace

std::span<const std::string_view> cosmicOceanStyleNames() { return kStyles; }

bool applyCosmicOceanStyle(CosmicOcean& o, std::string_view style) {
    const auto* found = std::find(std::begin(kStyles), std::end(kStyles), style);
    if (found == std::end(kStyles)) {
        return false;
    }
    const Palette p = paletteFor(style);
    o.color.deepSpace = p.deep;
    o.color.primary = p.primary;
    o.color.secondary = p.secondary;
    o.color.accent = p.accent;
    o.color.nebulaTint = p.nebula;
    o.color.atmosphereTint = p.atmos;
    o.color.blend = p.blend;
    o.color.saturation = p.saturation;
    // A style leaves `center`, every depth, every parallax and every density alone, for the reason
    // ADR-387 gives for the vortex's centre: where the ocean is and how it is arranged is a
    // composition the scene made, and a preset that moved it would silently unanchor the picture.
    // "Alien Dream" is the one exception the brief names -- it is a *look*, and the look includes
    // more nebula -- so it says so rather than hiding it in the palette.
    if (style == "Alien Dream") {
        o.nebulaMid.stratum.density = 0.52f;
        o.nebulaMid.shimmer = 0.55f;
        o.color.hueDrift = 0.14f;
        o.color.mediumSpeed = 0.055f;
    }
    if (style == "Deep Void") {
        o.nebulaFar.stratum.brightness = 0.022f;
        o.nebulaMid.stratum.brightness = 0.030f;
        o.galaxies.stratum.density = 0.02f;
    }
    return true;
}

// ---- packing -------------------------------------------------------------------------------------

CosmicOceanGpu packCosmicOcean(const CosmicOcean& ocean, float envelope, double seconds,
                               CosmicQualityScale quality) {
    CosmicOcean o = ocean;
    sanitiseCosmicOcean(o);

    CosmicOceanGpu g{};
    const float live = std::max(envelope, 0.0f) * std::max(o.intensity, 0.0f) * (o.enabled ? 1.0f : 0.0f);
    const float scale = std::max(o.globalScale, 1.0e-3f);
    // `flow.speed` is the global time multiplier §16 asks for, so it multiplies every phase below
    // and nothing else. Applied here rather than in the shader so that "slow everything down" is
    // one number on the CPU and not thirteen branches on the GPU.
    const double t = seconds * static_cast<double>(std::max(o.flow.speed, 0.0f));

    g.master = glm::vec4(live, std::max(o.brightness, 0.0f), std::max(o.contrast, 0.0f),
                         std::max(o.saturation, 0.0f));
    g.master2 = glm::vec4(std::exp2(o.exposure), scale, o.seed, std::max(o.color.saturation, 0.0f));

    const glm::vec3 maskDir = skyDirection(o.mask.azimuth, o.mask.elevation);
    g.center = glm::vec4(o.center, std::clamp(o.mask.amount, 0.0f, 1.0f));
    g.mask = glm::vec4(maskDir, std::cos(std::clamp(o.mask.innerAngle, 0.0f, 180.0f) * kDegToRad));
    g.mask2 = glm::vec4(std::cos(std::clamp(o.mask.outerAngle, 0.0f, 180.0f) * kDegToRad),
                        std::clamp(o.mask.suppression, 0.0f, 1.0f),
                        std::clamp(o.mask.horizonBias, -1.0f, 1.0f), 0.0f);

    g.colorDeep = glm::vec4(o.color.deepSpace, phase(t, o.color.slowSpeed) * o.color.hueDrift);
    g.colorPrimary = glm::vec4(o.color.primary, std::clamp(o.color.blend, 0.0f, 1.0f));
    g.colorSecondary = glm::vec4(o.color.secondary, phase(t, o.color.mediumSpeed));
    g.colorAccent = glm::vec4(o.color.accent, phase(t, o.color.fastSpeed));
    g.colorAtmos = glm::vec4(o.color.atmosphereTint, std::clamp(o.color.fastAmount, 0.0f, 1.0f));

    const float octaveCeiling = std::clamp(o.quality.nebulaOctaves * std::max(quality.octaveScale, 0.05f),
                                           1.0f, 6.0f);
    const auto packNebula = [&](const CosmicNebula& n, glm::vec4& a, glm::vec4& b, glm::vec4& c,
                                glm::vec4& d) {
        a = glm::vec4(n.stratum.depth * scale, n.stratum.parallax, n.stratum.density, n.scale);
        b = glm::vec4(std::min(n.detail, octaveCeiling), n.turbulence, n.warp, n.stratum.brightness);
        c = glm::vec4(phase(t, n.flowSpeed), std::clamp(n.softness, 0.0f, 1.0f), std::max(n.contrast, 0.0f),
                      std::clamp(n.colorMix, 0.0f, 1.0f));
        // z: the octave count of the DOMAIN WARP, which is not the same lever as the field's own
        // and is the cheaper one. The warp is three separate fBM calls -- one per component of the
        // displacement vector -- so an octave here costs three noise samples against the field's
        // one, and the warp is a low-frequency displacement by construction: its second octave
        // moves the sample point by a fraction of a cell. Scaled by the tier's `octaveScale` and
        // floored at one, so Preview warps with three samples where Offline warps with nine.
        // w: whether the main draw should SAMPLE the nebulae rather than evaluate them. It is the
        // renderer's quality scale and not an authored field, which is why it arrives through
        // `CosmicQualityScale` -- an artist has no business knowing which buffer their nebula was
        // rasterised into, and a parameter they could set would be a rendering decision wearing a
        // control's clothes.
        d = glm::vec4(phase(t, n.evolveSpeed), std::max(n.shimmer, 0.0f),
                      std::clamp(std::round(2.0f * std::max(quality.octaveScale, 0.05f)), 1.0f, 3.0f),
                      quality.nebulaScale < 0.999f ? 1.0f : 0.0f);
    };
    packNebula(o.nebulaFar, g.nebFar0, g.nebFar1, g.nebFar2, g.nebFar3);
    packNebula(o.nebulaMid, g.nebMid0, g.nebMid1, g.nebMid2, g.nebMid3);
    g.nebulaTint = glm::vec4(o.color.nebulaTint, 0.0f);

    const auto packStars = [&](const CosmicStars& s, glm::vec4& lane) {
        lane = glm::vec4(s.stratum.depth * scale, s.stratum.parallax, s.stratum.density,
                         s.stratum.brightness);
    };
    packStars(o.starsUltra, g.starUltra);
    packStars(o.starsFar, g.starFar);
    packStars(o.starsMid, g.starMid);
    packStars(o.starsNear, g.starNear);
    // The four strata share a shape and a twinkle, because §7's point is that they differ in
    // *numbers*; four copies of "how big is a star" is four chances for them to disagree for no
    // artistic reason. The per-stratum twinkle depth still varies -- it rides in the density lane's
    // neighbour below via `starsNear.twinkle`, which is the loudest of the four and the one an
    // artist actually adjusts.
    g.starShape = glm::vec4(o.starsNear.size, std::clamp(o.starsNear.sizeVariance, 0.0f, 1.0f),
                            std::clamp(o.starsNear.colorVariation, 0.0f, 1.0f),
                            angularPhase(t, o.starsNear.drift));
    g.starTwinkle = glm::vec4(std::max(o.starsNear.twinkle, 0.0f), phase(t, o.starsNear.twinkleSpeed),
                              std::clamp(o.starsNear.glint, 0.0f, 1.0f), o.quality.starStrata);
    g.starTint = glm::vec4(o.color.starTint, 0.0f);

    const float cells = std::clamp(std::round(o.quality.planetCells * std::max(quality.sampleScale, 0.2f)),
                                   1.0f, 5.0f);
    g.planet0 = glm::vec4(o.planets.stratum.depth * scale, o.planets.stratum.parallax,
                          o.planets.stratum.density, o.planets.stratum.brightness);
    g.planet1 = glm::vec4(o.planets.scale, std::clamp(o.planets.scaleVariance, 0.0f, 1.0f),
                          std::clamp(o.planets.clustering, 0.0f, 1.0f),
                          std::clamp(o.planets.colorVariation, 0.0f, 1.0f));
    g.planet2 = glm::vec4(std::max(o.planets.atmosphereGlow, 0.0f), std::max(o.planets.terminator, 1.0e-3f),
                          std::max(o.planets.nightSide, 0.0f), std::clamp(o.planets.cloudBands, 0.0f, 1.0f));
    g.planet3 = glm::vec4(std::clamp(o.planets.rings, 0.0f, 1.0f), o.planets.ringSize,
                          std::max(o.planets.ringBrightness, 0.0f), cells);
    const glm::vec3 drift = skyDirection(o.planets.driftDirection, 0.0f);
    g.planet4 = glm::vec4(angularPhase(t, o.planets.rotationSpeed), angularPhase(t, o.planets.driftSpeed),
                          drift.x, drift.z);
    g.planetTint = glm::vec4(o.color.planetTint, 0.0f);

    const float dustCells = std::clamp(std::round(o.quality.dustCells * std::max(quality.sampleScale, 0.2f)),
                                       0.0f, 5.0f);
    g.dust0 = glm::vec4(o.dust.stratum.depth * scale, o.dust.stratum.parallax, o.dust.stratum.density,
                        o.dust.stratum.brightness);
    g.dust1 = glm::vec4(o.dust.size, std::clamp(o.dust.sizeVariance, 0.0f, 1.0f),
                        std::max(o.dust.turbulence, 0.0f),
                        // Dust drifts in metres per second through a lattice whose cell is a known
                        // size, so its phase is a *distance* wrapped by the cell, not a turn.
                        phase(t, o.dust.driftSpeed / std::max(o.dust.stratum.depth * scale, 1.0f)));
    g.dust2 = glm::vec4(o.dust.fadeDistance, dustCells, 0.0f, 0.0f);

    g.galaxy0 = glm::vec4(o.galaxies.stratum.depth * scale, o.galaxies.stratum.parallax,
                          o.galaxies.stratum.density, o.galaxies.stratum.brightness);
    g.galaxy1 = glm::vec4(o.galaxies.scale, std::clamp(o.galaxies.spiral, 0.0f, 1.0f),
                          std::max(o.galaxies.coreBrightness, 0.0f),
                          std::clamp(o.galaxies.inclination, 0.0f, 1.0f));
    g.galaxy2 = glm::vec4(angularPhase(t, o.galaxies.rotationSpeed), 0.0f, 0.0f, 0.0f);

    g.atmos0 = glm::vec4(std::max(o.atmosphere.haze, 0.0f), std::max(o.atmosphere.density, 0.0f),
                         std::clamp(o.atmosphere.brightnessFalloff, 0.0f, 1.0f),
                         std::clamp(o.atmosphere.saturationFalloff, 0.0f, 1.0f));
    g.atmos1 = glm::vec4(std::clamp(o.atmosphere.contrastFalloff, 0.0f, 1.0f),
                         std::clamp(o.atmosphere.scattering, 0.0f, 1.0f), std::max(o.atmosphere.glow, 0.0f),
                         0.0f);

    g.flow0 = glm::vec4(skyDirection(o.flow.directionAzimuth, o.flow.directionElevation),
                        std::max(o.flow.strength, 0.0f));
    g.flow1 = glm::vec4(std::max(o.flow.turbulence, 0.0f), std::clamp(o.flow.curl, 0.0f, 1.0f),
                        std::max(o.flow.scale, 1.0e-3f), phase(t, o.flow.evolveSpeed));

    g.event0 = glm::vec4(eventPeriod(o.events.shootingStars), eventPeriod(o.events.comets),
                         eventPeriod(o.events.flares), eventPeriod(o.events.pulses));
    g.event1 = glm::vec4(std::clamp(o.events.probability, 0.0f, 1.0f), std::max(o.events.intensity, 0.0f),
                         std::max(o.events.size, 0.0f), std::max(o.events.speed, 0.0f));
    // The events' clock is the one place a raw second reaches the shader, and it has to: an event's
    // identity is `floor(t / period)`, which a wrapped phase cannot express. It is packed as a
    // float, so past about 2^24 seconds -- 194 days of continuous timeline -- the slot index stops
    // advancing. That is not a limit worth engineering around, and it is written down so that
    // whoever finds it knows it was seen.
    g.event2 = glm::vec4(o.events.lifetime, static_cast<float>(t), 0.0f, 0.0f);
    g.eventColor = glm::vec4(o.events.color, 0.0f);

    return g;
}

// ---- the parameter tables ----------------------------------------------------------------------

namespace {

#define CO_GET(expr) +[](const CosmicOcean& o) { return (expr); }
#define CO_SET(lhs) +[](CosmicOcean& o, float v) { (lhs) = v; }
#define CO_CSET(lhs) +[](CosmicOcean& o, glm::vec3 v) { (lhs) = v; }
#define CO_BSET(lhs) +[](CosmicOcean& o, bool v) { (lhs) = v; }

// Depth, parallax, density and brightness for one stratum. A macro rather than nine copies,
// because the nine are genuinely the same four questions and the copies are where they drift.
// Depth's hard ceiling is five million metres: these shells are conceptually at infinity and the
// only thing that constrains them is float precision in the intersection.
#define CO_STRATUM(name, member, depthSoftLo, depthSoftHi, brightSoftHi)                            \
    {name "Depth", 1.0f, 5.0e6f, depthSoftLo, depthSoftHi, CO_GET(o.member.depth),                  \
     CO_SET(o.member.depth)},                                                                       \
        {name "Parallax", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.member.parallax),                        \
         CO_SET(o.member.parallax)},                                                                \
        {name "Density", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.member.density), CO_SET(o.member.density)},\
        {name "Brightness", 0.0f, 40.0f, 0.0f, brightSoftHi, CO_GET(o.member.brightness),           \
         CO_SET(o.member.brightness)}

constexpr CosmicFloatField kFloats[] = {
    // ---- master ----
    {"intensity", 0.0f, 8.0f, 0.0f, 2.0f, CO_GET(o.intensity), CO_SET(o.intensity)},
    {"brightness", 0.0f, 8.0f, 0.0f, 3.0f, CO_GET(o.brightness), CO_SET(o.brightness)},
    {"contrast", 0.0f, 4.0f, 0.2f, 2.0f, CO_GET(o.contrast), CO_SET(o.contrast)},
    {"saturation", 0.0f, 4.0f, 0.0f, 2.0f, CO_GET(o.saturation), CO_SET(o.saturation)},
    {"exposure", -8.0f, 8.0f, -3.0f, 3.0f, CO_GET(o.exposure), CO_SET(o.exposure)},
    {"globalScale", 0.01f, 20.0f, 0.25f, 4.0f, CO_GET(o.globalScale), CO_SET(o.globalScale)},
    {"seed", 0.0f, 100000.0f, 0.0f, 999.0f, CO_GET(o.seed), CO_SET(o.seed)},
    {"centerX", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f, CO_GET(o.center.x), CO_SET(o.center.x)},
    {"centerY", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f, CO_GET(o.center.y), CO_SET(o.center.y)},
    {"centerZ", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f, CO_GET(o.center.z), CO_SET(o.center.z)},

    // ---- far nebula ----
    CO_STRATUM("nebulaFar", nebulaFar.stratum, 10000.0f, 1.0e6f, 1.0f),
    {"nebulaFarScale", 0.01f, 40.0f, 0.2f, 8.0f, CO_GET(o.nebulaFar.scale), CO_SET(o.nebulaFar.scale)},
    {"nebulaFarDetail", 1.0f, 6.0f, 1.0f, 6.0f, CO_GET(o.nebulaFar.detail), CO_SET(o.nebulaFar.detail)},
    {"nebulaFarTurbulence", 0.0f, 3.0f, 0.0f, 1.0f, CO_GET(o.nebulaFar.turbulence),
     CO_SET(o.nebulaFar.turbulence)},
    {"nebulaFarWarp", 0.0f, 4.0f, 0.0f, 1.5f, CO_GET(o.nebulaFar.warp), CO_SET(o.nebulaFar.warp)},
    {"nebulaFarFlow", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.nebulaFar.flowSpeed),
     CO_SET(o.nebulaFar.flowSpeed)},
    {"nebulaFarEvolve", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.nebulaFar.evolveSpeed),
     CO_SET(o.nebulaFar.evolveSpeed)},
    {"nebulaFarSoftness", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.nebulaFar.softness),
     CO_SET(o.nebulaFar.softness)},
    {"nebulaFarContrast", 0.05f, 8.0f, 0.5f, 4.0f, CO_GET(o.nebulaFar.contrast),
     CO_SET(o.nebulaFar.contrast)},
    {"nebulaFarColorMix", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.nebulaFar.colorMix),
     CO_SET(o.nebulaFar.colorMix)},
    {"nebulaFarShimmer", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.nebulaFar.shimmer),
     CO_SET(o.nebulaFar.shimmer)},

    // ---- mid nebula ----
    CO_STRATUM("nebulaMid", nebulaMid.stratum, 5000.0f, 5.0e5f, 1.0f),
    {"nebulaMidScale", 0.01f, 40.0f, 0.2f, 8.0f, CO_GET(o.nebulaMid.scale), CO_SET(o.nebulaMid.scale)},
    {"nebulaMidDetail", 1.0f, 6.0f, 1.0f, 6.0f, CO_GET(o.nebulaMid.detail), CO_SET(o.nebulaMid.detail)},
    {"nebulaMidTurbulence", 0.0f, 3.0f, 0.0f, 1.0f, CO_GET(o.nebulaMid.turbulence),
     CO_SET(o.nebulaMid.turbulence)},
    {"nebulaMidWarp", 0.0f, 4.0f, 0.0f, 1.5f, CO_GET(o.nebulaMid.warp), CO_SET(o.nebulaMid.warp)},
    {"nebulaMidFlow", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.nebulaMid.flowSpeed),
     CO_SET(o.nebulaMid.flowSpeed)},
    {"nebulaMidEvolve", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.nebulaMid.evolveSpeed),
     CO_SET(o.nebulaMid.evolveSpeed)},
    {"nebulaMidSoftness", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.nebulaMid.softness),
     CO_SET(o.nebulaMid.softness)},
    {"nebulaMidContrast", 0.05f, 8.0f, 0.5f, 4.0f, CO_GET(o.nebulaMid.contrast),
     CO_SET(o.nebulaMid.contrast)},
    {"nebulaMidColorMix", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.nebulaMid.colorMix),
     CO_SET(o.nebulaMid.colorMix)},
    {"nebulaMidShimmer", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.nebulaMid.shimmer),
     CO_SET(o.nebulaMid.shimmer)},

    // ---- stars ----
    CO_STRATUM("starsUltra", starsUltra.stratum, 50000.0f, 2.0e6f, 2.0f),
    CO_STRATUM("starsFar", starsFar.stratum, 20000.0f, 1.0e6f, 4.0f),
    CO_STRATUM("starsMid", starsMid.stratum, 5000.0f, 5.0e5f, 6.0f),
    CO_STRATUM("starsNear", starsNear.stratum, 1000.0f, 2.0e5f, 10.0f),
    {"starSize", 0.0f, 16.0f, 0.0f, 4.0f, CO_GET(o.starsNear.size), CO_SET(o.starsNear.size)},
    {"starSizeVariation", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.starsNear.sizeVariance),
     CO_SET(o.starsNear.sizeVariance)},
    {"starTwinkle", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.starsNear.twinkle), CO_SET(o.starsNear.twinkle)},
    {"starTwinkleSpeed", 0.0f, 12.0f, 0.0f, 3.0f, CO_GET(o.starsNear.twinkleSpeed),
     CO_SET(o.starsNear.twinkleSpeed)},
    {"starColorVariation", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.starsNear.colorVariation),
     CO_SET(o.starsNear.colorVariation)},
    {"starDrift", -1.0f, 1.0f, -0.05f, 0.05f, CO_GET(o.starsNear.drift), CO_SET(o.starsNear.drift)},
    {"starGlint", 0.0f, 1.0f, 0.0f, 0.5f, CO_GET(o.starsNear.glint), CO_SET(o.starsNear.glint)},

    // ---- planets ----
    CO_STRATUM("planets", planets.stratum, 2000.0f, 2.0e5f, 4.0f),
    {"planetScale", 0.0f, 20.0f, 0.1f, 5.0f, CO_GET(o.planets.scale), CO_SET(o.planets.scale)},
    {"planetScaleVariation", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.planets.scaleVariance),
     CO_SET(o.planets.scaleVariance)},
    {"planetClustering", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.planets.clustering),
     CO_SET(o.planets.clustering)},
    {"planetGlow", 0.0f, 8.0f, 0.0f, 2.0f, CO_GET(o.planets.atmosphereGlow),
     CO_SET(o.planets.atmosphereGlow)},
    {"planetTerminator", 0.001f, 1.0f, 0.02f, 0.6f, CO_GET(o.planets.terminator),
     CO_SET(o.planets.terminator)},
    {"planetNightSide", 0.0f, 1.0f, 0.0f, 0.4f, CO_GET(o.planets.nightSide),
     CO_SET(o.planets.nightSide)},
    {"planetCloudBands", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.planets.cloudBands),
     CO_SET(o.planets.cloudBands)},
    {"planetRings", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.planets.rings), CO_SET(o.planets.rings)},
    {"planetRingSize", 1.0f, 6.0f, 1.2f, 3.5f, CO_GET(o.planets.ringSize), CO_SET(o.planets.ringSize)},
    {"planetRingBrightness", 0.0f, 8.0f, 0.0f, 2.0f, CO_GET(o.planets.ringBrightness),
     CO_SET(o.planets.ringBrightness)},
    {"planetColorVariation", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.planets.colorVariation),
     CO_SET(o.planets.colorVariation)},
    {"planetRotation", -2.0f, 2.0f, -0.1f, 0.1f, CO_GET(o.planets.rotationSpeed),
     CO_SET(o.planets.rotationSpeed)},
    {"planetDrift", -2.0f, 2.0f, -0.05f, 0.05f, CO_GET(o.planets.driftSpeed),
     CO_SET(o.planets.driftSpeed)},
    {"planetDriftDirection", -360.0f, 360.0f, 0.0f, 360.0f, CO_GET(o.planets.driftDirection),
     CO_SET(o.planets.driftDirection)},

    // ---- dust ----
    CO_STRATUM("dust", dust.stratum, 50.0f, 20000.0f, 2.0f),
    {"dustSize", 0.0f, 16.0f, 0.0f, 4.0f, CO_GET(o.dust.size), CO_SET(o.dust.size)},
    {"dustSizeVariation", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.dust.sizeVariance),
     CO_SET(o.dust.sizeVariance)},
    {"dustTurbulence", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.dust.turbulence), CO_SET(o.dust.turbulence)},
    {"dustDrift", 0.0f, 200.0f, 0.0f, 8.0f, CO_GET(o.dust.driftSpeed), CO_SET(o.dust.driftSpeed)},
    {"dustFade", 0.001f, 1.0f, 0.05f, 1.0f, CO_GET(o.dust.fadeDistance), CO_SET(o.dust.fadeDistance)},

    // ---- galaxies ----
    CO_STRATUM("galaxies", galaxies.stratum, 50000.0f, 2.0e6f, 3.0f),
    {"galaxyScale", 0.0f, 20.0f, 0.1f, 5.0f, CO_GET(o.galaxies.scale), CO_SET(o.galaxies.scale)},
    {"galaxySpiral", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.galaxies.spiral), CO_SET(o.galaxies.spiral)},
    {"galaxyCore", 0.0f, 20.0f, 0.0f, 6.0f, CO_GET(o.galaxies.coreBrightness),
     CO_SET(o.galaxies.coreBrightness)},
    {"galaxyInclination", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.galaxies.inclination),
     CO_SET(o.galaxies.inclination)},
    {"galaxyRotation", -1.0f, 1.0f, -0.02f, 0.02f, CO_GET(o.galaxies.rotationSpeed),
     CO_SET(o.galaxies.rotationSpeed)},

    // ---- atmosphere ----
    {"haze", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.haze), CO_SET(o.atmosphere.haze)},
    {"atmosphereDensity", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.density),
     CO_SET(o.atmosphere.density)},
    {"depthBrightnessFalloff", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.brightnessFalloff),
     CO_SET(o.atmosphere.brightnessFalloff)},
    {"depthSaturationFalloff", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.saturationFalloff),
     CO_SET(o.atmosphere.saturationFalloff)},
    {"depthContrastFalloff", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.contrastFalloff),
     CO_SET(o.atmosphere.contrastFalloff)},
    {"scattering", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.atmosphere.scattering),
     CO_SET(o.atmosphere.scattering)},
    {"glow", 0.0f, 8.0f, 0.0f, 2.0f, CO_GET(o.atmosphere.glow), CO_SET(o.atmosphere.glow)},

    // ---- colour ----
    {"colorBlend", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.color.blend), CO_SET(o.color.blend)},
    {"hueDrift", 0.0f, 1.0f, 0.0f, 0.5f, CO_GET(o.color.hueDrift), CO_SET(o.color.hueDrift)},
    {"colorSaturation", 0.0f, 4.0f, 0.0f, 2.0f, CO_GET(o.color.saturation), CO_SET(o.color.saturation)},
    {"colorSlowSpeed", 0.0f, 1.0f, 0.0f, 0.05f, CO_GET(o.color.slowSpeed), CO_SET(o.color.slowSpeed)},
    {"colorMediumSpeed", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.color.mediumSpeed),
     CO_SET(o.color.mediumSpeed)},
    {"colorFastSpeed", 0.0f, 20.0f, 0.0f, 4.0f, CO_GET(o.color.fastSpeed), CO_SET(o.color.fastSpeed)},
    {"colorFastAmount", 0.0f, 1.0f, 0.0f, 0.5f, CO_GET(o.color.fastAmount), CO_SET(o.color.fastAmount)},

    // ---- motion ----
    {"globalSpeed", 0.0f, 20.0f, 0.0f, 4.0f, CO_GET(o.flow.speed), CO_SET(o.flow.speed)},
    {"flowAzimuth", -360.0f, 360.0f, 0.0f, 360.0f, CO_GET(o.flow.directionAzimuth),
     CO_SET(o.flow.directionAzimuth)},
    {"flowElevation", -90.0f, 90.0f, -90.0f, 90.0f, CO_GET(o.flow.directionElevation),
     CO_SET(o.flow.directionElevation)},
    {"flowStrength", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.flow.strength), CO_SET(o.flow.strength)},
    {"flowTurbulence", 0.0f, 4.0f, 0.0f, 1.0f, CO_GET(o.flow.turbulence), CO_SET(o.flow.turbulence)},
    {"flowCurl", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.flow.curl), CO_SET(o.flow.curl)},
    {"flowScale", 0.01f, 20.0f, 0.1f, 4.0f, CO_GET(o.flow.scale), CO_SET(o.flow.scale)},
    {"flowEvolve", 0.0f, 2.0f, 0.0f, 0.2f, CO_GET(o.flow.evolveSpeed), CO_SET(o.flow.evolveSpeed)},

    // ---- events ----
    {"shootingStars", 0.0f, 120.0f, 0.0f, 6.0f, CO_GET(o.events.shootingStars),
     CO_SET(o.events.shootingStars)},
    {"eventComets", 0.0f, 60.0f, 0.0f, 2.0f, CO_GET(o.events.comets), CO_SET(o.events.comets)},
    {"eventFlares", 0.0f, 60.0f, 0.0f, 3.0f, CO_GET(o.events.flares), CO_SET(o.events.flares)},
    {"eventPulses", 0.0f, 60.0f, 0.0f, 3.0f, CO_GET(o.events.pulses), CO_SET(o.events.pulses)},
    {"eventProbability", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.events.probability),
     CO_SET(o.events.probability)},
    {"eventIntensity", 0.0f, 16.0f, 0.0f, 4.0f, CO_GET(o.events.intensity), CO_SET(o.events.intensity)},
    {"eventSize", 0.0f, 16.0f, 0.0f, 4.0f, CO_GET(o.events.size), CO_SET(o.events.size)},
    {"eventSpeed", 0.0f, 16.0f, 0.0f, 4.0f, CO_GET(o.events.speed), CO_SET(o.events.speed)},
    {"eventLifetime", 0.01f, 60.0f, 0.2f, 8.0f, CO_GET(o.events.lifetime), CO_SET(o.events.lifetime)},

    // ---- mask ----
    {"maskAmount", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.mask.amount), CO_SET(o.mask.amount)},
    {"maskAzimuth", -360.0f, 360.0f, 0.0f, 360.0f, CO_GET(o.mask.azimuth), CO_SET(o.mask.azimuth)},
    {"maskElevation", -90.0f, 90.0f, -90.0f, 90.0f, CO_GET(o.mask.elevation), CO_SET(o.mask.elevation)},
    {"maskInnerAngle", 0.0f, 180.0f, 0.0f, 90.0f, CO_GET(o.mask.innerAngle), CO_SET(o.mask.innerAngle)},
    {"maskOuterAngle", 0.0f, 180.0f, 0.0f, 120.0f, CO_GET(o.mask.outerAngle), CO_SET(o.mask.outerAngle)},
    {"maskSuppression", 0.0f, 1.0f, 0.0f, 1.0f, CO_GET(o.mask.suppression), CO_SET(o.mask.suppression)},
    {"maskHorizonBias", -1.0f, 1.0f, -1.0f, 1.0f, CO_GET(o.mask.horizonBias),
     CO_SET(o.mask.horizonBias)},

    // ---- quality (§23) ----
    // Sample counts, exposed because a scene may legitimately want a cheaper sky than its tier
    // gives it. Not a visibility switch: §18's rule is that a tier may scale samples and may not
    // remove a control, and these are the samples.
    {"qualityNebulaOctaves", 1.0f, 6.0f, 1.0f, 6.0f, CO_GET(o.quality.nebulaOctaves),
     CO_SET(o.quality.nebulaOctaves)},
    {"qualityStarStrata", 1.0f, 4.0f, 1.0f, 4.0f, CO_GET(o.quality.starStrata),
     CO_SET(o.quality.starStrata)},
    {"qualityPlanetCells", 1.0f, 5.0f, 1.0f, 5.0f, CO_GET(o.quality.planetCells),
     CO_SET(o.quality.planetCells)},
    {"qualityDustCells", 0.0f, 5.0f, 0.0f, 5.0f, CO_GET(o.quality.dustCells),
     CO_SET(o.quality.dustCells)},
};

constexpr CosmicColorField kColors[] = {
    {"colorPrimary", CO_GET(o.color.primary), CO_CSET(o.color.primary)},
    {"colorSecondary", CO_GET(o.color.secondary), CO_CSET(o.color.secondary)},
    {"colorAccent", CO_GET(o.color.accent), CO_CSET(o.color.accent)},
    {"colorDeepSpace", CO_GET(o.color.deepSpace), CO_CSET(o.color.deepSpace)},
    {"colorNebula", CO_GET(o.color.nebulaTint), CO_CSET(o.color.nebulaTint)},
    {"colorPlanet", CO_GET(o.color.planetTint), CO_CSET(o.color.planetTint)},
    {"colorStar", CO_GET(o.color.starTint), CO_CSET(o.color.starTint)},
    {"colorAtmosphere", CO_GET(o.color.atmosphereTint), CO_CSET(o.color.atmosphereTint)},
    {"colorEvent", CO_GET(o.events.color), CO_CSET(o.events.color)},
};

constexpr CosmicBoolField kBools[] = {
    // The ocean's own enable, distinct from the effect instance's. Two, because an artist wants to
    // A/B the sky without losing the instance's lifecycle -- and because §39's acceptance test is
    // literally "then disable Cosmic Ocean".
    {"oceanEnabled", CO_GET(o.enabled), CO_BSET(o.enabled)},
};

#undef CO_STRATUM
#undef CO_GET
#undef CO_SET
#undef CO_CSET
#undef CO_BSET

} // namespace

std::span<const CosmicFloatField> cosmicFloatFields() { return kFloats; }
std::span<const CosmicColorField> cosmicColorFields() { return kColors; }
std::span<const CosmicBoolField> cosmicBoolFields() { return kBools; }

} // namespace avgen::world
