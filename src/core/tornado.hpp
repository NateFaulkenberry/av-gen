#pragma once

// The tornado field (ADR-580): one deterministic, spatial field that every part of the renderer can
// ask "what is the medium doing at this point, at this time".
//
// Built on the precedent `core/wind.hpp` set and `core/vortex.hpp` repeated, deliberately and in
// the same shape, because it has now worked twice: a pure function of (packed parameters, position,
// time), no state, no wall clock, no per-instance storage, with `shaders/tornado.wgsl` as the
// transliteration and a test that compares the two through the *packed* form so both sides start
// from bytes that are identical by construction.
//
// ADR-388 records what the alternative costs. Before it, the vortex existed only inside
// `volume.wgsl`, so the only thing in the engine that could ask where the funnel was was the
// volumetric march; particles approximated it with an attractor and an orbit force, which is a
// different shape that happens to look similar. This field has four consumers by design -- the
// march (density), particles (§25's secondary layer, through `world::fields::FieldBus`), the
// ADR-032 grid solver (§24's advection, through a vector `FieldKind`) and a debug overlay (§38's
// diagnostic modes) -- and one answer between them.
//
// ---- what this is not --------------------------------------------------------------------------
//
// It is not `VortexField` with different names, and the brief's §2 forbidding that is not a
// temptation this file had to resist. `VortexField` describes a **cyclone**: mouth radius, eye, eye
// wall, spiral rainbands at a pitch angle, funnel depth, throat -- every one a feature of the
// horizontal plane at a given height, and its entire macro structure is a function of `(rr, angle)`
// with height entering as a Gaussian wall and a taper. It is a thing you look down at. A tornado's
// structure is in the vertical plane and is read from the side. There is no parameter of one that
// is a parameter of the other.
//
// ---- the split, and the trap it avoids -----------------------------------------------------------
//
// This is the GEOMETRY AND MOTION half only, exactly as `VortexField` is. Colour, emission,
// per-metre density and the rest of the appearance live on `world::Tornado`, because they are what
// the picture does with the field rather than part of it: a particle asking "which way is the
// medium moving here" must not have to carry a colour ramp to find out.
//
// Units, and the trap this file is one call away from. On `world::Tornado`, `density` is an
// extinction coefficient PER METRE and `emission` is an emissive density PER METRE; they are
// integrated over the march's step length. Adding a radiance to that integral without the
// conversion has been wrong four times in this codebase (ADR-374's density, ADR-379's spill,
// ADR-381's comet-on-fog, ADR-389's coefficient). `TornadoSample::density` is deliberately the
// normalised SHAPE in 0..1 and the caller multiplies by the per-metre coefficient, so the
// conversion happens where somebody can see both sides of it.

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::tornado {

inline constexpr float kTau = 6.28318530718f;

// ---- the field -----------------------------------------------------------------------------------

// Where the column stands, what shape it is, and how it turns. Metres, seconds and radians.
//
// The defaults are a **classic cone** at the proportions the references give: 800 m tall with an
// 80 m ground width is 10:1, inside the 6:1 to 15:1 the literature reports for a photogenic classic
// tornado. A rope is 20:1 to 40:1 and a wedge is 1:1 or wider by the WMO's own definition, and both
// are reachable from these controls without touching code, which is §37's G.
struct TornadoField {
    // The GROUND CONTACT point -- not a centre, and the distinction is load-bearing. A tornado is
    // defined by where it meets the surface: the debris skirt is there, the taper is measured from
    // there, and an artist placing one is placing its foot. `VortexField::center` is the mouth of a
    // funnel that descends, which is the opposite convention, and confusing the two puts the column
    // underground.
    glm::vec3 base{0.0f};
    float height = 0.0f; // metres to the wall cloud; 0 is off, is the default, and is the gate

    // ---- silhouette (§14, §16) ------------------------------------------------------------------
    //
    // Three radii and a taper, evaluated as a quadratic Bezier, which reaches every shape the
    // references name from one expression: cone, stovepipe, hourglass, wedge, rope. A cone with an
    // exponent -- the obvious cheaper thing -- cannot be an hourglass at all, which is why this is
    // a curve.
    float radiusBottom = 40.0f;
    float radiusMid = 52.0f; // the radius at MID HEIGHT; `packTornado` converts it to the control point
    float radiusTop = 130.0f;
    float taper = 1.0f; // warps where along the height the middle radius bites

    // ---- the condensation shell (§15, §22) --------------------------------------------------------
    //
    // A funnel is a surface, not a filled cone: water condenses in the sheath where the pressure
    // drop is steepest. `shellGain` is how dense that sheath is, `coreDensity` how much is inside
    // it, and between them they give §15's four requested looks -- a hollow eye, a solid smoky
    // core, a partially transparent one, and the turbulent case once detail arrives.
    float shellWidth = 0.22f;  // as a fraction of the radius at this height
    float shellGain = 1.0f;
    float coreRadius = 0.55f;  // where the interior fill starts falling off
    float coreDensity = 0.35f; // 0 is a hollow tube; 1 is a solid column
    float edgeSoft = 0.35f;    // how far past the shell the medium fades to nothing

    // ---- vertical profile -------------------------------------------------------------------------
    float wallCloudGain = 0.6f; // the funnel's own slight widening into the cloud above it
    // The wall cloud as a MASS of its own, which is a correction the first render forced. Making
    // it the funnel's top radius produced a smooth trumpet -- a champagne flute -- because a
    // continuous flare is the one thing that cannot read as a tornado. The references put the
    // cloud at three to ten times the funnel's width, and the proportion that says "tornado" is a
    // thin column under a broad cloud with a SHOULDER between them.
    float cloudWidth = 3.2f;   // multiples of `radiusTop`
    float cloudHeight = 0.16f; // as a fraction of the total height
    float cloudDensity = 0.9f;
    // §16's lifecycle as one control: how far down the funnel has condensed. Below 1 it hangs, and
    // only the skirt marks the circulation at the surface -- which is the order real tornadoes do
    // it in, the references being blunt that debris swirls appear before the funnel reaches ground.
    float touchdown = 1.0f;
    float footSoft = 0.04f;

    // ---- the debris skirt --------------------------------------------------------------------------
    //
    // The strongest read cue after the silhouette, and structure rather than decoration. The
    // references put it at 1.5x to 3x the funnel's ground width over 5-15% of the height, flaring
    // DOWNWARD against the funnel's taper -- which is what produces the hourglass read where the
    // two meet.
    float skirtWidth = 2.2f;   // multiples of `radiusBottom`
    float skirtHeight = 0.10f; // as a fraction of the total height
    float skirtDensity = 0.8f;
    float skirtFlare = 0.6f;

    // ---- helical striations (§21) --------------------------------------------------------------------
    //
    // The tornado's transposition of a cyclone's spiral rainbands, out of the (radius, angle) plane
    // and into the (angle, height) plane. This is what makes a STILL FRAME read as rotating; a
    // smooth funnel is ambiguous about whether it is spinning at all.
    float stripeCount = 4.0f;
    float stripePitch = 3.4f; // turns of the helix over the full height
    float stripeDepth = 0.35f;
    float stripeHarmonic = 0.4f;

    // ---- secondary vortices (§27) and what §20's vorticity actually drives -------------------
    //
    // Real violent tornadoes are multi-vortex: two to six *suction vortices* orbit the parent axis
    // at roughly the radius of maximum wind, turning faster than the parent, each a scaled copy of
    // the same circulation. They are structure -- a named physical thing -- rather than detail,
    // which is why they are here and not in the breakup stage.
    //
    // ADR-580 refuses vorticity confinement in the analytic tier, because confinement restores
    // energy that numerical diffusion removed and an analytic field has none. THIS is what §20's
    // Vorticity control drives, and the visual consequence §20 asks for is delivered by a structure
    // that cannot dissipate because it is evaluated rather than integrated.
    float suctionCount = 0.0f;    // lobes; 0 is off and is the default
    float suctionStrength = 0.0f;
    float suctionRadius = 1.0f;   // where they ride, as a fraction of the funnel radius
    float suctionWidth = 0.35f;   // the radial window's width
    float suctionSpeed = 0.9f;    // their own turn rate, ON TOP of the parent's

    // ---- detail (§21, §26, §29) ----------------------------------------------------------------
    //
    // Everything above is the tornado. This is what makes it look natural, and the distinction is
    // the whole architecture: `cloudAmount` at 0 gives the analytic field exactly, which is §38's
    // Mode 1 and is a slider rather than a rebuild. The detail term's mean is exactly 1, because
    // `density` is a per-metre coefficient calibrated against this field's mean (ADR-389).
    float cloudAmount = 0.0f; // 0 is off and is the default; 1 is the full stack
    float macroAmp = 1.0f;    // §21's huge cloud structures
    float mesoAmp = 0.5f;     // rolling smoke masses
    float microAmp = 0.25f;   // wisps and breakup
    float detailContrast = 1.6f;
    // Scale is ONE control, not three: the meso and micro octaves sit at fixed 3.1x and 9.7x
    // ratios above it. Three independent scale sliders get set to the same number and produce one
    // octave at triple amplitude, which is the failure §21 exists to prevent.
    float detailScale = 2.2f;
    // §29. How fast detail is carried UP the column, in normalised heights per second. This is a
    // change of coordinates rather than an advection: the noise is sampled in the column's
    // co-moving frame, so a feature sits still in a frame that is itself rising and turning.
    float climbRate = 0.06f;
    // How much harder detail bites where the structure is already thin -- what makes wisps break
    // AWAY from the column instead of the whole thing fading evenly.
    //
    // There was a second control beside this, `edgeWidth`, deciding what counted as thin. It is
    // gone, and the reason is a hard constraint rather than taste: ADR-562's medium slot is 16
    // lanes and lane 15 carries the kind tag, so a tornado has 60 floats and wanted 61. One
    // artist control removed deliberately beats a control that silently overwrites the tag that
    // selects this kind's own density function -- which is what the sixteenth lane was doing.
    // `kEdgeWidth` in the shader is this control's old default, which is where it had settled.
    float erosion = 1.2f;

    // ---- motion (§13, §17-§19) ---------------------------------------------------------------------
    //
    // A Burgers-Rott vortex: `u_r = -a r / 2`, `u_z = +a z`, `u_theta = (G / 2 pi r)(1 - e^{-r^2/Rc^2})`.
    // Divergence-free because `-a + a = 0`, which is why the same `a` (`inflow`) appears in both the
    // radial and the axial term. `lift` scales the axial one independently because artists ask for
    // it; anything other than 1 breaks the cancellation, and that is said here rather than found out.
    float circulation = 900.0f;   // Gamma / 2 pi, in m^2/s
    float coreRadiusMetres = 0.0f; // 0 means "track the funnel": packTornado fills it from radiusBottom
    float inflow = 0.25f;          // the strain rate `a`, in 1/s
    float lift = 1.0f;

    float rotationBottom = 1.0f; // multipliers on the swirl, over height (§17)
    float rotationTop = 0.55f;
    float rotationCurve = 1.0f;

    // ---- asymmetry (§28) ----------------------------------------------------------------------------
    glm::vec2 lean{0.0f};    // metres of lateral offset at the top; quadratic in height
    float wobbleAmount = 0.0f; // metres of slow lateral snaking
    float wobbleSpeed = 0.15f;

    [[nodiscard]] bool active() const { return height > 0.0f; }
};

// The GPU-ready form. Nine `vec4`s; the layout is documented at the top of `shaders/tornado.wgsl`
// and the two must agree member for member, which is what the parity test checks.
struct TornadoUniforms {
    glm::vec4 t0{0.0f}; // base.xyz, height (0 = off, the gate)
    glm::vec4 t1{0.0f}; // radiusBottom, radiusMidControl, radiusTop, taper
    glm::vec4 t2{0.0f}; // shellWidth, shellGain, coreRadius, coreDensity
    glm::vec4 t3{0.0f}; // edgeSoft, wallCloudGain, touchdown, footSoft
    glm::vec4 t4{0.0f}; // skirtWidth, skirtHeight, skirtDensity, skirtFlare
    glm::vec4 t5{0.0f}; // stripeCount, stripePitch, stripeDepth, stripeHarmonic
    glm::vec4 t6{0.0f}; // circulation, coreRadiusMetres, inflow, lift
    glm::vec4 t7{0.0f}; // leanX, leanZ, wobbleAmount, wobbleSpeed
    glm::vec4 t8{0.0f}; // rotationBottom, rotationTop, rotationCurve, cloudWidth
    glm::vec4 t9{0.0f};  // cloudHeight, cloudDensity, suctionCount, suctionStrength
    glm::vec4 t10{0.0f}; // suctionRadius, suctionWidth, suctionSpeed, cloudAmount
    glm::vec4 t11{0.0f}; // macroAmp, mesoAmp, microAmp, detailContrast
    glm::vec4 t12{0.0f}; // detailScale, climbRate, erosion, edgeWidth
};
static_assert(sizeof(TornadoUniforms) == 208);

[[nodiscard]] TornadoUniforms packTornado(const TornadoField& field);

// ---- what the medium is doing at a point ------------------------------------------------------------

struct TornadoSample {
    // The normalised shape in 0..1: what `volume.wgsl` multiplies by the per-metre density and
    // emission. Zero outside the column, and exactly zero -- the early-out is part of the function
    // rather than an optimisation the caller may skip, because ADR-374 measured its absence as the
    // cost of the whole effect.
    float density = 0.0f;
    // World-space velocity of the medium, m/s: the Burgers-Rott field above.
    glm::vec3 velocity{0.0f};
    // Where in the column this is, for anything that wants to vary with position without
    // re-deriving the geometry.
    float radialT = 0.0f; // 0 on the axis, 1 at the funnel wall, may exceed 1 just outside
    float heightT = 0.0f; // 0 at the ground contact, 1 at the wall cloud
    // The shape without any detail on it -- which, in Phase 2, is the same number as `density`,
    // because there is no detail yet. It stays a separate member because §38's Mode 1 is exactly
    // this and a consumer wanting containment rather than appearance should not have to pay for
    // detail to get it.
    float envelope = 0.0f;
};

// `p` is a world position; `t` is render time in seconds. Pure: the same arguments give the same
// answer on the CPU and on the GPU, at any frame rate, in any order.
[[nodiscard]] TornadoSample sampleTornado(const TornadoUniforms& v, const glm::vec3& p, float t);

// The shape alone, which is what the volumetric march wants and all it wants.
//
// `filterWidth` is the world-space distance between the caller's samples, and 0 means "a point
// sample, not an integral". The analytic terms are band-limited by construction and ignore it; the
// detail stack uses it to fade out any octave whose world period falls below twice that spacing,
// because what such an octave contributes is not detail but aliasing. ADR-389: not a knob -- the
// right answer changes with `volumeSteps` and `volumeMaxDistance`, which change between tiers.
[[nodiscard]] float tornadoDensity(const TornadoUniforms& v, const glm::vec3& p, float t,
                                   float filterWidth = 0.0f);

// The medium's velocity alone, for a consumer that wants where it is going and not what it looks
// like -- the grid solver's advection, and a debug overlay's arrows.
[[nodiscard]] glm::vec3 tornadoVelocity(const TornadoUniforms& v, const glm::vec3& p, float t);

} // namespace avgen::tornado
