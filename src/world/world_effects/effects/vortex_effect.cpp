// The cosmic vortex (ADR-387, ported to the ADR-500 registry).
//
// Everything a vortex is, is in this file. It is the one kind in the family that is *placed in the
// world* rather than on the sky dome, and it is drawn by the volumetric march in
// `shaders/volume.wgsl` rather than by `shaders/atmosphere_fx.wgsl` -- which ADR-387 records as a
// rendering detail: this family is about authoring, not about which pass rasterises the result.
//
// Two units are load-bearing and are not negotiable (ADR-374): `density` is extinction PER METRE
// and `emission` is emissive density PER METRE. They are two knobs because they are two physical
// quantities, and labelling them "opacity" and "glow" is how they got confused in the first place.

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

// The rows, in the order and with the labels, formats and tooltips the World Effects panel shipped
// with -- carried across from `ui_logic.hpp`'s `vortexRows` / `vortexAdvancedRows` unchanged, so
// the panel an artist opens tomorrow is the panel they closed today. `density` is extinction and
// `emission` is light (ADR-374 is the whole reason they are two knobs), so they are labelled as
// two different things rather than as "opacity" and "glow", which is how they got confused.
constexpr EffectField kFields[] = {
    colorField("colorDeep", "Deep colour", GET(e.vortex.colorDeep), SETC(e.vortex.colorDeep)).main(),
    colorField("colorMid", "Mid colour", GET(e.vortex.colorMid), SETC(e.vortex.colorMid)).main(),
    colorField("colorAccent", "Accent colour", GET(e.vortex.colorAccent), SETC(e.vortex.colorAccent)).main(),
    floatField("emission", "Brightness", 0.0f, 20.0f, 0.0f, 0.2f, GET(e.vortex.emission),
               SETF(e.vortex.emission)).fmt("%.3f /m").main(),
    floatField("density", "Thickness", 0.0f, 8.0f, 0.0f, 0.01f, GET(e.vortex.density),
               SETF(e.vortex.density)).fmt("%.4f /m").main(),
    floatField("radius", "Mouth radius", 0.0f, 20000.0f, 0.0f, 1500.0f, GET(e.vortex.radius),
               SETF(e.vortex.radius)).fmt("%.0f m").log().main(),
    floatField("funnelDepth", "Funnel depth", 0.0f, 20000.0f, 0.0f, 3000.0f, GET(e.vortex.funnelDepth),
               SETF(e.vortex.funnelDepth)).fmt("%.0f m").log().main(),
    floatField("rotationSpeed", "Rotation", -4.0f, 4.0f, -0.4f, 0.4f, GET(e.vortex.rotationSpeed),
               SETF(e.vortex.rotationSpeed)).fmt("%.3f rad/s").main(),
    floatField("swirl", "Swirl", -32.0f, 32.0f, -8.0f, 8.0f, GET(e.vortex.swirl),
               SETF(e.vortex.swirl)).main(),
    floatField("filaments", "Filaments", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.vortex.filaments),
               SETF(e.vortex.filaments)).main(),
    // ADR-389, the smoke controls. Soft ranges are the whole of the useful span in each case,
    // because a modulation route clamps to the HARD range and a slider whose interesting region is
    // in its first hair is the `scene/windSpeed` defect.
    floatField("smokeWarp", "Smoke", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.vortex.smokeWarp),
               SETF(e.vortex.smokeWarp)).main()
        .tooltip("Drags the fine detail into the big swirl instead of letting it sit on top as speckle.\n"
                 "The single control that decides whether this reads as smoke or as noise -- raise it\n"
                 "first, before reaching for anything else here."),
    floatField("smokeBillow", "Billow", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.smokeBillow),
               SETF(e.vortex.smokeBillow)).main()
        .tooltip("0 is wispy and filamentary; 1 is rounded, puffy masses with creases between them.\n"
                 "The difference between a nebula and a smoke column."),
    floatField("detail", "Fine detail", 0.0f, 2.0f, 0.0f, 1.0f, GET(e.vortex.detail),
               SETF(e.vortex.detail)).main()
        .tooltip("Weight of the finest noise octave. Detail below what the volume march can sample is\n"
                 "faded out automatically, so raising this past the point where it stops changing the\n"
                 "picture means the march is the limit, not this."),
    floatField("spill", "Light spill", 0.0f, 20.0f, 0.0f, 6.0f, GET(e.vortex.spill),
               SETF(e.vortex.spill)).main()
        .tooltip("How much of the funnel's own light lands on the surfaces above it. Separate from\n"
                 "Brightness so it can be tuned against the island without changing the funnel."),
    // ADR-388, and the range is a measurement rather than a guess. Laddered on the shipped Tree of
    // Life at t=6, mean frame luminance of 255: 0.00 -> 65.5, 0.05 -> 66.4, 0.20 -> 68.9,
    // 0.50 -> 73.4, 1.00 -> 80.3. Nearly linear and usable across the whole of 0..1, so 0..1 IS the
    // soft range; the hard maximum is 4 because a route driven from a drop may overshoot on purpose.
    floatField("scattering", "Scene light inside", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.scattering),
               SETF(e.vortex.scattering)).main()
        .tooltip("At 0 the funnel makes its own light and the scene's lights do not appear inside it --\n"
                 "a spotlight aimed up through it stops at its edge. Above 0 they do; watch the tree's\n"
                 "key light, which is bright enough to flatten the whole funnel if this goes far."),

    // ---- Advanced.
    //
    // The centre is three parameters and one `"center": [x, y, z]` in the file -- a numeric JSON
    // segment indexes the array, which is how the registry serves a vec3 the file writes as one key
    // and the parameter system must see as three automatable numbers.
    floatField("centerX", "Centre X", -1e5f, 1e5f, -500.0f, 500.0f, GET(e.vortex.center.x),
               SETF(e.vortex.center.x)).json("center/0").fmt("%.1f m").sec("Placement"),
    floatField("centerY", "Centre Y", -1e5f, 1e5f, -2000.0f, 500.0f, GET(e.vortex.center.y),
               SETF(e.vortex.center.y)).json("center/1").fmt("%.1f m"),
    floatField("centerZ", "Centre Z", -1e5f, 1e5f, -500.0f, 500.0f, GET(e.vortex.center.z),
               SETF(e.vortex.center.z)).json("center/2").fmt("%.1f m")
        .tooltip("Where the mouth of the funnel sits in the world. A particle system that names this\n"
                 "vortex as its attractor follows it here, so the island and the funnel stay related\n"
                 "when either of them moves."),
    floatField("thickness", "Wall thickness", 0.1f, 5000.0f, 5.0f, 600.0f, GET(e.vortex.thickness),
               SETF(e.vortex.thickness)).fmt("%.0f m").log().sec("Shape"),
    floatField("throat", "Throat", 0.02f, 1.0f, 0.05f, 1.0f, GET(e.vortex.throat),
               SETF(e.vortex.throat)).fmt("%.2f of mouth"),
    floatField("throatDensity", "Throat thickness", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.throatDensity),
               SETF(e.vortex.throatDensity)),
    floatField("innerVoid", "Inner void", 0.0f, 0.95f, 0.0f, 0.6f, GET(e.vortex.innerVoid),
               SETF(e.vortex.innerVoid)),
    floatField("contrast", "Contrast", 0.05f, 12.0f, 0.5f, 5.0f, GET(e.vortex.contrast),
               SETF(e.vortex.contrast)),
    floatField("turbulence", "Turbulence", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.turbulence),
               SETF(e.vortex.turbulence)).sec("Motion"),
    floatField("turbulenceScale", "Turbulence scale", 0.001f, 40.0f, 0.1f, 8.0f,
               GET(e.vortex.turbulenceScale), SETF(e.vortex.turbulenceScale)),
    floatField("breathAmount", "Breath amount", 0.0f, 1.0f, 0.0f, 0.3f, GET(e.vortex.breathAmount),
               SETF(e.vortex.breathAmount))
        .tooltip("Applied to the RADIUS rather than to the density, so the silhouette moves.\n"
                 "Scaling density alone just pulses the brightness."),
    floatField("breathSpeed", "Breath speed", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.breathSpeed),
               SETF(e.vortex.breathSpeed)),
    floatField("cometResponse", "Comet light", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.vortex.cometResponse),
               SETF(e.vortex.cometResponse)).sec("Comet response"),
    floatField("cometReach", "Comet reach", 1.0f, 40.0f, 1.0f, 12.0f, GET(e.vortex.cometReach),
               SETF(e.vortex.cometReach)).fmt("%.1f x")
        .tooltip("How much of a comet's light this medium takes, and how far past the comet's ground\n"
                 "pool it reaches. Off by default: the funnel must not scatter the scene's ordinary\n"
                 "lights, which is what the control above is for."),
};

#undef GET
#undef SETF
#undef SETC

// ADR-387. "Cosmic Funnel" is the Tree of Life's shipped funnel, value for value: a preset that
// only approximates the scene it was taken from is a preset that drifts from it. The other two are
// the two directions that turned out to read -- a shallow disc seen from above, and a deeper,
// denser throat.
constexpr std::array<std::string_view, 3> kStyleNames{"Cosmic Funnel", "Shallow Disc",
                                                      "Deep Maelstrom"};

bool applyStyle(AtmosphericEffect& e, std::string_view style) {
    Vortex& v = e.vortex;
    // Every style writes every field it touches, for the reason the comet's does: a style that
    // leaves the previous one's throat behind reads as the preset being broken.
    // `center` is deliberately not written -- see the header.
    const glm::vec3 keep = v.center;
    v = Vortex{};
    v.center = keep;
    if (style == kStyleNames[0]) { // Cosmic Funnel -- the Tree of Life's
        v.radius = 200.0f;
        v.thickness = 70.0f;
        v.funnelDepth = 1500.0f;
        v.throat = 0.1f;
        v.throatDensity = 0.55f;
        v.swirl = 6.5f;
        v.rotationSpeed = 0.028f;
        v.density = 0.0013f;
        v.innerVoid = 0.24f;
        v.contrast = 3.6f;
        v.turbulence = 0.65f;
        v.turbulenceScale = 2.4f;
        v.breathAmount = 0.05f;
        v.breathSpeed = 0.18f;
        v.emission = 0.04f;
        v.filaments = 2.2f;
        v.spill = 2.5f;
        v.cometResponse = 0.6f;
        v.cometReach = 6.0f;
        v.colorDeep = {0.016f, 0.012f, 0.062f};
        v.colorMid = {0.050f, 0.085f, 0.240f};
        v.colorAccent = {0.100f, 0.340f, 0.460f};
        return true;
    }
    if (style == kStyleNames[1]) { // Shallow Disc
        v.radius = 260.0f;
        v.thickness = 45.0f;
        v.funnelDepth = 0.0f;
        v.throat = 0.25f;
        v.throatDensity = 0.6f;
        v.swirl = 4.2f;
        v.rotationSpeed = 0.045f;
        v.density = 0.0016f;
        v.innerVoid = 0.30f;
        v.contrast = 2.6f;
        v.turbulence = 0.45f;
        v.turbulenceScale = 2.0f;
        v.breathAmount = 0.06f;
        v.breathSpeed = 0.22f;
        v.emission = 0.05f;
        v.filaments = 1.6f;
        v.spill = 2.0f;
        v.cometResponse = 0.4f;
        v.cometReach = 6.0f;
        v.colorDeep = {0.014f, 0.018f, 0.055f};
        v.colorMid = {0.045f, 0.100f, 0.210f};
        v.colorAccent = {0.120f, 0.380f, 0.420f};
        return true;
    }
    if (style == kStyleNames[2]) { // Deep Maelstrom
        v.radius = 170.0f;
        v.thickness = 95.0f;
        v.funnelDepth = 2600.0f;
        v.throat = 0.06f;
        v.throatDensity = 0.75f;
        v.swirl = 9.0f;
        v.rotationSpeed = 0.020f;
        v.density = 0.0018f;
        v.innerVoid = 0.18f;
        v.contrast = 4.4f;
        v.turbulence = 0.80f;
        v.turbulenceScale = 3.0f;
        v.breathAmount = 0.04f;
        v.breathSpeed = 0.14f;
        v.emission = 0.055f;
        v.filaments = 2.8f;
        v.spill = 3.2f;
        v.cometResponse = 0.8f;
        v.cometReach = 7.0f;
        v.colorDeep = {0.022f, 0.010f, 0.070f};
        v.colorMid = {0.060f, 0.070f, 0.260f};
        v.colorAccent = {0.140f, 0.300f, 0.520f};
        return true;
    }
    return false;
}

void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0},
    {kStyleNames[1].data(), style1},
    {kStyleNames[2].data(), style2},
};

// ADR-392 records the five routes the shipped Tree of Life project authors on its funnel -- bass to
// density and breath, mid to turbulence, treble to filaments -- and those are the mapping here,
// because a default taken from the one scene that has tuned a vortex is evidence and a default
// invented for the function is not.
//
// The one departure: the shipped project drives `emission` from `time.progress`, which ramps the
// funnel's brightness across the song. That is a composition decision rather than a property of
// vortices, so the brightness route here is `beat.pulse` -- the same leaf `beatLeaf` names, so the
// Beat response slider finds this route instead of writing a second one beside it.
//
// Every depth is a fraction of the leaf's soft range: density's soft range ends at 0.01 /m and
// emission's at 0.2, which is why these numbers look small next to an aurora's.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "density", 0.0020f, 70.0f, 450.0f},
    {"audio.bass", "breathAmount", 0.080f, 90.0f, 520.0f},
    {"audio.mid", "turbulence", 0.150f, 45.0f, 340.0f},
    {"audio.treble", "filaments", 0.400f, 25.0f, 240.0f},
    {"beat.pulse", "emission", 0.030f, 10.0f, 260.0f},
};

AtmosphericEffect make(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::Vortex;
    e.style = std::string(kStyleNames[0]);
    applyStyle(e, kStyleNames[0]);
    // A vortex is scenery, not an event, and it has no ground pool of its own: ADR-379's spill is
    // how it lights what floats over it, and that is a field on the vortex rather than ADR-230's
    // ground glow, because the glow lands on terrain and there is no terrain under this one.
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.ground.mode = GroundGlow::Off;
    return e;
}

// ADR-387: a vortex has no per-frame trajectory to resolve -- it is a static field the volume march
// samples -- so resolution is "is it live", and the payload travels unchanged. §68: its anchor is
// its own centre, because the question "what is the air doing where this funnel stands" is asked at
// the funnel.
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
    s.kind = AtmosphereKind::Vortex;
    
    s.key = "vortex";
    s.enumName = "Vortex";
    s.displayName = "Cosmic Vortex";
    s.addLabel = "Add vortex";
    s.addTip = "A turning funnel of luminous medium, drawn inside the volumetric march.\n"
               "It is placed in the world rather than on the sky dome, and it is the\n"
               "only atmospheric that a particle system can name as its attractor.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "emission";
    // ADR-387: no ground glow. The pool is a patch on terrain and the vortex is under the island
    // with no terrain beneath it; its light on the world is `spill`. A combo that changed nothing
    // would be worse than no combo.
    s.groundGlow = false;
    s.factory = make;
    s.resolve.bucket = EffectBucket::Vortex;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& vortexSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
