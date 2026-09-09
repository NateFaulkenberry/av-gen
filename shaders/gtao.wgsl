// Ground-truth ambient occlusion (ADR-034), after Jimenez et al. 2016. Two half-resolution passes:
//
//   fs_gtao     horizon search over the linear depth target in `slices` directions, giving the
//               ground-truth visibility integral and a bent normal;
//   fs_temporal reprojects the previous result with the camera motion, clamps it to the
//               neighbourhood of the new one and blends.
//
// Output: rg = octahedral bent normal (world space), b = visibility, a = view depth (which the
// shading pass uses to weight its bilateral upsample).
//
// The per-pixel rotation and offset come from interleaved gradient noise indexed by the frame
// number, never by a clock, so two renders of the same frame are bit-identical.
#include "common.wgsl"

struct AoUniforms {
    sizes: vec4<f32>,      // AO width, height, 1 / width, 1 / height
    fullSize: vec4<f32>,   // scene width, height, 1 / width, 1 / height
    params: vec4<f32>,     // world radius, strength, slice count, steps per slice
    temporal: vec4<f32>,   // frame index, history blend, 1 when a history exists, thickness
    projection: vec4<f32>, // tan(fovY/2) * aspect, tan(fovY/2), near, far
};

@group(1) @binding(0) var<uniform> ao: AoUniforms;
@group(1) @binding(1) var linearDepth: texture_2d<f32>;
@group(1) @binding(2) var aoHistory: texture_2d<f32>;
@group(1) @binding(3) var aoRaw: texture_2d<f32>;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) index: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    let p = positions[index];
    var out: FsIn;
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

// Interleaved gradient noise offset by the frame index: deterministic and temporally varying.
fn aoNoise(pixel: vec2<f32>, offset: f32) -> f32 {
    let p = pixel + vec2<f32>(offset * 5.588238, offset * 3.141593);
    return fract(52.9829189 * fract(dot(p, vec2<f32>(0.06711056, 0.00583715))));
}

fn depthAt(uv: vec2<f32>) -> f32 {
    let texel = clamp(uv * ao.fullSize.xy, vec2<f32>(0.0), ao.fullSize.xy - vec2<f32>(1.0));
    return textureLoad(linearDepth, vec2<i32>(texel), 0).r;
}

// View-space position of a screen point at view depth `d` (the camera looks down -Z).
fn viewPosition(uv: vec2<f32>, d: f32) -> vec3<f32> {
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3<f32>(ndc.x * ao.projection.x * d, ndc.y * ao.projection.y * d, -d);
}

fn viewToWorldDirection(v: vec3<f32>) -> vec3<f32> {
    return frame.cameraRight.xyz * v.x + frame.cameraUp.xyz * v.y - frame.cameraForward.xyz * v.z;
}

fn viewToWorldPoint(v: vec3<f32>) -> vec3<f32> {
    return frame.cameraPos.xyz + viewToWorldDirection(v);
}

// Best-fit normal from the depth buffer: pick the closer neighbour on each axis so silhouettes do
// not smear a normal across them.
fn depthNormal(uv: vec2<f32>, p: vec3<f32>) -> vec3<f32> {
    let step = ao.fullSize.zw;
    let left = viewPosition(uv - vec2<f32>(step.x, 0.0), depthAt(uv - vec2<f32>(step.x, 0.0)));
    let right = viewPosition(uv + vec2<f32>(step.x, 0.0), depthAt(uv + vec2<f32>(step.x, 0.0)));
    let down = viewPosition(uv - vec2<f32>(0.0, step.y), depthAt(uv - vec2<f32>(0.0, step.y)));
    let up = viewPosition(uv + vec2<f32>(0.0, step.y), depthAt(uv + vec2<f32>(0.0, step.y)));
    let dx = select(right - p, p - left, abs(left.z - p.z) < abs(right.z - p.z));
    let dy = select(up - p, p - down, abs(down.z - p.z) < abs(up.z - p.z));
    let n = cross(dx, dy);
    if (dot(n, n) < 1e-12) {
        return vec3<f32>(0.0, 0.0, 1.0);
    }
    let unit = normalize(n);
    // Face the camera.
    return select(unit, -unit, unit.z < 0.0);
}

@fragment
fn fs_gtao(in: FsIn) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let depth = depthAt(uv);
    let far = ao.projection.w;
    if (depth <= 0.0 || depth >= far * 0.999) {
        return vec4<f32>(octEncode(vec3<f32>(0.0, 1.0, 0.0)), 1.0, depth);
    }
    let p = viewPosition(uv, depth);
    let n = depthNormal(uv, p);
    let v = normalize(-p);

    let radius = ao.params.x;
    // The world radius in pixels at this depth; clamped so near geometry does not stall the march.
    let radiusPixels = clamp(radius * ao.projection.y * 0.0 + radius / max(depth, 1e-3) *
                                 (0.5 * ao.fullSize.y / max(ao.projection.y, 1e-4)),
                             4.0, 96.0);
    let slices = max(u32(ao.params.z), 1u);
    let steps = max(u32(ao.params.w), 1u);
    let pixel = in.clip.xy;
    let rotation = aoNoise(pixel, ao.temporal.x);
    let offset = aoNoise(pixel + vec2<f32>(37.0, 17.0), ao.temporal.x);

    var visibility = 0.0;
    var bent = vec3<f32>(0.0);
    for (var s = 0u; s < slices; s = s + 1u) {
        let phi = PI * (f32(s) + rotation) / f32(slices);
        let sliceDir = vec2<f32>(cos(phi), sin(phi));
        let planeNormal = normalize(cross(vec3<f32>(sliceDir, 0.0), v));
        let tangent = cross(v, planeNormal);
        let projected = n - planeNormal * dot(n, planeNormal);
        let projLength = length(projected);
        if (projLength < 1e-4) {
            continue;
        }
        let projNormal = projected / projLength;
        let cosN = clamp(dot(projNormal, v), -1.0, 1.0);
        let signN = select(1.0, -1.0, dot(projNormal, tangent) > 0.0);
        let nAngle = signN * acos(cosN);

        var horizons = vec2<f32>(-1.0, -1.0);
        for (var side = 0u; side < 2u; side = side + 1u) {
            let dir = select(sliceDir, -sliceDir, side == 1u);
            var best = -1.0;
            for (var t = 1u; t <= steps; t = t + 1u) {
                let stepPixels = (f32(t) - offset) / f32(steps) * radiusPixels;
                let sampleUv = uv + dir * stepPixels * ao.fullSize.zw;
                if (any(sampleUv < vec2<f32>(0.0)) || any(sampleUv > vec2<f32>(1.0))) {
                    break;
                }
                let sampleDepth = depthAt(sampleUv);
                if (sampleDepth <= 0.0) {
                    continue;
                }
                let sp = viewPosition(sampleUv, sampleDepth);
                let delta = sp - p;
                let dist = length(delta);
                if (dist < 1e-5) {
                    continue;
                }
                let cosH = dot(delta / dist, v);
                // Range check: an occluder further away than the radius fades out instead of
                // stopping abruptly, which is what stops the halo around distant silhouettes.
                let falloff = clamp(1.0 - (dist - radius) / max(radius, 1e-4), 0.0, 1.0);
                best = max(best, mix(-1.0, cosH, falloff));
            }
            if (side == 0u) {
                horizons.x = best;
            } else {
                horizons.y = best;
            }
        }
        let h0 = nAngle + max(-acos(clamp(horizons.x, -1.0, 1.0)) - nAngle, -PI * 0.5);
        let h1 = nAngle + min(acos(clamp(horizons.y, -1.0, 1.0)) - nAngle, PI * 0.5);
        let sinN = sin(nAngle);
        let cosNa = cos(nAngle);
        let arc0 = -cos(2.0 * h0 - nAngle) + cosNa + 2.0 * h0 * sinN;
        let arc1 = -cos(2.0 * h1 - nAngle) + cosNa + 2.0 * h1 * sinN;
        visibility = visibility + projLength * 0.25 * (arc0 + arc1);
        let bentAngle = (h0 + h1) * 0.5;
        bent = bent + (v * cos(bentAngle) + tangent * sin(bentAngle)) * projLength;
    }
    visibility = clamp(visibility / f32(slices), 0.0, 1.0);
    var bentWorld = viewToWorldDirection(n);
    if (dot(bent, bent) > 1e-8) {
        bentWorld = normalize(viewToWorldDirection(normalize(bent)));
    }
    return vec4<f32>(octEncode(bentWorld), visibility, depth);
}

@fragment
fn fs_temporal(in: FsIn) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let texel = vec2<i32>(clamp(uv * ao.sizes.xy, vec2<f32>(0.0), ao.sizes.xy - vec2<f32>(1.0)));
    let current = textureLoad(aoRaw, texel, 0);
    if (ao.temporal.z < 0.5) {
        return current;
    }
    // Neighbourhood of the new frame: the history is clamped into it, which removes the ghosting
    // reprojection alone would leave behind a moving object.
    var lo = current;
    var hi = current;
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let c = clamp(texel + vec2<i32>(x, y), vec2<i32>(0), vec2<i32>(ao.sizes.xy) - vec2<i32>(1));
            let s = textureLoad(aoRaw, c, 0);
            lo = min(lo, s);
            hi = max(hi, s);
        }
    }
    let depth = current.a;
    if (depth <= 0.0) {
        return current;
    }
    // Where this point was last frame, from the camera motion (the AO pass runs before the
    // velocity target exists, so the reprojection uses the previous view-projection).
    let world = viewToWorldPoint(viewPosition(uv, depth));
    let prevClip = frame.prevViewProj * vec4<f32>(world, 1.0);
    if (prevClip.w <= 0.0) {
        return current;
    }
    let prevNdc = prevClip.xy / prevClip.w;
    if (any(abs(prevNdc) > vec2<f32>(1.0))) {
        return current;
    }
    let prevUv = vec2<f32>(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
    let prevTexel = vec2<i32>(clamp(prevUv * ao.sizes.xy, vec2<f32>(0.0), ao.sizes.xy - vec2<f32>(1.0)));
    var history = textureLoad(aoHistory, prevTexel, 0);
    // A history sample from a different surface is worthless: reject it by depth.
    if (history.a <= 0.0 || abs(history.a - depth) > max(depth * 0.06, ao.temporal.w)) {
        return current;
    }
    history = clamp(history, lo, hi);
    let blend = clamp(ao.temporal.y, 0.02, 1.0);
    var out = mix(history, current, blend);
    out.a = depth;
    return out;
}
