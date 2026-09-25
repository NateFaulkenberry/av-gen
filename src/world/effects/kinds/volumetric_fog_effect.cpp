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
// **And the same header said the vortex's dead controls were "simply not declared here". Eight
// were.** ADR-713 found them by the lane audit its brief asked for: Billow, Churn, Wisps, Wisp
// scale and Fine detail packed into lanes 2 and 6, which ADR-571 had already given to the drift
// and the density curve, and Drift, Swell and Swell speed packed into lanes nothing in
// `shaders/fog.wgsl` read -- declared, drawn on the panel, set by every preset, routed from the
// audio, and reaching nothing. The five noise-stack rows are CUT (ADR-441: no aliases); the three
// motion rows were §16's own controls under vortex names, so they are made LIVE instead -- Drift
// as Swirl, Swell as §16's expansion and contraction. The rule this adds to the one above: *a
// panel's rows are claims too, and the lane audit is the test of them.*
//
// **Where its numbers live.** `e.vortex`, aliased on purpose: it is the same medium, so it is the
// same struct and the same block in the saved file (the rows say so with an absolute `/vortex/...`
// JSON path). The nine controls a bank has no use for are simply not declared here -- which means
// the panel does not draw them, registration does not produce them, and a route cannot aim at them.

#include "core/vortex.hpp"
#include "world/effects/effect_registry.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <iterator>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = EffectInstance;

float storedOf(const EffectInstance& e, const char* leaf, float fallback) {
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
    // ADR-579, the brief's §36: the panel is organised around artistic concepts, and its first
    // eight rows carried no heading at all -- density, size, colours and glow in one unlabelled
    // run. §36 names these groups: "Fog: Density, Height, ... Thickness" and "Appearance: Color,
    // Density, Emission, Contrast".
    floatField("fogDensity", "Fog density", 0.0f, 8.0f, 0.0f, 4.0f, GET(e.vortex.density),
               SETF(e.vortex.density)).json("/vortex/density").fmt("%.2f").main().sec("Fog")
        .tooltip("How much of what is behind the bank it hides, looking THROUGH it along its\n"
                 "long axis. 1.0 is \"you can just see through it\"; it means the same at any\n"
                 "size, so changing Bank radius does not change how thick it looks.\n"
                 "This is thickness, not brightness -- Self glow below is the light."),
    floatField("bankRadius", "Bank radius", 0.0f, 20000.0f, 0.0f, 3000.0f, GET(e.vortex.field.radius),
               SETF(e.vortex.field.radius)).json("/vortex/radius").fmt("%.0f m").main()
        .tooltip("How far the bank reaches from its centre. 0 switches it off, and that is\n"
                 "the default: a volumetric medium costs the march real time, so it is opt-in."),
    floatField("bankHeight", "Bank height", 0.1f, 5000.0f, 10.0f, 1200.0f, GET(e.vortex.field.thickness),
               SETF(e.vortex.field.thickness)).json("/vortex/thickness").fmt("%.0f m").main()
        .tooltip("How tall the bank is. It is densest near its base and thins upward at the\n"
                 "rate Height falloff sets, rather than being a slab with a lid -- fog sits ON\n"
                 "something. Dome adds a rounded top if you want one."),
    colorField("fogColor", "Fog colour", GET(e.vortex.colorMid), SETC(e.vortex.colorMid))
        .sec("Appearance")
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
    // ADR-561. §53's control, and the reason it is a MAIN row on a fog bank rather than an advanced
    // one: the brief's §4 D, its §44 bar 1 and its Definition of Done are all this parameter. "With
    // all noise and detail at zero the system must still produce clean, coherent fog" is a claim an
    // artist has to be able to CHECK, and until this row existed they could not -- the vortex
    // declared `cloudNoise` and the fog bank did not, so every diagnostic arm in ADR-560 had to be
    // hand-authored into JSON, where it round-trips only because `EffectInstance::toJson` walks
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
    // ADR-713 (§16): SWIRL. This row existed as "Drift" -- "how fast the whole bank turns over" --
    // on `rotationSpeed`, which `packVortex` puts in lane 1.z, and nothing in `shaders/fog.wgsl`
    // read lane 1.z. So it was §16's swirl under a vortex's name, dead. Renamed for what it does
    // and made live: the bank's INTERNAL structure (detail and turbulence) circulates rigidly
    // about its vertical axis. Radians a second, because that is the unit the lane has always had.
    floatField("swirl", "Swirl", -4.0f, 4.0f, -0.1f, 0.1f, GET(e.vortex.field.rotationSpeed),
               SETF(e.vortex.field.rotationSpeed)).json("/vortex/rotationSpeed").fmt("%.3f rad/s")
        .tooltip("The bank's inner structure circulates about its vertical axis, in radians a\n"
                 "second (0.05 is a turn every two minutes). It turns the detail and the\n"
                 "turbulence, not the outline -- so with both at 0 there is nothing to see turn.\n"
                 "Slow: the rim of a 1 km bank at 0.05 moves 50 m a second."),
    // ADR-713 (§16): TURBULENCE, which is also §16's CURL -- one mechanism, because a flow sampled
    // per position is what shears a structure, and ADR-572 said so before either existed. The
    // whole field is evaluated at a point displaced by a divergence-free flow, so the OUTLINE
    // deforms, not only the density inside it. Measured in the bank's own semi-axes, so the same
    // number deforms a 30 m wisp and a 2 km bank alike.
    storedFloat("turbulence", "Turbulence", 0.0f, 0.0f, 1.0f, 0.0f, 0.5f)
        .tooltip("Deforms the bank's whole shape with a swirling, divergence-free flow, so its\n"
                 "edge billows and folds instead of staying the primitive's clean outline.\n"
                 "The number is how far, as a fraction of the bank's own size in each direction.\n"
                 "The flow moves density; it cannot make fog where the bank has none."),
    storedFloat("turbulenceScale", "Turbulence scale", 1.5f, 0.25f, 8.0f, 0.5f, 4.0f)
        .tooltip("How many turbulent folds fit across the bank. Low is a few big lobes; high is\n"
                 "a ragged edge."),
    storedFloat("turbulenceSpeed", "Turbulence evolution", 0.25f, 0.0f, 4.0f, 0.0f, 1.0f)
        .tooltip("How fast the turbulence changes shape in place. 0 freezes it -- it still drifts\n"
                 "and swirls with the bank. A function of the song's time, so scrubbing to a\n"
                 "moment lands on the same shape playing to it does."),
    // ADR-571 (§24): the density response curve's threshold and softness. `Contrast` is its third
    // term and is declared below, as the absolute row on `e.vortex.field.contrast` it always was.
    storedFloat("densityThreshold", "Density threshold", 0.0f, 0.0f, 0.99f, 0.0f, 0.8f)
        .sec("Density curve")
        .tooltip("Clears density below this and renormalises what is left. Turn it up to make thin\n"
                 "haze into clear air and give the bank a definite boundary instead of a long tail."),
    storedFloat("densitySoftness", "Density softness", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("Bends the threshold's knee from a straight line into a smooth one. Section 23 of\n"
                 "the brief: softness matters more than detail."),
    // ADR-579 (§36): Contrast MOVED here, to sit with the two rows it works with. It is the third term of
    // ADR-571's density response curve -- `pow(s, contrast)` after the threshold and the knee --
    // and it sat under "Structure" from before it reached the field at all (ADR-571).
    // Moved rather than re-sectioned: giving it its own `.sec` reopened "Density curve" a
    // second time on the same page, which is the very defect the guard had just caught.
    floatField("contrast", "Contrast", 0.05f, 12.0f, 0.5f, 5.0f, GET(e.vortex.field.contrast),
               SETF(e.vortex.field.contrast)).json("/vortex/contrast")
        .tooltip("Low is an even wash; high separates the bank into distinct masses with\n"
                 "clear air between them."),
    // ADR-575, the brief's §26: emission wants intensity, colour, density influence AND height
    // influence, and the march had the first three. It reuses the density's vertical profile
    // rather than introducing a second vertical shape, so a bank whose glow follows its height
    // follows the same curve its density does.
    storedFloat("emissionHeight", "Glow follows height", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)
        .sec("Glow")
        .tooltip("How much the bank's glow follows its height profile instead of being even.\n"
                 "At 1 a ground-hugging bank glows at its floor and fades upward with its\n"
                 "density; at 0 it glows evenly through its whole height."),
    // ADR-714 (§25): HEIGHT and DISTANCE colour, in addition to the three depth colours. Both are
    // LUMINANCE-PRESERVING -- they change the hue of the glow and never its brightness, because
    // brightness is where the eye reads density and §25's warning is exactly "avoid making colour
    // responsible for structure". Like the depth colours, they tint the bank's own GLOW.
    storedColor("heightColor", "Height colour", glm::vec3(0.34f, 0.24f, 0.30f))
        .sec("Colour by place")
        .tooltip("The hue the top of the bank takes, blending in from its densest layer to two\n"
                 "heights above it. Changes the glow's colour, never its brightness.\n"
                 "Needs Self glow above 0: a bank that does not glow takes its colour from the lights."),
    storedFloat("heightColorAmount", "Height colour amount", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    storedColor("distanceColor", "Distance colour", glm::vec3(0.14f, 0.18f, 0.28f))
        .tooltip("The hue the far parts of the bank take -- aerial perspective inside the fog.\n"
                 "Changes the glow's colour, never its brightness."),
    storedFloat("distanceColorAmount", "Distance colour amount", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    storedFloat("distanceColorRange", "Distance colour range", 600.0f, 1.0f, 50000.0f, 50.0f, 4000.0f)
        .fmt("%.0f m")
        .tooltip("How far from the camera the distance colour is about two-thirds of the way in."),
    floatField("detailAmount", "Detail amount", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.vortex.field.cloudNoise),
               SETF(e.vortex.field.cloudNoise)).json("/vortex/cloudNoise").sec("Detail").main()
        .tooltip("How much of the bank's density comes from procedural detail rather than from\n"
                 "its shape. At 0 the bank is its analytic volume alone -- which is the check\n"
                 "that the fog is fog and not a noise field: it should still read as fog."),

    // ---- Advanced
    // ADR-713: this block was the vortex's NOISE STACK -- Churn, Wisps, Wisp scale, Fine detail --
    // and all four packed into lanes 2 and 6, which ADR-571 gave to the drift and the density curve.
    // They drew, they were set by every preset, `audio.mid` routed to Churn, and none of them reached
    // the field. CUT, not aliased (ADR-441). The fog's detail is `detailAmount`/`detailScale` and its
    // turbulence is `turbulence` above; Threads is the one survivor because it is read (lane 3.w,
    // the accent in the densest folds).
    floatField("threads", "Threads", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.vortex.filaments),
               SETF(e.vortex.filaments)).json("/vortex/filaments").sec("Filaments")
        .tooltip("Bright luminous threads in the densest folds, in Glow colour. 0 for\n"
                 "ordinary fog; this is what makes a bank read as alive."),
    // ADR-713 (§16's EXPANSION and CONTRACTION): this tooltip described the vortex's breath, and
    // for a fog bank it was not true -- lanes 3.x/3.y reached the ray BOUND (ADR-566) and never the
    // field. `fogSwell` reads them now. Periodic, a pure function of the transport second; a bank
    // that grows or clears ONCE over a shot is Bank radius or Fog density keyframed, which the
    // timeline already does exactly (ADR-018), so no second row claims it.
    floatField("swell", "Swell", 0.0f, 1.0f, 0.0f, 0.3f, GET(e.vortex.field.breathAmount),
               SETF(e.vortex.field.breathAmount)).json("/vortex/breathAmount").sec("Breathing")
        .tooltip("The bank expands and contracts slowly, by this fraction of its size. Applied to\n"
                 "the RADIUS rather than the density, so the outline moves -- scaling density alone\n"
                 "just pulses it. For a bank that grows or clears once over a shot, keyframe\n"
                 "Bank radius or Fog density instead."),
    floatField("swellSpeed", "Swell speed", 0.0f, 4.0f, 0.0f, 1.0f, GET(e.vortex.field.breathSpeed),
               SETF(e.vortex.field.breathSpeed)).json("/vortex/breathSpeed").fmt("%.3f rad/s")
        .tooltip("How fast the swell cycles, in radians a second: 0.1 is one breath a minute."),

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

// ADR-579, the brief's §37, which names the ten a fog system is expected to ship with. The three
// that were here -- Valley Mist, Glowmere Haze, Dense Bank -- are the first, the eighth and the
// fifth of these under other names, and they are RENAMED rather than kept beside them (ADR-441:
// no aliases). Nothing ships with a fog effect, so nothing migrates; `tools/make_fog_arms.py` is
// the only caller by name and it moves with them.
//
// "Presets are starting points, not hard-coded special effects": every one of these is reachable
// from any other by moving controls an artist can see, and none of them sets anything the panel
// does not draw.
constexpr std::array<std::string_view, 10> kStyleNames{
    "Ground Mist",     // thin, hugging, barely there
    "Valley Fog",      // dense and low, lying along a valley
    "Rolling Bank",    // large, moving, with a top
    "Forest Mist",     // soft, medium, between trees
    "Dense Cinematic", // thick and dramatic
    "Distant Haze",    // extremely subtle depth
    "Moonlit Mist",    // cool, lit from one side
    "Cosmic Mist",     // Tree of Life stylised, self-luminous
    "Dream Fog",       // soft, surreal, uniform
    "Horror Fog",      // dense, low, visibility-killing
};

// Every style writes every field it touches -- including, and this is the part that matters for a
// kind sharing a struct with the vortex, the nine a bank has no use for. A fog preset applied to an
// effect that was a vortex a moment ago must not leave a spiral and a throat behind, and a preset
// that only set what it wanted would.
//
// `center` is deliberately not written: where the bank is in the world is a placement decision the
// scene made, and a preset that moved it would silently unanchor it (ADR-387's rule for vortices).
void applyStyle(EffectInstance& e, std::string_view style) {
    Vortex& v = e.vortex;
    const glm::vec3 keep = v.field.center;
    v = Vortex{};
    v.field.center = keep;
    // ADR-579 (§37): and the OTHER half of the state, which this function has been leaving behind.
    //
    // The comment below explains why every style writes every field it touches -- "a fog preset
    // applied to an effect that was a vortex a moment ago must not leave a spiral and a throat
    // behind". That argument is right and `v = Vortex{}` only covers the struct. Since ADR-566 a
    // bank's shape, its drift, its density curve and its glow height live in
    // `EffectInstance::values`, and a preset applied after an artist set Shape to Box got a box.
    //
    // Reset from the SCHEMA's declared defaults rather than from a list here, so a row added
    // tomorrow is reset without anybody remembering to add it -- which is the failure this is.
    if (const EffectSchema* schema = effectSchema(EffectKind::VolumetricFog)) {
        for (const EffectField& f : schema->fields) {
            if (!f.stored) {
                continue;
            }
            switch (f.type) {
            case FieldType::Color: e.values.setColor(storeKey(*schema, f), f.storedColor); break;
            case FieldType::Bool:
            case FieldType::Choice:
            case FieldType::Float: e.values.setFloat(storeKey(*schema, f), f.storedDefault); break;
            }
        }
    }
    // What makes it a bank rather than a funnel, in four numbers.
    v.field.swirl = 0.0f;        // no spiral
    v.field.funnelDepth = 0.0f;  // no throat descending below the mouth
    v.field.throat = 1.0f;       // ...and the mouth does not narrow
    v.field.throatDensity = 0.0f;
    v.field.innerVoid = 0.0f;    // filled, not a ring
    // ADR-713: the two motion rows that became live. `Vortex{}` spins at 0.035 rad/s and breathes
    // 5% every 35 s, which is a cyclone's resting motion and was harmless while the fog read
    // neither; a preset that wants a swirl or a swell now says so below, like any other row.
    v.field.rotationSpeed = 0.0f;
    v.field.breathAmount = 0.0f;
    v.field.breathSpeed = 0.0f;
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

    // Every preset writes every number it cares about, and the stored rows were reset above, so
    // what is not named here is the schema's declared default rather than the last preset's value.
    const auto set = [&e](const char* leaf, float value) { e.values.setFloat(std::string("fog/") + leaf, value); };
    const auto shape = [&set](FogShape s) { set("shape", static_cast<float>(static_cast<int>(s))); };

    if (style == kStyleNames[0]) { // Ground Mist -- a thin layer hugging the ground
        v.field.radius = 1800.0f; v.field.thickness = 26.0f;
        v.density = 0.9f; v.emission = 0.002f; v.field.contrast = 1.0f;
        shape(FogShape::Bank); set("bankLength", 2.2f); set("edgeSoftness", 0.7f);
        set("groundHug", 0.0f); set("heightFalloff", 3.2f); set("driftSpeed", 0.6f);
        v.field.cloudNoise = 0.35f;
        v.scattering = 0.7f; v.spill = 0.4f;
        v.colorDeep = {0.060f, 0.066f, 0.078f}; v.colorMid = {0.150f, 0.163f, 0.185f};
        v.colorAccent = {0.230f, 0.245f, 0.270f};
    } else if (style == kStyleNames[1]) { // Valley Fog -- dense, low, lying along a valley
        v.field.radius = 1400.0f; v.field.thickness = 90.0f;
        v.density = 2.4f; v.emission = 0.004f; v.field.contrast = 1.5f;
        shape(FogShape::Bank); set("bankLength", 3.4f); set("edgeSoftness", 0.35f);
        set("groundHug", 0.0f); set("heightFalloff", 1.8f); set("driftSpeed", 1.2f);
        v.field.cloudNoise = 0.5f;
        v.field.breathAmount = 0.06f; v.field.breathSpeed = 0.09f;
        v.scattering = 0.6f; v.spill = 0.8f;
        v.colorDeep = {0.050f, 0.062f, 0.085f}; v.colorMid = {0.140f, 0.170f, 0.210f};
        v.colorAccent = {0.230f, 0.270f, 0.320f};
    } else if (style == kStyleNames[2]) { // Rolling Bank -- large, moving, with a definite top
        v.field.radius = 1100.0f; v.field.thickness = 240.0f;
        v.density = 3.0f; v.emission = 0.006f; v.field.contrast = 2.0f;
        shape(FogShape::Bank); set("bankLength", 2.6f); set("edgeSoftness", 0.45f);
        set("groundHug", 0.25f); set("heightFalloff", 1.1f); set("domeShape", 0.55f);
        set("driftSpeed", 4.5f); set("driftWind", 1.0f); set("detailScale", 4.0f);
        v.field.cloudNoise = 0.6f;
        v.field.breathAmount = 0.08f; v.field.breathSpeed = 0.07f;
        v.scattering = 0.7f; v.spill = 1.0f;
        v.colorDeep = {0.055f, 0.058f, 0.068f}; v.colorMid = {0.165f, 0.172f, 0.190f};
        v.colorAccent = {0.290f, 0.300f, 0.325f};
    } else if (style == kStyleNames[3]) { // Forest Mist -- soft, medium, between trees
        v.field.radius = 500.0f; v.field.thickness = 110.0f;
        v.density = 1.6f; v.emission = 0.003f; v.field.contrast = 1.2f;
        shape(FogShape::Ellipsoid); set("bankLength", 1.8f); set("edgeSoftness", 0.8f);
        set("heightInfluence", 0.7f); set("groundHug", 0.1f); set("heightFalloff", 2.2f);
        set("driftSpeed", 0.8f); set("densitySoftness", 0.6f);
        v.field.cloudNoise = 0.45f;
        v.scattering = 0.9f; v.spill = 0.6f;
        v.colorDeep = {0.042f, 0.058f, 0.050f}; v.colorMid = {0.130f, 0.165f, 0.148f};
        v.colorAccent = {0.215f, 0.265f, 0.240f};
    } else if (style == kStyleNames[4]) { // Dense Cinematic -- thick, tall, dramatic
        v.field.radius = 650.0f; v.field.thickness = 320.0f;
        v.density = 5.5f; v.emission = 0.010f; v.field.contrast = 3.4f;
        shape(FogShape::Bank); set("bankLength", 1.6f); set("edgeSoftness", 0.25f);
        // ADR-579: the same interaction, measured -- centre was 0.035 with the threshold at 0.15.
        set("groundHug", 0.30f); set("heightFalloff", 1.0f); set("densityThreshold", 0.08f);
        set("densitySoftness", 0.8f); set("driftSpeed", 1.5f); set("detailScale", 5.0f);
        v.field.cloudNoise = 0.55f;
        v.filaments = 0.3f; v.field.breathAmount = 0.12f; v.field.breathSpeed = 0.16f;
        v.scattering = 0.8f; v.spill = 1.4f;
        v.colorDeep = {0.060f, 0.058f, 0.070f}; v.colorMid = {0.190f, 0.185f, 0.205f};
        v.colorAccent = {0.330f, 0.320f, 0.350f};
    } else if (style == kStyleNames[5]) { // Distant Haze -- extremely subtle depth
        v.field.radius = 4000.0f; v.field.thickness = 600.0f;
        v.density = 0.35f; v.emission = 0.001f; v.field.contrast = 0.8f;
        shape(FogShape::Bank); set("bankLength", 1.0f); set("edgeSoftness", 1.0f);
        set("groundHug", 0.5f); set("heightFalloff", 0.35f); set("driftSpeed", 0.2f);
        v.field.cloudNoise = 0.15f;
        v.scattering = 0.5f; v.spill = 0.2f;
        v.colorDeep = {0.070f, 0.078f, 0.092f}; v.colorMid = {0.150f, 0.163f, 0.185f};
        v.colorAccent = {0.200f, 0.215f, 0.240f};
    } else if (style == kStyleNames[6]) { // Moonlit Mist -- cool, thin, lit from one side
        v.field.radius = 1000.0f; v.field.thickness = 70.0f;
        v.density = 1.3f; v.emission = 0.002f; v.field.contrast = 1.6f;
        shape(FogShape::Bank); set("bankLength", 2.8f); set("edgeSoftness", 0.6f);
        set("groundHug", 0.0f); set("heightFalloff", 2.4f); set("driftSpeed", 0.9f);
        v.field.cloudNoise = 0.4f;
        // Scene light is what makes this one read: it is lit rather than self-luminous, which is
        // the difference between moonlight and a glow (§20, and ADR-570's self-shadow march is
        // what gives it a direction).
        v.scattering = 1.6f; v.spill = 0.5f;
        v.colorDeep = {0.040f, 0.050f, 0.075f}; v.colorMid = {0.118f, 0.140f, 0.190f};
        v.colorAccent = {0.210f, 0.240f, 0.310f};
    } else if (style == kStyleNames[7]) { // Cosmic Mist -- Tree of Life stylised, self-luminous
        v.field.radius = 900.0f; v.field.thickness = 160.0f;
        v.density = 2.6f; v.emission = 0.030f; v.field.contrast = 2.6f;
        shape(FogShape::Bank); set("bankLength", 1.4f); set("edgeSoftness", 0.5f);
        set("groundHug", 0.4f); set("heightFalloff", 1.4f); set("emissionHeight", 0.4f);
        set("driftSpeed", 1.0f); set("detailScale", 7.0f);
        v.field.cloudNoise = 0.55f;
        v.filaments = 1.4f; v.field.breathAmount = 0.10f; v.field.breathSpeed = 0.14f;
        v.scattering = 0.35f; v.spill = 2.0f;
        v.colorDeep = {0.014f, 0.040f, 0.048f}; v.colorMid = {0.040f, 0.150f, 0.145f};
        v.colorAccent = {0.110f, 0.360f, 0.300f};
    } else if (style == kStyleNames[8]) { // Dream Fog -- soft, surreal, nearly uniform
        v.field.radius = 1200.0f; v.field.thickness = 400.0f;
        v.density = 1.8f; v.emission = 0.012f; v.field.contrast = 0.6f;
        shape(FogShape::Sphere); set("edgeSoftness", 1.0f); set("heightInfluence", 0.0f);
        set("densitySoftness", 1.0f); set("driftSpeed", 0.4f); set("driftVertical", 0.3f);
        v.field.cloudNoise = 0.25f;
        v.field.breathAmount = 0.18f; v.field.breathSpeed = 0.05f;
        v.scattering = 0.9f; v.spill = 1.2f;
        v.colorDeep = {0.085f, 0.070f, 0.100f}; v.colorMid = {0.200f, 0.175f, 0.225f};
        v.colorAccent = {0.320f, 0.290f, 0.355f};
    } else { // Horror Fog -- dense, low, visibility-killing
        v.field.radius = 900.0f; v.field.thickness = 60.0f;
        v.density = 7.0f; v.emission = 0.0f; v.field.contrast = 4.0f;
        shape(FogShape::Bank); set("bankLength", 1.2f); set("edgeSoftness", 0.15f);
        // ADR-579: `heightFalloff` 4.0 with a 0.3 threshold made a razor-thin slab whose density
        // was EXACTLY ZERO at its own centre -- the threshold subtracts from the shape AFTER the
        // vertical profile, so a steep profile and a high floor clear everything but the base.
        // Measured, then tuned: centre 0.000 before, 0.30 after. A preset that is empty where an
        // artist points at it is not a starting point.
        set("groundHug", 0.0f); set("heightFalloff", 2.2f); set("densityThreshold", 0.10f);
        set("driftSpeed", 0.5f); set("detailScale", 9.0f);
        v.field.cloudNoise = 0.5f;
        v.scattering = 0.25f; v.spill = 0.1f;
        v.colorDeep = {0.020f, 0.022f, 0.024f}; v.colorMid = {0.070f, 0.074f, 0.078f};
        v.colorAccent = {0.130f, 0.136f, 0.142f};
    }
    e.kind = EffectKind::VolumetricFog;
    e.style = std::string(style);
}
void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }
void style4(E& e) { applyStyle(e, kStyleNames[4]); }
void style5(E& e) { applyStyle(e, kStyleNames[5]); }
void style6(E& e) { applyStyle(e, kStyleNames[6]); }
void style7(E& e) { applyStyle(e, kStyleNames[7]); }
void style8(E& e) { applyStyle(e, kStyleNames[8]); }
void style9(E& e) { applyStyle(e, kStyleNames[9]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0}, {kStyleNames[1].data(), style1},
    {kStyleNames[2].data(), style2}, {kStyleNames[3].data(), style3},
    {kStyleNames[4].data(), style4}, {kStyleNames[5].data(), style5},
    {kStyleNames[6].data(), style6}, {kStyleNames[7].data(), style7},
    {kStyleNames[8].data(), style8}, {kStyleNames[9].data(), style9},
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
    {"audio.treble", "threads", 0.300f, 30.0f, 260.0f},
    {"beat.pulse", "selfGlow", 0.020f, 12.0f, 300.0f},
};

EffectInstance make(std::string name) {
    EffectInstance e;
    e.name = std::move(name);
    e.kind = EffectKind::VolumetricFog;
    applyStyle(e, kStyleNames[0]);
    // Weather, not an event: it is there, and the music moves it.
    e.activation = Activation::Always;
    // The fade-in WAS 3 s, on the argument that "a bank that appears is a cut and a bank that
    // gathers is weather". The argument is right about a shot and wrong about a button.
    //
    // `buildAtmosphericFrame` drops an effect whose envelope is at or below 1e-4 rather than
    // seating it dim (`atmospherics.cpp:736`), which is correct -- an invisible medium should not
    // cost a march. The consequence is that a 3 s fade-in does not make a new bank faint at t=0,
    // it makes it ABSENT: no slot, `mediumCount == 0`, and `VolumeRenderer::enabled(scene)` false
    // with it. An artist who presses Add and looks at frame 0 -- which is where a scene opens --
    // sees nothing at all and reports the effect as broken. That is exactly how this was found.
    //
    // So the gather is a SHOT decision and belongs to whoever authors the shot, not to the button.
    // Nothing in the repository is changed by this: eleven example scenes author a vortex and not
    // one authors a fog bank, which is the other half of why a 3 s hole at the origin survived a
    // green suite -- there was no artifact anybody opened.
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    // No ground pool. A bank lights what stands IN it, through `spill` and `scattering`; ADR-230's
    // ground glow is a coloured patch on terrain from something in the sky, which this is not.
    e.ground.mode = GroundGlow::Off;
    return e;
}

// A bank has no per-frame trajectory: it is a static field the march samples. Its anchor is its own
// centre, so "what is the air doing where this bank stands" is asked at the bank -- which is what
// lets a fog bank and a comet subscribe to one wind and agree without anybody matching two numbers.
bool fill(const EffectInstance& e, std::size_t, const EffectContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    r.anchor = e.vortex.field.center;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const EffectInstance& e) { return e.vortex.validate(); }

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
// ADR-713/714: THE FOG SLOT'S LANE MAP AS IT NOW STANDS, from the audit
// `grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl` run BEFORE any row was written. For a slot
// tagged fog, a lane is free when its only readers are `mediumVortexUniforms` and
// `mediumTornadoUniforms` (arms a fog slot never takes) and `mediumCapOf`'s tornado branch:
//
//   0   centre.xyz, radius                         bound, field
//   1   x thickness (bound)  y --  z SWIRL rad/s    w density (extinction/m)
//   2   xyz DRIFT m/s        w DISTANCE COLOUR RANGE (m)
//   3   x SWELL  y swell rate   z emission  w filaments
//   4   x depth (bound), yzw untouched: `debug_visualizer.cpp` reads 4.y as a throat for every kind
//   5   x comet  y comet reach  z scene scattering  w HEIGHT COLOUR AMOUNT
//   6   xyz density curve (contrast, threshold, softness)   w TURBULENCE RATE
//   7   x detail scale  y TURBULENCE AMOUNT  z detail amount  w TURBULENCE SCALE
//   8   xyz DISTANCE COLOUR  w its AMOUNT
//   9..11  rgb the three depth colours; their .w are the HEIGHT COLOUR's r, g, b
//   12  spill, shape, height influence, emission height
//   13  thickness, bankLength, cos/sin rotation     14  edge softness, bias, falloff, dome
//   15  the kind tag
//
// Spare for a fog slot after ADR-714: 1.y and 4.yzw (the latter only with the debug view taught
// the kind). Upper-case entries are what ADR-713/714 added.
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
// ADR-572 (§17) and ADR-580 §68, in one place rather than two.
//
// §68: a bank leans downwind by MOVING, exactly as a cosmic vortex does -- it is the same placed
// medium with a different authoring surface, so it answers the wind the same way. That is separate
// from `driftWind` below, which steers the bank's INTERNAL structure: one moves the volume, the
// other moves what is inside it, and an artist can want either without the other.
//
// It is written here rather than inherited from a shared stage because the frame builder used to
// write `leaned.vortex.field.center` unconditionally, which was right for the two kinds that store
// in `e.vortex` and did nothing at all for a tornado, whose axis is a curve and which leans by
// bending. `agent/tornado` fixed that with a `lean` hook in the registry; `agent/fog` had already
// given this packer the flow. The merge kept ONE channel (ADR-441), the flow argument, so a kind's
// wind response is now the body of its own packer -- and a kind that forgets is still named by
// `effect_conformance`'s `flow-reaches` check, which compares packed frames and never knew which
// hook produced them.
void packMedium(const E& e, float envelope, const MediumFlowInput& flow, MediumSlot& out) {
    const Vortex& v = e.vortex;
    constexpr float kLeanFraction = 0.10f; // of the radius, per unit influence
    vortex::VortexField field = v.field;
    field.center += flowLean(flow.sample, flow.influence) * (kLeanFraction * std::max(field.radius, 0.0f));
    const vortex::VortexUniforms f = vortex::packVortex(field);
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
    // ADR-571 wrote here that the flow could not reach this function, because `EffectResolve::pack`
    // was handed the effect and an envelope and nothing else. ADR-572 is that revisit note carried
    // out: it does reach here, as `flow`, and the paragraph below is what this lane does with it.
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
    // ADR-714: the distance colour's range, in the drift lane's spare slot.
    out.lane[2].w = std::max(storedOf(e, "distanceColorRange", 600.0f), 1.0f);
    out.lane[3] = glm::vec4(f.v3.x, f.v3.y, std::max(v.emission, 0.0f) * envelope,
                            std::max(v.filaments, 0.0f));
    out.lane[4] = f.v4;
    out.lane[5] = glm::vec4(std::max(v.cometResponse, 0.0f), std::max(v.cometReach, 1.0f),
                            std::max(v.scattering, 0.0f),
                            // ADR-714: the height colour's amount.
                            std::clamp(storedOf(e, "heightColorAmount", 0.0f), 0.0f, 1.0f));
    // ADR-571 (§24): lane 6 is the bank's DENSITY RESPONSE CURVE. Free for a fog bank by the same
    // audit as lane 2 -- `mediumVortexUniforms` is its only reader in the march and a bank never
    // takes that arm. `contrast` finally reaches the field through it: the row has existed since
    // this kind did and all three styles set it, and nothing has read it since ADR-563.
    out.lane[6] = glm::vec4(std::max(v.field.contrast, 0.05f),
                            std::clamp(storedOf(e, "densityThreshold", 0.0f), 0.0f, 0.99f),
                            std::clamp(storedOf(e, "densitySoftness", 0.0f), 0.0f, 1.0f),
                            // ADR-713: how fast the turbulence evolves in place.
                            std::max(storedOf(e, "turbulenceSpeed", 0.25f), 0.0f));
    // ADR-565: a fog bank has no eye, so lane 7's first two slots carry its macro-detail terms
    // instead. `.z` stays `cloudNoise` exactly where `packVortex` put it, so the control an artist
    // moves is the one the field reads.
    //
    // ADR-713: `.y` carried `storedOf(e, "detailDrift", 0.01f)` -- a row ADR-571 removed, so a
    // constant 0.01 nobody read. It is the TURBULENCE amount now, and `.w` its scale.
    out.lane[7] = glm::vec4(std::max(storedOf(e, "detailScale", 6.0f), 0.05f),
                            std::clamp(storedOf(e, "turbulence", 0.0f), 0.0f, 1.0f), f.v7.z,
                            std::max(storedOf(e, "turbulenceScale", 1.5f), 0.05f));
    // ADR-714: lane 8 was the vortex's spiral bands, read only by the vortex arm; for a fog slot it
    // is the DISTANCE COLOUR and its amount.
    out.lane[8] = glm::vec4(glm::max(e.values.getColor("fog/distanceColor", glm::vec3(0.14f, 0.18f, 0.28f)),
                                     glm::vec3(0.0f)),
                            std::clamp(storedOf(e, "distanceColorAmount", 0.0f), 0.0f, 1.0f));
    // ADR-714: and the HEIGHT COLOUR rides in the three depth colours' fourth components -- the
    // colour lanes carry a fourth colour.
    const glm::vec3 heightColor =
        glm::max(e.values.getColor("fog/heightColor", glm::vec3(0.34f, 0.24f, 0.30f)), glm::vec3(0.0f));
    out.lane[9] = glm::vec4(v.colorDeep, heightColor.r);
    out.lane[10] = glm::vec4(v.colorMid, heightColor.g);
    out.lane[11] = glm::vec4(v.colorAccent, heightColor.b);
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
    s.kind = EffectKind::VolumetricFog;
    s.key = "fog";
    s.enumName = "VolumetricFog";
    s.displayName = "Volumetric Fog";
    s.description = "A placed bank of fog you can see the shape of: a volumetric medium lit by the scene, with structure before noise.";
    s.performance = PerformanceClass::High;
    s.primaryCost = CostFragment | CostExtraPass;
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
    // ADR-702: attached to the World; evaluated at its bucket's stage.
    s.targets = targetBit(EffectTarget::World);
    s.category = EffectCategory::Atmosphere;
    s.stage = RenderStage::Volumetric;
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
