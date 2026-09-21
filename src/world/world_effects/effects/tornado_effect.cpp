// The tornado (ADR-580).
//
// **What it is.** A rotating column of dust and condensate standing in the world: a funnel with a
// condensation shell, a debris skirt at its foot, a wall cloud at its head and helical striations
// winding up its surface. Read from the SIDE, from the ground, which is the one thing that makes it
// a different phenomenon from the effect next door rather than a preset of it.
//
// **Why it is not a Vortex with different numbers**, since that is the first question anybody will
// ask and ADR-580 §2 was written to answer it. `Vortex` is a cyclone: its vocabulary is mouth
// radius, eye, eye wall, spiral rainbands at a pitch angle, funnel depth and throat, and its entire
// macro structure is `vortexRadialProfile(rr) * vortexSpiralBands(rr, angle)` -- both functions of
// the horizontal plane at a height, with height entering only as a Gaussian wall and a taper. Those
// are features you look DOWN at. There is no parameter of one that is a parameter of the other, and
// the brief's instruction not to rename the old class was never a temptation, because the old class
// computes the wrong thing.
//
// **What it costs outside this file, and it is more than four lines.** ADR-500 is explicit that a
// kind reaching the picture through an integrator the engine already has is one file, and a kind
// needing a NEW one needs WGSL, a GPU struct and a renderer change. This is the second kind. The
// march in `shaders/volume.wgsl` gained a third density term, `VolumeUniforms` gained twelve
// `vec4`s, and `shaders/tornado.wgsl` and `core/tornado.{hpp,cpp}` are new. What the registry still
// buys, and it is most of the work, is everything above the march: these rows ARE the registration,
// the panel, the JSON, the modulation targets, the timeline keys and the save entries.
//
// **Where its numbers live.** `e.tornado`, which holds `tornado::TornadoField` BY VALUE rather than
// restating its members. `world::Vortex` restates all of `vortex::VortexField` and the renderer
// rebuilds one from the other at the packing site; ADR-388 records what that cost -- `packVortex`
// had one caller, the parity test, and the bytes the shipped frame marched were assembled somewhere
// else entirely. One list cannot disagree with itself, so the rows below reach through `field`.

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

// The rows, in the order an artist reaches for them, grouped as the brief's §33 asks. Soft ranges
// are the usable span and hard ranges are wider, because a modulation route clamps to the HARD
// range and a slider whose interesting region is in its first hair is a defect this repository has
// a name for.
constexpr EffectField kFields[] = {
    // ---- Shape. The silhouette is the effect, so it is first and it is all `main`.
    floatField("height", "Height", 0.0f, 20000.0f, 0.0f, 2000.0f, GET(e.tornado.field.height),
               SETF(e.tornado.field.height)).fmt("%.0f m").log().main().sec("Shape")
        .tooltip("Ground to wall cloud. 0 switches the tornado off and is the default.\n"
                 "With the default radii, 800 m is a classic cone at about 10:1 -- inside the\n"
                 "6:1 to 15:1 a photogenic tornado actually runs."),
    floatField("radiusBottom", "Ground radius", 0.0f, 4000.0f, 2.0f, 200.0f,
               GET(e.tornado.field.radiusBottom), SETF(e.tornado.field.radiusBottom))
        .fmt("%.1f m").log().main()
        .tooltip("How wide the funnel is where it meets the ground. This is the number the\n"
                 "height:width ratio is read against, and the one that decides whether this is a\n"
                 "rope, a cone or a wedge."),
    floatField("radiusMid", "Mid radius", 0.0f, 4000.0f, 2.0f, 260.0f,
               GET(e.tornado.field.radiusMid), SETF(e.tornado.field.radiusMid))
        .fmt("%.1f m").log().main()
        .tooltip("The radius at HALF HEIGHT, and it is a real radius rather than a curve handle --\n"
                 "the conversion to a Bezier control point happens where the field is packed.\n"
                 "Below both ends it is an hourglass; between them, a cone."),
    floatField("radiusTop", "Top radius", 0.0f, 4000.0f, 2.0f, 600.0f,
               GET(e.tornado.field.radiusTop), SETF(e.tornado.field.radiusTop))
        .fmt("%.1f m").log().main()
        .tooltip("How wide the funnel is where it enters the wall cloud."),
    floatField("taper", "Taper", 0.05f, 6.0f, 0.3f, 3.0f, GET(e.tornado.field.taper),
               SETF(e.tornado.field.taper)).main()
        .tooltip("Where along the height the middle radius bites. Below 1 the funnel flares low\n"
                 "and stays narrow for most of its height; above 1 it stays wide and pinches near\n"
                 "the ground. Two very different storms at the same three radii."),

    // ---- Cloud. The mass, which §22 puts ahead of everything except the silhouette.
    floatField("density", "Thickness", 0.0f, 8.0f, 0.0f, 0.4f, GET(e.tornado.density),
               SETF(e.tornado.density)).fmt("%.4f /m").main().sec("Cloud")
        .tooltip("How much of what is behind the column it hides, per metre travelled. This is\n"
                 "EXTINCTION, not brightness -- Glow below is the light. It is also the control\n"
                 "that decides whether the tornado reads as a dark shape against a bright sky."),
    floatField("coreDensity", "Core fill", 0.0f, 4.0f, 0.0f, 1.5f, GET(e.tornado.field.coreDensity),
               SETF(e.tornado.field.coreDensity)).main()
        .tooltip("How much medium is INSIDE the funnel wall. 0 is a hollow tube whose far wall\n"
                 "shows through its near one; high is a solid smoke column. This and the two rows\n"
                 "below are the whole of the brief's request for a solid core, a hollow eye or a\n"
                 "partly transparent one."),
    floatField("coreRadius", "Core radius", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.tornado.field.coreRadius),
               SETF(e.tornado.field.coreRadius)).main()
        .tooltip("Where the interior fill starts falling off, as a fraction of the funnel radius."),
    floatField("shellGain", "Wall", 0.0f, 8.0f, 0.0f, 3.0f, GET(e.tornado.field.shellGain),
               SETF(e.tornado.field.shellGain)).main()
        .tooltip("How dense the condensation sheath at the funnel's surface is. A real funnel IS\n"
                 "that sheath -- water condenses where the pressure drop is steepest -- which is\n"
                 "why the density peaks at the wall rather than at the axis."),
    floatField("shellWidth", "Wall width", 0.005f, 2.0f, 0.05f, 0.8f,
               GET(e.tornado.field.shellWidth), SETF(e.tornado.field.shellWidth)).main()
        .tooltip("How thick that sheath is, as a fraction of the radius at each height.\n"
                 "Narrow reads as a violent, tightly wound storm; wide reads as a soft column."),
    floatField("edgeSoft", "Edge softness", 0.01f, 3.0f, 0.05f, 1.5f, GET(e.tornado.field.edgeSoft),
               SETF(e.tornado.field.edgeSoft)).main()
        .tooltip("How far past the wall the medium fades to nothing. There is deliberately no\n"
                 "hard edge anywhere in this field for a line to live on."),
    floatField("wallCloudGain", "Funnel flare", 0.0f, 8.0f, 0.0f, 4.0f,
               GET(e.tornado.field.wallCloudGain), SETF(e.tornado.field.wallCloudGain)).main()
        .tooltip("How much the funnel ITSELF widens toward the top. The mass of the cloud above it\n"
                 "is the three rows below; this is only the join between them."),
    floatField("cloudDensity", "Wall cloud", 0.0f, 6.0f, 0.0f, 3.0f,
               GET(e.tornado.field.cloudDensity), SETF(e.tornado.field.cloudDensity)).main()
        .tooltip("The rotating cloud base the funnel descends from, as a mass of its own. Without\n"
                 "some of this the column reads as a floating tube, because nothing in the picture\n"
                 "explains what it is hanging from."),
    floatField("cloudWidth", "Wall cloud width", 1.0f, 20.0f, 1.0f, 10.0f,
               GET(e.tornado.field.cloudWidth), SETF(e.tornado.field.cloudWidth)).fmt("%.2f x").main()
        .tooltip("Multiples of the funnel's top radius. Real wall clouds run three to ten times the\n"
                 "funnel's width, and that ratio is most of what makes the silhouette read: a thin\n"
                 "column under a BROAD cloud. Bring it near 1 and the two merge into a smooth\n"
                 "trumpet, which is a vortex and is not a tornado."),
    floatField("cloudHeight", "Wall cloud depth", 0.005f, 0.9f, 0.02f, 0.4f,
               GET(e.tornado.field.cloudHeight), SETF(e.tornado.field.cloudHeight)).main()
        .tooltip("How far down from the top the cloud reaches, as a fraction of the height."),

    // ---- Ground. The skirt, which is the strongest read cue after the silhouette.
    floatField("touchdown", "Touchdown", 0.0f, 1.0f, 0.0f, 1.0f, GET(e.tornado.field.touchdown),
               SETF(e.tornado.field.touchdown)).main().sec("Ground")
        .tooltip("How far down the condensation funnel has reached. At 1 it touches the ground; \n"
                 "below that it hangs, and only the debris skirt marks the circulation at the\n"
                 "surface. That is not a stylisation -- debris swirls are visible BEFORE the\n"
                 "funnel condenses to the ground, and this is the control that animates it."),
    floatField("skirtDensity", "Debris", 0.0f, 6.0f, 0.0f, 2.5f, GET(e.tornado.field.skirtDensity),
               SETF(e.tornado.field.skirtDensity)).main()
        .tooltip("The cloud of dust and debris thrown up where the column meets the ground.\n"
                 "Reach for this before anything else if the tornado is not reading: a funnel\n"
                 "without a skirt reads as a cloud, not as a tornado."),
    floatField("skirtWidth", "Debris width", 1.0f, 12.0f, 1.0f, 5.0f, GET(e.tornado.field.skirtWidth),
               SETF(e.tornado.field.skirtWidth)).fmt("%.2f x").main()
        .tooltip("Multiples of the ground radius. Real debris clouds run 1.5 to 3 times the\n"
                 "funnel's width at the ground."),
    floatField("skirtHeight", "Debris height", 0.005f, 0.8f, 0.02f, 0.35f,
               GET(e.tornado.field.skirtHeight), SETF(e.tornado.field.skirtHeight)).main()
        .tooltip("As a fraction of the total height. 5 to 15 per cent is what photographs show."),
    floatField("skirtFlare", "Debris flare", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.tornado.field.skirtFlare),
               SETF(e.tornado.field.skirtFlare)).main()
        .tooltip("How much the skirt widens DOWNWARD -- the opposite sense to the funnel's taper,\n"
                 "which is what produces the hourglass read where the two meet."),

    // ---- Rotation. §21's striations, which are what make a STILL frame read as turning.
    floatField("stripeCount", "Striations", 0.0f, 32.0f, 0.0f, 12.0f,
               GET(e.tornado.field.stripeCount), SETF(e.tornado.field.stripeCount))
        .fmt("%.0f").main().sec("Rotation")
        .tooltip("How many condensation bands wind up the funnel. 0 switches them off and leaves\n"
                 "the column smooth, which is the diagnostic: a smooth funnel is ambiguous about\n"
                 "whether it is spinning at all, and no amount of motion in playback fixes a\n"
                 "still frame that is not."),
    floatField("stripePitch", "Striation pitch", -40.0f, 40.0f, 0.0f, 12.0f,
               GET(e.tornado.field.stripePitch), SETF(e.tornado.field.stripePitch)).main()
        .tooltip("Turns of the helix over the full height. Near 0 the bands are horizontal rings;\n"
                 "large values wind them into a tight barber's pole. Negative reverses the\n"
                 "handedness against the rotation, which reads as shear."),
    floatField("stripeDepth", "Striation contrast", 0.0f, 0.68f, 0.0f, 0.68f,
               GET(e.tornado.field.stripeDepth), SETF(e.tornado.field.stripeDepth)).main()
        .tooltip("How much denser a band is than the gap beside it. Capped below 1 so the density\n"
                 "cannot go negative whatever the harmonics do. These cost nothing to sample and\n"
                 "cannot alias, so this is the control to reach for before any detail."),
    floatField("stripeHarmonic", "Striation detail", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.tornado.field.stripeHarmonic), SETF(e.tornado.field.stripeHarmonic)).main()
        .tooltip("Adds two finer sets of bands inside the main ones, at a third and a ninth of the\n"
                 "contrast. Structure rather than noise: it survives freezing time and going\n"
                 "monochrome, which is the test."),
    floatField("rotationBottom", "Spin (low)", -8.0f, 8.0f, 0.0f, 3.0f,
               GET(e.tornado.field.rotationBottom), SETF(e.tornado.field.rotationBottom)).main()
        .tooltip("A multiplier on the swirl near the ground. The core spins faster where it is\n"
                 "narrower, and a uniform angular velocity is the single most recognisable tell\n"
                 "of a turntable rather than a storm."),
    floatField("rotationTop", "Spin (high)", -8.0f, 8.0f, 0.0f, 3.0f,
               GET(e.tornado.field.rotationTop), SETF(e.tornado.field.rotationTop)).main()
        .tooltip("The same multiplier where the funnel enters the wall cloud."),

    // ---- Appearance.
    colorField("colorThin", "Thin colour", GET(e.tornado.colorThin), SETC(e.tornado.colorThin))
        .main().sec("Appearance")
        .tooltip("What a thin sliver of the medium looks like -- the edges and the wisps."),
    colorField("colorThick", "Thick colour", GET(e.tornado.colorThick), SETC(e.tornado.colorThick))
        .main()
        .tooltip("What the densest part looks like. Thin lighter than thick is ordinary smoke;\n"
                 "thick lighter than thin is a luminous, cosmic storm."),
    floatField("emission", "Glow", 0.0f, 20.0f, 0.0f, 1.0f, GET(e.tornado.emission),
               SETF(e.tornado.emission)).fmt("%.3f /m").main()
        .tooltip("Light the column makes for itself, per metre. 0 is ordinary smoke that is only\n"
                 "LIT, which is the default and is what a tornado is. Raise it for the cosmic\n"
                 "direction, where the storm glows on a black sky."),
    floatField("scattering", "Scene light", 0.0f, 4.0f, 0.0f, 2.0f, GET(e.tornado.scattering),
               SETF(e.tornado.scattering)).main()
        .tooltip("How much of the SCENE's light this medium scatters. It defaults to 1 -- lit --\n"
                 "which is the reverse of a cosmic vortex, and the reversal is the point: a nebula\n"
                 "below an island is not lit by that island, and a storm column the sun does not\n"
                 "touch is the one thing that cannot read as one. Turn it to 0 for a storm that\n"
                 "makes all of its own light."),

    // ---- Motion and asymmetry. §28: a physically perfect tornado is not the most cinematic one.
    floatField("wobbleAmount", "Wobble", 0.0f, 2000.0f, 0.0f, 120.0f,
               GET(e.tornado.field.wobbleAmount), SETF(e.tornado.field.wobbleAmount))
        .fmt("%.1f m").main().sec("Motion")
        .tooltip("How far the column snakes off its own axis, in metres, at the top. Two\n"
                 "incommensurate frequencies over the height, so it snakes rather than bows.\n"
                 "Scaled toward zero at the ground: a tornado is pinned at the surface, and a\n"
                 "base that wanders is a base that leaves the frame while the shot is held."),
    floatField("wobbleSpeed", "Wobble speed", 0.0f, 4.0f, 0.0f, 0.8f,
               GET(e.tornado.field.wobbleSpeed), SETF(e.tornado.field.wobbleSpeed)).main(),

    // ---- Advanced: placement and the flow field itself.
    floatField("baseX", "Base X", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.tornado.field.base.x),
               SETF(e.tornado.field.base.x)).json("base/0").fmt("%.1f m").sec("Placement"),
    floatField("baseY", "Base Y", -1e5f, 1e5f, -500.0f, 500.0f, GET(e.tornado.field.base.y),
               SETF(e.tornado.field.base.y)).json("base/1").fmt("%.1f m"),
    floatField("baseZ", "Base Z", -1e5f, 1e5f, -2000.0f, 2000.0f, GET(e.tornado.field.base.z),
               SETF(e.tornado.field.base.z)).json("base/2").fmt("%.1f m")
        .tooltip("Where the column MEETS THE GROUND -- not its centre. A tornado is defined by its\n"
                 "contact point: the debris is there and the taper is measured from there. A\n"
                 "cosmic vortex's centre is the mouth of a funnel that descends, which is the\n"
                 "opposite convention, and confusing the two puts the column underground."),
    floatField("leanX", "Lean X", -1e4f, 1e4f, -500.0f, 500.0f, GET(e.tornado.field.lean.x),
               SETF(e.tornado.field.lean.x)).json("lean/0").fmt("%.1f m").sec("Asymmetry"),
    floatField("leanZ", "Lean Z", -1e4f, 1e4f, -500.0f, 500.0f, GET(e.tornado.field.lean.y),
               SETF(e.tornado.field.lean.y)).json("lean/1").fmt("%.1f m")
        .tooltip("Metres of lateral offset at the TOP, quadratic in height, so the column stands\n"
                 "vertical where it meets the ground and tilts aloft. This is also what a wind\n"
                 "subscription drives: a storm column answers weather by bending, not by sliding."),
    floatField("footSoft", "Foot softness", 0.001f, 0.5f, 0.005f, 0.2f,
               GET(e.tornado.field.footSoft), SETF(e.tornado.field.footSoft)).sec("Shape"),
    floatField("circulation", "Circulation", 0.0f, 100000.0f, 0.0f, 5000.0f,
               GET(e.tornado.field.circulation), SETF(e.tornado.field.circulation))
        .fmt("%.0f m2/s").log().sec("Flow field")
        .tooltip("Gamma over two pi: the strength of the swirl in the Burgers-Rott velocity field.\n"
                 "Nothing in the picture reads this yet -- it is what particles, the grid solver\n"
                 "and a debug overlay sample, and what the striations turn at."),
    floatField("coreRadiusMetres", "Vortex core", 0.0f, 4000.0f, 0.0f, 200.0f,
               GET(e.tornado.field.coreRadiusMetres), SETF(e.tornado.field.coreRadiusMetres))
        .fmt("%.1f m")
        .tooltip("The radius of maximum wind, in metres. 0 means TRACK THE FUNNEL and take the\n"
                 "ground radius, which is the default because a fixed core against an animated\n"
                 "radius is a tornado whose visible funnel and whose fastest air stop agreeing."),
    floatField("inflow", "Inflow", -4.0f, 4.0f, 0.0f, 1.0f, GET(e.tornado.field.inflow),
               SETF(e.tornado.field.inflow)).fmt("%.3f /s")
        .tooltip("The strain rate. It is ONE number for the inward pull and the updraft together,\n"
                 "and that is physics rather than a saving: Burgers-Rott is divergence-free exactly\n"
                 "because the same rate appears in both, so air is neither created nor destroyed."),
    floatField("lift", "Lift", 0.0f, 8.0f, 0.0f, 3.0f, GET(e.tornado.field.lift),
               SETF(e.tornado.field.lift))
        .tooltip("Scales the updraft against the inflow. 1 is the physical value and anything else\n"
                 "breaks the divergence-free cancellation above -- which is allowed, because the\n"
                 "brief asks for the control, but it is stated here rather than discovered."),
    floatField("rotationCurve", "Spin curve", 0.05f, 6.0f, 0.2f, 3.0f,
               GET(e.tornado.field.rotationCurve), SETF(e.tornado.field.rotationCurve))
        .sec("Rotation"),
};

#undef GET
#undef SETF
#undef SETC

// §47 asks for a general procedural tornado generator rather than a one-off, and the way to show
// that is presets that are genuinely different STORMS rather than three tints of one. These are the
// silhouettes the references name, at the proportions they give.
constexpr std::array<std::string_view, 5> kStyleNames{"Classic Cone", "Rope", "Wedge",
                                                      "Dust Devil", "Cosmic Storm"};

void reset(Tornado& tn) {
    // Every style writes every field it touches, for the reason the comet's and the vortex's do: a
    // style that leaves the previous one's skirt behind reads as the preset being broken. The BASE
    // is deliberately kept -- a preset that moves the thing you placed is a preset you cannot use.
    const glm::vec3 keep = tn.field.base;
    tn = Tornado{};
    tn.field.base = keep;
}

bool applyStyle(AtmosphericEffect& e, std::string_view style) {
    Tornado& tn = e.tornado;
    if (style == kStyleNames[0]) { // Classic Cone -- 800 m over 80 m at the ground, about 10:1
        reset(tn);
        tn.field.height = 800.0f;
        tn.field.radiusBottom = 40.0f;
        tn.field.radiusMid = 52.0f;
        tn.field.radiusTop = 70.0f;
        tn.field.taper = 1.4f;
        tn.field.shellWidth = 0.22f;
        tn.field.shellGain = 1.0f;
        tn.field.coreRadius = 0.55f;
        tn.field.coreDensity = 0.35f;
        tn.field.wallCloudGain = 0.6f;
        tn.field.cloudWidth = 4.6f;
        tn.field.cloudHeight = 0.16f;
        tn.field.cloudDensity = 0.9f;
        tn.field.skirtWidth = 2.2f;
        tn.field.skirtHeight = 0.10f;
        tn.field.skirtDensity = 0.8f;
        tn.field.skirtFlare = 0.6f;
        tn.field.stripeCount = 4.0f;
        tn.field.stripePitch = 3.4f;
        tn.field.stripeDepth = 0.35f;
        tn.field.stripeHarmonic = 0.4f;
        tn.field.circulation = 900.0f;
        tn.field.inflow = 0.25f;
        tn.field.rotationBottom = 1.0f;
        tn.field.rotationTop = 0.55f;
        tn.field.wobbleAmount = 18.0f;
        tn.field.wobbleSpeed = 0.15f;
        tn.density = 0.06f;
        tn.emission = 0.0f;
        tn.scattering = 1.0f;
        tn.colorThin = {0.62f, 0.60f, 0.58f};
        tn.colorThick = {0.16f, 0.15f, 0.16f};
        return true;
    }
    if (style == kStyleNames[1]) { // Rope -- the dissipation stage, 1000 m over 30 m, about 33:1
        reset(tn);
        tn.field.height = 1000.0f;
        tn.field.radiusBottom = 12.0f;
        tn.field.radiusMid = 18.0f;
        tn.field.radiusTop = 42.0f;
        tn.field.taper = 2.4f;
        tn.field.shellWidth = 0.42f;
        tn.field.shellGain = 1.3f;
        tn.field.coreRadius = 0.4f;
        tn.field.coreDensity = 0.5f;
        tn.field.wallCloudGain = 0.5f;
        tn.field.cloudWidth = 6.0f;
        tn.field.cloudHeight = 0.13f;
        tn.field.cloudDensity = 1.0f;
        tn.field.skirtWidth = 3.5f;
        tn.field.skirtHeight = 0.06f;
        tn.field.skirtDensity = 0.5f;
        tn.field.skirtFlare = 1.1f;
        tn.field.stripeCount = 3.0f;
        tn.field.stripePitch = 9.0f;
        tn.field.stripeDepth = 0.45f;
        tn.field.stripeHarmonic = 0.3f;
        tn.field.circulation = 500.0f;
        tn.field.inflow = 0.15f;
        tn.field.rotationBottom = 1.6f;
        tn.field.rotationTop = 0.4f;
        // A rope contorts, and the references say so: "may also become contorted", the funnel
        // lengthening as the winds inside it weaken by conservation of angular momentum.
        tn.field.wobbleAmount = 95.0f;
        tn.field.wobbleSpeed = 0.42f;
        tn.density = 0.09f;
        tn.emission = 0.0f;
        tn.scattering = 1.0f;
        tn.colorThin = {0.66f, 0.64f, 0.62f};
        tn.colorThick = {0.20f, 0.19f, 0.19f};
        return true;
    }
    if (style == kStyleNames[2]) { // Wedge -- the WMO's definition: at least as wide as it is tall
        reset(tn);
        tn.field.height = 700.0f;
        tn.field.radiusBottom = 330.0f;
        tn.field.radiusMid = 360.0f;
        tn.field.radiusTop = 430.0f;
        tn.field.taper = 0.7f;
        tn.field.shellWidth = 0.5f;
        tn.field.shellGain = 0.7f;
        tn.field.coreRadius = 0.25f;
        tn.field.coreDensity = 1.1f;
        tn.field.wallCloudGain = 0.4f;
        tn.field.cloudWidth = 2.0f;
        tn.field.cloudHeight = 0.26f;
        tn.field.cloudDensity = 1.3f;
        tn.field.skirtWidth = 1.5f;
        tn.field.skirtHeight = 0.16f;
        tn.field.skirtDensity = 1.6f;
        tn.field.skirtFlare = 0.35f;
        tn.field.stripeCount = 6.0f;
        tn.field.stripePitch = 1.6f;
        tn.field.stripeDepth = 0.28f;
        tn.field.stripeHarmonic = 0.55f;
        tn.field.circulation = 4200.0f;
        tn.field.inflow = 0.45f;
        tn.field.rotationBottom = 0.85f;
        tn.field.rotationTop = 0.6f;
        tn.field.wobbleAmount = 30.0f;
        tn.field.wobbleSpeed = 0.1f;
        tn.density = 0.05f;
        tn.emission = 0.0f;
        tn.scattering = 1.0f;
        tn.colorThin = {0.46f, 0.43f, 0.40f};
        tn.colorThick = {0.10f, 0.09f, 0.09f};
        return true;
    }
    if (style == kStyleNames[3]) { // Dust Devil -- small, fast, all skirt and no condensation
        reset(tn);
        tn.field.height = 70.0f;
        tn.field.radiusBottom = 3.5f;
        tn.field.radiusMid = 5.0f;
        tn.field.radiusTop = 9.0f;
        tn.field.taper = 1.3f;
        tn.field.shellWidth = 0.6f;
        tn.field.shellGain = 0.5f;
        tn.field.coreRadius = 0.3f;
        tn.field.coreDensity = 0.9f;
        tn.field.wallCloudGain = 0.0f;
        tn.field.cloudWidth = 1.0f;
        tn.field.cloudHeight = 0.02f;
        tn.field.cloudDensity = 0.0f; // nothing to hang from: a dust devil is surface-driven
        tn.field.touchdown = 1.0f;
        tn.field.skirtWidth = 4.0f;
        tn.field.skirtHeight = 0.30f;
        tn.field.skirtDensity = 1.8f;
        tn.field.skirtFlare = 1.4f;
        tn.field.stripeCount = 2.0f;
        tn.field.stripePitch = 5.0f;
        tn.field.stripeDepth = 0.30f;
        tn.field.stripeHarmonic = 0.2f;
        tn.field.circulation = 60.0f;
        tn.field.inflow = 0.8f;
        tn.field.rotationBottom = 2.2f;
        tn.field.rotationTop = 1.2f;
        tn.field.wobbleAmount = 6.0f;
        tn.field.wobbleSpeed = 1.1f;
        tn.density = 0.35f;
        tn.emission = 0.0f;
        tn.scattering = 1.2f;
        tn.colorThin = {0.72f, 0.60f, 0.42f};
        tn.colorThick = {0.34f, 0.24f, 0.14f};
        return true;
    }
    if (style == kStyleNames[4]) { // Cosmic Storm -- the Tree of Life direction, still a tornado
        reset(tn);
        tn.field.height = 1400.0f;
        tn.field.radiusBottom = 70.0f;
        tn.field.radiusMid = 110.0f;
        tn.field.radiusTop = 150.0f;
        tn.field.taper = 1.5f;
        tn.field.shellWidth = 0.3f;
        tn.field.shellGain = 1.6f;
        tn.field.coreRadius = 0.5f;
        tn.field.coreDensity = 0.25f;
        tn.field.wallCloudGain = 0.8f;
        tn.field.cloudWidth = 5.0f;
        tn.field.cloudHeight = 0.20f;
        tn.field.cloudDensity = 1.1f;
        tn.field.skirtWidth = 2.6f;
        tn.field.skirtHeight = 0.09f;
        tn.field.skirtDensity = 0.9f;
        tn.field.skirtFlare = 0.8f;
        tn.field.stripeCount = 5.0f;
        tn.field.stripePitch = 5.5f;
        tn.field.stripeDepth = 0.5f;
        tn.field.stripeHarmonic = 0.6f;
        tn.field.circulation = 2600.0f;
        tn.field.inflow = 0.3f;
        tn.field.rotationBottom = 1.3f;
        tn.field.rotationTop = 0.5f;
        tn.field.wobbleAmount = 60.0f;
        tn.field.wobbleSpeed = 0.22f;
        tn.density = 0.05f;
        // The one preset that makes its own light, and it takes none of the scene's: the whole
        // point of the cosmic direction is a storm on a black sky.
        tn.emission = 0.22f;
        tn.scattering = 0.15f;
        tn.colorThin = {0.28f, 0.62f, 0.78f};
        tn.colorThick = {0.06f, 0.10f, 0.30f};
        return true;
    }
    return false;
}

void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }
void style4(E& e) { applyStyle(e, kStyleNames[4]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), style0}, {kStyleNames[1].data(), style1},
    {kStyleNames[2].data(), style2}, {kStyleNames[3].data(), style3},
    {kStyleNames[4].data(), style4},
};

// §34 asks for modulation targets that produce visually useful MUSICAL responses rather than
// arbitrary low-level parameters, so these are chosen for what they do to the picture:
//
//   bass    -> thickness, because a storm that thickens on the low end is weather answering music,
//              and -> debris, because the ground is where the low end belongs.
//   mid     -> wobble, the column snaking. It is the motion the eye reads as force.
//   treble  -> striation contrast, which is the finest structure the field has and the only one
//              fast enough to answer a hi-hat without smearing.
//   beat    -> glow, the same leaf `beatLeaf` names, so the Beat response slider finds this route
//              instead of writing a second one beside it.
//
// Every depth is a fraction of the leaf's soft range.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "density", 0.060f, 80.0f, 520.0f},
    {"audio.bass", "skirtDensity", 0.500f, 60.0f, 400.0f},
    {"audio.mid", "wobbleAmount", 20.0f, 120.0f, 700.0f},
    {"audio.treble", "stripeDepth", 0.150f, 25.0f, 240.0f},
    {"beat.pulse", "emission", 0.080f, 12.0f, 300.0f},
};

AtmosphericEffect make(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::Tornado;
    e.style = std::string(kStyleNames[0]);
    applyStyle(e, kStyleNames[0]);
    // Weather, not an event. A tornado that appears is a cut; one that gathers is a storm, so the
    // fade-in is long and the fade-out is not, because they go before they are gone.
    e.activation = Activation::Always;
    e.timing.fadeIn = 4.0;
    e.timing.fadeOut = 0.0;
    // No ground pool. ADR-230's ground glow is a coloured patch cast on terrain by something in the
    // SKY; a tornado stands on the terrain, and what it does to the ground is the debris skirt,
    // which is geometry. A combo that changed nothing would be worse than no combo (ADR-421).
    e.ground.mode = GroundGlow::Off;
    return e;
}

// A tornado has no per-frame trajectory to resolve -- it is a static field the march samples -- so
// resolution is "is it live", and the payload travels unchanged. §68: its anchor is its own BASE,
// because the question "what is the air doing where this storm stands" is asked at the ground
// contact, which is also where a wind field is defined (ADR-055's wind is columnar).
bool fill(const AtmosphericEffect& e, std::size_t, const AtmosphericContext& ctx,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    r.anchor = e.tornado.field.base;
    const EffectFlow flow = resolveEffectFlow(e, r.anchor, ctx);
    r.flow = flow.sample;
    r.flowInfluence = flow.influence;
    return true;
}

Result<void> validate(const AtmosphericEffect& e) { return e.tornado.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = AtmosphereKind::Tornado;
    s.key = "tornado";
    s.enumName = "Tornado";
    s.displayName = "Tornado";
    s.addLabel = "Add tornado";
    s.addTip = "A rotating column of dust and condensate standing in the world: a funnel with a\n"
               "debris skirt at its foot and a wall cloud at its head, read from the side.\n"
               "Drawn by the volumetric march, so terrain and objects sit INSIDE it rather\n"
               "than in front of it. It has a medium slot of its own, so a fog bank does not\n"
               "displace it.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "emission";
    s.groundGlow = false;
    s.factory = make;
    s.resolve.bucket = EffectBucket::Tornado;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& tornadoSchema() {
    // A function-local static: built on first use, so it cannot be read before its own dynamic
    // initialisation has run. `builtinSchemas()` holds pointers to these, and a namespace-scope
    // object would make that a static-initialisation-order question nobody should have to answer.
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
