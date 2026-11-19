// ADR-2100: Cloud Environment v2 -- the temporal resolve and the depth-aware upsample (rendering::CloudRenderer).
//
// fs_cloud_resolve (at the march's resolution): last frame's accumulated cloud, reprojected through the view-
// projection it was drawn with at this pixel's cloud distance (the sky's far distance where there is no cloud, which
// reduces to the camera's rotation), clamped to the box of this frame's 3x3 neighbourhood so a moved or newly
// uncovered cloud cannot ghost, and blended by the history weight. Weight 0 (no history) passes the march through.
//
// fs_cloud_composite (at the frame's resolution): the four march texels around the pixel, weighted bilinearly and by
// how well the scene depth each was cut against matches this pixel's own, so cloud cut at a tower's edge does not
// smear over the tower or leave a halo of sky beside it. Premultiplied, over HDR.

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
    march2: vec4<f32>,
    sea: vec4<f32>,
    sea2: vec4<f32>,
    sea3: vec4<f32>,
    sea4: vec4<f32>,
    sea5: vec4<f32>,
    sea6: vec4<f32>,
    boxMin: vec4<f32>,
    boxMax: vec4<f32>,
    grid: vec4<f32>,
    grid2: vec4<f32>,
    drift: vec4<f32>,
    frameInfo: vec4<f32>,
    prevViewProj: mat4x4<f32>,
    glows: array<vec4<f32>, 32>,
};

@group(0) @binding(1) var<uniform> cu: CloudUniforms;
// resolve: 2 = this frame's march colour, 3 = its depth, 4 = the history, 5 = a clamping sampler
// composite: 2 = the resolved colour, 3 = the march depth, 4 = the frame's linear depth
@group(0) @binding(2) var cloudColour: texture_2d<f32>;
@group(0) @binding(3) var cloudMarchDepth: texture_2d<f32>;
@group(0) @binding(4) var cloudAux: texture_2d<f32>;
@group(0) @binding(5) var cloudClamp: sampler;

struct VsOut {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_cloud_fullscreen(@builtin(vertex_index) index: u32) -> VsOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VsOut;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

@fragment
fn fs_cloud_resolve(in: VsOut) -> @location(0) vec4<f32> {
    let size = vec2<i32>(textureDimensions(cloudColour, 0));
    let ip = vec2<i32>(in.clip.xy);
    let cur = textureLoad(cloudColour, ip, 0);
    let weight = cu.frameInfo.y;
    if (weight <= 0.0) {
        return cur;
    }
    var lo = cur;
    var hi = cur;
    for (var dy = -1; dy <= 1; dy = dy + 1) {
        for (var dx = -1; dx <= 1; dx = dx + 1) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            let q = clamp(ip + vec2<i32>(dx, dy), vec2<i32>(0), size - vec2<i32>(1));
            let c = textureLoad(cloudColour, q, 0);
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }
    let uv = in.clip.xy / vec2<f32>(size);
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let rd = cameraViewRay(ndc);
    let tc = textureLoad(cloudMarchDepth, ip, 0).r;
    let t = select(1.0e5, tc, tc > 0.0);
    let world = frame.cameraPos.xyz + rd * t;
    let pc = cu.prevViewProj * vec4<f32>(world, 1.0);
    if (pc.w <= 1e-4) {
        return cur;
    }
    let pn = pc.xy / pc.w;
    let puv = vec2<f32>(pn.x * 0.5 + 0.5, 0.5 - pn.y * 0.5);
    if (any(puv < vec2<f32>(0.0)) || any(puv > vec2<f32>(1.0))) {
        return cur;
    }
    let hist = clamp(textureSampleLevel(cloudAux, cloudClamp, puv, 0.0), lo, hi);
    return mix(cur, hist, weight);
}

@fragment
fn fs_cloud_composite(in: VsOut) -> @location(0) vec4<f32> {
    let msize = vec2<i32>(textureDimensions(cloudColour, 0));
    let fsize = vec2<f32>(textureDimensions(cloudAux, 0));
    let ipf = vec2<i32>(in.clip.xy);
    let z = textureLoad(cloudAux, ipf, 0).r;
    let mp = in.clip.xy / fsize * vec2<f32>(msize) - 0.5;
    let m0 = vec2<i32>(floor(mp));
    let fr = mp - floor(mp);
    var acc = vec4<f32>(0.0);
    var wsum = 0.0;
    var best = vec4<f32>(0.0);
    var bestErr = 1e30;
    for (var k = 0; k < 4; k = k + 1) {
        let o = vec2<i32>(k & 1, k >> 1);
        let q = clamp(m0 + o, vec2<i32>(0), msize - vec2<i32>(1));
        let c = textureLoad(cloudColour, q, 0);
        let zh = textureLoad(cloudMarchDepth, q, 0).g;
        let err = abs(zh - z) / max(z, 1.0);
        let bx = select(1.0 - fr.x, fr.x, o.x == 1);
        let by = select(1.0 - fr.y, fr.y, o.y == 1);
        let w = bx * by * (1.0 - smoothstep(0.0, 0.08, err)) + 1e-5 * bx * by;
        acc = acc + c * w;
        wsum = wsum + w;
        if (err < bestErr) {
            bestErr = err;
            best = c;
        }
    }
    var c = best;
    if (wsum > 1e-3) {
        c = acc / wsum;
    }
    if (c.a < 1.0 / 1024.0) {
        discard;
    }
    return c;
}
