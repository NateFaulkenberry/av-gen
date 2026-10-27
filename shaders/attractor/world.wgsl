// ADR-1850: THE STRANGE ATTRACTOR -- the world pass (rendering::AttractorRenderer).
//
// One fullscreen pass. Per pixel: the view ray is marched over the baked height field (scene::AttractorWorld), with a
// little near-field displacement; a hit is coloured by its stratum (the same warped, tilted strata coordinate the
// generator cut the terraces along, so each tread is one layer), lit by a low sun with a soft height-field shadow, a
// sky ambient and a ground bounce, and fogged by an analytic height fog whose colour leans to the sun's glow on one
// side and to the Belt of Venus on the other. A miss is the twilight sky. Writes HDR and clip depth.
//
// The palette arrives as 16 linear colours (the renderer blends the art-directed tables); see attractor_renderer.cpp.

#include "common.wgsl"

struct AttractorUniforms {
    world: vec4<f32>,   // xy origin, z extent, w resolution
    heights: vec4<f32>, // x min, y max, z texel (m), w time (s)
    sun: vec4<f32>,     // xyz toward the sun, w intensity
    strata: vec4<f32>,  // x tilt angle, y tilt slope, z band height (m), w phase
    fold: vec4<f32>,    // x direction angle, y wavelength (m), z amplitude (m)
    look: vec4<f32>,    // x atmosphere, y strata, z exposure, w detail
    look2: vec4<f32>,   // x ambient, y palette, z noise seed (u32 bits), w terraces
    view: vec4<f32>,    // xy view direction (xz), z sun side, w domain warp
    pal: array<vec4<f32>, 16>,
    rayForward: vec4<f32>, // ADR-1855: the view ray basis (double precision on the CPU)
    rayRight: vec4<f32>,   // right * tan(half fov x)
    rayUp: vec4<f32>,      // up * tan(half fov y)
};

@group(0) @binding(1) var<uniform> au: AttractorUniforms;
@group(0) @binding(2) var heightTex: texture_2d<f32>;
@group(0) @binding(3) var maskTex: texture_2d<f32>;
@group(0) @binding(4) var maskSampler: sampler;
@group(0) @binding(5) var flowTex: texture_2d<f32>; // ADR-1854: rg the river's downstream direction, b distance, a channel

const P_ZENITH: u32 = 0u;
const P_HORIZON: u32 = 1u;
const P_BELT: u32 = 2u;
const P_EARTHSHADOW: u32 = 3u;
const P_SUN: u32 = 4u;
const P_GLOW: u32 = 5u;
const P_HAZE: u32 = 6u;
const P_AMBIENT: u32 = 7u;
const P_BOUNCE: u32 = 8u;
const P_STRATA0: u32 = 9u;
const P_DUST: u32 = 13u;
const P_PLAYA: u32 = 14u;
const P_ROCK: u32 = 15u;

struct VsOut {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_world(@builtin(vertex_index) index: u32) -> VsOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VsOut;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

// ---- noise: the generator's gradient noise, bit-for-bit in its hashing (scene/attractor_world.cpp) ------------

fn hashU(xi: u32) -> u32 {
    var x = xi;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}

fn hash01(x: i32, y: i32, seed: u32) -> f32 {
    let h = hashU((bitcast<u32>(x) * 0x8da6b343u) ^ hashU((bitcast<u32>(y) * 0xd8163841u) ^ seed));
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn gradAt(x: i32, y: i32, seed: u32) -> vec2<f32> {
    let a = hash01(x, y, seed) * 6.28318530718;
    return vec2<f32>(cos(a), sin(a));
}

fn gnoise(p: vec2<f32>, seed: u32) -> f32 {
    let fl = floor(p);
    let i = vec2<i32>(fl);
    let f = p - fl;
    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    let a = dot(gradAt(i.x, i.y, seed), f);
    let b = dot(gradAt(i.x + 1, i.y, seed), f - vec2<f32>(1.0, 0.0));
    let c = dot(gradAt(i.x, i.y + 1, seed), f - vec2<f32>(0.0, 1.0));
    let d = dot(gradAt(i.x + 1, i.y + 1, seed), f - vec2<f32>(1.0, 1.0));
    return a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y;
}

const kOct = mat2x2<f32>(vec2<f32>(0.80, 0.60), vec2<f32>(-0.60, 0.80));

fn fbm(pin: vec2<f32>, octaves: i32, seed: u32) -> f32 {
    var p = pin;
    var sum = 0.0;
    var amp = 1.0;
    var norm = 0.0;
    for (var i = 0; i < octaves; i = i + 1) {
        sum = sum + amp * gnoise(p, seed + u32(i) * 101u);
        norm = norm + amp;
        amp = amp * 0.5;
        p = kOct * p * 2.03;
    }
    return sum / norm;
}

// A cheap value noise for the near-field detail (no need to match the CPU).
fn vhash(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
fn vnoise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    return mix(mix(vhash(i), vhash(i + vec2<f32>(1.0, 0.0)), u.x), mix(vhash(i + vec2<f32>(0.0, 1.0)), vhash(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}

// ---- the height field ------------------------------------------------------------------------------------------

fn texCoord(xz: vec2<f32>) -> vec2<f32> {
    return (xz - au.world.xy) / au.heights.z - 0.5;
}

// The baked field at `xz`: x height, y strata offset, zw the bilinear gradient (dh/dx, dh/dz).
fn fieldAt(xz: vec2<f32>) -> vec4<f32> {
    let n = i32(au.world.w);
    let t = clamp(texCoord(xz), vec2<f32>(0.0), vec2<f32>(f32(n - 1)));
    let i0 = min(vec2<i32>(t), vec2<i32>(n - 2));
    let f = t - vec2<f32>(i0);
    let a = textureLoad(heightTex, i0, 0).rg;
    let b = textureLoad(heightTex, i0 + vec2<i32>(1, 0), 0).rg;
    let c = textureLoad(heightTex, i0 + vec2<i32>(0, 1), 0).rg;
    let d = textureLoad(heightTex, i0 + vec2<i32>(1, 1), 0).rg;
    // Plain bilinear. (Gate 1 weighted it toward smoothstep to hide the texel grid in the shading; that flattened the
    // surface at every texel centre, so on a face the strata contours -- and the ledges `bandCut` cuts along them --
    // zigzagged at texel spacing. The shading normal and the near surface now come from `fieldSmooth`, which hides the grid itself.)
    let v = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    let gx = mix(b.x - a.x, d.x - c.x, f.y) / au.heights.z;
    let gz = mix(c.x - a.x, d.x - b.x, f.x) / au.heights.z;
    return vec4<f32>(v.x, v.y, gx, gz);
}

// The strata's band profile (scene/attractor_world.cpp `bandProfile`; the generator's riser is 0.2). Here the riser
// widens with distance: a ledge the size of a pixel aliases into hatching, a softened one does not.
// The band coordinate: the strata coordinate under a monotone wobble, so bands are of uneven thickness
// (scene/attractor_world.cpp `bandWarp`, line for line). x the warped coordinate, y its derivative.
fn bandWarp(s: f32) -> vec2<f32> {
    let band = au.strata.z;
    let a = s / (band * 2.3) + 0.7;
    let b = s / (band * 0.93) + 1.7;
    return vec2<f32>(s + band * (0.32 * sin(a) + 0.18 * sin(b)), 1.0 + 0.32 / 2.3 * cos(a) + 0.18 / 0.93 * cos(b));
}

fn bandProfile(s: f32, riser: f32) -> f32 {
    let band = au.strata.z;
    let u = bandWarp(s);
    let t = u.x / band;
    let i = floor(t);
    let f = t - i;
    let hard = smoothstep(0.35, 0.75, bandHardness(i));
    let gg = clamp((f - (1.0 - riser)) / riser, 0.0, 1.0);
    let st = gg * gg * (3.0 - 2.0 * gg);
    let tu = (i + f + (st - f) * hard) * band;
    return s + (tu - u.x) / u.y;
}

// The bands cut again on the steep faces, at sub-texel scale: a face spans less than a texel horizontally, so the
// generator's terraces cannot show there, and the colour bands would read as a wash over smooth rock. The band
// profile applied to the surface steps the face into ledges and risers the march sees (silhouettes included). It fades
// out, and its risers soften, with distance (ADR-1853: the hatching on far mesas).
fn bandCut(fd: vec4<f32>, dist: f32) -> f32 {
    let slope = length(fd.zw);
    let amt = au.look2.w * smoothstep(0.8, 1.8, slope) * (1.0 - smoothstep(2500.0, 5000.0, dist));
    if (amt <= 0.0) {
        return 0.0;
    }
    let s0 = fd.x + fd.y;
    let riser = 0.16 + 0.4 * smoothstep(1800.0, 4500.0, dist);
    return (bandProfile(s0, riser) - s0) * amt;
}

fn heightBase(xz: vec2<f32>) -> f32 {
    return fieldAt(xz).x;
}

// The baked field from a uniform cubic B-spline over 4x4 texels (C2): x height, y strata offset, zw the gradient. Used
// near the camera (and for every shading normal): a bilinear patch's contours turn at every texel edge, so on a steep
// face the band ledges cut along them wiggled at texel spacing (the wood grain) and each texel column faced a slightly
// different way (the curtains). The B-spline's contours and gradient run smoothly across texels.
fn bsplineW(f: f32) -> vec4<f32> {
    let g = 1.0 - f;
    return vec4<f32>(g * g * g, 3.0 * f * f * f - 6.0 * f * f + 4.0, -3.0 * f * f * f + 3.0 * f * f + 3.0 * f + 1.0, f * f * f) / 6.0;
}
fn bsplineD(f: f32) -> vec4<f32> {
    let g = 1.0 - f;
    return vec4<f32>(-g * g, 3.0 * f * f - 4.0 * f, -3.0 * f * f + 2.0 * f + 1.0, f * f) * 0.5;
}
fn fieldSmooth(xz: vec2<f32>) -> vec4<f32> {
    let n = i32(au.world.w);
    let t = texCoord(xz);
    let i = vec2<i32>(floor(t));
    let f = t - floor(t);
    let wx = bsplineW(f.x);
    let wy = bsplineW(f.y);
    let dx = bsplineD(f.x);
    let dy = bsplineD(f.y);
    var v = vec2<f32>(0.0);
    var g = vec2<f32>(0.0);
    for (var y = 0; y < 4; y = y + 1) {
        for (var x = 0; x < 4; x = x + 1) {
            let c = clamp(i + vec2<i32>(x - 1, y - 1), vec2<i32>(0), vec2<i32>(n - 1));
            let hv = textureLoad(heightTex, c, 0).rg;
            v = v + hv * (wx[x] * wy[y]);
            g = g + hv.x * vec2<f32>(dx[x] * wy[y], wx[x] * dy[y]);
        }
    }
    return vec4<f32>(v, g / au.heights.z);
}

// The surface's field at `dist` from the camera: the B-spline near, the bilinear far, blended between.
const kSmoothNear: f32 = 4000.0;
const kSmoothFar: f32 = 5500.0;
fn surfaceField(xz: vec2<f32>, dist: f32) -> vec4<f32> {
    if (dist >= kSmoothFar) {
        return fieldAt(xz);
    }
    let sm = fieldSmooth(xz);
    if (dist <= kSmoothNear) {
        return sm;
    }
    return mix(sm, fieldAt(xz), smoothstep(kSmoothNear, kSmoothFar, dist));
}

fn masksAt(xz: vec2<f32>) -> vec4<f32> {
    let uv = (xz - au.world.xy) / au.world.z;
    return textureSampleLevel(maskTex, maskSampler, uv, 0.0);
}

// Near-field relief on rock, so it holds up from tens of metres.
//   * On the flats: weathered plates and chips at metre scale.
//   * On the faces: a displacement that depends only on (x, z) can only vary ALONG a vertical face, so it carves
//     vertical grooves -- streaks again. So the faces' relief is a pattern in (along the face, the strata coordinate):
//     the strata coordinate climbs with height on a face, which makes the pattern two-dimensional ON the face --
//     horizontal bedding, blocks broken by joints, soft layers recessed. A height displacement moves a steep face
//     sideways by only (height / slope), so the relief grows with the slope to keep its depth.
// Faded out with distance.
fn detailAt(xz: vec2<f32>, dist: f32, fd: vec4<f32>) -> f32 {
    // metre-scale relief is sub-pixel beyond a kilometre and only aliases (a woven grid on the faces): fade it out
    let fade = 1.0 - smoothstep(250.0, 900.0, dist);
    if (fade <= 0.0 || au.look.w <= 0.0) {
        return 0.0;
    }
    let m = masksAt(xz);
    let slope = length(fd.zw);
    let steep = smoothstep(0.6, 1.6, slope);
    // flats
    // flats: rounded weathered relief (stepped plates drew stair-stepped silhouettes and specks on rims)
    let b1 = vnoise(xz / 7.0);
    let b2 = vnoise(kOct * xz / 2.6 + vec2<f32>(7.3, 1.1));
    let flat = 1.2 * b1 + 0.5 * b2 - 0.85;
    // faces: (along, up) on the face
    var face = 0.0;
    if (steep > 0.0) {
        let dir = fd.zw / max(slope, 1e-4);
        let along = dot(xz, vec2<f32>(-dir.y, dir.x));
        let up = fd.x + fd.y; // the strata coordinate: height on the face
        // beds of uneven thickness (the coordinate is wobbled before it is cut), recessed softly at their partings;
        // sparse broad joints, so the face reads as rock rather than brickwork; a broad weathering skin
        // beds of strongly uneven thickness (two incommensurate periods, warped along the face), each bed's
        // parting expressed only in some places, so the face never shows evenly spaced lines
        let bu = up / 7.3 + 0.9 * vnoise(vec2<f32>(along / 45.0, up / 13.0)) + 0.35 * sin(up / 3.1);
        let bed = fract(bu);
        let showing = smoothstep(0.35, 0.7, vnoise(vec2<f32>(along / 25.0, floor(bu) * 2.1)));
        let beds = 1.0 - (1.0 - smoothstep(0.0, 0.3, bed) * smoothstep(1.0, 0.8, bed)) * showing;
        let skin = vnoise(vec2<f32>(along / 11.0, up / 7.0));
        face = 0.35 * beds + 0.3 * skin - 0.5;
    }
    let relief = mix(flat, face, steep);
    let onRock = 0.25 + 0.75 * m.r;
    // on a face, `relief` metres of height move the face `relief / slope` sideways: about a metre and a half of depth
    // On a steep face the pattern's `up` changes by `slope` metres per horizontal metre, so the march samples it
    // coarsely across the face: the gain is capped (a high one turned that undersampling into a vertical comb).
    let gain = mix(1.0, 1.1 * clamp(slope, 1.0, 2.5), steep);
    return relief * onRock * gain * fade * au.look.w;
}

fn heightAt(xz: vec2<f32>, dist: f32) -> f32 {
    let fd = surfaceField(xz, dist);
    return fd.x + bandCut(fd, dist) + detailAt(xz, dist, fd);
}

fn normalAt(xz: vec2<f32>, dist: f32) -> vec3<f32> {
    // the smooth field's gradient, plus the band cut's and the detail's by central differences
    let c = fieldSmooth(xz);
    let e = max(0.25, dist * 0.0008);
    let ex = vec2<f32>(e, 0.0);
    let ez = vec2<f32>(0.0, e);
    let fxp = surfaceField(xz + ex, dist);
    let fxm = surfaceField(xz - ex, dist);
    let fzp = surfaceField(xz + ez, dist);
    let fzm = surfaceField(xz - ez, dist);
    let dx = bandCut(fxp, dist) - bandCut(fxm, dist) + detailAt(xz + ex, dist, fxp) - detailAt(xz - ex, dist, fxm);
    let dz = bandCut(fzp, dist) - bandCut(fzm, dist) + detailAt(xz + ez, dist, fzp) - detailAt(xz - ez, dist, fzm);
    let g = select(c.zw, (vec2<f32>(fxp.x - fxm.x, fzp.x - fzm.x)) / (2.0 * e), dist > kSmoothNear);
    return normalize(vec3<f32>(-(g.x + dx / (2.0 * e)), 1.0, -(g.y + dz / (2.0 * e))));
}

fn insideWorld(xz: vec2<f32>) -> bool {
    let lo = au.world.xy;
    let hi = au.world.xy + vec2<f32>(au.world.z);
    return all(xz >= lo) && all(xz <= hi);
}

fn sceneTop() -> f32 {
    return au.heights.y + 6.0;
}

// Returns the hit distance, or -1.
fn march(ro: vec3<f32>, rd: vec3<f32>, tmax: f32) -> f32 {
    let top = sceneTop();
    var t = 0.5;
    if (ro.y > top) {
        if (rd.y >= 0.0) {
            return -1.0;
        }
        t = (ro.y - top) / -rd.y;
    }
    var tPrev = t;
    for (var i = 0; i < 480; i = i + 1) {
        let p = ro + rd * t;
        if (t > tmax || (p.y > top && rd.y > 0.0) || !insideWorld(p.xz)) {
            return -1.0;
        }
        let d = p.y - heightAt(p.xz, t);
        if (d < 0.0) {
            // overshot: bisect back to the surface
            var a = tPrev;
            var b = t;
            for (var k = 0; k < 8; k = k + 1) {
                let m = 0.5 * (a + b);
                let pm = ro + rd * m;
                if (pm.y - heightAt(pm.xz, m) < 0.0) {
                    b = m;
                } else {
                    a = m;
                }
            }
            return 0.5 * (a + b);
        }
        if (d < 0.0006 * t) {
            return t;
        }
        tPrev = t;
        // the clearance is vertical, so it under-steps by a safety factor
        t = t + max(d * 0.38, 0.0012 * t + 0.03);
    }
    return -1.0;
}

// Soft shadow toward the sun. Near its start it tests the same surface the point was shaded on -- the band cut and
// the detail included -- because a tread the band cut lowered lies below the baked surface, and a shadow ray tested
// against the baked one started inside rock: those treads came out black (ADR-1853, the dashes). Farther along, the
// baked surface is enough.
fn softShadow(p: vec3<f32>, l: vec3<f32>, dist: f32) -> f32 {
    var res = 1.0;
    var t = 1.5;
    let top = sceneTop();
    for (var i = 0; i < 72; i = i + 1) {
        let q = p + l * t;
        if (q.y > top || !insideWorld(q.xz)) {
            break;
        }
        var h: f32;
        if (t < 120.0) {
            h = heightAt(q.xz, dist);
        } else {
            h = heightBase(q.xz);
        }
        let d = q.y - h;
        res = min(res, 10.0 * d / t);
        if (res < 0.0) {
            break;
        }
        t = t + clamp(d * 0.5, 0.5 + t * 0.02, 160.0);
    }
    res = clamp(res, 0.0, 1.0);
    return res * res * (3.0 - 2.0 * res);
}

// Ambient occlusion from the surface itself, near the camera (four taps up the normal): ledges and the bases of risers
// darken, which is what makes carved rock read as carved at close range.
fn nearAO(p: vec3<f32>, n: vec3<f32>, dist: f32) -> f32 {
    if (dist > 1500.0) {
        return 1.0;
    }
    var occ = 0.0;
    var w = 1.0;
    for (var i = 1; i <= 4; i = i + 1) {
        let h = 1.5 * f32(i);
        let q = p + n * h;
        occ = occ + w * max(0.0, h - (q.y - heightAt(q.xz, dist)) * max(n.y, 0.35)) / h;
        w = w * 0.6;
    }
    return mix(clamp(1.0 - 0.5 * occ, 0.35, 1.0), 1.0, smoothstep(800.0, 1500.0, dist));
}

// ---- strata ------------------------------------------------------------------------------------------------------

fn strataCoord(p: vec3<f32>) -> f32 {
    return p.y + fieldAt(p.xz).y; // the generator's own tilt + warp + fold, baked
}

fn pal(i: u32) -> vec3<f32> {
    return au.pal[i].rgb;
}

// Band hardness: the generator's own per-band hash (attractor_world.cpp, pass 1b). Hard bands stand as cliffs with a
// ledge on top; soft bands weather back to slopes.
fn bandHardness(bi: f32) -> f32 {
    return hash01(i32(bi), 7, bitcast<u32>(au.look2.z));
}

// The colour of a stratum. Everything is keyed to the terrace band the generator cut (`floor(s / band)`), so every
// colour boundary sits on a step in the rock, not across it:
//   * the band's colour comes from a slow noise over the band index (neighbours mostly agree, so formations several
//     bands thick emerge), biased by depth in the stack: rust and coral low, sandstone, a bone caprock high;
//   * hard bands are the cliff-formers: cleaner and lighter; soft bands are darker and weathered toward rubble;
//   * a bedding plane at each band's base and faint laminations inside.
fn strataColour(s: f32, xz: vec2<f32>) -> vec3<f32> {
    let seed = bitcast<u32>(au.look2.z);
    let band = au.strata.z;
    let t = bandWarp(s).x / band; // the bands' own (uneven) coordinate
    let bi = floor(t);
    let bf = t - bi;
    let q = bi * 0.23;
    let q0 = floor(q);
    let qf = q - q0;
    let n = mix(hash01(i32(q0), 3, seed), hash01(i32(q0) + 1, 3, seed), qf * qf * (3.0 - 2.0 * qf));
    let level = clamp(bi * band / max(au.heights.y, 1.0), 0.0, 1.0); // 0 low in the stack .. 1 high
    let r = n + (0.5 - level) * 0.55;
    var idx = 0u;                                   // sandstone
    if (r > 0.60) { idx = 1u; }                     // rust
    if (r > 0.88) { idx = 3u; }                     // the accent (coral)
    if (r < 0.20) { idx = 2u; }                     // bone
    var c = pal(P_STRATA0 + idx);
    let hard = bandHardness(bi);
    c = mix(mix(c, pal(P_DUST), 0.3) * 0.82, c * 1.06, smoothstep(0.35, 0.75, hard));
    c = c * (0.93 + 0.12 * hash01(i32(bi), 5, seed));
    c = c * mix(0.82, 1.0, smoothstep(0.0, 0.05, bf)); // bedding plane (the band cut's ledges carry most of it)
    let lam = 0.5 + 0.5 * sin((bf * 5.0 + hash01(i32(bi), 4, seed) * 6.0) * 6.2831);
    c = c * (0.97 + 0.03 * lam);
    return c;
}

// ---- sky and air --------------------------------------------------------------------------------------------------

fn skyColour(rd: vec3<f32>) -> vec3<f32> {
    let l = au.sun.xyz;
    let el = rd.y;
    let flat = normalize(vec2<f32>(rd.x, rd.z) + vec2<f32>(1e-5, 0.0));
    let sunFlat = normalize(vec2<f32>(l.x, l.z));
    let towardSun = dot(flat, sunFlat); // -1 anti-solar .. 1 solar
    // the horizon: warm toward the sun, the Belt of Venus away from it
    let horizon = mix(pal(P_BELT), pal(P_HORIZON), smoothstep(-0.5, 0.75, towardSun));
    let mid = mix(pal(P_EARTHSHADOW), pal(P_ZENITH), 0.35) * 1.1;
    var c = mix(horizon, mid, smoothstep(0.0, 0.16, el));
    c = mix(c, pal(P_ZENITH), smoothstep(0.10, 0.55, el));
    // Earth's shadow: a blue-grey band rising from the anti-solar horizon, under the belt
    let shadowEdge = 0.035 + 0.02 * (1.0 - smoothstep(-1.0, 0.0, towardSun));
    let shadowAmt = (1.0 - smoothstep(-0.2, 0.25, towardSun)) * (1.0 - smoothstep(shadowEdge * 0.6, shadowEdge, el));
    c = mix(c, pal(P_EARTHSHADOW), shadowAmt * 0.75);
    // sun glow and disc
    let mu = max(dot(rd, l), 0.0);
    c = c + pal(P_GLOW) * (0.45 * pow(mu, 6.0) + 0.9 * pow(mu, 48.0)) * au.sun.w;
    c = c + pal(P_SUN) * 30.0 * smoothstep(0.99985, 0.99995, mu) * au.sun.w;
    // thin high cloud streaks, lit from below by the sun side
    let q = rd.xz / max(el + 0.06, 0.02);
    let streak = fbm(vec2<f32>(q.x * 0.35, q.y * 1.6) + vec2<f32>(0.0, 3.0), 4, 91u);
    let cloud = smoothstep(0.05, 0.35, streak) * smoothstep(0.02, 0.10, el) * (1.0 - smoothstep(0.25, 0.6, el));
    let cloudCol = mix(pal(P_BELT) * 1.1, pal(P_GLOW) * 1.3, smoothstep(-0.3, 0.9, towardSun));
    c = mix(c, cloudCol, cloud * 0.35);
    return c;
}

// The haze along a ray of length t (Quilez's analytic height fog), and the colour it scatters toward the eye.
fn applyAir(col: vec3<f32>, ro: vec3<f32>, rd: vec3<f32>, t: f32) -> vec3<f32> {
    let a = 0.00005 * au.look.x;
    let b = 1.0 / 900.0;
    let rdy = select(rd.y, 1e-4, abs(rd.y) < 1e-4);
    let fogAmt = a / b * exp(-b * (ro.y - au.heights.x)) * (1.0 - exp(-t * rdy * b)) / rdy;
    // a touch of wavelength dependence: the reds survive further, so distance cools toward violet-blue
    let ext = exp(-fogAmt * vec3<f32>(0.85, 1.0, 1.25));
    let l = au.sun.xyz;
    let mu = max(dot(rd, l), 0.0);
    let flat = normalize(vec2<f32>(rd.x, rd.z) + vec2<f32>(1e-5, 0.0));
    let towardSun = dot(flat, normalize(vec2<f32>(l.x, l.z)));
    var air = mix(pal(P_HAZE), mix(pal(P_BELT), pal(P_HAZE), 0.5), (1.0 - smoothstep(-0.6, 0.2, towardSun)) * 0.6);
    air = air + pal(P_GLOW) * pow(mu, 5.0) * 0.6 * au.sun.w;
    return col * ext + air * (1.0 - ext);
}

// ---- shading ------------------------------------------------------------------------------------------------------

fn shade(p: vec3<f32>, rd: vec3<f32>, t: f32) -> vec3<f32> {
    let n0 = normalAt(p.xz, t);
    // a fine grain on the flats, fading with distance (no grooves on the faces: those read as streaks)
    let near = 1.0 - smoothstep(300.0, 2500.0, t);
    let steepN = smoothstep(0.3, 0.7, 1.0 - n0.y);
    let ge = 0.4;
    let gx = vnoise(p.xz / 1.7 + vec2<f32>(ge, 0.0)) - vnoise(p.xz / 1.7 - vec2<f32>(ge, 0.0));
    let gz = vnoise(p.xz / 1.7 + vec2<f32>(0.0, ge)) - vnoise(p.xz / 1.7 - vec2<f32>(0.0, ge));
    let n = normalize(n0 - vec3<f32>(gx, 0.0, gz) * 0.35 * (1.0 - steepN) * near * au.look.w);
    let m = masksAt(p.xz);
    let l = au.sun.xyz;
    let slope = 1.0 - n.y; // 0 flat .. 1 vertical
    // albedo: strata on the steep rock, dust on the flats, a pale playa on the valley floor
    let s = strataCoord(p);
    var strata = strataColour(s, p.xz);
    strata = mix(pal(P_ROCK) * 1.4, strata, clamp(au.look.y, 0.0, 1.0));
    // cliffs show the strata; talus is their rubble; flats are dust, with desert-varnish patches
    let cliffAmt = smoothstep(0.32, 0.6, slope) * (0.4 + 0.6 * m.r);
    let talusAmt = smoothstep(0.06, 0.3, slope) * m.r;
    let varnish = 0.72 + 0.5 * fbm(p.xz / 380.0, 3, 61u) + 0.12 * vnoise(p.xz / 23.0);
    let dust = mix(pal(P_DUST), strata, 0.22 * m.r) * varnish;
    let rubble = mix(strata, pal(P_DUST), 0.45) * (0.8 + 0.3 * vnoise(p.xz / 4.0));
    var albedo = mix(dust, rubble, talusAmt);
    albedo = mix(albedo, strata, cliffAmt);
    let rockAmt = max(cliffAmt, talusAmt);
    // weathering on the faces: patchy staining across, not down (no vertical streaks)
    let stain = vnoise(vec2<f32>(dot(p.xz, vec2<f32>(0.7, 0.7)) / 23.0, p.y / 9.0));
    albedo = albedo * mix(1.0, 0.9 + 0.18 * stain, cliffAmt * near);
    // the playa: pale mineral with faint polygonal cracking
    let crackW = 0.012 + t * 0.00004; // lines stay about a pixel wide
    let cracks = smoothstep(crackW, 0.0, abs(vnoise(p.xz / 2.8) - 0.5)) * 0.12 * (1.0 - smoothstep(300.0, 900.0, t));
    let playa = pal(P_PLAYA) * (0.95 - cracks + 0.08 * vnoise(p.xz / 140.0));
    albedo = mix(albedo, playa, m.g * (1.0 - rockAmt));

    // light
    // start the shadow ray off the surface by more than the relief's pixel-scale wrinkles (they self-shadowed as specks)
    let shadow = softShadow(p + n * (1.2 + 0.002 * t) + l * 0.8, l, t);
    let dif = max(dot(n, l), 0.0) * shadow;
    let cavRaw = clamp(0.35 + 1.3 * (m.b - 0.5) + 0.55, 0.25, 1.0);
    var cav = mix(cavRaw, 0.85, smoothstep(0.25, 0.55, slope)); // a 2D mask means nothing on a face
    cav = cav * nearAO(p, n, t);
    let skyAmb = (0.55 + 0.45 * n.y) * cav;
    let back = clamp(dot(n, normalize(vec3<f32>(-l.x, 0.0, -l.z))), 0.0, 1.0) * cav;
    let bounce = clamp(0.5 - 0.5 * n.y + 0.2, 0.0, 1.0) * cav;
    var light = pal(P_SUN) * 4.6 * au.sun.w * dif;
    light = light + pal(P_AMBIENT) * 0.32 * au.look2.x * skyAmb;
    light = light + pal(P_BELT) * 0.18 * au.look2.x * back;
    light = light + pal(P_BOUNCE) * 0.22 * au.look2.x * bounce * (0.4 + 0.6 * shadow);
    var col = albedo * light;
    // a soft sheen on the playa toward the sun
    let h = normalize(l - rd);
    col = col + pal(P_GLOW) * m.g * pow(max(dot(n, h), 0.0), 40.0) * 0.25 * shadow * au.sun.w;
    return col;
}

// ---- the river -------------------------------------------------------------------------------------------------

const kWaterY: f32 = -0.8; // the gorge's floor is at -2 m: the river stands 1.2 m deep there and nowhere else

// A cheap shade for what the river reflects: the same albedo and sun, no shadow march and no AO.
fn shadeReflected(p: vec3<f32>, t: f32) -> vec3<f32> {
    let n = normalAt(p.xz, t);
    let m = masksAt(p.xz);
    let l = au.sun.xyz;
    let slope = 1.0 - n.y;
    let s = strataCoord(p);
    let strata = strataColour(s, p.xz);
    let cliffAmt = smoothstep(0.32, 0.6, slope) * (0.4 + 0.6 * m.r);
    var albedo = mix(pal(P_DUST), strata, max(cliffAmt, 0.3 * m.r));
    albedo = mix(albedo, pal(P_PLAYA), m.g * (1.0 - cliffAmt));
    let dif = max(dot(n, l), 0.0) * smoothstep(-0.05, 0.25, dot(n, l));
    var light = pal(P_SUN) * 5.0 * au.sun.w * dif;
    light = light + pal(P_AMBIENT) * 0.32 * au.look2.x * (0.55 + 0.45 * n.y);
    light = light + pal(P_BOUNCE) * 0.22 * au.look2.x * clamp(0.7 - 0.5 * n.y, 0.0, 1.0);
    return albedo * light;
}

// The river's surface at `p` seen along `rd`: the sky and the canyon reflected (Schlick's Fresnel on a still surface
// stirred by fine ripples), over a dark body of water.
// The river (ADR-1854): moving water, not a mirror.
//   * Ripples and long flow streaks advected downstream along the baked flow direction, stretched along it, at about
//     3 m/s; they bend the reflection and break up its edges.
//   * Foam where the water is shallow against the banks, drawn out downstream; a few drifting flecks in the current.
//   * The reflection itself (the canyon and the sky, Schlick's Fresnel) over a dark body that warms in the shallows.
fn flowAt(xz: vec2<f32>) -> vec4<f32> {
    let uv = (xz - au.world.xy) / au.world.z;
    return textureSampleLevel(flowTex, maskSampler, uv, 0.0);
}

fn shadeWater(p: vec3<f32>, rd: vec3<f32>, t: f32) -> vec3<f32> {
    // ADR-1855: glassy, deep water. The motion is a few broad, slow, low swells drifting downstream along the baked flow
    // direction, so the reflected cliffs wobble gently and stay readable; no fine chop, no flecks (they smeared the
    // reflection and shimmered).
    let fl = flowAt(p.xz);
    var dir = fl.xy * 2.0 - 1.0;
    dir = select(vec2<f32>(1.0, 0.0), normalize(dir), dot(dir, dir) > 0.01);
    let across = vec2<f32>(-dir.y, dir.x);
    let time = au.heights.w;
    let u = dot(p.xz, dir) - 1.6 * time; // drifting downstream
    let v = dot(p.xz, across);
    let e = 0.5;
    let f1 = vec2<f32>(u / 22.0, v / 9.0);
    let f2 = vec2<f32>(u / 9.0, v / 6.0) + vec2<f32>(3.7, 1.9);
    let w2 = 0.0; // no second octave: it drew moire contours in the reflection
    let hu = (vnoise(f1 + vec2<f32>(e, 0.0)) - vnoise(f1 - vec2<f32>(e, 0.0))) / 22.0 + w2 * (vnoise(f2 + vec2<f32>(e, 0.0)) - vnoise(f2 - vec2<f32>(e, 0.0))) / 9.0;
    let hv = (vnoise(f1 + vec2<f32>(0.0, e)) - vnoise(f1 - vec2<f32>(0.0, e))) / 9.0 + w2 * (vnoise(f2 + vec2<f32>(0.0, e)) - vnoise(f2 - vec2<f32>(0.0, e))) / 6.0;
    // low amplitude: the reflection stays a mirror that wobbles, not a broken surface
    let slope = (dir * hu + across * hv) * 0.35;
    let n = normalize(vec3<f32>(-slope.x, 1.0, -slope.y));
    var r = reflect(rd, n);
    r.y = max(r.y, 0.002);
    let cosT = clamp(-dot(rd, n), 0.0, 1.0);
    let fres = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);
    let ro2 = p + vec3<f32>(0.0, 0.05, 0.0);
    let t2 = march(ro2, r, 6000.0);
    var refl: vec3<f32>;
    if (t2 > 0.0) {
        let p2 = ro2 + r * t2;
        refl = applyAir(shadeReflected(p2, t + t2), ro2, r, t2);
    } else {
        refl = skyColour(r);
    }
    let body = mix(pal(P_ROCK), pal(P_AMBIENT), 0.35) * 0.18 * au.look2.x;
    var col = mix(body, refl, fres);
    // a light, sparse touch at the banks: a thin pale line where the water meets the rock, broken along the current
    let depth = kWaterY - heightBase(p.xz);
    let edge = (1.0 - smoothstep(0.03, 0.35, depth)) * smoothstep(0.55, 0.8, vnoise(vec2<f32>(u / 14.0, v / 3.0)));
    let lit = pal(P_AMBIENT) * 0.5 * au.look2.x + pal(P_SUN) * 1.2 * au.sun.w * max(au.sun.y, 0.0);
    col = mix(col, pal(P_PLAYA) * lit, edge * 0.3 * fl.a);
    return col;
}

struct WorldOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};

@fragment
fn fs_world(in: VsOut) -> WorldOut {
    let uv = in.clip.xy * frame.targetSize.zw;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let ro = frame.cameraPos.xyz;
    // ADR-1855: the ray from the camera basis, not from invViewProj (float32-ill-conditioned at this near/far ratio)
    let rd = normalize(au.rayForward.xyz + ndc.x * au.rayRight.xyz + ndc.y * au.rayUp.xyz);

    var out: WorldOut;
    let tmax = au.world.z * 1.6;
    let t = march(ro, rd, tmax);
    // the river: the ray meets the water plane before it meets the land (the land is above the plane everywhere
    // but in the gorge's channels)
    var tw = -1.0;
    if (rd.y < 0.0 && ro.y > kWaterY) {
        tw = (ro.y - kWaterY) / -rd.y;
    }
    var col: vec3<f32>;
    if (tw > 0.0 && (t < 0.0 || tw < t) && insideWorld((ro + rd * tw).xz)) {
        let p = ro + rd * tw;
        col = applyAir(shadeWater(p, rd, tw), ro, rd, tw);
        let clip = frame.viewProj * vec4<f32>(p, 1.0);
        out.depth = clamp(clip.z / clip.w, 0.0, 0.999999);
    } else if (t > 0.0) {
        let p = ro + rd * t;
        col = shade(p, rd, t);
        col = applyAir(col, ro, rd, t);
        let clip = frame.viewProj * vec4<f32>(p, 1.0);
        out.depth = clamp(clip.z / clip.w, 0.0, 0.999999);
    } else {
        col = skyColour(rd);
        out.depth = 1.0;
    }
    out.color = vec4<f32>(col * au.look.z, 1.0);
    return out;
}
