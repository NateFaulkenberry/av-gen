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
    let local = position * df.misc.y;
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

@fragment
fn fs_offset(in: ProxyOut) -> OffsetOut {
    let p = proxies[in.index];
    let texel = vec2<i32>(in.clip.xy);
    let uv = in.clip.xy * df.viewport.zw;
    let cam = frame.cameraPos.xyz;
    let fwd = frame.cameraForward.xyz;
    let sceneZ = dfSceneDepth(texel);

    // The lens plane, and the cheap rejection first: a scene surface nearer than the lens plane is
    // not bent whatever the field does there -- the whole warp behind a wall costs one texel load.
    let lensZ = dot(p.centre.xyz - cam, fwd) + p.centre.w;
    let band = max(p.axis2.w, 1e-3);
    let depthGate = smoothstep(lensZ, lensZ + band, sceneZ);

    // The view ray and its closest approach to the centre, in the ellipsoid's unit-sphere frame.
    // The semi-axes are orthogonal, so the inverse frame is the axes over their squared lengths.
    let farH = frame.invViewProj * vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
    let dir = normalize(farH.xyz / farH.w - cam);
    let i0 = p.axis0.xyz / max(dot(p.axis0.xyz, p.axis0.xyz), 1e-8);
    let i1 = p.axis1.xyz / max(dot(p.axis1.xyz, p.axis1.xyz), 1e-8);
    let i2 = p.axis2.xyz / max(dot(p.axis2.xyz, p.axis2.xyz), 1e-8);
    let oc = cam - p.centre.xyz;
    let o = vec3<f32>(dot(oc, i0), dot(oc, i1), dot(oc, i2));
    let v = vec3<f32>(dot(dir, i0), dot(dir, i1), dot(dir, i2));
    let near = max(frame.clusterDepth.z, 1e-3);
    // Clamped in front of the camera: from inside a proxy the closest approach can be behind the eye,
    // and a point behind the eye has no screen position to offset from.
    let t = max(-dot(o, v) / max(dot(v, v), 1e-12), near * 4.0);
    let bl = o + v * t;
    let r = length(bl);
    if (r >= 1.0) {
        discard;
    }
    let q = cam + dir * t;

    // Coverage: 1 inside, fading over the edge softness, so a field never ends on a hard ring.
    let soft = max(p.shape.y, 1e-3);
    let win = 1.0 - smoothstep(1.0 - soft, 1.0, r);
    let f = dfWarpProfile(r, p.rim.w, p.shape.x) * win;

    // The field, as a world displacement at q.
    let b = q - p.centre.xyz;
    let bLen = length(b);
    let bn = select(vec3<f32>(0.0), b / bLen, bLen > 1e-5);
    // The pull toward the centre is capped so a tap never crosses the inner radius: past it lies the
    // owner (which the mask would refuse, leaving a hole to fall back across) or, for a warp at a
    // point, the centre itself (which would fold the image over). Within the cap the lens is free.
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
    let d = radial + (bow + swirl + turb) * (f * p.terms.w);

    var offset = (dfToUv(q + d) - dfToUv(q)) * depthGate;
    let len = length(offset);
    if (len > df.misc.x) {
        offset = offset * (df.misc.x / len);
    }

    // The rim: a thin ring just inside the silhouette, at the closest-approach point -- so it is
    // hidden by whatever stands in front of that point, and only by that.
    let rw = max(p.noise.z, 1e-3);
    let ring = exp(-pow((r - (1.0 - rw)) / (0.4 * rw), 2.0));
    let qZ = dot(q - cam, fwd);
    let rimVis = smoothstep(qZ - 0.05 * band, qZ, sceneZ);
    let rim = p.rim.rgb * (ring * rimVis);

    var out: OffsetOut;
    out.offset = vec4<f32>(offset, win * lensZ, win);
    out.aux = vec4<f32>(rim, win * depthGate * p.noise.y);
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
    // Untouched pixels are left exactly as they were, even inside the scissor: no write at all.
    if (!bent && !lit) {
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
    var out: ResolveOut;
    out.hdr = vec4<f32>(color.rgb + a.rgb, base.a);
    out.emission = vec4<f32>(a.rgb, 0.0); // additive: selective bloom sees the rim as emitted light
    return out;
}
