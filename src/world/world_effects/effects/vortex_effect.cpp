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

#include "core/vortex.hpp"
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
    floatField("radius", "Mouth radius", 0.0f, 20000.0f, 0.0f, 1500.0f, GET(e.vortex.field.radius),
               SETF(e.vortex.field.radius)).fmt("%.0f m").log().main(),
    floatField("funnelDepth", "Funnel depth", 0.0f, 20000.0f, 0.0f, 3000.0f, GET(e.vortex.field.funnelDepth),
               SETF(e.vortex.field.funnelDepth)).fmt("%.0f m").log().main(),
    floatField("rotationSpeed", "Rotation", -4.0f, 4.0f, -0.4f, 0.4f, GET(e.vortex.field.rotationSpeed),
               SETF(e.vortex.field.rotationSpeed)).fmt("%.3f rad/s").main(),
    floatField("swirl", "Swirl", -32.0f, 32.0f, -8.0f, 8.0f, GET(e.vortex.field.swirl),
               SETF(e.vortex.field.swirl)).main(),
    floatField("filaments", "Filaments", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.vortex.filaments),
               SETF(e.vortex.filaments)).main(),
    // Vortex 2.0 §7-§11 (ADR-461), carried onto the registry unchanged. Its own section because
    // it is a different KIND of control from the ones around it: these shape the storm, the smoke
    // rows below texture it. The owner's rule is that if it is visible in the picture an artist
    // must be able to find it, and `findable` is not `reachable in principle` -- so the eye and
    // the bands get named rows in the order somebody would reach for them, not one row called
    // "macro". Soft ranges are the usable span and hard ranges are wider, because a modulation
    // route clamps to the HARD range.
    //
    // There is no `eyeRadius`: `innerVoid` IS the eye's radius, and it moves up here from Shape
    // because with a wall around it it is no longer a detail. A second control would have left one
    // of the two doing nothing, which the parity test's reachability probe reported as
    // `changing innerVoid moved 0 of 160 GPU samples` the first time this was written.
    floatField("eyeWallWidth", "Eye wall width", 0.005f, 0.6f, 0.02f, 0.4f,
               GET(e.vortex.field.eyeWallWidth), SETF(e.vortex.field.eyeWallWidth)).main().sec("Cyclone structure")
        .tooltip("How far, as a fraction of the mouth radius, the density takes to climb out of the eye.\n"
                 "Narrow reads as a violent storm; wide reads as a slow one."),
    floatField("eyeWallGain", "Eye wall", 0.0f, 8.0f, 0.0f, 3.0f, GET(e.vortex.field.eyeWallGain),
               SETF(e.vortex.field.eyeWallGain)).main()
        .tooltip("How much denser the ring around the eye is than the body of the storm. This is the\n"
                 "control that makes the silhouette read as a cyclone rather than as a hole in a cloud.\n"
                 "It raises the overall thickness of the medium as well, so check Thickness after it."),
    // Arm count is a float because every field in this table is, and because a route sweeping it
    // continuously is a legitimate thing to want -- the band term is a cosine and does not care
    // whether the count is whole.
    floatField("bandArms", "Spiral arms", 0.0f, 24.0f, 0.0f, 8.0f, GET(e.vortex.field.bandArms),
               SETF(e.vortex.field.bandArms)).fmt("%.0f").main()
        .tooltip("How many spiral bands wind out of the eye. 0 switches the bands off and leaves the\n"
                 "envelope smooth, which is what it was before. Two or three reads as a hurricane; more\n"
                 "reads as a galaxy."),
    // Clamped to 2..80 degrees where it is packed: 0 is a circle and 90 is a radial spoke, and
    // neither of those is a band. The hard range says so rather than letting a route find out.
    floatField("bandPitchDegrees", "Arm pitch", 2.0f, 80.0f, 8.0f, 40.0f,
               GET(e.vortex.field.bandPitchDegrees), SETF(e.vortex.field.bandPitchDegrees)).fmt("%.0f deg").main()
        .tooltip("How tightly the arms wind: the angle an arm makes with the circle it crosses. Real\n"
                 "rainbands run 10 to 25 degrees. Small is tightly coiled, large is nearly radial."),
    floatField("bandDepth", "Arm contrast", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.bandDepth),
               SETF(e.vortex.field.bandDepth)).main()
        .tooltip("How much denser a band is than the gap beside it. The bands cost nothing to sample and\n"
                 "cannot alias, so this is the control to reach for before Filaments or Fine detail."),
    floatField("bandHarmonic", "Arm detail", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.bandHarmonic),
               SETF(e.vortex.field.bandHarmonic)).main()
        .tooltip("Adds two finer sets of arms inside the main ones, at a third and a ninth of the\n"
                 "contrast. Structure rather than noise: it survives freezing time and going monochrome."),
    // §53's failure test as a control. The whole of 0..1 is usable and 0 is the diagnostic §5 asks
    // to be shown, so the soft range is the hard range.
    floatField("cloudNoise", "Cloud noise", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.cloudNoise),
               SETF(e.vortex.field.cloudNoise)).main()
        .tooltip("The weight of the whole noise stack against a smooth medium. At 0 you see the cyclone's\n"
                 "structure alone -- eye, wall, arms, funnel -- with no detail on it at all. That render\n"
                 "is the test: if it is not already impressive at 0, no amount of detail will save it."),
    // ADR-389, the smoke controls. Soft ranges are the whole of the useful span in each case,
    // because a modulation route clamps to the HARD range and a slider whose interesting region is
    // in its first hair is the `scene/windSpeed` defect.
    floatField("smokeWarp", "Smoke", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.vortex.field.smokeWarp),
               SETF(e.vortex.field.smokeWarp)).main()
        .tooltip("Drags the fine detail into the big swirl instead of letting it sit on top as speckle.\n"
                 "The single control that decides whether this reads as smoke or as noise -- raise it\n"
                 "first, before reaching for anything else here."),
    floatField("smokeBillow", "Billow", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.smokeBillow),
               SETF(e.vortex.field.smokeBillow)).main()
        .tooltip("0 is wispy and filamentary; 1 is rounded, puffy masses with creases between them.\n"
                 "The difference between a nebula and a smoke column."),
    floatField("detail", "Fine detail", 0.0f, 2.0f, 0.0f, 1.0f, GET(e.vortex.field.detail),
               SETF(e.vortex.field.detail)).main()
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
    floatField("centerX", "Centre X", -1e5f, 1e5f, -500.0f, 500.0f, GET(e.vortex.field.center.x),
               SETF(e.vortex.field.center.x)).json("center/0").fmt("%.1f m").sec("Placement"),
    floatField("centerY", "Centre Y", -1e5f, 1e5f, -2000.0f, 500.0f, GET(e.vortex.field.center.y),
               SETF(e.vortex.field.center.y)).json("center/1").fmt("%.1f m"),
    floatField("centerZ", "Centre Z", -1e5f, 1e5f, -500.0f, 500.0f, GET(e.vortex.field.center.z),
               SETF(e.vortex.field.center.z)).json("center/2").fmt("%.1f m")
        .tooltip("Where the mouth of the funnel sits in the world. A particle system that names this\n"
                 "vortex as its attractor follows it here, so the island and the funnel stay related\n"
                 "when either of them moves."),
    floatField("thickness", "Wall thickness", 0.1f, 5000.0f, 5.0f, 600.0f, GET(e.vortex.field.thickness),
               SETF(e.vortex.field.thickness)).fmt("%.0f m").log().sec("Shape"),
    floatField("throat", "Throat", 0.02f, 1.0f, 0.05f, 1.0f, GET(e.vortex.field.throat),
               SETF(e.vortex.field.throat)).fmt("%.2f of mouth"),
    floatField("throatDensity", "Throat thickness", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.throatDensity),
               SETF(e.vortex.field.throatDensity)),
    floatField("innerVoid", "Eye radius", 0.0f, 0.95f, 0.0f, 0.6f, GET(e.vortex.field.innerVoid),
               SETF(e.vortex.field.innerVoid)).main().sec("Cyclone structure")
        .tooltip("The clear centre of the storm, as a fraction of the mouth radius. This is the same\n"
                 "number the Advanced section used to call Inner void; it is here because with a wall\n"
                 "around it it is no longer a detail, it is the shape of the thing."),
    floatField("contrast", "Contrast", 0.05f, 12.0f, 0.5f, 5.0f, GET(e.vortex.field.contrast),
               SETF(e.vortex.field.contrast)),
    floatField("turbulence", "Turbulence", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.turbulence),
               SETF(e.vortex.field.turbulence)).sec("Motion"),
    floatField("turbulenceScale", "Turbulence scale", 0.001f, 40.0f, 0.1f, 8.0f,
               GET(e.vortex.field.turbulenceScale), SETF(e.vortex.field.turbulenceScale)),
    floatField("breathAmount", "Breath amount", 0.0f, 1.0f, 0.0f, 0.3f, GET(e.vortex.field.breathAmount),
               SETF(e.vortex.field.breathAmount))
        .tooltip("Applied to the RADIUS rather than to the density, so the silhouette moves.\n"
                 "Scaling density alone just pulses the brightness."),
    floatField("breathSpeed", "Breath speed", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.field.breathSpeed),
               SETF(e.vortex.field.breathSpeed)),
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
    const glm::vec3 keep = v.field.center;
    v = Vortex{};
    v.field.center = keep;
    if (style == kStyleNames[0]) { // Cosmic Funnel -- the Tree of Life's
        v.field.radius = 200.0f;
        v.field.thickness = 70.0f;
        v.field.funnelDepth = 1500.0f;
        v.field.throat = 0.1f;
        v.field.throatDensity = 0.55f;
        v.field.swirl = 6.5f;
        v.field.rotationSpeed = 0.028f;
        v.density = 0.0013f;
        v.field.innerVoid = 0.24f;
        v.field.contrast = 3.6f;
        v.field.turbulence = 0.65f;
        v.field.turbulenceScale = 2.4f;
        v.field.breathAmount = 0.05f;
        v.field.breathSpeed = 0.18f;
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
        v.field.radius = 260.0f;
        v.field.thickness = 45.0f;
        v.field.funnelDepth = 0.0f;
        v.field.throat = 0.25f;
        v.field.throatDensity = 0.6f;
        v.field.swirl = 4.2f;
        v.field.rotationSpeed = 0.045f;
        v.density = 0.0016f;
        v.field.innerVoid = 0.30f;
        v.field.contrast = 2.6f;
        v.field.turbulence = 0.45f;
        v.field.turbulenceScale = 2.0f;
        v.field.breathAmount = 0.06f;
        v.field.breathSpeed = 0.22f;
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
        v.field.radius = 170.0f;
        v.field.thickness = 95.0f;
        v.field.funnelDepth = 2600.0f;
        v.field.throat = 0.06f;
        v.field.throatDensity = 0.75f;
        v.field.swirl = 9.0f;
        v.field.rotationSpeed = 0.020f;
        v.density = 0.0018f;
        v.field.innerVoid = 0.18f;
        v.field.contrast = 4.4f;
        v.field.turbulence = 0.80f;
        v.field.turbulenceScale = 3.0f;
        v.field.breathAmount = 0.04f;
        v.field.breathSpeed = 0.14f;
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
    r.anchor = e.vortex.field.center;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const AtmosphericEffect& e) { return e.vortex.validate(); }

// ADR-562: the authored numbers as the sixteen lanes the march reads.
//
// THE LANE MAP, and it is the contract. `shaders/volume.wgsl` reads these by index and
// `shaders/vortex.wgsl` names them `v0`..`v8`; the two must agree and this comment is where they
// are held against each other.
//
//   0  centre.xyz, radius (0 is off, and it is the gate)
//   1  thickness, swirl, rotationSpeed, DENSITY (extinction per metre)
//   2  innerVoid, contrast, turbulence, turbulenceScale
//   3  breathAmount, breathSpeed, EMISSION (per metre), filaments
//   4  funnelDepth, throat, throatDensity, 0
//   5  cometResponse, cometReach, sceneScattering, 0
//   6  smokeWarp, smokeBillow, detail, 0
//   7  eyeWallWidth, eyeWallGain, cloudNoise, 0
//   8  bandArms, cot(bandPitch), bandDepth, bandHarmonic
//   9  deep colour
//  10  mid colour
//  11  luminous accent
//  12  SPILL, 0, 0, 0   -- the thirteenth lane, and it is not decoration
//  13-15 unused (headroom; the tornado fills through 13)
//
// Lane 12 exists because `spill` is a SURFACE irradiance consumed by the lit pass (ADR-379), not a
// per-metre coefficient the march integrates -- so it appears in no lane the march needs, and
// packing the medium without it would have silently broken the vortex's glow on the island above
// it. `scene_renderer.cpp` reads it from here now. It was the one reader of the authored struct
// that the audit found genuinely needed something the twelve march lanes did not carry.
//
// The geometry and motion lanes come from `vortex::packVortex`, which is the ONE packing, so the
// CPU sampler, the shader and this site cannot disagree about the field (ADR-388, and ADR-401 for
// what happens when they can). The appearance lanes are assembled here because they are what the
// picture does with the field rather than part of it.
void packMedium(const E& e, float envelope, MediumSlot& out) {
    const Vortex& v = e.vortex;
    const vortex::VortexUniforms f = vortex::packVortex(v.field);
    out.lane[0] = f.v0;
    // The envelope scales the two PER-METRE coefficients and nothing else (ADR-387): fading a
    // medium means less of it in the air. Fading its colours would leave a full-strength grey
    // ghost; fading its radius would shrink it rather than dim it.
    out.lane[1] = glm::vec4(glm::vec3(f.v1), std::max(v.density, 0.0f) * envelope);
    out.lane[2] = f.v2;
    out.lane[3] = glm::vec4(f.v3.x, f.v3.y, std::max(v.emission, 0.0f) * envelope,
                            std::max(v.filaments, 0.0f));
    out.lane[4] = f.v4;
    out.lane[5] = glm::vec4(std::max(v.cometResponse, 0.0f), std::max(v.cometReach, 1.0f),
                            std::max(v.scattering, 0.0f), 0.0f);
    out.lane[6] = f.v6;
    out.lane[7] = f.v7;
    out.lane[8] = f.v8;
    out.lane[9] = glm::vec4(v.colorDeep, 0.0f);
    out.lane[10] = glm::vec4(v.colorMid, 0.0f);
    out.lane[11] = glm::vec4(v.colorAccent, 0.0f);
    out.lane[12] = glm::vec4(std::max(v.spill, 0.0f), 0.0f, 0.0f, 0.0f);
}

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
    s.resolve.bucket = EffectBucket::Medium;
    s.resolve.pack = packMedium;
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
