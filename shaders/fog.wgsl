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
//   f4  = LANE 12. x is `spill`, which the SURFACE GLOW reads (ADR-562) and this file must not
//        touch. y and z are the vortex's two zeroes and ADR-566's two new numbers:
//        y = shape index (the primitive), z = height influence.
//
//        Both of them in ONE lane on purpose. ADR-562 §9's finding is that a per-kind lane map is
//        only safe where every reader knows which kind it is holding, and the cheapest way to keep
//        that true is to add nothing to a lane a shared accessor already reads. Lane 12.y and 12.z
//        are read by this file and by `world::fogShapeAt`, and by nothing else in the engine.

struct FogUniformsWgsl {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
    f3: vec4<f32>,
    f4: vec4<f32>,
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
    let drift = f.f3.y * t;
    let n = fbm3(p * scale + vec3<f32>(drift, drift * 0.3, -drift * 0.7), 41u);
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

// The bank, analytic and complete. Zero outside, and compactly so, which is the property ADR-374
// measured the march's cost against and ADR-562's ray interval depends on.
fn fogShapeAt(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> f32 {
    if (f.f0.w <= 0.0) {
        return 0.0;
    }
    let rel = p - f.f0.xyz;
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
    return max(rim * profile * fogMacroDetail(f, p, t), 0.0);
}
