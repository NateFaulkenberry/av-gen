// The volumetric fog bank (ADR-500's second proof that an effect is one file).
//
// **What it is.** A placed bank of luminous medium sitting in the world -- a drift of mist filling a
// valley, a glowing haze around an island -- with a soft rim, a Gaussian vertical profile and
// billowing internal structure. Authored in fog vocabulary: a density per metre, a radius, a height,
// a churn and a billow, and three colours through its depth.
//
// **How it reaches the GPU without a line of new shader, and why that is honest rather than a
// dodge.** `shaders/volume.wgsl` already marches exactly this: `vortexShapeAt` with `innerVoid` at
// 0 is a filled disc rather than a ring, with `rim` fading smoothly out past the radius and `wall`
// a Gaussian in Y; with `swirl` and `funnelDepth` at 0 it has no spiral and no throat, which leaves
// a soft-edged bank of billowing medium that drifts. So a fog bank IS the placed volumetric medium
// the engine has, marched with a different set of numbers.
//
// That makes this kind a different **authoring surface** onto one primitive rather than a second
// primitive, and the distinction is the point ADR-500 is making. What an artist gets that a vortex
// with the swirl turned down would not give them is the whole of what a first-class effect is: its
// own Add button, its own vocabulary, its own presets, its own default audio routes, its own
// conformance entry, and a panel with no Throat, Funnel depth or Swirl on it to be confused by.
// A control that does nothing teaches an artist that the system is broken (ADR-421), and nine of a
// vortex's controls do nothing to a fog bank.
//
// **The limit, stated rather than discovered.** The volumetric march has ONE medium slot
// (`AtmosphericFrame::hasVortex`), for the reason ADR-374 measured: the single vortex costs +5.5 ms
// of a 13.5 ms frame, and it is the most expensive term in the scene. So a fog bank and a cosmic
// vortex compete for that slot, and the second one in a scene is counted in `AtmosphericCounts::
// dropped` and reported -- a stated limit, not a silent no-op. A second slot is a deliberate future
// decision with a measurement attached, not an oversight.
//
// **Where its numbers live.** `e.vortex`, aliased on purpose: it is the same medium, so it is the
// same struct and the same block in the saved file (the rows say so with an absolute `/vortex/...`
// JSON path). The nine controls a bank has no use for are simply not declared here -- which means
// the panel does not draw them, registration does not produce them, and a route cannot aim at them.

#include "world/world_effects/effect_registry.hpp"

#include <array>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = AtmosphericEffect;

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }

constexpr EffectField kFields[] = {
    // ADR-374: this is EXTINCTION per metre. It is the quantity the march integrates, and the
    // coefficients the look was tuned against are per metre too -- changing the unit here would
    // invalidate them, which is the general form ADR-379/381/389 keep restating.
    floatField("fogDensity", "Fog density", 0.0f, 8.0f, 0.0f, 0.01f, GET(e.vortex.density),
               SETF(e.vortex.density)).json("/vortex/density").fmt("%.4f /m").main()
        .tooltip("How much of what is behind the bank it hides, per metre travelled.\n"
                 "This is thickness, not brightness -- Self glow below is the light."),
    floatField("bankRadius", "Bank radius", 0.0f, 20000.0f, 0.0f, 3000.0f, GET(e.vortex.radius),
               SETF(e.vortex.radius)).json("/vortex/radius").fmt("%.0f m").main()
        .tooltip("How far the bank reaches from its centre. 0 switches it off, and that is\n"
                 "the default: a volumetric medium costs the march real time, so it is opt-in."),
    floatField("bankHeight", "Bank height", 0.1f, 5000.0f, 10.0f, 1200.0f, GET(e.vortex.thickness),
               SETF(e.vortex.thickness)).json("/vortex/thickness").fmt("%.0f m").main()
        .tooltip("The vertical half-extent, as a soft Gaussian rather than a slab with a lid --\n"
                 "so there is no edge anywhere for a hard line to live on."),
    colorField("fogColor", "Fog colour", GET(e.vortex.colorMid), SETC(e.vortex.colorMid))
        .json("/vortex/colorMid").main(),
    colorField("deepColor", "Deep colour", GET(e.vortex.colorDeep), SETC(e.vortex.colorDeep))
        .json("/vortex/colorDeep").main()
        .tooltip("What the thinnest part of the bank reads as. The bank ramps from this,\n"
                 "through Fog colour, to the accent in its densest folds."),
    colorField("glowColor", "Glow colour", GET(e.vortex.colorAccent), SETC(e.vortex.colorAccent))
        .json("/vortex/colorAccent").main(),
    floatField("selfGlow", "Self glow", 0.0f, 20.0f, 0.0f, 0.2f, GET(e.vortex.emission),
               SETF(e.vortex.emission)).json("/vortex/emission").fmt("%.3f /m").main()
        .tooltip("Emissive density per metre: the bank's own light. Bioluminescent mist glows;\n"
                 "ordinary fog does not, so 0 is a real setting here rather than a floor."),
    floatField("billow", "Billow", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.smokeBillow),
               SETF(e.vortex.smokeBillow)).json("/vortex/smokeBillow").main()
        .tooltip("0 is a thin wispy haze; 1 is rounded masses with creases between them.\n"
                 "The difference between a mist and a bank of cloud."),
    floatField("drift", "Drift", -4.0f, 4.0f, -0.2f, 0.2f, GET(e.vortex.rotationSpeed),
               SETF(e.vortex.rotationSpeed)).json("/vortex/rotationSpeed").main()
        .tooltip("How fast the whole bank turns over. Slow: fog that moves quickly reads as\n"
                 "smoke, and the eye notices the motion instead of the place."),

    // ---- Advanced
    floatField("churn", "Churn", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.vortex.smokeWarp),
               SETF(e.vortex.smokeWarp)).sec("Structure").json("/vortex/smokeWarp")
        .tooltip("ADR-389: drags the fine detail through the coarse flow, so the detail is\n"
                 "carried BY the structure rather than sitting ON it. The one control that\n"
                 "turns noise into something that looks like moving air."),
    floatField("wisps", "Wisps", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.turbulence),
               SETF(e.vortex.turbulence)).json("/vortex/turbulence"),
    floatField("wispScale", "Wisp scale", 0.001f, 40.0f, 0.1f, 8.0f, GET(e.vortex.turbulenceScale),
               SETF(e.vortex.turbulenceScale)).json("/vortex/turbulenceScale"),
    floatField("fineDetail", "Fine detail", 0.0f, 2.0f, 0.0f, 1.0f, GET(e.vortex.detail),
               SETF(e.vortex.detail)).json("/vortex/detail"),
    floatField("threads", "Threads", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.vortex.filaments),
               SETF(e.vortex.filaments)).json("/vortex/filaments")
        .tooltip("Bright luminous threads in the densest folds, in Glow colour. 0 for\n"
                 "ordinary fog; this is what makes a bank read as alive."),
    floatField("contrast", "Contrast", 0.05f, 12.0f, 0.5f, 5.0f, GET(e.vortex.contrast),
               SETF(e.vortex.contrast)).json("/vortex/contrast")
        .tooltip("Low is an even wash; high separates the bank into distinct masses with\n"
                 "clear air between them."),
    floatField("swell", "Swell", 0.0f, 1.0f, 0.0f, 0.3f, GET(e.vortex.breathAmount),
               SETF(e.vortex.breathAmount)).json("/vortex/breathAmount").sec("Breathing")
        .tooltip("The bank widens and narrows slowly. Applied to the RADIUS rather than the\n"
                 "density, so the silhouette moves -- scaling density alone just pulses it."),
    floatField("swellSpeed", "Swell speed", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.breathSpeed),
               SETF(e.vortex.breathSpeed)).json("/vortex/breathSpeed"),

    // ADR-388's measured range, unchanged from the vortex's: laddered on the shipped Tree of Life,
    // nearly linear and usable across the whole of 0..1, with a hard ceiling of 4 because a route
    // driven from a drop is entitled to overshoot the slider on purpose.
    floatField("scattering", "Scene scattering", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.scattering),
               SETF(e.vortex.scattering)).json("/vortex/scattering").sec("Light")
        .tooltip("How much of the SCENE's light this fog scatters. Unlike a cosmic vortex a\n"
                 "fog bank is usually LIT rather than self-luminous, so this is the control\n"
                 "that matters here -- a spotlight's beam reads inside the bank."),
    floatField("spill", "Light spill", 0.0f, 20.0f, 0.0f, 6.0f, GET(e.vortex.spill),
               SETF(e.vortex.spill)).json("/vortex/spill")
        .tooltip("Surface irradiance on what stands in the bank (ADR-379)."),

    floatField("centerX", "Centre X", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.vortex.center.x),
               SETF(e.vortex.center.x)).json("/vortex/center/0").fmt("%.0f m").sec("Placement"),
    floatField("centerY", "Centre Y", -1e5f, 1e5f, -500.0f, 1000.0f, GET(e.vortex.center.y),
               SETF(e.vortex.center.y)).json("/vortex/center/1").fmt("%.0f m"),
    floatField("centerZ", "Centre Z", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.vortex.center.z),
               SETF(e.vortex.center.z)).json("/vortex/center/2").fmt("%.0f m"),
};

#undef GET
#undef SETF
#undef SETC

// ---- presets ------------------------------------------------------------------------------------

constexpr std::array<std::string_view, 3> kStyleNames{"Valley Mist", "Glowmere Haze", "Dense Bank"};

// Every style writes every field it touches -- including, and this is the part that matters for a
// kind sharing a struct with the vortex, the nine a bank has no use for. A fog preset applied to an
// effect that was a vortex a moment ago must not leave a spiral and a throat behind, and a preset
// that only set what it wanted would.
//
// `center` is deliberately not written: where the bank is in the world is a placement decision the
// scene made, and a preset that moved it would silently unanchor it (ADR-387's rule for vortices).
void applyStyle(AtmosphericEffect& e, std::string_view style) {
    Vortex& v = e.vortex;
    const glm::vec3 keep = v.center;
    v = Vortex{};
    v.center = keep;
    // What makes it a bank rather than a funnel, in four numbers.
    v.swirl = 0.0f;        // no spiral
    v.funnelDepth = 0.0f;  // no throat descending below the mouth
    v.throat = 1.0f;       // ...and the mouth does not narrow
    v.throatDensity = 0.0f;
    v.innerVoid = 0.0f;    // filled, not a ring
    v.cometResponse = 0.0f;

    if (style == kStyleNames[0]) { // Valley Mist -- thin, wide, low, barely lit
        v.radius = 1400.0f;
        v.thickness = 90.0f;
        v.density = 0.0016f;
        v.emission = 0.004f;
        v.contrast = 1.5f;
        v.turbulence = 0.35f;
        v.turbulenceScale = 1.1f;
        v.smokeWarp = 0.9f;
        v.smokeBillow = 0.35f;
        v.detail = 0.25f;
        v.filaments = 0.0f;
        v.rotationSpeed = 0.012f;
        v.breathAmount = 0.06f;
        v.breathSpeed = 0.09f;
        v.scattering = 0.6f;
        v.spill = 0.8f;
        v.colorDeep = {0.050f, 0.062f, 0.085f};
        v.colorMid = {0.140f, 0.170f, 0.210f};
        v.colorAccent = {0.230f, 0.270f, 0.320f};
    } else if (style == kStyleNames[1]) { // Glowmere Haze -- bioluminescent, threaded, self-lit
        v.radius = 900.0f;
        v.thickness = 160.0f;
        v.density = 0.0021f;
        v.emission = 0.030f;
        v.contrast = 2.6f;
        v.turbulence = 0.55f;
        v.turbulenceScale = 1.8f;
        v.smokeWarp = 1.4f;
        v.smokeBillow = 0.55f;
        v.detail = 0.35f;
        v.filaments = 1.4f;
        v.rotationSpeed = 0.020f;
        v.breathAmount = 0.10f;
        v.breathSpeed = 0.14f;
        v.scattering = 0.35f;
        v.spill = 2.0f;
        v.colorDeep = {0.014f, 0.040f, 0.048f};
        v.colorMid = {0.040f, 0.150f, 0.145f};
        v.colorAccent = {0.110f, 0.360f, 0.300f};
    } else { // Dense Bank -- thick, tall, billowing, opaque
        v.radius = 650.0f;
        v.thickness = 320.0f;
        v.density = 0.0055f;
        v.emission = 0.010f;
        v.contrast = 3.4f;
        v.turbulence = 0.70f;
        v.turbulenceScale = 2.6f;
        v.smokeWarp = 2.0f;
        v.smokeBillow = 0.90f;
        v.detail = 0.45f;
        v.filaments = 0.3f;
        v.rotationSpeed = 0.030f;
        v.breathAmount = 0.12f;
        v.breathSpeed = 0.16f;
        v.scattering = 0.8f;
        v.spill = 1.4f;
        v.colorDeep = {0.060f, 0.058f, 0.070f};
        v.colorMid = {0.190f, 0.185f, 0.205f};
        v.colorAccent = {0.330f, 0.320f, 0.350f};
    }
    e.kind = AtmosphereKind::VolumetricFog;
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

// The depths are fractions of each leaf's soft range, and every one is small next to the value it
// moves: `Add` so silence leaves the authored pose exactly as it was written, which is the rule the
// whole family follows and the reason an unplayed project does not look wrong.
//
// Bass to density is the one that reads: a bank that thickens on the low end is weather answering
// the music. Swell on the bass is the same answer in the silhouette rather than in the opacity.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "fogDensity", 0.0018f, 90.0f, 620.0f},
    {"audio.bass", "swell", 0.060f, 110.0f, 700.0f},
    {"audio.mid", "churn", 0.400f, 60.0f, 420.0f},
    {"audio.treble", "threads", 0.300f, 30.0f, 260.0f},
    {"beat.pulse", "selfGlow", 0.020f, 12.0f, 300.0f},
};

AtmosphericEffect make(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::VolumetricFog;
    applyStyle(e, kStyleNames[0]);
    // Weather, not an event: it is there, and the music moves it. The fade-in is long because a
    // bank that appears is a cut and a bank that gathers is weather.
    e.activation = Activation::Always;
    e.timing.fadeIn = 3.0;
    e.timing.fadeOut = 0.0;
    // No ground pool. A bank lights what stands IN it, through `spill` and `scattering`; ADR-230's
    // ground glow is a coloured patch on terrain from something in the sky, which this is not.
    e.ground.mode = GroundGlow::Off;
    return e;
}

// A bank has no per-frame trajectory: it is a static field the march samples. Its anchor is its own
// centre, so "what is the air doing where this bank stands" is asked at the bank -- which is what
// lets a fog bank and a comet subscribe to one wind and agree without anybody matching two numbers.
bool fill(const AtmosphericEffect& e, std::size_t, const AtmosphericContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    r.anchor = e.vortex.center;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const AtmosphericEffect& e) { return e.vortex.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = AtmosphereKind::VolumetricFog;
    s.key = "fog";
    s.enumName = "VolumetricFog";
    s.displayName = "Volumetric Fog";
    s.addLabel = "Add fog bank";
    s.addTip = "A placed bank of medium with a soft rim and a billowing interior: mist in a\n"
               "valley, or a glowing haze around an island. It is drawn by the volumetric\n"
               "march, so terrain and objects sit INSIDE it rather than in front of it.\n"
               "Shares the march's one medium slot with a cosmic vortex.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "selfGlow";
    s.groundGlow = false;
    s.factory = make;
    s.resolve.bucket = EffectBucket::Vortex;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& volumetricFogSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
