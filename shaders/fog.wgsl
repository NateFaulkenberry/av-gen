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
//        NOT lane 12 -- that is `spill`, read by the surface glow (ADR-562), and a fog bank uses it.

struct FogUniformsWgsl {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
    f3: vec4<f32>,
};

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

// The bank, analytic and complete. Zero outside, and compactly so, which is the property ADR-374
// measured the march's cost against and ADR-562's ray interval depends on.
fn fogShapeAt(f: FogUniformsWgsl, p: vec3<f32>, t: f32) -> f32 {
    if (f.f0.w <= 0.0) {
        return 0.0;
    }
    let rel = p - f.f0.xyz;
    let rr = fogEllipticalRadius(f, rel);
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
    return max(rim * fogVerticalProfile(f, rel.y) * fogMacroDetail(f, p, t), 0.0);
}
