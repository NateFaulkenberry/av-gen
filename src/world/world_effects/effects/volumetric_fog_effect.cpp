// The volumetric fog bank (ADR-500's second proof that an effect is one file).
//
// **What it is.** A placed volume of luminous medium sitting in the world -- a drift of mist
// filling a valley, a glowing haze around an island, a ball of fog hanging off a cliff -- with a
// soft rim, an asymmetric height profile and optional low-frequency structure. Authored in fog
// vocabulary: an optical depth, a radius, a height, a shape, and three colours through its depth.
//
// **THIS HEADER WAS WRONG FOR THREE ADRs AND IS BEING FIXED WITH THE FOURTH.** What it said, and
// what is actually true now:
//
//   - *"a density per metre"* -- no. ADR-564: the authored number is an OPTICAL DEPTH through the
//     bank's own crossing path, converted to an extinction in `packMedium`. Per-metre meant a
//     preset could not ship a correct density, because the depth scaled with the radius slider.
//   - *"a Gaussian vertical profile"* -- no. ADR-563 replaced it with a base and an exponential
//     falloff, because `exp(-y^2/t^2)` is a cloud floating in nothing and fog sits ON something.
//     (That stale sentence is not harmless: the march's vertical bound was sized as a Gaussian's
//     three sigma and stayed that way until ADR-566 measured what it was cutting off.)
//   - *"it reaches the GPU without a line of new shader ... a different authoring surface onto one
//     primitive rather than a second primitive"* -- no, and this was the load-bearing one. ADR-563
//     gave the fog its OWN field in `shaders/fog.wgsl`, because the vortex's field with the funnel
//     switched off is monotone in radius and completely uniform in angle: a circular grey disc.
//     Every feature anybody had ever seen in a fog bank came out of the fBM underneath, which is
//     the "procedural texture rendered as a volume" the brief opens by rejecting.
//   - *"the volumetric march has ONE medium slot ... this is the owner's live bug"* -- fixed.
//     ADR-562 gave the march four slots with a per-kind dispatch, and what it cannot seat reaches
//     the panel and the headless log as `mediaDropped` instead of vanishing.
//
// **The rule that comes out of that list**, and it is ADR-385 aimed at a comment rather than at a
// claim in conversation: *a header that describes the architecture is a claim with no test on it.*
// Four sentences here survived the changes that falsified them, and one of them -- the Gaussian --
// was still being relied on by a bound in another file the day after it stopped being true. When
// an ADR changes what a file IS, the file's own first paragraph is part of the change.
//
// **What an artist gets** that a vortex with the swirl turned down would not give them is the
// whole of what a first-class effect is: its own Add button, its own vocabulary, its own presets,
// its own default audio routes, its own conformance entry, six volume primitives (ADR-566), and a
// panel with no Throat, Funnel depth or Swirl on it to be confused by. A control that does nothing
// teaches an artist that the system is broken (ADR-421), and nine of a vortex's controls do
// nothing to a fog bank.
//
// **Where its numbers live.** `e.vortex`, aliased on purpose: it is the same medium, so it is the
// same struct and the same block in the saved file (the rows say so with an absolute `/vortex/...`
// JSON path). The nine controls a bank has no use for are simply not declared here -- which means
// the panel does not draw them, registration does not produce them, and a route cannot aim at them.

#include "core/vortex.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <iterator>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = AtmosphericEffect;

float storedOf(const AtmosphericEffect& e, const char* leaf, float fallback) {
    std::string key = "fog/";
    key.append(leaf);
    return e.values.getFloat(key, fallback);
}

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }

// ADR-566: the names an artist picks from, in `world::FogShape` order. A scene file carries the
// NAME rather than the index (`FieldType::Choice`), so this list is APPEND ONLY -- inserting in
// the middle would change what every saved bank is, silently and in every scene at once.
constexpr const char* const kFogShapeNames[] = {"Bank",    "Sphere",  "Ellipsoid",
                                                "Box",     "Capsule", "Cylinder"};
static_assert(std::size(kFogShapeNames) == static_cast<std::size_t>(kFogShapeCount),
              "the panel's list and world::FogShape must have the same length");

constexpr EffectField kFields[] = {
    // ADR-374: this is EXTINCTION per metre. It is the quantity the march integrates, and the
    // coefficients the look was tuned against are per metre too -- changing the unit here would
    // invalidate them, which is the general form ADR-379/381/389 keep restating.
    // ADR-564 (§24): this is the optical depth THROUGH THE BANK, not an extinction per metre.
    //
    // It used to be per metre, and that made it a control whose meaning depended on the bank's
    // size: measured, one authored 0.0016 gives an optical depth of 0.45 at a 100 m radius and
    // 4.03 at 900 m -- invisible to opaque slab, across a range an artist crosses by dragging the
    // radius slider without touching this one. A preset therefore could not ship a density, because
    // there is no number that is right at two sizes.
    //
    // Normalised by the bank's own long-axis crossing in `packMedium`, so 1.0 means "you can just
    // see through it" whatever the bank's size. `Environment::volumeAbsorption` still scales it as
    // a scene-wide multiplier, which is what that control is for.
    floatField("fogDensity", "Fog density", 0.0f, 8.0f, 0.0f, 4.0f, GET(e.vortex.density),
               SETF(e.vortex.density)).json("/vortex/density").fmt("%.2f").main()
        .tooltip("How much of what is behind the bank it hides, per metre travelled.\n"
                 "This is thickness, not brightness -- Self glow below is the light."),
    floatField("bankRadius", "Bank radius", 0.0f, 20000.0f, 0.0f, 3000.0f, GET(e.vortex.field.radius),
               SETF(e.vortex.field.radius)).json("/vortex/radius").fmt("%.0f m").main()
        .tooltip("How far the bank reaches from its centre. 0 switches it off, and that is\n"
                 "the default: a volumetric medium costs the march real time, so it is opt-in."),
    floatField("bankHeight", "Bank height", 0.1f, 5000.0f, 10.0f, 1200.0f, GET(e.vortex.field.thickness),
               SETF(e.vortex.field.thickness)).json("/vortex/thickness").fmt("%.0f m").main()
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
    floatField("billow", "Billow", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.smokeBillow),
               SETF(e.vortex.field.smokeBillow)).json("/vortex/smokeBillow").main()
        .tooltip("0 is a thin wispy haze; 1 is rounded masses with creases between them.\n"
                 "The difference between a mist and a bank of cloud."),
    // ADR-561. §53's control, and the reason it is a MAIN row on a fog bank rather than an advanced
    // one: the brief's §4 D, its §44 bar 1 and its Definition of Done are all this parameter. "With
    // all noise and detail at zero the system must still produce clean, coherent fog" is a claim an
    // artist has to be able to CHECK, and until this row existed they could not -- the vortex
    // declared `cloudNoise` and the fog bank did not, so every diagnostic arm in ADR-560 had to be
    // hand-authored into JSON, where it round-trips only because `AtmosphericEffect::toJson` walks
    // every registered kind's rows and not just this one's.
    //
    // The JSON path is the vortex's own, so the two rows write one key and cannot disagree -- the
    // same aliasing every other row in this file uses, and the reason they are declared absolute.
    //
    // Named for what it does to the picture rather than for the fBM it weights: ADR-560 measured
    // that at 0 this bank is a grey card, which is a defect in the FIELD and not in this control.
    // Turning it down is how an artist sees that, which is what a diagnostic is for.
    // ADR-563, the brief's §10: a bank has a SHAPE. These are what make the field structured with
    // the detail at zero, and they live in `EffectValueStore` rather than on `world::Vortex`
    // because that struct is shared with the tornado and these are a fog bank's alone (ADR-500's
    // rule for a kind declared after the registry).
    // ADR-566, the brief's §9: which local volume primitive this bank is. First row of the panel,
    // because it is the one control that changes what every control under it means -- `Bank length`
    // is a long axis on a capsule and a semi-axis on a box, and an artist has to see which they
    // are holding before they reach for it.
    //
    // The list is the order of `world::FogShape` and of the constants in `shaders/fog.wgsl`, and
    // `test_fog_primitives.cpp` checks the three agree rather than trusting that they do -- which
    // is ADR-562 §9's rule applied to a list instead of to a lane.
    storedChoice("shape", "Shape", 0, kFogShapeNames).sec("Shape").main()
        .tooltip("Bank: fog lying in the world, ending upward by its height profile rather than\n"
                 "at a lid -- the shape a valley or a lakeside fills with.\n"
                 "The other five are closed volumes you place: a ball of mist, a stretched\n"
                 "ellipsoid, a box, a capsule lying along its long axis, a standing cylinder."),
    storedFloat("bankLength", "Bank length", 1.0f, 0.2f, 6.0f, 0.5f, 3.0f).main(),
    storedFloat("bankRotation", "Bank rotation", 0.0f, -180.0f, 180.0f, -180.0f, 180.0f).main(),
    storedFloat("edgeSoftness", "Edge softness", 0.35f, 0.02f, 1.0f, 0.05f, 0.9f).main(),
    storedFloat("groundHug", "Ground hug", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f).main(),
    storedFloat("heightFalloff", "Height falloff", 1.4f, 0.05f, 8.0f, 0.3f, 4.0f).sec("Structure"),
    storedFloat("domeShape", "Dome", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    // ADR-566, §9's "height influence". A Bank has no vertical geometry of its own -- the profile
    // IS its top -- so it takes the profile whole and this row does nothing to it. The five closed
    // primitives already have a top and a bottom, so for them the profile is an optional
    // modulation inside the volume: 0 is a uniform ball of mist, 1 is one that pools at its floor.
    // Stated here and in the tooltip rather than left for an artist to discover, which is what
    // ADR-421 asks of a control that is inert in one of its own modes.
    storedFloat("heightInfluence", "Height influence", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How much the height profile above shapes the density INSIDE a closed volume.\n"
                 "0 fills it evenly; 1 makes it pool at its floor the way a bank does.\n"
                 "A Bank always takes the profile whole, so this does nothing to one."),
    // ADR-565 (§13/§14): the macro detail's own two controls. Frequency is expressed against the
    // bank's radius, so the detail is the same SHAPE at any size -- ADR-564's size independence
    // applied to structure rather than to density.
    storedFloat("detailScale", "Detail scale", 6.0f, 0.5f, 40.0f, 1.0f, 16.0f).sec("Detail"),
    // ADR-571 (§15/§16): the bank's MOTION, in metres per second, along its own long axis.
    //
    // `detailDrift` was a rate in NOISE space, divided by `detailScale` and by the radius on the
    // way in, so one number meant a different speed in every bank -- and its direction was not a
    // control at all, which is the first line of §16's list. It is metres a second now, the same
    // move ADR-564 made for density, and it is GONE rather than aliased (ADR-441).
    //
    // The direction is the bank's own long axis (`bankRotation`), which costs no control and is
    // the answer an artist expects: a bank lying along a valley drifts along the valley.
    storedFloat("driftSpeed", "Drift speed", 0.0f, -40.0f, 40.0f, -4.0f, 4.0f)
        .sec("Motion")
        .fmt("%.2f m/s")
        .tooltip("How fast the bank's internal structure is carried through the world, along the\n"
                 "bank's long axis. This is an ADVECTION: the detail moves rather than changing in\n"
                 "place, so at any speed the fog still reads as fog rather than boiling."),
    storedFloat("driftVertical", "Drift vertical", 0.0f, -20.0f, 20.0f, -2.0f, 2.0f)
        .fmt("%.2f m/s")
        .tooltip("Upward or downward drift, in metres a second. Mist lifting off water is a small\n"
                 "positive number; a bank settling into a valley is a small negative one."),
    // ADR-572 (§17): the flow the effect subscribes to steers the drift. It takes the DIRECTION
    // only -- the speed above stays the artist's -- because a direction is unit-free and the two
    // publishers of a flow disagree about units by design.
    storedFloat("driftWind", "Drift follows flow", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How much the field this effect subscribes to steers the drift, instead of the\n"
                 "bank's own long axis. It sets the DIRECTION only: the speed stays Drift speed,\n"
                 "so turning this up cannot make the bank move faster than you asked.\n"
                 "Does nothing until the effect subscribes to a flow field."),
    // ADR-571 (§24): the density response curve's threshold and softness. `Contrast` is its third
    // term and is declared below, as the absolute row on `e.vortex.field.contrast` it always was.
    storedFloat("densityThreshold", "Density threshold", 0.0f, 0.0f, 0.99f, 0.0f, 0.8f)
        .sec("Density curve")
        .tooltip("Clears density below this and renormalises what is left. Turn it up to make thin\n"
                 "haze into clear air and give the bank a definite boundary instead of a long tail."),
    storedFloat("densitySoftness", "Density softness", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("Bends the threshold's knee from a straight line into a smooth one. Section 23 of\n"
                 "the brief: softness matters more than detail."),
    // ADR-575, the brief's §26: emission wants intensity, colour, density influence AND height
    // influence, and the march had the first three. It reuses the density's vertical profile
    // rather than introducing a second vertical shape, so a bank whose glow follows its height
    // follows the same curve its density does.
    storedFloat("emissionHeight", "Glow follows height", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .sec("Glow")
        .tooltip("How much the bank's glow follows its height profile instead of being even.\n"
                 "At 1 a ground-hugging bank glows at its floor and fades upward with its\n"
                 "density; at 0 it glows evenly through its whole height."),
    floatField("detailAmount", "Detail amount", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.cloudNoise),
               SETF(e.vortex.field.cloudNoise)).json("/vortex/cloudNoise").sec("Detail").main()
        .tooltip("How much of the bank's density comes from procedural detail rather than from\n"
                 "its shape. At 0 the bank is its analytic volume alone -- which is the check\n"
                 "that the fog is fog and not a noise field: it should still read as fog."),
    floatField("drift", "Drift", -4.0f, 4.0f, -0.2f, 0.2f, GET(e.vortex.field.rotationSpeed),
               SETF(e.vortex.field.rotationSpeed)).json("/vortex/rotationSpeed").main()
        .tooltip("How fast the whole bank turns over. Slow: fog that moves quickly reads as\n"
                 "smoke, and the eye notices the motion instead of the place."),

    // ---- Advanced
    floatField("churn", "Churn", 0.0f, 8.0f, 0.0f, 2.0f, GET(e.vortex.field.smokeWarp),
               SETF(e.vortex.field.smokeWarp)).sec("Structure").json("/vortex/smokeWarp")
        .tooltip("ADR-389: drags the fine detail through the coarse flow, so the detail is\n"
                 "carried BY the structure rather than sitting ON it. The one control that\n"
                 "turns noise into something that looks like moving air."),
    floatField("wisps", "Wisps", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.turbulence),
               SETF(e.vortex.field.turbulence)).json("/vortex/turbulence"),
    floatField("wispScale", "Wisp scale", 0.001f, 40.0f, 0.1f, 8.0f, GET(e.vortex.field.turbulenceScale),
               SETF(e.vortex.field.turbulenceScale)).json("/vortex/turbulenceScale"),
    floatField("fineDetail", "Fine detail", 0.0f, 2.0f, 0.0f, 1.0f, GET(e.vortex.field.detail),
               SETF(e.vortex.field.detail)).json("/vortex/detail"),
    floatField("threads", "Threads", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.vortex.filaments),
               SETF(e.vortex.filaments)).json("/vortex/filaments")
        .tooltip("Bright luminous threads in the densest folds, in Glow colour. 0 for\n"
                 "ordinary fog; this is what makes a bank read as alive."),
    floatField("contrast", "Contrast", 0.05f, 12.0f, 0.5f, 5.0f, GET(e.vortex.field.contrast),
               SETF(e.vortex.field.contrast)).json("/vortex/contrast")
        .tooltip("Low is an even wash; high separates the bank into distinct masses with\n"
                 "clear air between them."),
    floatField("swell", "Swell", 0.0f, 1.0f, 0.0f, 0.3f, GET(e.vortex.field.breathAmount),
               SETF(e.vortex.field.breathAmount)).json("/vortex/breathAmount").sec("Breathing")
        .tooltip("The bank widens and narrows slowly. Applied to the RADIUS rather than the\n"
                 "density, so the silhouette moves -- scaling density alone just pulses it."),
    floatField("swellSpeed", "Swell speed", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.field.breathSpeed),
               SETF(e.vortex.field.breathSpeed)).json("/vortex/breathSpeed"),

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

    floatField("centerX", "Centre X", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.vortex.field.center.x),
               SETF(e.vortex.field.center.x)).json("/vortex/center/0").fmt("%.0f m").sec("Placement"),
    floatField("centerY", "Centre Y", -1e5f, 1e5f, -500.0f, 1000.0f, GET(e.vortex.field.center.y),
               SETF(e.vortex.field.center.y)).json("/vortex/center/1").fmt("%.0f m"),
    floatField("centerZ", "Centre Z", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.vortex.field.center.z),
               SETF(e.vortex.field.center.z)).json("/vortex/center/2").fmt("%.0f m"),
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
    const glm::vec3 keep = v.field.center;
    v = Vortex{};
    v.field.center = keep;
    // What makes it a bank rather than a funnel, in four numbers.
    v.field.swirl = 0.0f;        // no spiral
    v.field.funnelDepth = 0.0f;  // no throat descending below the mouth
    v.field.throat = 1.0f;       // ...and the mouth does not narrow
    v.field.throatDensity = 0.0f;
    v.field.innerVoid = 0.0f;    // filled, not a ring
    // ADR-561, and `innerVoid = 0` alone did NOT achieve it. The envelope's eye term is
    // `smoothstep(innerVoid, innerVoid + eyeWallWidth, rr)`, and `eyeWallWidth` defaults to the
    // 0.22 ADR-374 hardcoded for the vortex -- so with the void at zero the density still climbed
    // from EXACTLY ZERO on the axis to full at 22% of the radius. Every fog bank in the product had
    // a soft hole in the middle of it: 200 metres of clear air inside a 900 metre bank, inherited
    // from a cyclone's eye by an effect that has no use for an eye, with no row on the panel to
    // close it. Measured before this line: envelope 0.0000 at rr = 0, 0.4320 at rr = 0.10, 1.0000
    // at rr = 0.25.
    //
    // Zero rather than a small number: `vortexRadialProfile` clamps it to 1e-3, so the rise happens
    // over a thousandth of the radius and the bank is filled to its axis with no edge introduced
    // (ADR-369's rule still holds -- the term is still a smoothstep, it is just no longer wide).
    v.field.eyeWallWidth = 0.0f;
    v.field.eyeWallGain = 0.0f;  // and no ring of extra density around a hole that is no longer there
    v.cometResponse = 0.0f;

    if (style == kStyleNames[0]) { // Valley Mist -- thin, wide, low, barely lit
        v.field.radius = 1400.0f;
        v.field.thickness = 90.0f;
        v.density = 2.0f;  // optical depth: reads as fog
        v.emission = 0.004f;
        v.field.contrast = 1.5f;
        v.field.turbulence = 0.35f;
        v.field.turbulenceScale = 1.1f;
        v.field.smokeWarp = 0.9f;
        v.field.smokeBillow = 0.35f;
        v.field.detail = 0.25f;
        v.filaments = 0.0f;
        v.field.rotationSpeed = 0.012f;
        v.field.breathAmount = 0.06f;
        v.field.breathSpeed = 0.09f;
        v.scattering = 0.6f;
        v.spill = 0.8f;
        v.colorDeep = {0.050f, 0.062f, 0.085f};
        v.colorMid = {0.140f, 0.170f, 0.210f};
        v.colorAccent = {0.230f, 0.270f, 0.320f};
    } else if (style == kStyleNames[1]) { // Glowmere Haze -- bioluminescent, threaded, self-lit
        v.field.radius = 900.0f;
        v.field.thickness = 160.0f;
        v.density = 2.6f;
        v.emission = 0.030f;
        v.field.contrast = 2.6f;
        v.field.turbulence = 0.55f;
        v.field.turbulenceScale = 1.8f;
        v.field.smokeWarp = 1.4f;
        v.field.smokeBillow = 0.55f;
        v.field.detail = 0.35f;
        v.filaments = 1.4f;
        v.field.rotationSpeed = 0.020f;
        v.field.breathAmount = 0.10f;
        v.field.breathSpeed = 0.14f;
        v.scattering = 0.35f;
        v.spill = 2.0f;
        v.colorDeep = {0.014f, 0.040f, 0.048f};
        v.colorMid = {0.040f, 0.150f, 0.145f};
        v.colorAccent = {0.110f, 0.360f, 0.300f};
    } else { // Dense Bank -- thick, tall, billowing, opaque
        v.field.radius = 650.0f;
        v.field.thickness = 320.0f;
        v.density = 5.0f;  // thick, still not a slab
        v.emission = 0.010f;
        v.field.contrast = 3.4f;
        v.field.turbulence = 0.70f;
        v.field.turbulenceScale = 2.6f;
        v.field.smokeWarp = 2.0f;
        v.field.smokeBillow = 0.90f;
        v.field.detail = 0.45f;
        v.filaments = 0.3f;
        v.field.rotationSpeed = 0.030f;
        v.field.breathAmount = 0.12f;
        v.field.breathSpeed = 0.16f;
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
    r.anchor = e.vortex.field.center;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const AtmosphericEffect& e) { return e.vortex.validate(); }

// ADR-562: the authored numbers as the sixteen lanes the march reads.
//
// IDENTICAL to the vortex's packer, deliberately and not by accident: a fog bank IS the
// placed medium the engine has, authored in fog vocabulary (see this file's opening note).
// Sharing the lane map is what makes that true rather than merely claimed. If the two ever
// need to differ, that is the moment the fog bank stops being a different authoring surface
// onto one primitive and becomes a second primitive, which is a decision with an ADR in it.
//
// THE LANE MAP, and it is the contract. `shaders/volume.wgsl` reads these by index and
// `shaders/vortex.wgsl` names them `v0`..`v8`; the two must agree and this comment is where they
// are held against each other.
//
//   0  centre.xyz, radius (0 is off, and it is the gate)
//   1  thickness, swirl, rotationSpeed, DENSITY (extinction per metre)
//   2  ADR-571: the fog bank's DRIFT VELOCITY (m/s, world space). The vortex packs
//      (innerVoid, contrast, turbulence, turbulenceScale) here and no reader of a fog bank's slot
//      looks at it -- audited with `grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl`.
//   6  ADR-571: the DENSITY RESPONSE CURVE (contrast, threshold, softness). Free for the same
//      reason and audited the same way.
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
void packMedium(const E& e, float envelope, const MediumFlowInput& flow, MediumSlot& out) {
    const Vortex& v = e.vortex;
    const vortex::VortexUniforms f = vortex::packVortex(v.field);
    out.lane[0] = f.v0;
    // The envelope scales the two PER-METRE coefficients and nothing else (ADR-387): fading a
    // medium means less of it in the air. Fading its colours would leave a full-strength grey
    // ghost; fading its radius would shrink it rather than dim it.
    // ADR-564: the authored number is an optical depth through the bank; the march wants an
    // extinction per metre. The crossing path is the ellipse's long axis, which is the direction a
    // bank is usually looked through, so `density` means the same thing at any size.
    const float crossing = std::max(2.0f * v.field.radius * std::max(storedOf(e, "bankLength", 1.0f), 0.05f), 1.0f);
    out.lane[1] = glm::vec4(glm::vec3(f.v1), (std::max(v.density, 0.0f) / crossing) * envelope);
    // ADR-571 (§15/§16): lane 2 is the bank's DRIFT VELOCITY in metres per second, world space.
    //
    // It is the vortex's lane 2 reinterpreted, and the audit that says that is safe is
    // `grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl`: lane 2 has exactly one reader in the
    // march, `mediumVortexUniforms`, and that is the arm a fog bank never takes. ADR-562 §9's
    // rule applied before writing rather than after something broke.
    //
    // The flow field a scene subscribes to cannot reach here -- `EffectResolve::pack` is handed
    // the effect and an envelope and not the resolved flow -- so §17's wind coupling is not in
    // this lane yet. ADR-571's revisit note says what it would take.
    const float driftRot = glm::radians(storedOf(e, "bankRotation", 0.0f));
    const float driftSpeed = storedOf(e, "driftSpeed", 0.0f);
    glm::vec3 driftDir(std::cos(driftRot), 0.0f, std::sin(driftRot));

    // ADR-572, the brief's §17: the flow sets the DIRECTION and never the speed.
    //
    // That is a unit decision, not a simplification. `FlowSample::units` exists because the two
    // publishers genuinely differ -- the wind's `speed` is a dimensionless strength and a vortex's
    // is metres per second of real medium -- and this codebase has produced the same unit bug four
    // times by assuming (ADR-374's density, ADR-379's spill, ADR-381's comet-on-fog, ADR-389's
    // coefficient). A DIRECTION is unit-free, so taking only the direction is correct against both
    // publishers with no branch on `units` and no chance of a control meaning two things. The
    // metres a second stay the artist's, in `driftSpeed`, where they are legible.
    //
    // §17: "flow affects movement, not basic existence." Setting which way the structure travels is
    // movement; it cannot make the bank exist anywhere it did not.
    const float windAmount = std::clamp(storedOf(e, "driftWind", 0.0f), 0.0f, 1.0f) * flow.influence;
    if (windAmount > 0.0f) {
        const float len = glm::length(flow.sample.flow);
        if (len > 1e-6f) {
            const glm::vec3 blended = glm::mix(driftDir, flow.sample.flow / len, std::min(windAmount, 1.0f));
            const float blendedLen = glm::length(blended);
            if (blendedLen > 1e-6f) {
                driftDir = blended / blendedLen; // renormalised: a blend of two units is not one
            }
        }
    }
    out.lane[2] = glm::vec4(driftDir * driftSpeed, 0.0f);
    out.lane[2].y = storedOf(e, "driftVertical", 0.0f) + driftDir.y * driftSpeed;
    out.lane[3] = glm::vec4(f.v3.x, f.v3.y, std::max(v.emission, 0.0f) * envelope,
                            std::max(v.filaments, 0.0f));
    out.lane[4] = f.v4;
    out.lane[5] = glm::vec4(std::max(v.cometResponse, 0.0f), std::max(v.cometReach, 1.0f),
                            std::max(v.scattering, 0.0f), 0.0f);
    // ADR-571 (§24): lane 6 is the bank's DENSITY RESPONSE CURVE. Free for a fog bank by the same
    // audit as lane 2 -- `mediumVortexUniforms` is its only reader in the march and a bank never
    // takes that arm. `contrast` finally reaches the field through it: the row has existed since
    // this kind did and all three styles set it, and nothing has read it since ADR-563.
    out.lane[6] = glm::vec4(std::max(v.field.contrast, 0.05f),
                            std::clamp(storedOf(e, "densityThreshold", 0.0f), 0.0f, 0.99f),
                            std::clamp(storedOf(e, "densitySoftness", 0.0f), 0.0f, 1.0f), 0.0f);
    // ADR-565: a fog bank has no eye, so lane 7's first two slots carry its macro-detail terms
    // instead. `.z` stays `cloudNoise` exactly where `packVortex` put it, so the control an artist
    // moves is the one the field reads.
    out.lane[7] = glm::vec4(std::max(storedOf(e, "detailScale", 6.0f), 0.05f),
                            storedOf(e, "detailDrift", 0.01f), f.v7.z, 0.0f);
    out.lane[8] = f.v8;
    out.lane[9] = glm::vec4(v.colorDeep, 0.0f);
    out.lane[10] = glm::vec4(v.colorMid, 0.0f);
    out.lane[11] = glm::vec4(v.colorAccent, 0.0f);
    // ADR-566: lane 12's first slot is `spill`, which the surface glow reads and this kind must
    // leave alone. The two zeroes beside it are where the primitive selector and its height
    // influence live -- one lane, read by `shaders/fog.wgsl` and `world::fogShapeAt` and by
    // nothing else, which is ADR-562 §9's rule for adding a number to a per-kind lane map.
    out.lane[12] = glm::vec4(std::max(v.spill, 0.0f),
                             std::clamp(storedOf(e, "shape", 0.0f), 0.0f,
                                        static_cast<float>(kFogShapeCount - 1)),
                             std::clamp(storedOf(e, "heightInfluence", 0.0f), 0.0f, 1.0f),
                             // ADR-575 (§26): the GLOW's own height influence, separate from the
                             // density's on purpose -- a bank can be densest at its floor and glow
                             // evenly, or be uniform and glow only where it is low.
                             std::clamp(storedOf(e, "emissionHeight", 0.0f), 0.0f, 1.0f));
    // ADR-563: the bank's own shape, in the lanes the vortex leaves empty. `shaders/fog.wgsl`
    // reads 0, 13 and 14; lane 15 is the kind tag `buildAtmosphericFrame` writes.
    const float rot = glm::radians(storedOf(e, "bankRotation", 0.0f));
    out.lane[13] = glm::vec4(std::max(v.field.thickness, 1e-3f),
                             std::max(storedOf(e, "bankLength", 1.0f), 0.05f),
                             std::cos(rot), std::sin(rot));
    out.lane[14] = glm::vec4(std::clamp(storedOf(e, "edgeSoftness", 0.35f), 0.02f, 1.0f),
                             std::clamp(storedOf(e, "groundHug", 0.0f), 0.0f, 1.0f),
                             std::max(storedOf(e, "heightFalloff", 1.4f), 0.01f),
                             std::clamp(storedOf(e, "domeShape", 0.0f), 0.0f, 1.0f));
}

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
    s.resolve.bucket = EffectBucket::Medium;
    s.resolve.pack = packMedium;
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
