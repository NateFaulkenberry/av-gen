// The aurora (ADR-230 §4, ported to the ADR-500 registry).
//
// Everything an aurora is, is in this file: rows, ranges, labels, tooltips, panel placement, the
// saved file's shape, the clamps, the five styles, the default audio routes and the resolve. See
// `effects/comet_effect.cpp` for what that replaced and why the port is behaviour-neutral.
//
// An aurora is a set of vertical cylinders, not a plane: the ray is intersected with K shells at
// stepped radii, and the hit's azimuth and height index a curtain whose top is driven by the audio
// spectrum. §4.2's per-band depths below are **depths on a signal that already exists**, not a
// second analyzer -- each scales how much a band already in the frame block moves its feature.

#include "world/effects/effect_registry.hpp"

#include <array>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = EffectInstance;

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }
#define SETB(lhs) +[](E& e, bool v) { (lhs) = v; }

constexpr EffectField kFields[] = {
    // Three colours rather than two, because an aurora's signature is a *vertical* hue ramp --
    // green at the base through cyan to violet at the fading top -- and interpolating two cannot
    // produce it.
    colorField("lowColor", "Base colour", GET(e.aurora.appearance.lowColor),
               SETC(e.aurora.appearance.lowColor)).json("appearance/lowColor").main(),
    colorField("midColor", "Middle colour", GET(e.aurora.appearance.midColor),
               SETC(e.aurora.appearance.midColor)).json("appearance/midColor").main(),
    colorField("topColor", "Top colour", GET(e.aurora.appearance.topColor),
               SETC(e.aurora.appearance.topColor)).json("appearance/topColor").main(),
    floatField("intensity", "Brightness", 0.0f, 60.0f, 0.0f, 8.0f, GET(e.aurora.appearance.intensity),
               SETF(e.aurora.appearance.intensity)).json("appearance/intensity").main(),
    floatField("curtainHeight", "Height", 50.0f, 30000.0f, 400.0f, 8000.0f,
               GET(e.aurora.shape.curtainHeight), SETF(e.aurora.shape.curtainHeight))
        .json("shape/curtainHeight").fmt("%.0f m").main().floorAt(1.0f),
    // Hard-clamped to the shader's loop bound, because a route that pushed it to seven would
    // silently do nothing past five and that is a control that lies.
    floatField("curtains", "Curtains", 1.0f, 5.0f, 1.0f, 5.0f, GET(e.aurora.shape.curtainCount),
               SETF(e.aurora.shape.curtainCount)).json("shape/curtainCount").fmt("%.0f").main().clampTo(1.0f, 5.0f),
    floatField("flowSpeed", "Flow", -2.0f, 2.0f, -0.4f, 0.4f, GET(e.aurora.shape.flowSpeed),
               SETF(e.aurora.shape.flowSpeed)).json("shape/flowSpeed").main(),
    floatField("audioSensitivity", "Audio response", 0.0f, 6.0f, 0.0f, 3.0f,
               GET(e.aurora.audio.sensitivity), SETF(e.aurora.audio.sensitivity))
        .json("audio/sensitivity").main(),
    // How much of the curtain's height comes from the *spectrum* rather than from a flat base. 0
    // ignores the music's shape and still answers its level through the routes; 1 is a visualiser.
    floatField("spectrumShape", "Spectrum shape", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.aurora.audio.spectrumShape), SETF(e.aurora.audio.spectrumShape))
        .json("audio/spectrumShape").main(),
    boolField("rainbow", "Colour cycle", GET(e.aurora.rainbow.enabled), SETB(e.aurora.rainbow.enabled))
        .json("rainbow/enabled").main(),

    // ---- Advanced: shape. The section header is drawn by the anchor combo above it.
    floatField("radius", "Distance", 200.0f, 40000.0f, 1000.0f, 12000.0f, GET(e.aurora.shape.radius),
               SETF(e.aurora.shape.radius)).json("shape/radius").fmt("%.0f m").log().floorAt(1.0f),
    floatField("layerSpacing", "Layer spacing", 0.0f, 3.0f, 0.0f, 1.0f, GET(e.aurora.shape.layerSpacing),
               SETF(e.aurora.shape.layerSpacing)).json("shape/layerSpacing"),
    floatField("baseHeight", "Base height", -4000.0f, 4000.0f, -400.0f, 400.0f,
               GET(e.aurora.shape.baseHeight), SETF(e.aurora.shape.baseHeight))
        .json("shape/baseHeight").fmt("%.0f m"),
    floatField("waveAmplitude", "Wave amount", 0.0f, 3.0f, 0.0f, 1.0f, GET(e.aurora.shape.waveAmplitude),
               SETF(e.aurora.shape.waveAmplitude)).json("shape/waveAmplitude"),
    floatField("waveScale", "Wave scale", 0.0f, 30.0f, 0.2f, 8.0f, GET(e.aurora.shape.waveScale),
               SETF(e.aurora.shape.waveScale)).json("shape/waveScale"),
    floatField("turbulence", "Turbulence", 0.0f, 3.0f, 0.0f, 1.5f, GET(e.aurora.shape.turbulence),
               SETF(e.aurora.shape.turbulence)).json("shape/turbulence"),
    floatField("complexity", "Ray structure", 0.0f, 160.0f, 4.0f, 60.0f, GET(e.aurora.shape.complexity),
               SETF(e.aurora.shape.complexity)).json("shape/complexity").fmt("%.0f"),
    floatField("driftSpeed", "Fold drift", -4.0f, 4.0f, -1.0f, 1.0f, GET(e.aurora.shape.driftSpeed),
               SETF(e.aurora.shape.driftSpeed)).json("shape/driftSpeed"),
    floatField("verticalSpeed", "Vertical drift", -4.0f, 4.0f, -1.0f, 1.0f,
               GET(e.aurora.shape.verticalSpeed), SETF(e.aurora.shape.verticalSpeed))
        .json("shape/verticalSpeed"),

    floatField("emission", "Bloom weight", 0.0f, 2.0f, 0.0f, 1.0f, GET(e.aurora.appearance.emission),
               SETF(e.aurora.appearance.emission)).json("appearance/emission").sec("Appearance"),
    floatField("opacity", "Curtain opacity", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.aurora.appearance.opacity),
               SETF(e.aurora.appearance.opacity)).json("appearance/opacity"),
    floatField("edgeBrightness", "Edge brightness", 0.0f, 30.0f, 0.0f, 6.0f,
               GET(e.aurora.appearance.edgeBrightness), SETF(e.aurora.appearance.edgeBrightness))
        .json("appearance/edgeBrightness"),
    floatField("filaments", "Filaments", 0.0f, 10.0f, 0.0f, 3.0f, GET(e.aurora.appearance.filaments),
               SETF(e.aurora.appearance.filaments)).json("appearance/filaments"),
    floatField("sparkle", "Sparkle", 0.0f, 10.0f, 0.0f, 3.0f, GET(e.aurora.appearance.sparkle),
               SETF(e.aurora.appearance.sparkle)).json("appearance/sparkle"),
    floatField("horizonGlow", "Horizon glow", 0.0f, 6.0f, 0.0f, 2.0f,
               GET(e.aurora.appearance.horizonGlow), SETF(e.aurora.appearance.horizonGlow))
        .json("appearance/horizonGlow"),

    // §4.2's per-band depths. They scale the bands already in the frame block; they are not a
    // second analyzer, and every one of them is itself an ordinary parameter a route can drive.
    floatField("audioBass", "Bass -> height", 0.0f, 6.0f, 0.0f, 2.0f, GET(e.aurora.audio.bass),
               SETF(e.aurora.audio.bass)).json("audio/bass").sec("Audio response"),
    floatField("audioLowMid", "Low-mid -> waves", 0.0f, 6.0f, 0.0f, 2.0f, GET(e.aurora.audio.lowMid),
               SETF(e.aurora.audio.lowMid)).json("audio/lowMid"),
    floatField("audioMid", "Mid -> folds", 0.0f, 6.0f, 0.0f, 2.0f, GET(e.aurora.audio.mid),
               SETF(e.aurora.audio.mid)).json("audio/mid"),
    floatField("audioHigh", "High -> filaments", 0.0f, 6.0f, 0.0f, 2.0f, GET(e.aurora.audio.high),
               SETF(e.aurora.audio.high)).json("audio/high"),
    floatField("audioBeat", "Beat -> pulse", 0.0f, 6.0f, 0.0f, 2.0f, GET(e.aurora.audio.beat),
               SETF(e.aurora.audio.beat)).json("audio/beat"),

    floatField("rainbowSpeed", "Speed", -4.0f, 4.0f, -0.5f, 0.5f, GET(e.aurora.rainbow.speed),
               SETF(e.aurora.rainbow.speed)).json("rainbow/speed").sec("Rainbow"),
    floatField("rainbowScale", "Scale", 0.0f, 12.0f, 0.0f, 3.0f, GET(e.aurora.rainbow.scale),
               SETF(e.aurora.rainbow.scale)).json("rainbow/scale"),
    floatField("rainbowHue", "Hue offset", -4.0f, 4.0f, 0.0f, 1.0f, GET(e.aurora.rainbow.hueOffset),
               SETF(e.aurora.rainbow.hueOffset)).json("rainbow/hueOffset"),
    floatField("rainbowSaturation", "Saturation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.aurora.rainbow.saturation), SETF(e.aurora.rainbow.saturation)).json("rainbow/saturation"),
    floatField("rainbowBrightness", "Brightness", 0.0f, 4.0f, 0.0f, 2.0f,
               GET(e.aurora.rainbow.brightness), SETF(e.aurora.rainbow.brightness)).json("rainbow/brightness"),
};

#undef GET
#undef SETF
#undef SETC
#undef SETB

constexpr std::array<std::string_view, 5> kStyleNames{"Glowmere Bioluminescence", "Vibrant Emerald",
                                                      "Violet Cosmic", "Rainbow Aurora",
                                                      "Subtle Night"};

bool applyStyle(EffectInstance& e, std::string_view style) {
    AuroraAppearance& a = e.aurora.appearance;
    AuroraShape& s = e.aurora.shape;
    SkyRainbow& r = e.aurora.rainbow;
    r = SkyRainbow{};
    if (style == kStyleNames[0]) { // Glowmere Bioluminescence
        a.lowColor = {0.14f, 1.0f, 0.62f};
        a.midColor = {0.16f, 0.82f, 1.0f};
        a.topColor = {0.58f, 0.32f, 1.0f};
        a.intensity = 2.8f;
        a.emission = 1.0f;
        a.opacity = 0.85f;
        a.edgeBrightness = 2.6f;
        a.filaments = 0.95f;
        a.horizonGlow = 0.60f;
        s.curtainCount = 3.0f;
        s.waveAmplitude = 0.30f;
        s.waveScale = 2.4f;
        s.turbulence = 0.45f;
        s.complexity = 26.0f;
    } else if (style == kStyleNames[1]) { // Vibrant Emerald
        a.lowColor = {0.10f, 1.0f, 0.38f};
        a.midColor = {0.22f, 1.0f, 0.72f};
        a.topColor = {0.20f, 0.90f, 1.0f};
        a.intensity = 3.4f;
        a.emission = 1.0f;
        a.opacity = 0.92f;
        a.edgeBrightness = 3.2f;
        a.filaments = 1.10f;
        a.horizonGlow = 0.72f;
        s.curtainCount = 4.0f;
        s.waveAmplitude = 0.36f;
        s.waveScale = 2.0f;
        s.turbulence = 0.40f;
        s.complexity = 30.0f;
    } else if (style == kStyleNames[2]) { // Violet Cosmic
        a.lowColor = {0.32f, 0.28f, 1.0f};
        a.midColor = {0.62f, 0.26f, 1.0f};
        a.topColor = {1.0f, 0.34f, 0.86f};
        a.intensity = 2.9f;
        a.emission = 1.0f;
        a.opacity = 0.80f;
        a.edgeBrightness = 2.8f;
        a.filaments = 0.85f;
        a.horizonGlow = 0.50f;
        s.curtainCount = 3.0f;
        s.waveAmplitude = 0.42f;
        s.waveScale = 1.7f;
        s.turbulence = 0.55f;
        s.complexity = 22.0f;
    } else if (style == kStyleNames[3]) { // Rainbow Aurora
        a.lowColor = {1.0f, 1.0f, 1.0f};
        a.midColor = {1.0f, 1.0f, 1.0f};
        a.topColor = {1.0f, 1.0f, 1.0f};
        a.intensity = 2.6f;
        a.emission = 1.0f;
        a.opacity = 0.84f;
        a.edgeBrightness = 2.4f;
        a.filaments = 1.0f;
        a.horizonGlow = 0.55f;
        s.curtainCount = 4.0f;
        s.waveAmplitude = 0.34f;
        s.waveScale = 2.2f;
        s.turbulence = 0.48f;
        s.complexity = 28.0f;
        r.enabled = true;
        // Under one turn of azimuth, so the sky reads as a slow sweep through the spectrum rather
        // than as bands: §4.4's rainbow mode is a colour *cycle*, not a striping.
        r.scale = 0.75f;
        r.speed = 0.06f;
        r.saturation = 0.88f;
        r.brightness = 1.0f;
    } else if (style == kStyleNames[4]) { // Subtle Night
        a.lowColor = {0.18f, 0.72f, 0.52f};
        a.midColor = {0.18f, 0.55f, 0.72f};
        a.topColor = {0.28f, 0.30f, 0.62f};
        a.intensity = 0.85f;
        a.emission = 0.5f;
        a.opacity = 0.55f;
        a.edgeBrightness = 0.9f;
        a.filaments = 0.35f;
        a.horizonGlow = 0.30f;
        s.curtainCount = 2.0f;
        s.waveAmplitude = 0.22f;
        s.waveScale = 1.9f;
        s.turbulence = 0.32f;
        s.complexity = 18.0f;
    } else {
        return false;
    }
    e.kind = EffectKind::Aurora;
    e.style = std::string(style);
    return true;
}

void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }
void style4(E& e) { applyStyle(e, kStyleNames[4]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0}, {kStyleNames[1].data(), style1}, {kStyleNames[2].data(), style2},
    {kStyleNames[3].data(), style3}, {kStyleNames[4].data(), style4},
};

// §4.2's proposed mapping, as the default rather than as the only answer. The depths are fractions
// of each parameter's soft range, and the smoothing is what stops the curtain jittering: a 40 ms
// attack and a ~400 ms release is a curtain that answers the music rather than one that strobes.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "curtainHeight", 900.0f, 60.0f, 420.0f},
    {"audio.rms", "intensity", 0.8f, 80.0f, 500.0f},
    {"audio.lowMid", "waveAmplitude", 0.12f, 50.0f, 380.0f},
    {"audio.mid", "turbulence", 0.20f, 40.0f, 320.0f},
    {"audio.treble", "filaments", 0.55f, 25.0f, 240.0f},
    {"beat.pulse", "edgeBrightness", 1.1f, 10.0f, 260.0f},
};

EffectInstance make(std::string name) {
    EffectInstance e;
    e.name = std::move(name);
    e.kind = EffectKind::Aurora;
    applyStyle(e, kStyleNames[0]);
    // An aurora is scenery that breathes rather than an event: it is on, and the music moves it.
    e.activation = Activation::Always;
    e.timing.fadeIn = 2.0;
    e.timing.fadeOut = 0.0;
    e.ground.mode = GroundGlow::Subtle;
    e.ground.color = {0.16f, 0.72f, 0.58f};
    e.ground.intensity = 0.5f;
    return e;
}

// An aurora has no per-frame trajectory: the shells are where they were authored, and the curtain's
// motion is phase, computed in `packAurora` from the elapsed second. So the resolve is the anchor
// and the field sample, and nothing else.
bool fill(const EffectInstance& e, std::size_t, const EffectContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    const AuroraShape& s = e.aurora.shape;
    r.anchor = anchorOf(s.anchor, s.anchorPosition, ctx.cameraPosition);
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const EffectInstance& e) { return e.aurora.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Aurora;
    
    s.key = "aurora";
    s.enumName = "Aurora";
    s.displayName = "Aurora";
    s.addLabel = "Add aurora";
    s.addTip = "Curtains rising from the horizon, shaped by the audio spectrum.\n"
               "Always on and fading up: an aurora is scenery that breathes.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "edgeBrightness";
    s.groundGlow = true;
    s.anchorSection = "Shape";
    s.anchorJson = "shape";
    s.getAnchor = +[](const E& e) { return e.aurora.shape.anchor; };
    s.setAnchor = +[](E& e, SkyAnchor a) { e.aurora.shape.anchor = a; };
    s.getAnchorPosition = +[](const E& e) { return e.aurora.shape.anchorPosition; };
    s.setAnchorPosition = +[](E& e, glm::vec3 v) { e.aurora.shape.anchorPosition = v; };
    s.factory = make;
    s.resolve.bucket = EffectBucket::Aurora;
    // ADR-702: attached to the World; evaluated at its bucket's stage.
    s.targets = targetBit(EffectTarget::World);
    s.category = EffectCategory::Sky;
    s.stage = RenderStage::Sky;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& auroraSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
