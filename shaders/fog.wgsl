// The fog field (ADR-563, the brief's §46 B): a fog bank with SHAPE and no noise in it anywhere.
//
// Why it exists at all, which is ADR-560's finding. A fog bank used to be `shaders/vortex.wgsl`'s
// field with the funnel switched off, and what that leaves is
//
//     rim(rr) * exp(-y^2 / thickness^2)
//
// -- monotone in radius, **completely uniform in angle**, symmetric in height. A circular grey
// disc with a soft edge. So every feature anybody had ever seen in a fog bank came out of the fBM
// stack underneath, which is exactly the "procedural texture rendered as volume" the brief opens by
// rejecting, and it is why §44's first quality bar -- *noise disabled, still looks like fog* -- was
// unreachable by construction rather than by tuning.
//
// Everything here is ANALYTIC: a handful of ALU, no noise, band-limited by construction, so it
// survives the march's sample spacing intact and costs nothing next to the fBM it replaces. The
// same argument `core/vortex.hpp` makes for the cyclone's macro structure, for the same reason.
//
// Needs noise.wgsl for nothing -- deliberately. This file must stay noise-free: the moment it
// samples an fBM, the bar it exists to make reachable becomes unreachable again.
//
//   f0  xyz = centre, w = radius (0 is off, and it is the gate)
//   f1  x = thickness, y = length/width ratio, z = cos(rotation), w = sin(rotation)
//   f2  x = edgeSoftness, y = heightBias, z = heightFalloff, w = domeShape
//   f3  = LANE 7, which the vortex packs as (eyeWallWidth, eyeWallGain, cloudNoise, 0) and a fog
//        bank has no eye to want the first two for. So the fog packer reuses them:
//        x = detail scale, y = drift speed, z = detail amount (cloudNoise), w = 0.
//   f6  = LANE 6, free for a fog bank by the same audit as lane 2. ADR-571 gives it §24's DENSITY
//        RESPONSE CURVE: x = contrast, y = threshold, z = softness, w = 0.
//   f5  = LANE 2, which the vortex packs as part of its field and which NOTHING reads for a fog
//        bank: `mediaLane(s, 2u)` has exactly one reader in the whole march, `mediumVortexUniforms`,
//        and that is the arm a fog bank never takes. So ADR-571 gives it to the bank's MOTION:
//        xyz = a world-space drift velocity in metres per second, w = 0.
//
//        Checked rather than assumed -- `grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl` is
//        the audit, and it is the one ADR-562 §9 prescribes: grep the lane index, not the feature.
//   f4  = LANE 12. x is `spill`, which the SURFACE GLOW reads (ADR-562) and this file must not
//        touch. y and z are the vortex's two zeroes and ADR-566's two new numbers:
//        y = shape index (the primitive), z = height influence, w = §26's EMISSION height
//        influence, which is a separate number from z on purpose: a bank can be densest at its
//        floor and glow evenly, or be uniform and glow only where it is low.
//
//        Both of them in ONE lane on purpose. ADR-562 §9's finding is that a per-kind lane map is
//        only safe where every reader knows which kind it is holding, and the cheapest way to keep
//        that true is to add nothing to a lane a shared accessor already reads. Lane 12.y and 12.z
//        are read by this file and by `world::fogShapeAt`, and by nothing else in the engine.

//
// ADR-713 (§16, flow) and ADR-714 (§25, colour) read seven more lanes, every one of them audited
// free for a fog slot with `grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl` BEFORE a row was
// written -- the same audit ADR-571 ran, and the lane map beside `packMedium` is the contract:
//   f7  = LANE 1.  z = SWIRL, rad/s (the vortex's `rotationSpeed`, packed by `packVortex` where it
//        always was; nothing read it for a fog bank until ADR-713). x/w are the bound's thickness
//        and the density, which this file does not touch.
//   f8  = LANE 3.  x = SWELL amount, y = swell rate (rad/s) -- `breathAmount`/`breathSpeed`, which
//        the BOUND has carried since ADR-566 and the field never read. z/w are emission.
//   f9  = LANE 5.  w = HEIGHT COLOUR amount. x..z are comet and scattering, not read here.
//   f10 = LANE 8.  xyz = DISTANCE COLOUR, w = its amount.
//   f11 = LANE 9, f12 = LANE 10, f13 = LANE 11: rgb are the three depth colours (read by
//        `mediumEmissionAt`, not here); their three `.w` are the HEIGHT COLOUR's r, g, b.
//   and three single slots in lanes this struct already had: f3.y = TURBULENCE amount (the dead
//   `detailDrift` write ADR-571 left behind), f3.w = turbulence scale, f6.w = turbulence rate,
//   and f5.w = the distance colour's range in metres.
struct FogUniformsWgsl {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
    f3: vec4<f32>,
    f4: vec4<f32>,
    f5: vec4<f32>,
    f6: vec4<f32>,
    f7: vec4<f32>,
    f8: vec4<f32>,
    f9: vec4<f32>,
    f10: vec4<f32>,
    f11: vec4<f32>,
    f12: vec4<f32>,
    f13: vec4<f32>,
};

// ADR-566, the brief's §9: the five local volume primitives, plus the bank that was here first.
// The index is lane 12.y; `volumetric_fog_effect.cpp` owns the names an artist picks from and the
// two lists must agree, which `test_fog_primitives.cpp` is the check on.
const kFogShapeBank: u32 = 0u;
const kFogShapeSphere: u32 = 1u;
const kFogShapeEllipsoid: u32 = 2u;
const kFogShapeBox: u32 = 3u;
const kFogShapeCapsule: u32 = 4u;
const kFogShapeCylinder: u32 = 5u;

fn fogShapeKind(f: FogUniformsWgsl) -> u32 {
    return u32(clamp(f.f4.y, 0.0, 5.0) + 0.5);
}

// The bank's horizontal footprint: an ELLIPSE, rotated.
//
// This single term is what breaks "uniform in angle", and it is the cheapest structure available --
// two multiplies and a rotation. A fog bank in the world is not a disc: it lies along a valley, it
// drifts across a lake, it has a long axis. Giving it one means the silhouette changes as the
// camera moves around it, which is most of what makes a volume read as a PLACE rather than as a
// smudge centred on the viewer.
//
// Returns normalised elliptical radius: 1 at the bank's edge, whatever direction that edge is in.
fn fogEllipticalRadius(f: FogUniformsWgsl, rel: vec3<f32>) -> f32 {
    let c = f.f1.z;
    let s = f.f1.w;
    // Into the bank's own frame.
    let x = rel.x * c + rel.z * s;
    let z = -rel.x * s + rel.z * c;
    let radius = max(f.f0.w, 1e-3);
    let along = max(f.f1.y, 0.05); // length as a multiple of width; 1 is the old circle
    let u = x / (radius * along);
    let v = z / radius;
    return sqrt(u * u + v * v);
}

// The vertical profile, and it is NOT the Gaussian it replaces.
//
// `exp(-y^2/t^2)` is symmetric about the centre, which is a cloud floating in nothing. Fog sits ON
// something: it is densest near its base and thins upward, and the rate it thins at is the single
// control that separates ground mist from a standing bank. So the profile is built from a base and
// a falloff rather than from a centre and a width.
//
//   heightBias   0 puts the densest layer at the bank's floor (ground fog), 1 at its top
//   heightFalloff  how fast it thins going up, in units of the thickness
//   domeShape    blends the top from a soft exponential toward a rounded cap, which is the
//                difference between mist dissipating and a bank with a definite lid
fn fogVerticalProfile(f: FogUniformsWgsl, relY: f32) -> f32 {
    let thickness = max(f.f1.x, 1e-3);
    let bias = clamp(f.f2.y, 0.0, 1.0);
    // Height above the densest layer, normalised. The bias slides where that layer sits.
    let base = -thickness + 2.0 * thickness * bias;
    let h = (relY - base) / thickness;
    // Below the densest layer the fog ends quickly -- there is ground, or there is clear air it has
    // settled out of, and either way it is a shorter distance than the thinning above.
    if (h < 0.0) {
        let below = h * 3.0;
        return exp(-below * below);
    }
    let falloff = max(f.f2.z, 0.01);
    let thin = exp(-h * falloff);
    // The rounded lid: 1 at the base, falling to 0 at the top of the layer, squared for a soft
    // shoulder rather than a cone.
    let dome = clamp(1.0 - h * 0.5, 0.0, 1.0);
    return mix(thin, dome * dome, clamp(f.f2.w, 0.0, 1.0));
}

// ---- ADR-713, the brief's §16: the flow controls ----------------------------------------------
//
// Every one of these is a PURE FUNCTION OF THE TRANSPORT SECOND and the packed lanes (ADR-091):
// no history, no accumulation, so a seek to t lands on the same field a play to t does. And every
// one is the IDENTITY at its default by a branch rather than by a small number, so a bank that
// does not use them evaluates the same expressions on the same bits it did before ADR-713.

// SWIRL: the bank's internal structure circulates about its own vertical axis, rigidly, at
// `f7.z` radians a second. It returns where in the structure's REST frame the world point `p` is,
// before the drift -- the drift is subtracted by the callers exactly as ADR-571 wrote it.
//
// Rigid on purpose. A differential swirl (the core turning faster than the rim) winds the
// structure into a spiral whose pitch shrinks without bound as t grows: at 0.02 rad/s it is
// twelve turns of shear after ten minutes, finer than the march can carry. A rigid rotation is
// periodic in t, so the field at t = 3600 is as well-sampled as the field at t = 0.
//
// It moves what is INSIDE the bank -- the detail and the turbulence -- and not the primitive:
// rotating the silhouette itself is `bankRotation` keyframed, which the timeline already does.
fn fogStructureFrame(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> vec3<f32> {
    let omega = f.f7.z;
    if (omega == 0.0) {
        return p;
    }
    let a = -omega * t;
    let c = cos(a);
    let s = sin(a);
    let rel = p - f.f0.xyz;
    return f.f0.xyz + vec3<f32>(rel.x * c - rel.z * s, rel.y, rel.x * s + rel.z * c);
}

// SWELL: the bank widens and narrows, `1 + amount * sin(rate * t)`, the vortex's own breath
// (`vortex.wgsl`) applied to the fog's primitive. Returned as the factor the horizontal offset is
// DIVIDED by, which is the same as multiplying the radius and the long axis by it -- and for the
// sphere, whose one size is the radius in every direction, the vertical too.
//
// The amount is clamped below 0.9 so the divisor never reaches zero; the bound
// (`mediumBoundOf`) has carried `1 + amount` horizontally since ADR-566, which is the most this
// can widen the field. ADR-713 adds the sphere's vertical to it.
fn fogSwell(f: FogUniformsWgsl, t: f32) -> f32 {
    let amount = clamp(f.f8.x, 0.0, 0.9);
    if (amount <= 0.0) {
        return 1.0;
    }
    return 1.0 + amount * sin(t * f.f8.y);
}

fn fogSwellOffset(f: FogUniformsWgsl, rel: vec3<f32>, swell: f32) -> vec3<f32> {
    if (swell == 1.0) {
        return rel;
    }
    if (fogShapeKind(f) == kFogShapeSphere) {
        return rel / swell;
    }
    return vec3<f32>(rel.x / swell, rel.y, rel.z / swell);
}

// The primitive's three semi-axes in its own yawed frame (ax along the long axis, ay up, az
// across), which is what the turbulence is measured against: a displacement of 0.2 means a fifth
// of the bank's own size in each direction, so the same number deforms a 30 m wisp and a 2 km
// bank alike -- ADR-564's size independence applied to motion.
fn fogSemiAxes(f: FogUniformsWgsl) -> vec3<f32> {
    let radius = max(f.f0.w, 1e-3);
    if (fogShapeKind(f) == kFogShapeSphere) {
        return vec3<f32>(radius);
    }
    return vec3<f32>(radius * max(f.f1.y, 0.05), max(f.f1.x, 1e-3), radius);
}

// The flow's magnitude is O(1) with a long tail; this scales it so the clamp below touches only
// the tail (measured in `test_fog_turbulence.cpp`: the clamp engages on under 2% of samples).
const kFogTurbulenceGain: f32 = 1.3;

// TURBULENCE -- and it is also §16's CURL, for the reason ADR-572 gave in advance: a flow sampled
// PER POSITION shears the structure, which is what turbulence is. So there is one mechanism, not
// two rows that do the same thing.
//
// The world point `p` is displaced by a DIVERGENCE-FREE flow (`flowCurl`, noise.wgsl: the cross
// product of two noise gradients, so it swirls and never sources or sinks) sampled in the
// structure's rest frame -- so it drifts and swirls with the bank -- and evolving at `f6.w` of
// its own rate. The WHOLE field is evaluated at the displaced point: silhouette, height profile
// and detail. That is the difference from the detail term, which can only modulate density
// inside a boundary that stays where it was.
//
// Bounded, and the bound is a proof rather than a tuning: the flow is clamped to length 1, so the
// displacement is at most `amount` of each semi-axis, and `mediumBoundOf` grows by exactly that.
// §17's rule holds: this moves density that exists, it cannot make any where the primitive has none
// within `amount` of a semi-axis.
fn fogTurbulence(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> vec3<f32> {
    let amount = clamp(f.f3.y, 0.0, 1.0);
    if (amount <= 0.0) {
        return vec3<f32>(0.0);
    }
    let scale = max(f.f3.w, 0.05);
    let rate = max(f.f6.w, 0.0);
    let rest = fogStructureFrame(f, p, t) - f.f5.xyz * t - f.f0.xyz;
    let c = f.f1.z;
    let s = f.f1.w;
    let semi = fogSemiAxes(f);
    let local = vec3<f32>((rest.x * c + rest.z * s) / semi.x, rest.y / semi.y,
                          (-rest.x * s + rest.z * c) / semi.z);
    var n = flowCurl(local * scale, t * rate, 53u) * kFogTurbulenceGain;
    let len = length(n);
    if (len > 1.0) {
        n = n / len;
    }
    let d = n * amount * semi;
    // Back from the bank's frame to the world's.
    return vec3<f32>(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
}

// How far, in the primitive's normalised distance, a displacement of `amount` semi-axes can move
// a sample -- which is what the early-out below must allow for. 1 for every primitive whose
// distance is a norm of the per-axis ratios; the capsule measures its long axis in units of its
// width, so a displacement along it counts `bankLength` times over.
fn fogTurbulenceReach(f: FogUniformsWgsl) -> f32 {
    if (fogShapeKind(f) == kFogShapeCapsule) {
        return max(f.f1.y, 1.0);
    }
    return 1.0;
}

// §13/§14: MACRO detail, and the three properties that keep it secondary.
//
// 1. **Identity at zero.** `detail` 0 returns exactly 1.0, so the analytic field is untouched and
//    §44's first bar -- the whole reason this file exists -- stays reachable by construction rather
//    than by a small number.
// 2. **Mean-preserving by construction**, which ADR-560 measured the vortex's version is NOT: its
//    `mix(flatLevel, shaped, cloudNoise)` raised the frame's mean luminance by 20.7 levels when the
//    detail was turned OFF, because the compensation assumed a mean the billow and turbulence
//    moved. This is written `1 + amount * (n * 2 - 1)`, whose mean over a zero-centred noise is
//    exactly 1 whatever the amount -- the same form `vortexSpiralBands` uses and for the same
//    reason (ADR-389's family rule).
// 3. **One octave, at a LOW frequency.** The brief's §13: noise distorts large boundaries, it does
//    not create the density. A period tied to the bank's own radius means it cannot introduce
//    spatial frequencies the march's sample spacing cannot carry, which is ADR-389's finding and
//    the reason the vortex needed a band-limit this does not.
fn fogMacroDetail(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> f32 {
    let amount = clamp(f.f3.z, 0.0, 1.0);
    if (amount <= 0.0) {
        return 1.0;
    }
    // Tied to the bank's radius, so the detail is the same SHAPE at any size -- the size
    // independence ADR-564 gave the density, applied to the structure.
    let scale = max(f.f3.x, 0.05) / max(f.f0.w, 1.0);
    // ADR-571, the brief's §15 and §16. **This is an ADVECTION, not an animation**, and §16 gives
    // the form in one line: "sample the density field at `position - velocity x time`". Written
    // that way it has a property no amount of tuning gives an offset added in noise space:
    //
    //     detail(p + v*dt, t + dt) == detail(p, t), EXACTLY.
    //
    // The structure is carried through the world rather than regenerated in place, which is what
    // §15 means by "the fog should move through space, not shimmer internally because the texture
    // changed" -- and `test_fog_flow.cpp` asserts that identity rather than describing it.
    //
    // What it replaced: `p * scale + vec3(drift, drift*0.3, -drift*0.7)`, a hard-coded direction
    // in NOISE space. Its speed therefore depended on `detailScale` and on the bank's radius, so
    // one number meant a different speed in every bank -- and its direction was not a control at
    // all. The artist had a drift knob with no drift direction, which is the first line of §16's
    // list.
    //
    // ADR-713: through `fogStructureFrame`, which is `p` itself at swirl 0 -- so this is the same
    // expression, evaluated on the same bits, as the line ADR-571 wrote.
    let n = fbm3((fogStructureFrame(f, p, t) - f.f5.xyz * t) * scale, 41u);
    return 1.0 + amount * (n * 2.0 - 1.0);
}

// ADR-566, the brief's §9: the NORMALISED DISTANCE to the primitive's surface.
//
// One number for all six shapes, and the contract on it is what makes the rest of the file
// shape-blind: **1 at the surface, less inside, and the field is zero past 1.35 whatever the
// shape is.** Everything downstream -- the rim smoothstep, the early-out, the ray interval in
// `volume.wgsl`, the CPU twin -- was written against that threshold for the bank alone, and it
// keeps working for five more shapes because they are normalised the same way rather than because
// each was given its own cutoff. A per-shape cutoff would be five more numbers for a bound to get
// wrong, and ADR-566 exists because the bound got ONE number wrong.
//
// Semi-axes, in the bank's own yawed frame: `ax` along the long axis, `az` across it, `ay` up.
// `bankLength` is what makes them differ, so the five primitives inherit the bank's one shape
// control rather than declaring five of their own (ADR-500's economy, and §9's "scale").
//
// The forms are deliberately the cheap ones -- a max for the box rather than a rounded SDF, a
// clamp for the capsule -- because every one of these is evaluated per sample per slot. The C0
// creases a `max` leaves on a box's edges are then smoothed by the rim's smoothstep, which is
// already there and already wide.
fn fogPrimitiveDistance(f: FogUniformsWgsl, rel: vec3<f32>) -> f32 {
    let radius = max(f.f0.w, 1e-3);
    let along = max(f.f1.y, 0.05);
    let c = f.f1.z;
    let s = f.f1.w;
    // Into the bank's own frame (a yaw; §9's "rotation where applicable", and for a volume that
    // sits on the ground the applicable rotation is the one about up).
    let x = rel.x * c + rel.z * s;
    let z = -rel.x * s + rel.z * c;
    let y = rel.y;
    let ax = radius * along;
    let az = radius;
    let ay = max(f.f1.x, 1e-3); // thickness is the vertical semi-axis for every closed primitive

    let kind = fogShapeKind(f);
    if (kind == kFogShapeSphere) {
        // Deliberately isotropic: it ignores `bankLength` and `thickness`. A sphere an artist has
        // to set two other controls to 1 to get back is not a sphere, it is a trap.
        return length(vec3<f32>(x, y, z)) / radius;
    }
    if (kind == kFogShapeEllipsoid) {
        return length(vec3<f32>(x / ax, y / ay, z / az));
    }
    if (kind == kFogShapeBox) {
        return max(max(abs(x) / ax, abs(y) / ay), abs(z) / az);
    }
    if (kind == kFogShapeCapsule) {
        // A segment along the long axis with a hemispherical (elliptical in Y) cap at each end.
        // The half-length is what is LEFT of the long axis after the caps, so a capsule at
        // `bankLength` 1 degenerates to the sphere/ellipsoid it should and never inverts.
        let half = max(ax - az, 0.0);
        let qx = x - clamp(x, -half, half);
        return length(vec3<f32>(qx, y * (az / ay), z)) / az;
    }
    if (kind == kFogShapeCylinder) {
        let u = x / ax;
        let v = z / az;
        return max(sqrt(u * u + v * v), abs(y) / ay);
    }
    // kFogShapeBank: horizontal only. Its vertical extent is the PROFILE rather than a semi-axis,
    // which is the whole difference between a bank and the five closed volumes above -- fog lying
    // in a valley has a top that thins, not a lid at a height.
    return fogEllipticalRadius(f, rel);
}

// §24's density response curve: raw density -> remap -> final density.
//
// **It is here because `contrast` was a dead knob.** A fog bank has declared a `Contrast` row
// since it existed, and all three of its shipped styles set it -- Valley Mist 1.5, Glowmere Haze
// 2.6, Dense Bank 3.4. ADR-563 gave the fog its own field and `fogShapeAt` never read the number,
// so from that commit onward an artist loading "Dense Bank" got a contrast of 3.4 that did
// nothing at all. Same family as ADR-561's eye hole and ADR-565's `detailAmount`: a control that
// is declared, set by a preset, and unreachable. **Turning it on changes no shipped frame, because
// `grep -rl '"kind": "fog"' examples/` returns ZERO scenes** -- which is also why so many of this
// kind's controls were able to die unnoticed.
//
// All three terms are the IDENTITY at their defaults (threshold 0, softness 0, contrast 1), so the
// curve is something an artist opts into rather than something they have to undo.
//
//   threshold  clears density below it and renormalises what is left, which is how thin haze
//              becomes clear air and how a bank gets a definite boundary instead of a long tail.
//   softness   bends the knee from a straight line into a smoothstep. §23: softness matters more
//              than detail, so this is a first-class control rather than a hidden constant.
//   contrast   the response curve itself. Above 1 the bank is thin at its edge and dense in its
//              core -- cinematic cloud; below 1 it fills out toward a uniform slab.
fn fogDensityRemap(f: FogUniformsWgsl, shape: f32) -> f32 {
    let threshold = clamp(f.f6.y, 0.0, 0.99);
    var s = max(shape - threshold, 0.0) / max(1.0 - threshold, 1e-4);
    let softness = clamp(f.f6.z, 0.0, 1.0);
    if (softness > 0.0) {
        s = mix(s, smoothstep(0.0, 1.0, s), softness);
    }
    let contrast = max(f.f6.x, 0.05);
    if (contrast != 1.0) {
        s = pow(max(s, 0.0), contrast);
    }
    return clamp(s, 0.0, 1.0);
}

// §26's "height influence" on EMISSION, which the brief lists beside intensity, colour and density
// influence and which the march had only three of.
//
// It reuses `fogVerticalProfile` rather than introducing a second vertical shape, so a bank whose
// glow follows its height is following the same curve its density does -- one vertical model for
// the medium, which is the argument ADR-567 makes one level up for the whole atmosphere. At 0 the
// emission is uniform through the bank's height, which is what it has always been and is the
// default.
fn fogEmissionHeight(f: FogUniformsWgsl, relY: f32) -> f32 {
    let amount = clamp(f.f4.w, 0.0, 1.0);
    if (amount <= 0.0) {
        return 1.0;
    }
    return mix(1.0, fogVerticalProfile(f, relY), amount);
}

// ---- ADR-714, the brief's §25: height colour and distance colour ---------------------------
//
// Two more colours beside the three through the bank's depth (`colorDeep/Mid/Accent`, mixed on
// density in `mediumEmissionAt`), and the brief's own warning is the design: *"avoid making colour
// responsible for structure."* In this medium the eye reads structure from LUMINANCE -- the depth
// hierarchy is dark where the bank is thin and bright where it is dense -- so a height or distance
// colour that changed luminance would be drawing density that is not there. So both tints are
// **luminance-preserving**: the tint colour is rescaled to the luminance of the colour it replaces
// before it is mixed in, and the mix therefore changes hue and saturation and leaves luminance
// exactly where the density put it. `test_fog_colour.cpp` holds that as an equality.
//
// Both weights are functions of WHERE the sample is (its height in the bank, its distance from
// the camera) and never of the density: the same point is the same tint in a thick bank and a
// thin one. Both are the identity at amount 0, by a branch.

// Rec. 709 luminance, the weights the rest of the engine's grading uses.
fn fogLuminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

// `base` with `tint`'s hue at `base`'s luminance, `w` of the way. A tint too dark to carry a hue
// (a black height colour) leaves the colour alone rather than dividing by nothing.
fn fogHueMix(base: vec3<f32>, tint: vec3<f32>, w: f32) -> vec3<f32> {
    let lt = fogLuminance(tint);
    if (w <= 0.0 || lt <= 1e-4) {
        return base;
    }
    return mix(base, tint * (fogLuminance(base) / lt), w);
}

// The height weight: 0 at the bank's densest layer, 1 two thicknesses above it -- the SAME vertical
// frame `fogVerticalProfile` measures in, so "the top of the bank" means the same height to the
// density, to the glow's height influence (§26) and to this. One vertical model (ADR-567).
fn fogHeightColourWeight(f: FogUniformsWgsl, relY: f32) -> f32 {
    let amount = clamp(f.f9.w, 0.0, 1.0);
    if (amount <= 0.0) {
        return 0.0;
    }
    let thickness = max(f.f1.x, 1e-3);
    let bias = clamp(f.f2.y, 0.0, 1.0);
    let base = -thickness + 2.0 * thickness * bias;
    return amount * smoothstep(0.0, 2.0, (relY - base) / thickness);
}

// The distance weight: aerial perspective inside the bank, `1 - exp(-d / range)` -- 63% of the
// way at `range` metres from the camera. Exponential rather than a smoothstep between two
// distances because that is what extinction along a path is; one number instead of two.
fn fogDistanceColourWeight(f: FogUniformsWgsl, cameraDistance: f32) -> f32 {
    let amount = clamp(f.f10.w, 0.0, 1.0);
    if (amount <= 0.0) {
        return 0.0;
    }
    return amount * (1.0 - exp(-max(cameraDistance, 0.0) / max(f.f5.w, 1.0)));
}

// The depth colour `base` with both tints applied, height first. The identity when both amounts
// are 0: `fogHueMix` returns `base` itself on a zero weight.
fn fogTintedColour(f: FogUniformsWgsl, base: vec3<f32>, relY: f32, cameraDistance: f32) -> vec3<f32> {
    let height = vec3<f32>(f.f11.w, f.f12.w, f.f13.w);
    let c = fogHueMix(base, height, fogHeightColourWeight(f, relY));
    return fogHueMix(c, f.f10.xyz, fogDistanceColourWeight(f, cameraDistance));
}

// The bank, analytic and complete. Zero outside, and compactly so, which is the property ADR-374
// measured the march's cost against and ADR-562's ray interval depends on.
fn fogShapeAt(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> f32 {
    if (f.f0.w <= 0.0) {
        return 0.0;
    }
    // ADR-713: swell and turbulence. At their defaults the field is evaluated at `p` itself,
    // through the pre-ADR-713 expression `p - f.f0.xyz` and on its own branch -- NOT through a
    // `var q = p` that the flow branch may overwrite. The version that shared one path was the
    // identity in exact arithmetic and still moved one pixel of a default bank by one level: the
    // Metal compiler contracted the shared path differently. Measured, then split.
    let swell = fogSwell(f, t);
    let turbulence = clamp(f.f3.y, 0.0, 1.0);
    if (swell == 1.0 && turbulence <= 0.0) {
        return fogShapeFrom(f, p, p - f.f0.xyz, t);
    }
    var q = p;
    if (turbulence > 0.0) {
        // The flow costs sixteen noise gradients, so skip it where no displacement it can produce
        // reaches the primitive: `amount` semi-axes, measured in the swelled frame.
        let reach0 = fogPrimitiveDistance(f, fogSwellOffset(f, p - f.f0.xyz, swell));
        if (reach0 > 1.35 + turbulence * fogTurbulenceReach(f) / min(swell, 1.0)) {
            return 0.0;
        }
        q = p + fogTurbulence(f, p, t);
    }
    return fogShapeFrom(f, q, fogSwellOffset(f, q - f.f0.xyz, swell), t);
}

// The field at the (possibly displaced) point `q`, whose offset from the centre in the (possibly
// swelled) primitive frame is `rel`. Everything from here down is what `fogShapeAt` was before
// ADR-713.
fn fogShapeFrom(f: FogUniformsWgsl, q: vec3<f32>, rel: vec3<f32>, t: f32) -> f32 {
    let rr = fogPrimitiveDistance(f, rel);
    if (rr > 1.35) {
        return 0.0;
    }
    // The edge. `edgeSoftness` is how much of the bank is edge: at 0 the rim is a tight shoulder
    // just inside the boundary, at 1 the density falls from the very centre. Smoothstep, so there
    // is no edge anywhere for a hard line to live on (ADR-369).
    let soft = clamp(f.f2.x, 0.02, 1.0);
    let rim = 1.0 - smoothstep(1.0 - soft, 1.0 + soft * 0.35, rr);
    if (rim <= 0.0) {
        return 0.0;
    }
    // §9's "height influence". The bank IS the vertical profile and takes it whole; a closed
    // primitive already has a top and a bottom of its own, so the profile is an optional
    // modulation inside it -- 0 for a uniform ball of mist, 1 for one that pools at its floor.
    //
    // The bank cannot be given the choice and the reason is the bound, not the picture: `rim` is
    // horizontal for a bank, so the profile is the ONLY term that makes it end in Y. Blend that
    // toward 1 and the bank becomes an infinite vertical column with a finite bound around it --
    // ADR-566's own defect, reintroduced by a control. So it is read for the five and ignored for
    // the one, and `mediumBoundOf` in `volume.wgsl` is written against exactly that rule.
    var profile = fogVerticalProfile(f, rel.y);
    if (fogShapeKind(f) != kFogShapeBank) {
        profile = mix(1.0, profile, clamp(f.f4.z, 0.0, 1.0));
    }
    return fogDensityRemap(f, max(rim * profile * fogMacroDetail(f, q, t), 0.0));
}
