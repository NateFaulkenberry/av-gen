// ADR-1289: sculptural clouds from authored lobes (rendering::CloudRenderer).
//
// A fullscreen pass. Per pixel: the view ray is cut to the lobes it can touch (a ray-sphere test against each lobe's
// bounding sphere, at most kMaxHit kept, nearest first), then marched from the first lobe entry to the last exit or
// the opaque scene, whichever is nearer. Density is the union of soft ellipsoids, eroded by a domain-warped fbm so the
// edges break up and the interiors billow. Each sample is lit by:
//   * the key (the moon), through a short shadow march toward it over the same lobes, with a two-lobe phase (a
//     forward silver lining and a little back-scatter) and the powder term that darkens thin edges facing away;
//   * a two-colour ambient, sky above and ground below, by height in the mass;
//   * the warm horizon light, which only reaches samples low in the sky AND facing the warm direction -- undersides
//     and flanks toward the glow, never the tops.
// Distance haze fades the result toward the haze colour; the output is premultiplied (alpha = 1 - transmittance).

#include "common.wgsl"

struct CloudUniforms {
    light: vec4<f32>,
    lightColor: vec4<f32>,
    ambientTop: vec4<f32>,
    ambientBottom: vec4<f32>,
    warm: vec4<f32>,
    warmColor: vec4<f32>,
    haze: vec4<f32>,
    march: vec4<f32>,
    bounds: vec4<f32>,
    misc: vec4<f32>,
};

struct Lobe {
    centreRadius: vec4<f32>,
    scaleDensity: vec4<f32>,
    soft: vec4<f32>,
};

@group(0) @binding(2) var cloudDepth: texture_2d<f32>;
@group(0) @binding(3) var<uniform> cu: CloudUniforms;
@group(0) @binding(4) var<storage, read> lobes: array<Lobe>;

const kMaxHit: u32 = 160u;

struct VsOut {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_clouds(@builtin(vertex_index) index: u32) -> VsOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VsOut;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

fn cHash3(p: vec3<f32>) -> f32 {
    let q = fract(p * vec3<f32>(0.1031, 0.1030, 0.0973));
    let r = q + dot(q, q.yxz + 33.33);
    return fract((r.x + r.y) * r.z);
}

fn cNoise(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = p - i;
    let u = f * f * (3.0 - 2.0 * f);
    let a = mix(mix(cHash3(i), cHash3(i + vec3<f32>(1.0, 0.0, 0.0)), u.x),
                mix(cHash3(i + vec3<f32>(0.0, 1.0, 0.0)), cHash3(i + vec3<f32>(1.0, 1.0, 0.0)), u.x), u.y);
    let b = mix(mix(cHash3(i + vec3<f32>(0.0, 0.0, 1.0)), cHash3(i + vec3<f32>(1.0, 0.0, 1.0)), u.x),
                mix(cHash3(i + vec3<f32>(0.0, 1.0, 1.0)), cHash3(i + vec3<f32>(1.0, 1.0, 1.0)), u.x), u.y);
    return mix(a, b, u.z);
}

fn cFbm(p: vec3<f32>, octaves: u32) -> f32 {
    var s = 0.0;
    var a = 0.5;
    var q = p;
    for (var o = 0u; o < octaves; o = o + 1u) {
        s = s + a * cNoise(q);
        q = q * 2.03 + vec3<f32>(17.1, -9.3, 4.7);
        a = a * 0.5;
    }
    return s / (1.0 - pow(0.5, f32(octaves)));
}

// the union of the hit lobes (indices in `hit`, count `n`) at `p`, eroded
fn cDensity(p: vec3<f32>, hit: ptr<function, array<u32, 160>>, n: u32, detail: u32) -> f32 {
    var base = 0.0;
    for (var k = 0u; k < n; k = k + 1u) {
        let l = lobes[(*hit)[k]];
        let d = (p - l.centreRadius.xyz) / (l.centreRadius.w * l.scaleDensity.xyz);
        let r = length(d);
        let shape = 1.0 - smoothstep(1.0 - l.soft.x, 1.0, r);
        base = max(base, shape * l.scaleDensity.w);
    }
    if (base <= 0.0) {
        return 0.0;
    }
    let ns = cu.march.w;
    let seed = vec3<f32>(cu.misc.y * 7.13, cu.misc.y * 3.71, 0.0);
    let warp = vec3<f32>(cNoise(p * ns * 0.5 + seed), cNoise(p * ns * 0.5 + seed + 11.0),
                         cNoise(p * ns * 0.5 + seed + 23.0)) - 0.5;
    let n2 = cFbm(p * ns + warp * 2.5 + seed, detail);
    // erode from the outside in: thin parts go first, so edges break into wisps and interiors stay solid
    return clamp((base - cu.warmColor.w * (1.0 - n2)) / max(1.0 - cu.warmColor.w * 0.5, 1e-3), 0.0, 1.0);
}

fn cPhase(cosT: f32, g: f32) -> f32 {
    let g2 = g * g;
    return (1.0 - g2) / (12.566371 * pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-4), 1.5));
}

@fragment
fn fs_clouds(in: VsOut) -> @location(0) vec4<f32> {
    let uv = in.clip.xy * frame.targetSize.zw;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let farP = frame.invViewProj * vec4<f32>(ndc, 1.0, 1.0);
    let nearP = frame.invViewProj * vec4<f32>(ndc, 0.0, 1.0);
    let ro = frame.cameraPos.xyz;
    let rd = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    // the scene's distance along this ray (linear depth is along the camera axis)
    let size = vec2<f32>(textureDimensions(cloudDepth, 0));
    let texel = clamp(vec2<i32>(uv * size), vec2<i32>(0), vec2<i32>(size) - vec2<i32>(1));
    let axial = textureLoad(cloudDepth, texel, 0).r;
    let sceneT = axial / max(dot(rd, frame.cameraForward.xyz), 1e-3);

    // the whole set's bounding sphere first
    let oc = ro - cu.bounds.xyz;
    let bb = dot(oc, rd);
    let cc = dot(oc, oc) - cu.bounds.w * cu.bounds.w;
    if (bb * bb - cc < 0.0) {
        discard;
    }
    // the lobes this ray touches, with the span they cover
    var hit: array<u32, 160>;
    var n = 0u;
    var t0 = 1e30;
    var t1 = 0.0;
    let count = u32(cu.misc.x + 0.5);
    for (var i = 0u; i < count; i = i + 1u) {
        let l = lobes[i];
        let rad = l.centreRadius.w * max(l.scaleDensity.x, max(l.scaleDensity.y, l.scaleDensity.z));
        let o = ro - l.centreRadius.xyz;
        let b = dot(o, rd);
        let c = dot(o, o) - rad * rad;
        let h = b * b - c;
        if (h < 0.0) {
            continue;
        }
        let sq = sqrt(h);
        let a0 = max(-b - sq, 0.0);
        let a1 = -b + sq;
        if (a1 <= 0.0 || a0 >= sceneT) {
            continue;
        }
        if (n < kMaxHit) {
            hit[n] = i;
            n = n + 1u;
        }
        t0 = min(t0, a0);
        t1 = max(t1, min(a1, sceneT));
    }
    if (n == 0u || t1 <= t0) {
        discard;
    }

    let steps = u32(cu.march.x + 0.5);
    let dt = (t1 - t0) / f32(steps);
    let jitter = fract(sin(dot(in.clip.xy, vec2<f32>(12.9898, 78.233))) * 43758.5453);
    let L = cu.light.xyz;
    let cosT = dot(rd, L);
    let phase = mix(cPhase(cosT, cu.lightColor.w), cPhase(cosT, -0.25), 0.25) * 12.566371;
    let ext = cu.ambientTop.w;
    let lightSteps = u32(cu.march.y + 0.5);
    let lightDt = cu.march.z / max(f32(lightSteps), 1.0);
    // vertical span of the hit lobes, for the ambient gradient
    var yLo = 1e30;
    var yHi = -1e30;
    for (var k = 0u; k < n; k = k + 1u) {
        let l = lobes[hit[k]];
        yLo = min(yLo, l.centreRadius.y - l.centreRadius.w * l.scaleDensity.y);
        yHi = max(yHi, l.centreRadius.y + l.centreRadius.w * l.scaleDensity.y);
    }
    var trans = 1.0;
    var col = vec3<f32>(0.0);
    var tFirst = -1.0;
    for (var s = 0u; s < steps; s = s + 1u) {
        let t = t0 + (f32(s) + jitter) * dt;
        let p = ro + rd * t;
        let dens = cDensity(p, &hit, n, 4u);
        if (dens <= 0.002) {
            continue;
        }
        if (tFirst < 0.0) {
            tFirst = t;
        }
        // the key's light reaching this point
        var od = 0.0;
        for (var j = 1u; j <= lightSteps; j = j + 1u) {
            od = od + cDensity(p + L * (f32(j) * lightDt), &hit, n, 2u) * lightDt;
        }
        let shadow = exp(-ext * od);
        let powder = mix(1.0, 1.0 - exp(-ext * dens * 160.0), cu.ambientBottom.w * (1.0 - max(cosT, 0.0)));
        let h01 = clamp((p.y - yLo) / max(yHi - yLo, 1.0), 0.0, 1.0);
        let ambient = mix(cu.ambientBottom.rgb, cu.ambientTop.rgb, h01);
        // warm: low in the sky (by height) and toward the glow (by the ray's own heading)
        let warmLow = 1.0 - smoothstep(0.0, cu.warm.w, p.y - frame.cameraPos.y);
        let warmFace = smoothstep(-0.2, 0.9, dot(normalize(vec3<f32>(rd.x, 0.0, rd.z)), cu.warm.xyz));
        let warm = cu.warmColor.rgb * warmLow * warmFace * (1.0 - 0.6 * h01) * mix(0.4, 1.0, shadow);
        let radiance = cu.lightColor.rgb * cu.light.w * shadow * phase * powder + ambient + warm;
        let a = 1.0 - exp(-ext * dens * dt);
        col = col + trans * a * radiance;
        trans = trans * (1.0 - a);
        if (trans < 0.01) {
            break;
        }
    }
    if (tFirst < 0.0) {
        discard;
    }
    let alpha = 1.0 - trans;
    // distance haze: far cloud melts into the air's colour
    let hz = 1.0 - exp(-tFirst / max(cu.haze.w, 1.0));
    col = mix(col, cu.haze.rgb * alpha, hz);
    return vec4<f32>(col, alpha);
}
