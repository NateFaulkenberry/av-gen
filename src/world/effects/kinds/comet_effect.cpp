// The comet (ADR-230 §3, ported to the ADR-500 registry).
//
// **Everything a comet is, is in this file**: its fields with their ranges, units, artist labels
// and tooltips; which of them the panel shows above the fold and which behind Advanced; where each
// one lives in the saved file; the clamps a modulation route must not push it past; its five style
// presets; the audio routes a newly added one gets; and how it resolves into the view-ray trail
// integrator each frame.
//
// Before ADR-500 those were spread across `atmospherics.cpp` (JSON in two directions, the presets,
// the resolve arm), `atmospheric_params.cpp` (the field table, `sanitise`, the default routes),
// `ui_logic.hpp` (two row tables) and `world_effects_panel.cpp` (the preset combo, the checkboxes,
// the beat target). ADR-392 counted nine sites for one field, two of them bare string literals.
//
// Nothing about the comet's *behaviour* changed in the port. Every accessor below reads the same
// member the field table read, every range is the range it had, and the resolve is the same
// expressions in the same order -- which is what makes "the three existing effects render
// byte-identically" a claim about a mechanical transformation rather than a hope.
//
// A comet is a curve in the world, not a streak on the screen: see `world/atmospherics.hpp` for why
// the trajectory is a great-circle arc and not a chord, and `shaders/atmosphere_fx.wgsl` for the
// integrator this resolves into.

#include "world/effects/effect_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

#include <glm/geometric.hpp>

namespace avgen::world {
namespace {

using E = EffectInstance;

// The same accessor shape `atmospheric_params.cpp`'s tables used, so the port is a move rather than
// a rewrite: `+[]` decays a captureless lambda to the function pointer the row holds.
#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }
#define SETB(lhs) +[](E& e, bool v) { (lhs) = v; }

// ---- the rows ---------------------------------------------------------------------------------
//
// One row is nine things at once (ADR-392's census): a registered parameter, a modulation target, a
// timeline key, a preset member, a save entry, apply, capture, a panel slider and a JSON key. They
// are in panel order -- `.main()` above the fold, the rest behind Advanced -- because the panel
// walks this list and there is no second list for it to disagree with.
constexpr EffectField kFields[] = {
    colorField("coreColor", "Core colour", GET(e.comet.appearance.coreColor),
               SETC(e.comet.appearance.coreColor)).json("appearance/coreColor").main(),
    colorField("tailColor", "Tail colour", GET(e.comet.appearance.tailColor),
               SETC(e.comet.appearance.tailColor)).json("appearance/tailColor").main(),
    // Every radiance's hard ceiling is far above its soft one: a route driving the core on a drop is
    // the reason this system exists, and a hard clamp at the slider's end would make the route do
    // nothing at exactly the moment it matters.
    floatField("coreIntensity", "Core brightness", 0.0f, 200.0f, 0.0f, 40.0f,
               GET(e.comet.appearance.coreIntensity), SETF(e.comet.appearance.coreIntensity))
        .json("appearance/coreIntensity").main(),
    floatField("headSize", "Head size", 0.5f, 400.0f, 2.0f, 80.0f, GET(e.comet.appearance.headSize),
               SETF(e.comet.appearance.headSize)).json("appearance/headSize").fmt("%.0f m").main().floorAt(0.1f),
    floatField("tailLength", "Tail length", 10.0f, 6000.0f, 100.0f, 2000.0f,
               GET(e.comet.appearance.tailLength), SETF(e.comet.appearance.tailLength))
        .json("appearance/tailLength").fmt("%.0f m").main().floorAt(1.0f),
    floatField("tailWidth", "Tail width", 1.0f, 600.0f, 5.0f, 200.0f, GET(e.comet.appearance.tailWidth),
               SETF(e.comet.appearance.tailWidth)).json("appearance/tailWidth").fmt("%.0f m").main().floorAt(0.5f),
    floatField("travelSeconds", "Crossing", 0.05f, 600.0f, 1.0f, 30.0f, GET(e.comet.path.travelSeconds),
               SETF(e.comet.path.travelSeconds)).json("path/travelSeconds").fmt("%.1f s").main().floorAt(0.05f),
    boolField("sparkle", "Sparkling fragments", GET(e.comet.sparkle.enabled),
              SETB(e.comet.sparkle.enabled)).json("sparkle/enabled").main(),
    boolField("rainbow", "Rainbow", GET(e.comet.rainbow.enabled), SETB(e.comet.rainbow.enabled))
        .json("rainbow/enabled").main(),

    // ---- Advanced: the trajectory. The section header is drawn by the anchor combo above it.
    floatField("startAzimuth", "Start bearing", -720.0f, 720.0f, -180.0f, 180.0f,
               GET(e.comet.path.startAzimuth), SETF(e.comet.path.startAzimuth))
        .json("path/startAzimuth").fmt("%.0f deg"),
    floatField("startElevation", "Start height", -20.0f, 89.0f, 0.0f, 80.0f,
               GET(e.comet.path.startElevation), SETF(e.comet.path.startElevation))
        .json("path/startElevation").fmt("%.0f deg"),
    floatField("endAzimuth", "End bearing", -720.0f, 720.0f, -180.0f, 180.0f, GET(e.comet.path.endAzimuth),
               SETF(e.comet.path.endAzimuth)).json("path/endAzimuth").fmt("%.0f deg"),
    floatField("endElevation", "End height", -20.0f, 89.0f, 0.0f, 80.0f, GET(e.comet.path.endElevation),
               SETF(e.comet.path.endElevation)).json("path/endElevation").fmt("%.0f deg"),
    floatField("distance", "Distance", 50.0f, 40000.0f, 400.0f, 8000.0f, GET(e.comet.path.distance),
               SETF(e.comet.path.distance)).json("path/distance").fmt("%.0f m").log().floorAt(1.0f),
    floatField("speed", "Speed", 0.01f, 20.0f, 0.1f, 4.0f, GET(e.comet.path.speedScale),
               SETF(e.comet.path.speedScale)).json("path/speedScale").floorAt(0.01f),
    // Floored just above -0.5, where the reparameterisation stops being monotone; the hard range is
    // what a route is clamped to, so this is the clamp that stops a modulated comet reversing.
    floatField("acceleration", "Acceleration", -0.45f, 8.0f, -0.4f, 3.0f, GET(e.comet.path.acceleration),
               SETF(e.comet.path.acceleration)).json("path/acceleration"),
    floatField("arcLift", "Arc lift", -4000.0f, 4000.0f, -600.0f, 600.0f, GET(e.comet.path.arcLift),
               SETF(e.comet.path.arcLift)).json("path/arcLift").fmt("%.0f m"),
    floatField("curvature", "Curvature", -4000.0f, 4000.0f, -800.0f, 800.0f, GET(e.comet.path.curvature),
               SETF(e.comet.path.curvature)).json("path/curvature").fmt("%.0f m"),

    colorField("haloColor", "Halo colour", GET(e.comet.appearance.haloColor),
               SETC(e.comet.appearance.haloColor)).json("appearance/haloColor").sec("Appearance"),
    floatField("haloIntensity", "Halo brightness", 0.0f, 60.0f, 0.0f, 10.0f,
               GET(e.comet.appearance.haloIntensity), SETF(e.comet.appearance.haloIntensity))
        .json("appearance/haloIntensity"),
    floatField("haloSize", "Halo size", 1.0f, 2000.0f, 10.0f, 400.0f, GET(e.comet.appearance.haloSize),
               SETF(e.comet.appearance.haloSize)).json("appearance/haloSize").fmt("%.0f m").floorAt(1.0f),
    floatField("tailIntensity", "Tail brightness", 0.0f, 80.0f, 0.0f, 15.0f,
               GET(e.comet.appearance.tailIntensity), SETF(e.comet.appearance.tailIntensity))
        .json("appearance/tailIntensity"),
    floatField("tailFalloff", "Tail falloff", 0.05f, 8.0f, 0.5f, 4.0f, GET(e.comet.appearance.tailFalloff),
               SETF(e.comet.appearance.tailFalloff)).json("appearance/tailFalloff").floorAt(0.05f),
    floatField("wispAmount", "Wisp amount", 0.0f, 400.0f, 0.0f, 150.0f, GET(e.comet.appearance.wispAmount),
               SETF(e.comet.appearance.wispAmount)).json("appearance/wispAmount").fmt("%.0f m"),
    floatField("wispScale", "Wisp scale", 0.0f, 0.05f, 0.0f, 0.01f, GET(e.comet.appearance.wispScale),
               SETF(e.comet.appearance.wispScale)).json("appearance/wispScale").fmt("%.4f"),
    floatField("flowSpeed", "Wisp flow", -4.0f, 4.0f, -1.0f, 1.0f, GET(e.comet.appearance.flowSpeed),
               SETF(e.comet.appearance.flowSpeed)).json("appearance/flowSpeed"),

    // Sparkle. `density` is fragments per metre of trail, so its numbers are small: a 900 m tail at
    // 0.016 sheds fourteen.
    floatField("sparkleDensity", "Density", 0.0f, 0.2f, 0.0f, 0.05f, GET(e.comet.sparkle.density),
               SETF(e.comet.sparkle.density)).json("sparkle/density").fmt("%.3f /m").sec("Fragments"),
    floatField("sparkleSize", "Size", 0.0f, 1.0f, 0.02f, 0.8f, GET(e.comet.sparkle.size),
               SETF(e.comet.sparkle.size)).json("sparkle/size"),
    floatField("sparkleIntensity", "Brightness", 0.0f, 80.0f, 0.0f, 20.0f, GET(e.comet.sparkle.intensity),
               SETF(e.comet.sparkle.intensity)).json("sparkle/intensity"),
    floatField("sparkleSpeed", "Twinkle", 0.0f, 12.0f, 0.0f, 4.0f, GET(e.comet.sparkle.speed),
               SETF(e.comet.sparkle.speed)).json("sparkle/speed"),

    floatField("rainbowSpeed", "Speed", -4.0f, 4.0f, -1.0f, 1.0f, GET(e.comet.rainbow.speed),
               SETF(e.comet.rainbow.speed)).json("rainbow/speed").sec("Rainbow"),
    floatField("rainbowScale", "Scale", 0.0f, 12.0f, 0.0f, 4.0f, GET(e.comet.rainbow.scale),
               SETF(e.comet.rainbow.scale)).json("rainbow/scale"),
    floatField("rainbowHue", "Hue offset", -4.0f, 4.0f, 0.0f, 1.0f, GET(e.comet.rainbow.hueOffset),
               SETF(e.comet.rainbow.hueOffset)).json("rainbow/hueOffset"),
    floatField("rainbowSaturation", "Saturation", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.comet.rainbow.saturation),
               SETF(e.comet.rainbow.saturation)).json("rainbow/saturation"),
    floatField("rainbowBrightness", "Brightness", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.comet.rainbow.brightness),
               SETF(e.comet.rainbow.brightness)).json("rainbow/brightness"),
};

#undef GET
#undef SETF
#undef SETC
#undef SETB

// ---- the presets (ADR-230 §10) -------------------------------------------------------------------

constexpr std::array<std::string_view, 5> kStyleNames{"Bioluminescent Cyan", "Rainbow Cosmic",
                                                      "Emerald Teal", "Magenta Blue",
                                                      "Subtle Shooting Star"};

bool applyStyle(EffectInstance& e, std::string_view style) {
    CometAppearance& a = e.comet.appearance;
    Sparkle& s = e.comet.sparkle;
    SkyRainbow& r = e.comet.rainbow;
    // Every style writes every field it cares about, including turning things off. A style that
    // only sets what it wants leaves the previous style's rainbow on, and the user reads that as
    // the preset being broken rather than as two presets overlapping.
    r = SkyRainbow{};
    s.enabled = true;
    s.seed = 1;
    if (style == kStyleNames[0]) { // Bioluminescent Cyan
        a.coreColor = {0.55f, 1.0f, 1.0f};
        a.coreIntensity = 20.0f;
        a.headSize = 19.0f;
        a.haloColor = {0.16f, 0.78f, 1.0f};
        a.haloIntensity = 2.1f;
        a.haloSize = 95.0f;
        a.tailColor = {0.42f, 0.34f, 1.0f};
        a.tailIntensity = 5.0f;
        a.tailLength = 2000.0f;
        a.tailWidth = 38.0f;
        a.tailFalloff = 1.85f;
        a.wispAmount = 62.0f;
        a.wispScale = 0.0022f;
        a.flowSpeed = 0.35f;
        s.density = 0.011f;
        s.size = 0.13f;
        s.intensity = 7.0f;
        s.speed = 1.3f;
    } else if (style == kStyleNames[1]) { // Rainbow Cosmic
        // The colours are left near-white on purpose: rainbow replaces the *hue* and keeps the
        // magnitude, exactly as ADR-207's Rainbow style does, so a tinted base would fight it.
        a.coreColor = {1.0f, 1.0f, 1.0f};
        // Lower than the explicit-colour presets on purpose. Rainbow multiplies a hue into a white
        // base, so at 22 the core clipped to white and the one part of the comet an eye goes
        // straight to was the one part with no colour left in it.
        a.coreIntensity = 13.0f;
        a.headSize = 21.0f;
        a.haloColor = {0.9f, 0.9f, 1.0f};
        a.haloIntensity = 2.2f;
        a.haloSize = 105.0f;
        a.tailColor = {1.0f, 1.0f, 1.0f};
        a.tailIntensity = 5.5f;
        a.tailLength = 2400.0f;
        a.tailWidth = 44.0f;
        a.tailFalloff = 1.65f;
        a.wispAmount = 78.0f;
        a.wispScale = 0.0019f;
        a.flowSpeed = 0.42f;
        s.density = 0.013f;
        s.size = 0.15f;
        s.intensity = 8.5f;
        s.speed = 1.6f;
        r.enabled = true;
        // Just over one turn across the tail: less and it is a single hue, much more and it stripes
        // into the "broken RGB shader" §3.3 warns about.
        r.scale = 1.25f;
        r.speed = 0.28f;
        r.saturation = 0.92f;
        r.brightness = 1.05f;
    } else if (style == kStyleNames[2]) { // Emerald Teal
        a.coreColor = {0.55f, 1.0f, 0.78f};
        a.coreIntensity = 17.0f;
        a.headSize = 17.0f;
        a.haloColor = {0.10f, 0.85f, 0.62f};
        a.haloIntensity = 1.9f;
        a.haloSize = 88.0f;
        a.tailColor = {0.13f, 0.92f, 0.72f};
        a.tailIntensity = 4.6f;
        a.tailLength = 1750.0f;
        a.tailWidth = 34.0f;
        a.tailFalloff = 2.0f;
        a.wispAmount = 54.0f;
        a.wispScale = 0.0024f;
        a.flowSpeed = 0.31f;
        s.density = 0.010f;
        s.size = 0.12f;
        s.intensity = 6.5f;
        s.speed = 1.2f;
    } else if (style == kStyleNames[3]) { // Magenta Blue
        a.coreColor = {1.0f, 0.68f, 1.0f};
        a.coreIntensity = 19.0f;
        a.headSize = 19.0f;
        a.haloColor = {0.85f, 0.22f, 0.95f};
        a.haloIntensity = 2.0f;
        a.haloSize = 98.0f;
        a.tailColor = {0.30f, 0.32f, 1.0f};
        a.tailIntensity = 5.2f;
        a.tailLength = 1900.0f;
        a.tailWidth = 40.0f;
        a.tailFalloff = 1.75f;
        a.wispAmount = 66.0f;
        a.wispScale = 0.0021f;
        a.flowSpeed = 0.38f;
        s.density = 0.012f;
        s.size = 0.14f;
        s.intensity = 7.5f;
        s.speed = 1.4f;
    } else if (style == kStyleNames[4]) { // Subtle Shooting Star
        a.coreColor = {0.92f, 0.97f, 1.0f};
        a.coreIntensity = 7.0f;
        a.headSize = 6.0f;
        a.haloColor = {0.55f, 0.72f, 1.0f};
        a.haloIntensity = 0.7f;
        a.haloSize = 34.0f;
        a.tailColor = {0.60f, 0.75f, 1.0f};
        a.tailIntensity = 1.5f;
        a.tailLength = 700.0f;
        a.tailWidth = 11.0f;
        a.tailFalloff = 2.6f;
        a.wispAmount = 6.0f;
        a.wispScale = 0.0040f;
        a.flowSpeed = 0.20f;
        s.enabled = false;
        s.density = 0.010f;
        s.size = 0.18f;
        s.intensity = 3.0f;
        s.speed = 1.0f;
    } else {
        return false;
    }
    e.kind = EffectKind::Comet;
    e.style = std::string(style);
    return true;
}

// Five one-line trampolines over the body above, because a schema row holds a function pointer and
// a captureless lambda cannot carry which style it is. The body stays one if-chain so that the
// values below are the values ADR-230 shipped, unmoved.
void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }
void style4(E& e) { applyStyle(e, kStyleNames[4]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0}, {kStyleNames[1].data(), style1}, {kStyleNames[2].data(), style2},
    {kStyleNames[3].data(), style3}, {kStyleNames[4].data(), style4},
};

// ---- the default audio routes ---------------------------------------------------------------------
//
// A comet is an event, and most of its shape is authored rather than played. What answers the music
// is its brightness and its sparkle -- the two that read at a glance. `Add` so silence leaves the
// authored pose exactly as it was written.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "coreIntensity", 6.0f, 60.0f, 400.0f},
    {"beat.pulse", "tailIntensity", 1.4f, 10.0f, 280.0f},
    {"audio.treble", "sparkleIntensity", 4.0f, 20.0f, 220.0f},
};

// ---- the ready-made effect ------------------------------------------------------------------------

EffectInstance make(std::string name) {
    EffectInstance e;
    e.name = std::move(name);
    e.kind = EffectKind::Comet;
    applyStyle(e, kStyleNames[0]);
    // A comet is an *event*: §3.5 wants one launch on a musical transition, not a permanent object.
    // A window is the activation a sequencer can move; `lifetime` 0 means "as long as the window",
    // and the window is a little longer than the crossing so the tail has somewhere to drain.
    e.activation = Activation::Window;
    e.timing.windowStart = 4.0;
    e.timing.windowSeconds = 10.0;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.8;
    e.comet.path.travelSeconds = 7.0f;
    e.ground.mode = GroundGlow::Subtle;
    e.ground.color = {0.30f, 0.85f, 1.0f};
    return e;
}

// ---- resolution ------------------------------------------------------------------------------------
//
// The arm that was `case EffectKind::Comet:` in `resolveAtmosphericEffects`, unchanged: the same
// expressions, in the same order, on the same members. `base` already carries the envelope, the
// elapsed second and the effect pointer, which are the parts every kind shares.

constexpr float kEps = 1e-5f;

// `directionFromSky`, `reparameterise` and `anchorOf` come from `world/atmospherics.hpp` rather
// than being written here. A second transliteration of the arc is the defect ADR-388 records
// between the CPU and GPU halves of the vortex, and two copies inside one language would be that
// with none of the excuse -- and it is what makes "the comet renders byte-identically" a property
// rather than a hope: there is only one curve.

bool fill(const EffectInstance& e, std::size_t, const EffectContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    const CometPath& p = e.comet.path;
    r.anchor = anchorOf(p.anchor, p.anchorPosition, ctx.cameraPosition);
    r.dir0 = directionFromSky(p.startAzimuth, p.startElevation);
    r.dir1 = directionFromSky(p.endAzimuth, p.endElevation);
    r.distance = std::max(p.distance, 1.0f);
    // The angle between the two bearings, which with the distance is the arc's length. A comet whose
    // ends have collapsed onto each other has nowhere to fly; `validate` refuses that, but a
    // modulated azimuth can still reach it, so it is checked here too.
    const float cosOmega = std::clamp(glm::dot(r.dir0, r.dir1), -1.0f, 1.0f);
    r.omega = std::acos(cosOmega);
    if (r.omega <= 1e-4f) {
        return false;
    }
    r.pathLength = r.omega * r.distance;
    r.liftAmount = p.arcLift / r.distance;
    r.curveAmount = p.curvature / r.distance;

    const float progress =
        static_cast<float>(base.elapsed) * std::max(p.speedScale, kEps) / std::max(p.travelSeconds, kEps);
    r.travelled = reparameterise(progress, p.acceleration) * r.pathLength;
    r.launch = r.anchor + r.dir0 * r.distance;
    r.destination = r.anchor + r.dir1 * r.distance;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

// ---- what is in the file and is not a parameter -------------------------------------------------
//
// Two things in the whole family. A seed is a `uint32` and a fade distance is the range at which
// sparkle stops aliasing -- neither is a thing to automate, and both must still round-trip. They are
// here, in the comet's own file, rather than as a case in a shared serialiser.
void writeExtra(const EffectInstance& e, nlohmann::json& out) {
    out["sparkle"]["fadeDistance"] = e.comet.sparkle.fadeDistance;
    out["sparkle"]["seed"] = e.comet.sparkle.seed;
}

Result<void> readExtra(EffectInstance& e, const nlohmann::json& in) {
    if (!in.contains("sparkle") || !in.at("sparkle").is_object()) {
        return {};
    }
    const nlohmann::json& s = in.at("sparkle");
    if (s.contains("fadeDistance") && s.at("fadeDistance").is_number()) {
        e.comet.sparkle.fadeDistance = s.at("fadeDistance").get<float>();
    }
    if (s.contains("seed") && s.at("seed").is_number_unsigned()) {
        e.comet.sparkle.seed = s.at("seed").get<std::uint32_t>();
    }
    return {};
}

Result<void> validate(const EffectInstance& e) { return e.comet.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Comet;
    
    s.key = "comet";
    s.enumName = "Comet";
    s.displayName = "Bioluminescent Comet";
    s.description = "A bright head crossing the sky on a world-space arc, with a halo, a wispy trail and shed fragments; lights the ground below it.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Add comet";
    s.addTip = "A celestial object on a great-circle arc across the sky.\n"
               "It arrives as an event with an authored window, so it launches once;\n"
               "give it a repeat interval to make it a shower.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "coreIntensity";
    s.groundGlow = true;
    s.anchorSection = "Trajectory";
    s.anchorJson = "path";
    s.getAnchor = +[](const E& e) { return e.comet.path.anchor; };
    s.setAnchor = +[](E& e, SkyAnchor a) { e.comet.path.anchor = a; };
    s.getAnchorPosition = +[](const E& e) { return e.comet.path.anchorPosition; };
    s.setAnchorPosition = +[](E& e, glm::vec3 v) { e.comet.path.anchorPosition = v; };
    s.factory = make;
    s.resolve.bucket = EffectBucket::Comet;
    // ADR-702: attached to the World; evaluated at its bucket's stage.
    s.targets = targetBit(EffectTarget::World);
    s.category = EffectCategory::Sky;
    s.stage = RenderStage::Sky;
    s.resolve.count = nullptr; // exactly one
    s.resolve.fill = fill;
    s.writeExtra = writeExtra;
    s.readExtra = readExtra;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& cometSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
