// ADR-1820: glossy screen-space reflections for opaque entities. A Hi-Z march (Uludag, GPU Pro 5; the min-depth
// pyramid Godot's SSR builds) of this frame's linear view depth, the hit read from the refraction copy (the opaque
// frame before the water and the glass: ADR-1603's, read-only), blurred by the surface's roughness, and trusted less
// toward the screen's edge, the ray's reach, a ray that turns toward the camera and a hit behind a thin surface.
// Deterministic: no jitter and no history, so a seek lands on the frame play drew and a still is the same every run.
//
// Its own code, written for this pass: the water's SSR (water.wgsl, ADR-1286) is a different march and is not shared.
// Needs `frame`, `sceneLinearDepth` and `refractionCopy`/`refractionSampler` (refraction.wgsl) from its includer.

// Group 3 is the IBL group; the SSR pipeline's layout extends it with the pyramid and the parameters (ADR-1820).
@group(3) @binding(6) var ssrHiZ: texture_2d<f32>;  // the min-depth pyramid's levels 1.. as mips 0..
struct SsrParams {
    a: vec4<f32>, // x = reach (m), y = roughness where the fade ends, z = roughness where it starts, w = iterations
    b: vec4<f32>, // x = pyramid levels above the full-resolution depth, y = thickness at 1 m, z = thickness floor (m)
};
@group(3) @binding(7) var<uniform> ssrParams: SsrParams;

// The minimum linear depth over a cell of `level` (0 = a pixel of the depth itself).
fn ssrDepthAt(level: i32, cell: vec2<i32>) -> f32 {
    if (level == 0) {
        let size = vec2<i32>(textureDimensions(sceneLinearDepth, 0));
        return textureLoad(sceneLinearDepth, clamp(cell, vec2<i32>(0), size - vec2<i32>(1)), 0).r;
    }
    let size = vec2<i32>(textureDimensions(ssrHiZ, level - 1));
    return textureLoad(ssrHiZ, clamp(cell, vec2<i32>(0), size - vec2<i32>(1)), level - 1).r;
}

// The size of a cell of `level`, in full-resolution pixels: exactly 2^level (the pyramid is padded so it halves exactly).
fn ssrCellSize(level: i32, full: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(exp2(f32(level)));
}

// The reflected radiance at a hit, blurred over the lobe's footprint there (`radius`, in screen heights): the centre
// and two rings of six, the inner at half the radius and turned by 30 degrees, weighted as a rough Gaussian. Under
// two pixels of radius the centre alone: a ring of taps a few pixels apart prints a bright point (a highlight) seven
// times rather than blurring it.
fn ssrRadianceAt(uv: vec2<f32>, radius: f32) -> vec3<f32> {
    let centre = textureSampleLevel(refractionCopy, refractionSampler, uv, 0.0).rgb;
    if (radius * frame.targetSize.y < 2.0) {
        return centre;
    }
    let k = array<vec2<f32>, 6>(vec2<f32>(1.0, 0.0), vec2<f32>(0.5, 0.866), vec2<f32>(-0.5, 0.866),
                                vec2<f32>(-1.0, 0.0), vec2<f32>(-0.5, -0.866), vec2<f32>(0.5, -0.866));
    let aspect = vec2<f32>(frame.targetSize.y * frame.targetSize.z, 1.0);
    let turn = mat2x2<f32>(0.866, 0.5, -0.5, 0.866);
    var inner = vec3<f32>(0.0);
    var outer = vec3<f32>(0.0);
    for (var i = 0u; i < 6u; i = i + 1u) {
        let a = (turn * k[i]) * (0.5 * radius) * aspect;
        let b = k[i] * radius * aspect;
        inner = inner + textureSampleLevel(refractionCopy, refractionSampler, clamp(uv + a, vec2<f32>(0.0),
                                           vec2<f32>(1.0)), 0.0).rgb;
        outer = outer + textureSampleLevel(refractionCopy, refractionSampler, clamp(uv + b, vec2<f32>(0.0),
                                           vec2<f32>(1.0)), 0.0).rgb;
    }
    return (centre * 4.0 + inner * 1.0 + outer * 0.5) / 13.0;
}

// The reflection from `surface` (world), whose shading normal is `n`, along `r` for a surface of `roughness`.
// `spread` is how far the reflection direction turns from one pixel to the next (radians): on a small curved
// surface, or near its silhouette, one pixel's reflection covers a wide cone that a single ray undersamples -- a
// thin bright line that crawls and flickers as the camera moves. The cone widens the blur, as roughness does, and
// from half a degree a pixel the environment (prefiltered, smooth) takes over, by two degrees entirely: a flat
// floor or wall turns its reflection by under 0.1 degree a pixel, the S3 gold sphere (70 px across) by 1.7 at its centre.
// rgb = the reflected radiance, a = how much of it to trust (0: the environment stands).
fn glossySsr(surface: vec3<f32>, n: vec3<f32>, r: vec3<f32>, roughness: f32, spread: f32) -> vec4<f32> {
    let fadeEnd = ssrParams.a.y;
    let roughFade = (1.0 - smoothstep(ssrParams.a.z, fadeEnd, roughness)) * (1.0 - smoothstep(0.01, 0.03, spread));
    if (roughFade <= 0.0) {
        return vec4<f32>(0.0);
    }
    let full = vec2<f32>(textureDimensions(sceneLinearDepth, 0));
    let fwd = frame.cameraForward.xyz;
    // A ray toward the camera finds the back of what it hits, which the depth does not hold: trusted less, then not.
    let toward = dot(r, fwd);
    let facing = smoothstep(-0.35, 0.0, toward);
    if (facing <= 0.0) {
        return vec4<f32>(0.0);
    }
    // The ray starts a little off the surface along its normal (a few millimetres per metre of depth), so a curved
    // surface's own neighbouring pixels are not taken for what it reflects.
    let depth0 = max(dot(surface - frame.cameraPos.xyz, fwd), 1e-3);
    let p = surface + n * (0.004 + 0.003 * depth0);
    let c0 = frame.viewProj * vec4<f32>(p, 1.0);
    // The ray's reach, shortened so its end stays in front of the camera.
    let w0 = max(c0.w, 1e-3);
    var reach = ssrParams.a.x;
    if (toward < 0.0) {
        reach = min(reach, (w0 - 0.05) / -toward);
    }
    if (reach <= 0.01) {
        return vec4<f32>(0.0);
    }
    let c1 = frame.viewProj * vec4<f32>(p + r * reach, 1.0);
    let w1 = max(c1.w, 1e-3);
    let s0 = vec2<f32>(c0.x / w0 * 0.5 + 0.5, 0.5 - c0.y / w0 * 0.5) * full;
    let s1 = vec2<f32>(c1.x / w1 * 0.5 + 0.5, 0.5 - c1.y / w1 * 0.5) * full;
    let k0 = 1.0 / w0;
    let k1 = 1.0 / w1;
    let d = s1 - s0;
    let lengthPx = length(d);
    if (lengthPx < 1.0) {
        return vec4<f32>(0.0);
    }
    // Where the ray leaves the screen.
    var tMax = 1.0;
    if (d.x > 0.0) { tMax = min(tMax, (full.x - 0.5 - s0.x) / d.x); }
    if (d.x < 0.0) { tMax = min(tMax, (0.5 - s0.x) / d.x); }
    if (d.y > 0.0) { tMax = min(tMax, (full.y - 0.5 - s0.y) / d.y); }
    if (d.y < 0.0) { tMax = min(tMax, (0.5 - s0.y) / d.y); }
    let levels = i32(ssrParams.b.x + 0.5);
    let iterations = u32(ssrParams.a.w + 0.5);
    let tStep = 1.0 / lengthPx; // one pixel along the ray
    var t = 1.5 * tStep;        // off the surface's own pixel
    var level = 0;
    var hit = false;
    var hitDepth = 0.0;
    var hitGap = 0.0;
    var hitCell = vec2<i32>(0);
    for (var i = 0u; i < iterations; i = i + 1u) {
        if (t > tMax) {
            break;
        }
        let pos = s0 + d * t;
        let cellSize = ssrCellSize(level, full);
        let cell = floor(pos / cellSize);
        // where the ray leaves this cell
        let lo = cell * cellSize;
        let hi = lo + cellSize;
        var tExit = 1e9;
        if (d.x > 0.0) { tExit = min(tExit, (hi.x - s0.x) / d.x); }
        if (d.x < 0.0) { tExit = min(tExit, (lo.x - s0.x) / d.x); }
        if (d.y > 0.0) { tExit = min(tExit, (hi.y - s0.y) / d.y); }
        if (d.y < 0.0) { tExit = min(tExit, (lo.y - s0.y) / d.y); }
        tExit = min(tExit, tMax) + 0.01 * tStep;
        let zMin = ssrDepthAt(level, vec2<i32>(cell));
        let zEnter = 1.0 / mix(k0, k1, t);
        let zExit = 1.0 / mix(k0, k1, tExit);
        if (max(zEnter, zExit) < zMin) {
            // the ray crosses the whole cell in front of everything in it: skip it, and look coarser
            t = tExit;
            level = min(level + 1, levels);
            continue;
        }
        if (level > 0) {
            // something in the cell is in front of the ray's far end: move to where the ray reaches it, look finer
            if (zEnter < zMin && abs(k1 - k0) > 1e-9) {
                let tz = (1.0 / zMin - k0) / (k1 - k0);
                t = clamp(tz, t, tExit);
            }
            level = level - 1;
            continue;
        }
        // a pixel: the ray is at or behind the surface here; a hit if it is not behind it by more than its thickness
        let thickness = max(ssrParams.b.z, zMin * ssrParams.b.y);
        let gap = max(zEnter, zExit) - zMin;
        if (min(zEnter, zExit) <= zMin + thickness) {
            hit = true;
            hitDepth = zMin;
            hitGap = max(gap, 0.0) / thickness;
            hitCell = vec2<i32>(cell);
            // Where in this pixel the ray reaches the surface, not where it entered the pixel: a hit snapped to pixel
            // entries samples the reflected image at positions that step as the camera moves (it shimmers).
            if (zEnter < zMin && abs(k1 - k0) > 1e-9) {
                t = clamp((1.0 / zMin - k0) / (k1 - k0), t, tExit);
            }
            break;
        }
        // behind something thin: pass under it
        t = tExit;
    }
    if (!hit) {
        return vec4<f32>(0.0);
    }
    let uv = (s0 + d * t) / full;
    let edge = smoothstep(0.0, 0.08, min(uv.x, 1.0 - uv.x)) * smoothstep(0.0, 0.08, min(uv.y, 1.0 - uv.y));
    let far = 1.0 - smoothstep(0.75, 1.0, t);
    let snug = 1.0 - smoothstep(0.5, 1.0, hitGap); // a hit well inside the thickness is trusted, its edge less
    // A hit on a silhouette (its neighbours' depths far from its own): which side of the edge a ray lands on flips
    // from frame to frame as the camera moves, a stepped edge that crawls. Trusted less there, so the reflected edge
    // is soft against the environment rather than stepped (never below a quarter: the reflection keeps its shape).
    let dl = ssrDepthAt(0, hitCell - vec2<i32>(1, 0));
    let dr = ssrDepthAt(0, hitCell + vec2<i32>(1, 0));
    let du = ssrDepthAt(0, hitCell - vec2<i32>(0, 1));
    let dd = ssrDepthAt(0, hitCell + vec2<i32>(0, 1));
    let jump = max(max(abs(dl - hitDepth), abs(dr - hitDepth)), max(abs(du - hitDepth), abs(dd - hitDepth))) / hitDepth;
    let settled = 1.0 - 0.75 * smoothstep(0.05, 0.25, jump);
    // the distance travelled, for the blur: the hit's view depth against the start's, along the ray
    let travelled = abs(hitDepth - w0) / max(abs(toward), 0.2);
    // the GGX lobe's spread is about alpha = roughness^2 radians; a metre at the hit spans 1 / (2 tan(fov/2) depth)
    // screen heights (cameraUp.w = tan of the half vertical field of view)
    let alpha = roughness * roughness + spread;
    let radius = clamp(alpha * travelled * 0.5 / (max(frame.cameraUp.w, 1e-3) * max(hitDepth, 0.5)), 0.0, 0.04);
    return vec4<f32>(ssrRadianceAt(uv, radius), roughFade * facing * edge * far * snug * settled);
}
