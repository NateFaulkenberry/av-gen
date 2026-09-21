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

struct FogUniformsWgsl {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
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
    return rim * fogVerticalProfile(f, rel.y);
}
