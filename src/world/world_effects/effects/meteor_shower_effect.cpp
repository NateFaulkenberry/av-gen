// The meteor shower (ADR-500's first proof that an effect is one file).
//
// **Read this file as the answer to "what does adding an effect cost now".** Everything below --
// the rows with their ranges and artist labels, which of them the panel shows above the fold, the
// saved file's shape, the clamps, the presets, the default audio routes, and how one authored
// shower becomes six streaks in the sky each frame -- is here. Outside it, this effect cost four
// lines in two files: the `AtmosphereKind::MeteorShower` enumerator, its entry in
// `kAtmosphereKinds`, and a declaration plus a reference in `builtinSchemas()`. Before ADR-500 the
// same effect would have been about twenty edits across six files that every other agent also had
// open, which is the reason seventy effects was unreachable.
//
// **How it reaches the GPU without a line of new shader.** A meteor is a comet: a bright head on a
// great-circle arc with a trail integrated along the view ray, which is exactly what
// `shaders/atmosphere_fx.wgsl` already does. So this kind writes into the comet bucket -- N records
// instead of one -- and the shower's whole contribution is deciding where each of the N goes and
// when. `EffectResolve::count` exists for this case and this is its first user.
//
// That is deliberately the easy half, and the honest boundary of what a registry can buy: a kind
// that needs a *new integrator* needs shader work, and a `.wgsl` file is not a `.cpp` file. What
// the registry removes is everything either side of the pixels.
//
// **Where its numbers live.** The trail's appearance is `e.comet` -- the same struct the Comet kind
// uses, aliased on purpose so the packer needs no change and the file format gains no second copy
// of thirteen colours (the rows below say so with an absolute `/comet/...` JSON path). The
// shower's own six numbers have no struct on the shared header, and adding one would be exactly the
// edit ADR-500 exists to remove, so they live in `AtmosphericEffect::values` under `meteors/`. Both
// kinds of row are declared the same way here and nothing downstream knows which it got.

#include "world/world_effects/effect_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <glm/geometric.hpp>

namespace avgen::world {
namespace {

using E = AtmosphericEffect;

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }
#define SETB(lhs) +[](E& e, bool v) { (lhs) = v; }

// The shader's per-frame comet array is the ceiling, and it is not raised here. ADR-388 measured
// that even moving the vortex uniforms into a function parameter moved 62 pixels of the shipped
// frame; growing a uniform array that every existing scene also declares is the same class of risk
// for a shower nobody has authored yet. Six meteors at once is what a sky holds before they stop
// reading as events anyway (ADR-230's note beside `kMaxGpuComets`).
constexpr float kMaxMeteors = static_cast<float>(kMaxGpuComets);

constexpr EffectField kFields[] = {
    // ---- the shower's own numbers, in the store.
    storedFloat("meteors", "Meteors", 5.0f, 1.0f, kMaxMeteors, 1.0f, kMaxMeteors)
        .fmt("%.0f").main().clampTo(1.0f, kMaxMeteors)
        .tooltip("How many streaks are in the air at once. Capped at six, which is the\n"
                 "shader's comet array and also about what a sky holds before they stop\n"
                 "reading as separate events."),
    storedFloat("spread", "Spread", 42.0f, 0.0f, 360.0f, 0.0f, 180.0f).fmt("%.0f deg").main()
        .tooltip("How far across the sky the radiant throws them. 0 is every meteor on the\n"
                 "same track, which reads as one bright object seen several times."),
    storedFloat("stagger", "Stagger", 0.55f, 0.0f, 30.0f, 0.0f, 4.0f).fmt("%.2f s").main()
        .tooltip("The gap between one meteor's launch and the next. 0 fires them together,\n"
                 "which is a burst; a second apart is a shower."),
    storedFloat("sizeVariation", "Size variation", 0.45f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How unequal the streaks are. 0 makes six identical meteors, which is the\n"
                 "one thing a real shower never looks like."),
    storedFloat("radiantDrift", "Radiant drift", 8.0f, -90.0f, 90.0f, -30.0f, 30.0f).fmt("%.0f deg")
        .tooltip("How far the whole radiant walks across the sky over one pass, so a long\n"
                 "shower is not six passes through the same two points."),
    // Not a slider anybody drags for a look: it is the one control that says "give me a different
    // arrangement of the same shower", which is what a seed is for.
    storedFloat("seed", "Arrangement", 1.0f, 1.0f, 9999.0f, 1.0f, 64.0f).fmt("%.0f")
        .tooltip("Re-rolls which meteor goes where. Purely a re-arrangement: every value in\n"
                 "this panel means the same thing whatever it is set to."),

    // ---- the streak itself, shared with the Comet kind.
    //
    // An absolute JSON path (leading `/`) puts these in the file's `comet` block rather than in
    // this kind's own, because they ARE the comet block: the same struct, so the same key. A
    // relative path would write a second copy of thirteen numbers that had to agree with the first.
    colorField("coreColor", "Head colour", GET(e.comet.appearance.coreColor),
               SETC(e.comet.appearance.coreColor)).json("/comet/appearance/coreColor").main(),
    colorField("tailColor", "Streak colour", GET(e.comet.appearance.tailColor),
               SETC(e.comet.appearance.tailColor)).json("/comet/appearance/tailColor").main(),
    floatField("coreIntensity", "Head brightness", 0.0f, 200.0f, 0.0f, 40.0f,
               GET(e.comet.appearance.coreIntensity), SETF(e.comet.appearance.coreIntensity))
        .json("/comet/appearance/coreIntensity").main(),
    floatField("tailLength", "Streak length", 10.0f, 6000.0f, 100.0f, 2000.0f,
               GET(e.comet.appearance.tailLength), SETF(e.comet.appearance.tailLength))
        .json("/comet/appearance/tailLength").fmt("%.0f m").main().floorAt(1.0f),
    floatField("travelSeconds", "Crossing", 0.05f, 600.0f, 0.3f, 8.0f, GET(e.comet.path.travelSeconds),
               SETF(e.comet.path.travelSeconds)).json("/comet/path/travelSeconds").fmt("%.2f s").main()
        .floorAt(0.05f)
        .tooltip("How long one meteor takes to cross. A shower's meteors are fast: under a\n"
                 "second reads as a streak, several seconds reads as a comet."),

    // ---- Advanced: where the radiant is and what the streaks look like.
    floatField("startAzimuth", "Radiant bearing", -720.0f, 720.0f, -180.0f, 180.0f,
               GET(e.comet.path.startAzimuth), SETF(e.comet.path.startAzimuth))
        .json("/comet/path/startAzimuth").fmt("%.0f deg").sec("Radiant")
        .tooltip("Where the shower comes FROM. Every meteor's track starts within Spread of\n"
                 "this bearing, which is what makes a shower read as having a source."),
    floatField("startElevation", "Radiant height", -20.0f, 89.0f, 0.0f, 80.0f,
               GET(e.comet.path.startElevation), SETF(e.comet.path.startElevation))
        .json("/comet/path/startElevation").fmt("%.0f deg"),
    floatField("endAzimuth", "Fall bearing", -720.0f, 720.0f, -180.0f, 180.0f,
               GET(e.comet.path.endAzimuth), SETF(e.comet.path.endAzimuth))
        .json("/comet/path/endAzimuth").fmt("%.0f deg"),
    floatField("endElevation", "Fall height", -20.0f, 89.0f, 0.0f, 80.0f,
               GET(e.comet.path.endElevation), SETF(e.comet.path.endElevation))
        .json("/comet/path/endElevation").fmt("%.0f deg"),
    floatField("distance", "Distance", 50.0f, 40000.0f, 400.0f, 8000.0f, GET(e.comet.path.distance),
               SETF(e.comet.path.distance)).json("/comet/path/distance").fmt("%.0f m").log().floorAt(1.0f),
    floatField("acceleration", "Acceleration", -0.45f, 8.0f, -0.4f, 3.0f,
               GET(e.comet.path.acceleration), SETF(e.comet.path.acceleration))
        .json("/comet/path/acceleration"),
    floatField("arcLift", "Arc lift", -4000.0f, 4000.0f, -600.0f, 600.0f, GET(e.comet.path.arcLift),
               SETF(e.comet.path.arcLift)).json("/comet/path/arcLift").fmt("%.0f m"),

    floatField("headSize", "Head size", 0.5f, 400.0f, 1.0f, 40.0f, GET(e.comet.appearance.headSize),
               SETF(e.comet.appearance.headSize)).json("/comet/appearance/headSize").fmt("%.0f m")
        .sec("Streak").floorAt(0.1f),
    floatField("tailWidth", "Streak width", 1.0f, 600.0f, 2.0f, 80.0f, GET(e.comet.appearance.tailWidth),
               SETF(e.comet.appearance.tailWidth)).json("/comet/appearance/tailWidth").fmt("%.0f m")
        .floorAt(0.5f),
    floatField("tailIntensity", "Streak brightness", 0.0f, 80.0f, 0.0f, 15.0f,
               GET(e.comet.appearance.tailIntensity), SETF(e.comet.appearance.tailIntensity))
        .json("/comet/appearance/tailIntensity"),
    floatField("tailFalloff", "Streak falloff", 0.05f, 8.0f, 0.5f, 4.0f,
               GET(e.comet.appearance.tailFalloff), SETF(e.comet.appearance.tailFalloff))
        .json("/comet/appearance/tailFalloff").floorAt(0.05f),
    colorField("haloColor", "Halo colour", GET(e.comet.appearance.haloColor),
               SETC(e.comet.appearance.haloColor)).json("/comet/appearance/haloColor"),
    floatField("haloIntensity", "Halo brightness", 0.0f, 60.0f, 0.0f, 10.0f,
               GET(e.comet.appearance.haloIntensity), SETF(e.comet.appearance.haloIntensity))
        .json("/comet/appearance/haloIntensity"),
    floatField("haloSize", "Halo size", 1.0f, 2000.0f, 5.0f, 200.0f, GET(e.comet.appearance.haloSize),
               SETF(e.comet.appearance.haloSize)).json("/comet/appearance/haloSize").fmt("%.0f m")
        .floorAt(1.0f),
    floatField("wispAmount", "Wisp amount", 0.0f, 400.0f, 0.0f, 150.0f,
               GET(e.comet.appearance.wispAmount), SETF(e.comet.appearance.wispAmount))
        .json("/comet/appearance/wispAmount").fmt("%.0f m"),
    floatField("wispScale", "Wisp scale", 0.0f, 0.05f, 0.0f, 0.01f, GET(e.comet.appearance.wispScale),
               SETF(e.comet.appearance.wispScale)).json("/comet/appearance/wispScale").fmt("%.4f"),
    floatField("flowSpeed", "Wisp flow", -4.0f, 4.0f, -1.0f, 1.0f, GET(e.comet.appearance.flowSpeed),
               SETF(e.comet.appearance.flowSpeed)).json("/comet/appearance/flowSpeed"),

    boolField("sparkle", "Sparkling fragments", GET(e.comet.sparkle.enabled),
              SETB(e.comet.sparkle.enabled)).json("/comet/sparkle/enabled").sec("Fragments"),
    floatField("sparkleDensity", "Density", 0.0f, 0.2f, 0.0f, 0.05f, GET(e.comet.sparkle.density),
               SETF(e.comet.sparkle.density)).json("/comet/sparkle/density").fmt("%.3f /m"),
    floatField("sparkleSize", "Size", 0.0f, 1.0f, 0.02f, 0.8f, GET(e.comet.sparkle.size),
               SETF(e.comet.sparkle.size)).json("/comet/sparkle/size"),
    floatField("sparkleIntensity", "Brightness", 0.0f, 80.0f, 0.0f, 20.0f,
               GET(e.comet.sparkle.intensity), SETF(e.comet.sparkle.intensity))
        .json("/comet/sparkle/intensity"),
    floatField("sparkleSpeed", "Twinkle", 0.0f, 12.0f, 0.0f, 4.0f, GET(e.comet.sparkle.speed),
               SETF(e.comet.sparkle.speed)).json("/comet/sparkle/speed"),
};

#undef GET
#undef SETF
#undef SETC
#undef SETB

// ---- the arrangement ------------------------------------------------------------------------------
//
// Every per-meteor number is a pure function of (seed, index) and of the transport second, with no
// counter and no previous frame -- ADR-360's contract, and the same rule ADR-230 states for the
// comet: an offline render of second N is byte-identical to a realtime playthrough of second N.
//
// A hash rather than a PRNG for exactly that reason: a stream would make meteor 4 depend on how
// many times meteor 3 was asked, which is a different answer after a scrub.
std::uint32_t hash(std::uint32_t a) {
    a ^= a >> 16;
    a *= 0x7feb352dU;
    a ^= a >> 15;
    a *= 0x846ca68bU;
    a ^= a >> 16;
    return a;
}

// 0..1 from (seed, index, lane). `lane` keeps the five questions asked of one meteor independent:
// without it, a meteor that is early is also, always, the dim one.
float unitHash(float seed, std::size_t index, std::uint32_t lane) {
    const std::uint32_t s = static_cast<std::uint32_t>(std::max(seed, 0.0f));
    const std::uint32_t h = hash(hash(s * 747796405U + static_cast<std::uint32_t>(index) * 2891336453U) +
                                 lane * 0x9e3779b9U);
    return static_cast<float>(h & 0xffffffU) / static_cast<float>(0x1000000U);
}

// -1..1
float signedHash(float seed, std::size_t index, std::uint32_t lane) {
    return unitHash(seed, index, lane) * 2.0f - 1.0f;
}

float storedOf(const AtmosphericEffect& e, const char* leaf, float fallback) {
    std::string key = "meteors/";
    key.append(leaf);
    return e.values.getFloat(key, fallback);
}

// ---- presets ----------------------------------------------------------------------------------------

constexpr std::array<std::string_view, 3> kStyleNames{"Perseid Night", "Glowmere Fall", "Fireball Burst"};

// Every style writes every field it cares about, including turning things off -- the rule the
// comet's presets state and for the same reason: a style that only sets what it wants leaves the
// previous style's settings behind, and an artist reads that as the preset being broken.
void applyStyle(AtmosphericEffect& e, std::string_view style) {
    CometAppearance& a = e.comet.appearance;
    CometPath& p = e.comet.path;
    Sparkle& s = e.comet.sparkle;
    e.comet.rainbow = SkyRainbow{};
    p.anchor = SkyAnchor::Camera; // a shower surrounds the viewer; a world anchor can leave frame
    p.curvature = 0.0f;
    s.enabled = false;
    s.seed = 1;

    if (style == kStyleNames[0]) { // Perseid Night -- thin, white, fast, high
        a.coreColor = {0.95f, 0.98f, 1.0f};
        a.coreIntensity = 24.0f;
        a.headSize = 5.0f;
        a.haloColor = {0.60f, 0.78f, 1.0f};
        a.haloIntensity = 0.9f;
        a.haloSize = 26.0f;
        a.tailColor = {0.70f, 0.85f, 1.0f};
        a.tailIntensity = 3.2f;
        a.tailLength = 620.0f;
        a.tailWidth = 9.0f;
        a.tailFalloff = 2.8f;
        a.wispAmount = 4.0f;
        a.wispScale = 0.0040f;
        a.flowSpeed = 0.15f;
        p.startAzimuth = -58.0f;
        p.startElevation = 62.0f;
        p.endAzimuth = 14.0f;
        p.endElevation = 11.0f;
        p.distance = 3400.0f;
        p.travelSeconds = 0.85f;
        p.speedScale = 1.0f;
        p.acceleration = 0.35f;
        p.arcLift = 60.0f;
        e.values.setFloat("meteors/meteors", 5.0f);
        e.values.setFloat("meteors/spread", 38.0f);
        e.values.setFloat("meteors/stagger", 0.55f);
        e.values.setFloat("meteors/sizeVariation", 0.5f);
        e.values.setFloat("meteors/radiantDrift", 6.0f);
    } else if (style == kStyleNames[1]) { // Glowmere Fall -- the valley's palette, slower, wider
        a.coreColor = {0.55f, 1.0f, 0.92f};
        a.coreIntensity = 17.0f;
        a.headSize = 9.0f;
        a.haloColor = {0.14f, 0.82f, 0.78f};
        a.haloIntensity = 1.6f;
        a.haloSize = 52.0f;
        a.tailColor = {0.20f, 0.90f, 0.72f};
        a.tailIntensity = 4.4f;
        a.tailLength = 1100.0f;
        a.tailWidth = 18.0f;
        a.tailFalloff = 2.0f;
        a.wispAmount = 22.0f;
        a.wispScale = 0.0030f;
        a.flowSpeed = 0.28f;
        p.startAzimuth = -80.0f;
        p.startElevation = 46.0f;
        p.endAzimuth = 40.0f;
        p.endElevation = 6.0f;
        p.distance = 2800.0f;
        p.travelSeconds = 1.6f;
        p.speedScale = 1.0f;
        p.acceleration = 0.15f;
        p.arcLift = 140.0f;
        s.enabled = true;
        s.density = 0.014f;
        s.size = 0.16f;
        s.intensity = 6.0f;
        s.speed = 1.5f;
        e.values.setFloat("meteors/meteors", 4.0f);
        e.values.setFloat("meteors/spread", 64.0f);
        e.values.setFloat("meteors/stagger", 1.10f);
        e.values.setFloat("meteors/sizeVariation", 0.42f);
        e.values.setFloat("meteors/radiantDrift", 12.0f);
    } else { // Fireball Burst -- few, huge, orange, together
        a.coreColor = {1.0f, 0.72f, 0.38f};
        a.coreIntensity = 40.0f;
        a.headSize = 22.0f;
        a.haloColor = {1.0f, 0.42f, 0.12f};
        a.haloIntensity = 3.4f;
        a.haloSize = 130.0f;
        a.tailColor = {1.0f, 0.55f, 0.20f};
        a.tailIntensity = 7.0f;
        a.tailLength = 1500.0f;
        a.tailWidth = 46.0f;
        a.tailFalloff = 1.5f;
        a.wispAmount = 70.0f;
        a.wispScale = 0.0020f;
        a.flowSpeed = 0.5f;
        p.startAzimuth = -30.0f;
        p.startElevation = 52.0f;
        p.endAzimuth = 24.0f;
        p.endElevation = 4.0f;
        p.distance = 2200.0f;
        p.travelSeconds = 2.2f;
        p.speedScale = 1.0f;
        p.acceleration = 0.6f;
        p.arcLift = 210.0f;
        s.enabled = true;
        s.density = 0.020f;
        s.size = 0.22f;
        s.intensity = 12.0f;
        s.speed = 2.0f;
        e.values.setFloat("meteors/meteors", 3.0f);
        e.values.setFloat("meteors/spread", 18.0f);
        e.values.setFloat("meteors/stagger", 0.18f);
        e.values.setFloat("meteors/sizeVariation", 0.7f);
        e.values.setFloat("meteors/radiantDrift", 0.0f);
    }
    e.values.setFloat("meteors/seed", 1.0f);
    e.kind = AtmosphereKind::MeteorShower;
    e.style = std::string(style);
}

void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0},
    {kStyleNames[1].data(), style1},
    {kStyleNames[2].data(), style2},
};

// The two things a shower answers with: how bright the heads are, and how many there are worth
// looking at. `meteors` on the bass is the one that reads across a room -- a drop puts more of them
// in the sky, which is what a shower is for.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "coreIntensity", 8.0f, 50.0f, 380.0f},
    {"beat.pulse", "tailIntensity", 2.0f, 10.0f, 260.0f},
    {"audio.bass", "meteors", 2.0f, 80.0f, 600.0f},
};

AtmosphericEffect make(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::MeteorShower;
    applyStyle(e, kStyleNames[0]);
    // A shower is scenery with a window, not a single event: it runs for a stretch of the song and
    // repeats within it, which is what `repeat` on the shared timing rows is for. The repeat is the
    // pass length; the stagger above is the gap WITHIN a pass.
    e.activation = Activation::Always;
    e.timing.fadeIn = 1.0;
    e.timing.fadeOut = 1.0;
    e.timing.repeatSeconds = 6.0;
    e.ground.mode = GroundGlow::Off;
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

constexpr float kEps = 1e-5f;

// `directionFromSky`, `reparameterise` and `anchorOf` come from `world/atmospherics.hpp` rather
// than being written here. A second transliteration of the arc is the defect ADR-388 records
// between the CPU and GPU halves of the vortex, and two copies inside one language would be that
// with none of the excuse -- and it is what makes "the comet renders byte-identically" a property
// rather than a hope: there is only one curve.

std::size_t count(const AtmosphericEffect& e) {
    const float n = storedOf(e, "meteors", 5.0f);
    return static_cast<std::size_t>(std::clamp(n, 1.0f, kMaxMeteors) + 0.5f);
}

// One meteor. The authored track is the *radiant's* track; each meteor is that track rotated by its
// own share of the spread, started at its own moment, and scaled to its own size.
bool fill(const AtmosphericEffect& e, std::size_t index, const AtmosphericContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    const CometPath& p = e.comet.path;
    const float seed = storedOf(e, "seed", 1.0f);
    const float spread = storedOf(e, "spread", 42.0f);
    const float stagger = storedOf(e, "stagger", 0.55f);
    const float sizeVar = std::clamp(storedOf(e, "sizeVariation", 0.45f), 0.0f, 1.0f);
    const float drift = storedOf(e, "radiantDrift", 8.0f);

    // When this one launches. Stagger is regular with a jitter inside it, so the shower has a pulse
    // without being a metronome.
    const float slot = static_cast<float>(index) + 0.35f * signedHash(seed, index, 4u);
    const double local = base.elapsed - static_cast<double>(slot) * stagger;
    if (local < 0.0) {
        return false; // this one has not started yet; the arm that follows it will
    }

    // The radiant walks across the sky over the pass, so a long shower is not the same two points
    // hit repeatedly. A pure function of the elapsed second, like everything else here.
    const float travel = std::max(p.travelSeconds, kEps);
    const float driftDegrees = drift * static_cast<float>(base.elapsed) / std::max(travel * 4.0f, kEps);

    const float half = spread * 0.5f;
    const float lane = signedHash(seed, index, 1u);
    const float azJitter = lane * half + driftDegrees;
    const float elJitter = signedHash(seed, index, 2u) * half * 0.35f;

    r.anchor = anchorOf(p.anchor, p.anchorPosition, ctx.cameraPosition);
    r.dir0 = directionFromSky(p.startAzimuth + azJitter, p.startElevation + elJitter);
    r.dir1 = directionFromSky(p.endAzimuth + azJitter * 1.25f, p.endElevation + elJitter * 0.5f);
    r.distance = std::max(p.distance, 1.0f);
    const float cosOmega = std::clamp(glm::dot(r.dir0, r.dir1), -1.0f, 1.0f);
    r.omega = std::acos(cosOmega);
    if (r.omega <= 1e-4f) {
        return false;
    }
    r.pathLength = r.omega * r.distance;
    r.liftAmount = p.arcLift / r.distance;
    r.curveAmount = p.curvature / r.distance;

    // Each meteor has its own speed within the variation, so they do not cross in formation.
    const float speed = std::max(p.speedScale, kEps) * (1.0f + sizeVar * 0.5f * signedHash(seed, index, 3u));
    const float progress = static_cast<float>(local) * std::max(speed, kEps) / travel;
    if (progress > 1.35f) {
        return false; // gone past the end of its track and the tail has drained
    }
    r.travelled = reparameterise(progress, p.acceleration) * r.pathLength;
    r.launch = r.anchor + r.dir0 * r.distance;
    r.destination = r.anchor + r.dir1 * r.distance;

    // Size and brightness variation reaches the picture through the envelope, which `packComet`
    // already folds into all three radiances. Using the envelope rather than a per-meteor appearance
    // is what keeps the packer untouched -- and it is also the right shape: a smaller meteor IS a
    // dimmer one.
    //
    // Floored at 0.25 so the variation never produces a meteor that is technically present and
    // invisible, which would read as the count control lying.
    const float size = 1.0f - sizeVar * unitHash(seed, index, 5u);
    r.envelope = base.envelope * std::max(size, 0.25f);
    // ...and each fades out over the last quarter of its own track, so a streak ends rather than
    // being switched off mid-sky.
    if (progress > 0.75f) {
        r.envelope *= std::max(1.0f - (progress - 0.75f) / 0.60f, 0.0f);
    }
    // Each meteor's own clock, so the wisps and the twinkle are not in lockstep across six streaks.
    r.elapsed = local;

    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

// The streak is a comet, so the comet's refusals apply to it. `e.comet.validate()` is asked rather
// than re-derived, which is the same reason the rows alias the same struct.
Result<void> validate(const AtmosphericEffect& e) { return e.comet.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = AtmosphereKind::MeteorShower;
    s.key = "meteors";
    s.enumName = "MeteorShower";
    s.displayName = "Meteor Shower";
    s.addLabel = "Add meteor shower";
    s.addTip = "A handful of streaks thrown from one point in the sky, staggered so they\n"
               "arrive as a shower rather than together. Each one is a comet's trail, so\n"
               "it has the same parallax against the stars -- and the same controls.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "coreIntensity";
    s.groundGlow = true;
    s.anchorSection = "Radiant";
    s.anchorJson = "/comet/path";
    s.getAnchor = +[](const AtmosphericEffect& e) { return e.comet.path.anchor; };
    s.setAnchor = +[](AtmosphericEffect& e, SkyAnchor a) { e.comet.path.anchor = a; };
    s.getAnchorPosition = +[](const AtmosphericEffect& e) { return e.comet.path.anchorPosition; };
    s.setAnchorPosition = +[](AtmosphericEffect& e, glm::vec3 v) { e.comet.path.anchorPosition = v; };
    s.factory = make;
    s.resolve.bucket = EffectBucket::Comet;
    s.resolve.count = count;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& meteorShowerSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
