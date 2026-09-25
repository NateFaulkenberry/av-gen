// DF: the distortion framework's GPU half (Effect Library Wave 1, roadmap 1.7). See
// src/world/effects/distortion_frame.hpp for the model and src/rendering/distortion_renderer.hpp for
// the passes. Two passes, both after the volumetric composite and before post:
//
//   1. OFFSET (`vs_proxy` / `fs_offset`): every proxy of the frame, instanced, into two RGBA16F
//      targets with ADDITIVE blending, so overlapping fields superpose (thin-lens superposition):
//        offset: xy = screen offset in UV (already weighted by the field), z = w * lens depth, w = w
//        aux:    rgb = rim emission (HDR),                                  a = w * chroma
//      Each fragment computes the view ray's closest approach to the proxy's centre analytically,
//      evaluates the field there as a WORLD displacement d, and writes proj(q + d) - proj(q) -- the
//      lesson in water.wgsl: a displacement authored in metres means the same at 2 m and at 50 m.
//
//   2. RESOLVE (`fs_resolve`): a full-screen triangle, scissored to the producers' screen rect, that
//      reads the HDR copy at uv + offset with the depth-aware foreground-leak mask, three chroma taps,
//      and adds the rim to HDR and to the emission target (so selective bloom sees it).
//
// **Wave 3 (the lens slice)** adds a second offset entry point, `fs_offset_ext`, for the shapes and
// fields from 4 up (Facing + Lens, Cylinder + Shimmer), drawn by its own pipeline over its own
// instance range, and a third offset target, COVER (R16F, additive): the opacity of what a proxy HIDES
// (a black hole's horizon), which the resolve applies before adding the glow. The Wave 1-2 entry
// point `fs_offset` is left textually unchanged ON PURPOSE: adding the new shapes' branches to it
// moved one pixel of an existing Space Warp frame by one level (the Metal compiler reassociated the
// shared code), and the promise is that every existing proxy renders byte-identically.
//
// **The depth rules** (Sousa, GPU Gems 2 ch.19, plus self-exclusion):
//   - The LENS PLANE of a proxy is at its centre's view depth plus its exclusion radius (the owner's
//     bounding radius for an entity, 0 for a warp at a point). A pixel nearer than the lens plane is
//     NOT bent -- which is both "a warp behind a wall draws nothing" and "the owner stays crisp": its
//     every visible surface is nearer than centre + bounding radius. The bend fades in over a band
//     behind the plane, so a floor that cuts the field has no seam. Evaluated per proxy in the offset
//     pass, against the scene depth read as a texture (read-only; the pass has no depth attachment,
//     which is what lets one pipeline serve a camera outside AND inside a proxy).
//   - A TAP that lands on something nearer than the lens plane (depth z/w of the summed targets) is
//     foreground and is refused: the tap retreats toward the pixel (k = 1, 1/2, 1/4, 0) until it lands
//     behind the lens. So nothing in front of a distorter -- the owner included -- is ever smeared
//     into the bend.

#include "common.wgsl"
#include "noise.wgsl"

// One proxy. Mirrors world::DistortionProxy (144 bytes); see distortion_frame.hpp for every lane.
struct DfProxy {
    centre: vec4<f32>, // xyz world centre, w exclusion radius (m)
    axis0: vec4<f32>,  // xyz semi-axis (world), w shape (0 ellipsoid)
    axis1: vec4<f32>,  // xyz semi-axis, w field (0 warp)
    axis2: vec4<f32>,  // xyz semi-axis, w depth band (m)
    terms: vec4<f32>,  // x radial, y bow, z swirl, w peak displacement (m)
    motion: vec4<f32>, // xyz unit motion direction, w motion weight
    shape: vec4<f32>,  // x falloff, y edge softness, z turbulence amount, w turbulence frequency
    noise: vec4<f32>,  // x turbulence phase, y chroma, z rim width, w seed
    rim: vec4<f32>,    // rgb rim radiance, w inner radius (0..1)
};

struct DfParams {
    viewport: vec4<f32>, // width, height, 1/width, 1/height
    copyRect: vec4<f32>, // the copied region in UV: min.xy, max.xy (taps are clamped inside it)
    misc: vec4<f32>,     // x = largest offset a pixel may take (UV), y = 1/inradius of the proxy mesh
};

@group(1) @binding(1) var<storage, read> proxies: array<DfProxy>;
@group(1) @binding(2) var dfDepth: texture_depth_2d;
@group(1) @binding(3) var<uniform> df: DfParams;
@group(1) @binding(4) var sceneCopy: texture_2d<f32>;
@group(1) @binding(5) var offsetTex: texture_2d<f32>;
@group(1) @binding(6) var auxTex: texture_2d<f32>;
@group(1) @binding(7) var copySampler: sampler;
@group(1) @binding(8) var coverTex: texture_2d<f32>;

const DF_FAR: f32 = 1.0e7;

// View-space depth (along the camera axis, as linear_depth.wgsl writes it) of the scene at `texel`.
fn dfSceneDepth(texel: vec2<i32>) -> f32 {
    let size = vec2<i32>(textureDimensions(dfDepth));
    let t = clamp(texel, vec2<i32>(0), size - vec2<i32>(1));
    let d = textureLoad(dfDepth, t, 0);
    if (d >= 1.0) {
        return DF_FAR; // the sky: infinitely far, so it is always behind a lens and always bends
    }
    let uv = (vec2<f32>(t) + 0.5) * df.viewport.zw;
    let ndc = vec3<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d);
    let w = frame.invViewProj * vec4<f32>(ndc, 1.0);
    return dot(w.xyz / w.w - frame.cameraPos.xyz, frame.cameraForward.xyz);
}

// The NEAREST scene depth under a bilinear tap at `uv`: the four texels the filter blends. A tap is
// judged by all of them, because a tap whose centre texel is background but whose neighbour is the
// foreground would otherwise blend the foreground in -- a speckle of the owner's colour along its edge.
// Depth is monotonic in view depth, so the minimum raw value is the nearest surface, linearised once.
fn dfTapDepth(uv: vec2<f32>) -> f32 {
    let size = vec2<i32>(textureDimensions(dfDepth));
    let base = vec2<i32>(floor(uv * df.viewport.xy - 0.5));
    var d = 1.0;
    for (var j = 0; j < 2; j = j + 1) {
        for (var i = 0; i < 2; i = i + 1) {
            let t = clamp(base + vec2<i32>(i, j), vec2<i32>(0), size - vec2<i32>(1));
            d = min(d, textureLoad(dfDepth, t, 0));
        }
    }
    if (d >= 1.0) {
        return DF_FAR;
    }
    let ndc = vec3<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d);
    let w = frame.invViewProj * vec4<f32>(ndc, 1.0);
    return dot(w.xyz / w.w - frame.cameraPos.xyz, frame.cameraForward.xyz);
}

fn dfToUv(p: vec3<f32>) -> vec2<f32> {
    let c = frame.viewProj * vec4<f32>(p, 1.0);
    let n = c.xy / max(c.w, 1e-4);
    return vec2<f32>(n.x * 0.5 + 0.5, 0.5 - n.y * 0.5);
}

// ---- pass 1: the offset field -------------------------------------------------------------------

struct ProxyOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) @interpolate(flat) index: u32,
};

// The proxy mesh is a unit icosphere; `df.misc.y` scales it out to CIRCUMSCRIBE the unit sphere, so
// the rasterised hull covers the whole analytic ellipsoid and the fragment decides the exact edge.
@vertex
fn vs_proxy(@location(0) position: vec3<f32>, @builtin(instance_index) index: u32) -> ProxyOut {
    let p = proxies[index];
    // A cylinder's hull is grown by sqrt(2), which circumscribes the unit cylinder (Wave 3; the
    // CPU's `distortionHullScale`). Every other shape is untouched.
    var local = position * df.misc.y;
    if (u32(p.axis0.w + 0.5) == 5u) {
        local = local * 1.41422;
    }
    let world = p.centre.xyz + p.axis0.xyz * local.x + p.axis1.xyz * local.y + p.axis2.xyz * local.z;
    var out: ProxyOut;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.index = index;
    return out;
}

struct OffsetOut {
    @location(0) offset: vec4<f32>,
    @location(1) aux: vec4<f32>,
};

// The Warp field's profile across the normalised radius: zero inside the owner (`inner`), rising to a
// peak and back to zero at the edge. `r(1 - r^2)^k` peaks at 1/sqrt(1 + 2k), so `falloff` (k) moves the
// peak inward as it grows; normalised so the peak is 1 whatever k is.
fn dfWarpProfile(r: f32, inner: f32, k: f32) -> f32 {
    let s = saturate((r - inner) / max(1.0 - inner, 1e-3));
    let kk = max(k, 0.05);
    let rp = inverseSqrt(1.0 + 2.0 * kk);
    let peak = rp * pow(1.0 - rp * rp, kk);
    return s * pow(max(1.0 - s * s, 0.0), kk) / max(peak, 1e-4);
}

// Wave 2. The Shock field's band across the normalised radius: a signed derivative-of-Gaussian around
// the front, compression outside and rarefaction inside, peaking at +-1 one half-thickness either
// side of it. `u` is (r - front) / halfThickness.
fn dfShockBand(u: f32) -> f32 {
    return u * exp(0.5 * (1.0 - u * u));
}

@fragment
fn fs_offset(in: ProxyOut) -> OffsetOut {
    let p = proxies[in.index];
    let texel = vec2<i32>(in.clip.xy);
    let uv = in.clip.xy * df.viewport.zw;
    let cam = frame.cameraPos.xyz;
    let fwd = frame.cameraForward.xyz;
    let sceneZ = dfSceneDepth(texel);
    let field = u32(p.axis1.w + 0.5);
    let shape = u32(p.axis0.w + 0.5);
    let band = max(p.axis2.w, 1e-3);

    let farH = frame.invViewProj * vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
    let dir = normalize(farH.xyz / farH.w - cam);
    let near = max(frame.clusterDepth.z, 1e-3);

    // ---- where this fragment samples the field ----
    // An ellipsoid is sampled at the view ray's closest approach to its centre, in the ellipsoid's
    // unit-sphere frame (the semi-axes are orthogonal, so the inverse frame is the axes over their
    // squared lengths). A disc (Wave 2) is sampled where the ray pierces its plane.
    var q = vec3<f32>(0.0);
    var bl = vec3<f32>(0.0);
    var r = 0.0;
    var lensZ = 0.0;
    if (shape == 1u) {
        let nrm = normalize(p.axis2.xyz);
        let denom = dot(dir, nrm);
        if (abs(denom) < 1e-4) {
            discard;
        }
        let tHit = dot(p.centre.xyz - cam, nrm) / denom;
        if (tHit <= near) {
            discard;
        }
        q = cam + dir * tHit;
        let b0 = q - p.centre.xyz;
        bl = vec3<f32>(dot(b0, p.axis0.xyz) / max(dot(p.axis0.xyz, p.axis0.xyz), 1e-8),
                       dot(b0, p.axis1.xyz) / max(dot(p.axis1.xyz, p.axis1.xyz), 1e-8), 0.0);
        r = length(bl.xy);
        if (r >= 1.0) {
            discard;
        }
        // A membrane's lens is the membrane itself where this ray meets it: what is behind the hit
        // point bends, what is in front of it does not -- exact for a tilted membrane too.
        lensZ = dot(q - cam, fwd) + p.centre.w;
    } else {
        let i0 = p.axis0.xyz / max(dot(p.axis0.xyz, p.axis0.xyz), 1e-8);
        let i1 = p.axis1.xyz / max(dot(p.axis1.xyz, p.axis1.xyz), 1e-8);
        let i2 = p.axis2.xyz / max(dot(p.axis2.xyz, p.axis2.xyz), 1e-8);
        let oc = cam - p.centre.xyz;
        let o = vec3<f32>(dot(oc, i0), dot(oc, i1), dot(oc, i2));
        let v = vec3<f32>(dot(dir, i0), dot(dir, i1), dot(dir, i2));
        // Clamped in front of the camera: from inside a proxy the closest approach can be behind the
        // eye, and a point behind the eye has no screen position to offset from.
        let t = max(-dot(o, v) / max(dot(v, v), 1e-12), near * 4.0);
        bl = o + v * t;
        r = length(bl);
        if (r >= 1.0) {
            discard;
        }
        q = cam + dir * t;
        // The lens plane: the centre's depth plus the exclusion radius. A scene surface nearer than it
        // is not bent whatever the field does there.
        lensZ = dot(p.centre.xyz - cam, fwd) + p.centre.w;
    }
    let depthGate = smoothstep(lensZ, lensZ + band, sceneZ);
    let b = q - p.centre.xyz;
    let bLen = length(b);
    let bn = select(vec3<f32>(0.0), b / bLen, bLen > 1e-5);

    // ---- the field, as a world displacement d at q, its coverage `win` and its emission ----
    var d = vec3<f32>(0.0);
    var win = 1.0;
    var glow = 0.0;
    if (field == 1u) {
        // Shock: a thin band at the front's radius.
        let hw = max(p.shape.y, 1e-4);
        let u = (r - p.shape.x) / hw;
        win = 1.0 - smoothstep(0.9, 1.0, r);
        // Nothing at the very centre: while the front is young its inner lobe reaches r = 0, where
        // the radial direction is undefined and the band would pinch the image into a knot.
        let core = smoothstep(0.0, 2.0 * hw, r);
        d = bn * (dfShockBand(u) * p.terms.x * p.terms.w * win * core);
        // The leading edge: a thin Gaussian just outside the front, where the compression is.
        let e = (u - 0.5) / 0.35;
        glow = exp(-e * e) * win;
    } else if (field == 2u) {
        // Ripple: a damped train inside the front, in the disc's plane along the radius.
        let cycles = p.shape.x;
        let front = p.motion.x;
        let soft = max(p.shape.y, 1e-3);
        win = 1.0 - smoothstep(1.0 - soft, 1.0, r);
        // Nothing ahead of the front; the leading wavelength ramps in so the front has no hard edge.
        let lead = 1.0 / max(cycles, 1e-3);
        let behind = 1.0 - smoothstep(front - lead, front, r);
        let s = sin(6.2831853 * (r * cycles - p.noise.x));
        // Faded in over the first half-wavelength: at r = 0 the radial direction is undefined, and the
        // innermost ring would pinch the image into a knot (as Shock's young front did).
        let core = smoothstep(0.0, 0.5 * lead, r);
        let a = s * exp(-r * p.motion.y) * behind * win * core;
        let radial = normalize(p.axis0.xyz * bl.x / max(length(p.axis0.xyz), 1e-6) +
                               p.axis1.xyz * bl.y / max(length(p.axis1.xyz), 1e-6) + vec3<f32>(1e-7));
        d = radial * (a * p.terms.x * p.terms.w);
        // Crests: the positive peaks, sharpened.
        glow = pow(max(s, 0.0), 8.0) * exp(-r * p.motion.y) * behind * win;
    } else if (field == 3u) {
        // Wake: ripples across a tube segment, a cos^2 window along it so neighbours sum to one.
        let rho = length(bl.yz);
        let alongW = cos(1.5707963 * clamp(bl.x, -1.0, 1.0));
        let along = alongW * alongW;
        let soft = max(p.shape.y, 1e-3);
        let skin = 1.0 - smoothstep(1.0 - soft, 1.0, rho);
        win = along * skin;
        let a1 = p.axis1.xyz / max(length(p.axis1.xyz), 1e-6);
        let a2 = p.axis2.xyz / max(length(p.axis2.xyz), 1e-6);
        let across = a1 * bl.y + a2 * bl.z;
        let an = select(vec3<f32>(0.0), across / max(rho, 1e-5), rho > 1e-5);
        // Zero on the axis (no fold there) and at the skin; a travelling ripple between.
        let s = sin(6.2831853 * (p.shape.x * rho - p.noise.x));
        let profile = rho * (1.0 - rho * rho);
        d = an * (s * profile * 2.6 * p.terms.x * p.terms.w * win);
    } else {
        // Warp (Wave 1): coverage 1 inside, fading over the edge softness.
        let soft = max(p.shape.y, 1e-3);
        win = 1.0 - smoothstep(1.0 - soft, 1.0, r);
        let f = dfWarpProfile(r, p.rim.w, p.shape.x) * win;
        // The pull toward the centre is capped so a tap never crosses the inner radius: past it lies
        // the owner (which the mask would refuse, leaving a hole to fall back across) or, for a warp
        // at a point, the centre itself (which would fold the image over). Within the cap the lens is
        // free.
        let reach = 0.9 * bLen * (1.0 - p.rim.w / max(r, 1e-3));
        let radial = -bn * min(p.terms.x * f * p.terms.w, max(reach, 0.0));
        let bow = p.motion.xyz * (p.terms.y * dot(bn, p.motion.xyz) * p.motion.w);
        let swirl = cross(dir, bn) * p.terms.z;
        var turb = vec3<f32>(0.0);
        if (p.shape.z > 0.0) {
            let a0 = normalize(p.axis0.xyz);
            let a1 = normalize(p.axis1.xyz);
            let a2 = normalize(p.axis2.xyz);
            let flow = flowCurl(bl * p.shape.w + vec3<f32>(p.noise.w), p.noise.x, 7u);
            // Soft-limited to unit length: the flow's magnitude varies, the amount is the artist's.
            let fl = flow / (1.0 + length(flow));
            turb = (a0 * fl.x + a1 * fl.y + a2 * fl.z) * (2.0 * p.shape.z);
        }
        d = radial + (bow + swirl + turb) * (f * p.terms.w);
        // The rim: a thin ring just inside the silhouette.
        let rw = max(p.noise.z, 1e-3);
        glow = exp(-pow((r - (1.0 - rw)) / (0.4 * rw), 2.0));
    }

    var offset = (dfToUv(q + d) - dfToUv(q)) * depthGate;
    let len = length(offset);
    if (len > df.misc.x) {
        offset = offset * (df.misc.x / len);
    }

    // The emission sits at the sampled point -- so it is hidden by whatever stands in front of that
    // point, and only by that.
    let qZ = dot(q - cam, fwd);
    let rimVis = smoothstep(qZ - 0.05 * band, qZ, sceneZ);
    let rim = p.rim.rgb * (glow * rimVis);

    var out: OffsetOut;
    out.offset = vec4<f32>(offset, win * lensZ, win);
    out.aux = vec4<f32>(rim, win * depthGate * p.noise.y);
    return out;
}

// ---- pass 1, Wave 3: the extended offset field ---------------------------------------------------
//
// Shapes 4 (Facing) and 5 (Cylinder), fields 4 (Shimmer) and 5 (Lens); see distortion_frame.hpp for
// every lane. The same outputs as `fs_offset` plus COVER. The renderer draws only proxies whose shape
// is 4 or more through this entry point.

struct OffsetExtOut {
    @location(0) offset: vec4<f32>,
    @location(1) aux: vec4<f32>,
    @location(2) cover: f32,
};

@fragment
fn fs_offset_ext(in: ProxyOut) -> OffsetExtOut {
    let p = proxies[in.index];
    let texel = vec2<i32>(in.clip.xy);
    let uv = in.clip.xy * df.viewport.zw;
    let cam = frame.cameraPos.xyz;
    let fwd = frame.cameraForward.xyz;
    let sceneZ = dfSceneDepth(texel);
    let field = u32(p.axis1.w + 0.5);
    let shape = u32(p.axis0.w + 0.5);
    let band = max(p.axis2.w, 1e-3);

    let farH = frame.invViewProj * vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
    let dir = normalize(farH.xyz / farH.w - cam);
    let near = max(frame.clusterDepth.z, 1e-3);

    var q = vec3<f32>(0.0);
    var bl = vec3<f32>(0.0);
    var r = 0.0;
    var lensZ = 0.0;
    var thick = 0.0;     // Cylinder: metres of the column between the camera and the scene surface
    var depthGate = 1.0; // Facing: the lens-plane fade; Cylinder: 1 (its thickness is its depth rule)
    if (shape == 4u) {
        // Facing: the plane through the centre whose normal is the camera's forward axis, so a circle
        // in it projects to a circle on screen wherever the proxy is in the frame.
        let denom = dot(dir, fwd);
        if (denom < 1e-4) {
            discard;
        }
        let tHit = dot(p.centre.xyz - cam, fwd) / denom;
        if (tHit <= near) {
            discard;
        }
        q = cam + dir * tHit;
        r = length(q - p.centre.xyz) / max(length(p.axis0.xyz), 1e-6);
        if (r >= 1.0) {
            discard;
        }
        lensZ = dot(p.centre.xyz - cam, fwd) + p.centre.w;
        depthGate = smoothstep(lensZ, lensZ + band, sceneZ);
    } else if (shape == 5u) {
        // Cylinder: the view ray against the unit cylinder x^2 + z^2 <= 1, |y| <= 1 in the proxy's
        // frame (axis1 its axis). t is the WORLD ray parameter (the frame is linear in it), so the
        // entry and exit are distances along the ray, and the scene surface cuts the stretch short.
        let i0 = p.axis0.xyz / max(dot(p.axis0.xyz, p.axis0.xyz), 1e-8);
        let i1 = p.axis1.xyz / max(dot(p.axis1.xyz, p.axis1.xyz), 1e-8);
        let i2 = p.axis2.xyz / max(dot(p.axis2.xyz, p.axis2.xyz), 1e-8);
        let oc = cam - p.centre.xyz;
        let o = vec3<f32>(dot(oc, i0), dot(oc, i1), dot(oc, i2));
        let v = vec3<f32>(dot(dir, i0), dot(dir, i1), dot(dir, i2));
        var t0 = -DF_FAR;
        var t1 = DF_FAR;
        let a = v.x * v.x + v.z * v.z;
        let hb = o.x * v.x + o.z * v.z;
        let c = o.x * o.x + o.z * o.z - 1.0;
        if (a > 1e-12) {
            let disc = hb * hb - a * c;
            if (disc <= 0.0) {
                discard;
            }
            let sq = sqrt(disc);
            t0 = (-hb - sq) / a;
            t1 = (-hb + sq) / a;
        } else if (c > 0.0) {
            discard; // parallel to the axis and outside the side
        }
        if (abs(v.y) > 1e-12) {
            let ta = (-1.0 - o.y) / v.y;
            let tb = (1.0 - o.y) / v.y;
            t0 = max(t0, min(ta, tb));
            t1 = min(t1, max(ta, tb));
        } else if (abs(o.y) > 1.0) {
            discard; // parallel to the caps and above or below them
        }
        let cosF = max(dot(dir, fwd), 1e-4);
        let tIn = max(t0, near);
        let tEnd = min(t1, sceneZ / cosF);
        if (tEnd <= tIn) {
            discard; // the column is behind the surface, or behind the camera: nothing bends
        }
        thick = tEnd - tIn;
        let tMid = 0.5 * (tIn + tEnd);
        q = cam + dir * tMid;
        bl = o + v * tMid;
        r = length(bl.xz);
        // The lens plane is where this ray ENTERS the column: a surface in front of that is never a
        // tap, and one inside the column bends by the air in front of it (`thick`), not by a gate.
        lensZ = cosF * tIn + p.centre.w;
    } else {
        discard; // not an extended shape: `fs_offset` draws it
    }

    var d = vec3<f32>(0.0);
    var win = 1.0;
    var glow = 0.0;
    var cover = 0.0;
    if (field == 4u) {
        // Shimmer: rising turbulence through a hot column. Strongest at the base and the axis, fading
        // up the column, toward its side and with distance; scaled by the air in front of the surface
        // (capped at terms.x reference thicknesses). Two flow layers, each scrolled up by its own
        // phase of the cycle and crossfaded with weights whose squares sum to one (see
        // heat_shimmer_effect.cpp): coherent in time, and no scroll distance ever grows large.
        let soft = max(p.shape.y, 1e-3);
        let side = 1.0 - smoothstep(1.0 - soft, 1.0, r);
        let h01 = saturate(0.5 * (bl.y + 1.0));
        let rise = pow(max(1.0 - h01, 0.0), p.rim.x);
        var fade = 1.0;
        if (p.rim.y > 0.0) {
            fade = 1.0 - smoothstep(0.5 * p.rim.y, p.rim.y, dot(q - cam, fwd));
        }
        win = side * rise * fade;
        let ratio = min(thick / max(p.shape.w, 1e-3), p.terms.x);
        let inv = 1.0 / max(p.shape.x, 1e-3);
        let wa = sin(3.14159265 * p.noise.x);
        let wb = sin(3.14159265 * p.noise.z);
        var flow = vec3<f32>(0.0);
        if (wa > 1e-4) {
            flow = flow + wa * flowCurl((q - p.motion.xyz * p.noise.x) * inv, p.rim.w * p.noise.x, u32(p.noise.w));
        }
        if (wb > 1e-4) {
            flow = flow + wb * flowCurl((q - p.motion.xyz * p.noise.z) * inv, p.rim.w * p.noise.z, u32(p.rim.z));
        }
        let fl = flow / (1.0 + length(flow));
        d = fl * (p.terms.w * ratio * win);
    } else if (field == 5u) {
        // Lens: the softened point-mass remap beta = theta (1 - tE^2 / (|theta|^2 + c^2)), c = tE / 4,
        // as a displacement beta - theta in the lens plane, feathered to zero at the proxy's edge and
        // weighted by the envelope. Plus the horizon's opacity and the ring's glow, their edges a pixel
        // wide wherever the lens is.
        let theta = q - p.centre.xyz;
        let th = length(theta);
        let tE = max(p.shape.x, 1e-4);
        let core = 0.25 * tE;
        let tn = select(vec3<f32>(0.0), theta / th, th > 1e-6);
        win = 1.0 - smoothstep(p.shape.w, 1.0, r);
        let bend = tE * tE * th / (th * th + core * core);
        d = -tn * (bend * win * p.motion.x);
        let pxM = 1.0 / max(length(dfToUv(q + frame.cameraRight.xyz) - dfToUv(q)) * df.viewport.x, 1e-6);
        let rh = p.shape.y * tE;
        if (rh > 0.0) {
            cover = (1.0 - smoothstep(rh - pxM, rh + pxM, th)) * p.motion.y;
        }
        let ringR = select(tE, rh + 0.5 * p.noise.z * tE, rh > 0.0);
        let sigma = max(0.5 * p.noise.z * tE, pxM);
        let e = (th - ringR) / sigma;
        glow = exp(-e * e) * win;
    }

    var offset = (dfToUv(q + d) - dfToUv(q)) * depthGate;
    // The lens remap is bounded by construction (2 tE) and must not be clipped: a clipped remap draws
    // a false ring where the clip starts. The shimmer keeps DF's safety bound.
    let len = length(offset);
    if (field != 5u && len > df.misc.x) {
        offset = offset * (df.misc.x / len);
    }

    // The emission and the cover sit at the sampled point: hidden by whatever stands in front of it.
    let qZ = dot(q - cam, fwd);
    let vis = smoothstep(qZ - 0.05 * band, qZ, sceneZ);

    var out: OffsetExtOut;
    out.offset = vec4<f32>(offset, win * lensZ, win);
    out.aux = vec4<f32>(p.rim.rgb * (glow * vis), win * depthGate * p.noise.y);
    out.cover = cover * vis;
    return out;
}

// ---- pass 2: the resolve ------------------------------------------------------------------------

struct ResolveIn {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_resolve(@builtin(vertex_index) index: u32) -> ResolveIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: ResolveIn;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

struct ResolveOut {
    @location(0) hdr: vec4<f32>,
    @location(1) emission: vec4<f32>,
};

// One channel's tap along the offset, masked against the foreground (Sousa). The full tap is taken
// when it lands behind the lens; otherwise the tap is pulled back toward the pixel by BISECTION to
// the furthest point that still lands behind it -- so where a bend runs into a foreground object the
// background stretches up to that object's edge instead of snapping back to the unbent image in
// steps (a fixed k = 1, 1/2, 1/4 ladder draws a staircase along every such edge). Clamped to the
// copied rect, so a tap that would leave the screen stops at its edge rather than reading what was
// never copied.
fn dfTapOk(u: vec2<f32>, lensZ: f32, eps: f32) -> bool {
    return dfTapDepth(u) >= lensZ - eps;
}

fn dfTap(uv: vec2<f32>, off: vec2<f32>, lensZ: f32, eps: f32, base: vec4<f32>) -> vec4<f32> {
    let full = clamp(uv + off, df.copyRect.xy, df.copyRect.zw);
    if (dfTapOk(full, lensZ, eps)) {
        return textureSampleLevel(sceneCopy, copySampler, full, 0.0);
    }
    var lo = 0.0; // known good: the pixel itself is behind the lens, or it would carry no offset
    var hi = 1.0; // known bad
    for (var i = 0; i < 5; i = i + 1) {
        let mid = 0.5 * (lo + hi);
        if (dfTapOk(clamp(uv + off * mid, df.copyRect.xy, df.copyRect.zw), lensZ, eps)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    if (lo <= 0.0) {
        return base;
    }
    return textureSampleLevel(sceneCopy, copySampler, clamp(uv + off * lo, df.copyRect.xy, df.copyRect.zw), 0.0);
}

@fragment
fn fs_resolve(in: ResolveIn) -> ResolveOut {
    let texel = vec2<i32>(in.clip.xy);
    let o = textureLoad(offsetTex, texel, 0);
    let a = textureLoad(auxTex, texel, 0);
    let bent = dot(o.xy, o.xy) > 1e-12;
    let lit = any(a.rgb > vec3<f32>(0.0));
    let cov = saturate(textureLoad(coverTex, texel, 0).r);
    let covered = cov > 0.0;
    // Untouched pixels are left exactly as they were, even inside the scissor: no write at all.
    if (!bent && !lit && !covered) {
        discard;
    }
    let base = textureLoad(sceneCopy, texel, 0);
    var color = base;
    if (bent) {
        let uv = (vec2<f32>(texel) + 0.5) * df.viewport.zw;
        let lensZ = o.z / max(o.w, 1e-4);
        let eps = 0.01 * lensZ + 0.05;
        // Three taps along the bend: red further, blue shorter. Each is masked on its own, so a
        // fringe never borrows a colour from the foreground.
        let c = 0.5 * saturate(a.a);
        let g = dfTap(uv, o.xy, lensZ, eps, base);
        if (c > 1e-3) {
            let rr = dfTap(uv, o.xy * (1.0 + c), lensZ, eps, base);
            let bb = dfTap(uv, o.xy * (1.0 - c), lensZ, eps, base);
            color = vec4<f32>(rr.r, g.g, bb.b, g.a);
        } else {
            color = g;
        }
    }
    // Wave 3: what a horizon hides, before the glow is added (a ring in front of the disc stays lit).
    if (covered) {
        color = vec4<f32>(color.rgb * (1.0 - cov), color.a);
    }
    var out: ResolveOut;
    out.hdr = vec4<f32>(color.rgb + a.rgb, base.a);
    out.emission = vec4<f32>(a.rgb, 0.0); // additive: selective bloom sees the rim as emitted light
    return out;
}
