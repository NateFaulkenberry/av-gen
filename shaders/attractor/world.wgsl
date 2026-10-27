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
    tubeMin: vec4<f32>,   // ADR-1852: xyz min corner, w volume resolution (0 = no ribbon)
    tubeMax: vec4<f32>,   // xyz max corner, w segment count
    formation: vec4<f32>, // x landmark count, y ribbon swell, z the formations' top (y), w 0
    landmarks: array<vec4<f32>, 12>,
};

@group(0) @binding(1) var<uniform> au: AttractorUniforms;
@group(0) @binding(2) var heightTex: texture_2d<f32>;
@group(0) @binding(3) var maskTex: texture_2d<f32>;
@group(0) @binding(4) var maskSampler: sampler;

#include "attractor/formations.wgsl"

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
    // zigzagged at texel spacing. The shading normal now comes from `gradientSmooth`, which hides the grid itself.)
    let v = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    let gx = mix(b.x - a.x, d.x - c.x, f.y) / au.heights.z;
    let gz = mix(c.x - a.x, d.x - b.x, f.x) / au.heights.z;
    return vec4<f32>(v.x, v.y, gx, gz);
}

// The strata's band profile (scene/attractor_world.cpp `bandProfile`, line for line).
fn bandProfile(s: f32) -> f32 {
    let band = au.strata.z;
    let t = s / band;
    let i = floor(t);
    let f = t - i;
    let hard = smoothstep(0.35, 0.75, bandHardness(i));
    let riser = 0.2;
    let gg = clamp((f - (1.0 - riser)) / riser, 0.0, 1.0);
    let st = gg * gg * (3.0 - 2.0 * gg);
    return (i + f + (st - f) * hard) * band;
}

// The bands cut again on the steep faces, at sub-texel scale: a face spans less than a texel horizontally, so the
// generator's terraces cannot show there, and the colour bands would read as a wash over smooth rock. Applying the
// band profile to the bilinear surface steps the face into ledges and risers the march sees (silhouettes included).
fn bandCut(fd: vec4<f32>, dist: f32) -> f32 {
    let slope = length(fd.zw);
    let amt = au.look2.w * smoothstep(0.8, 1.8, slope) * (1.0 - smoothstep(2500.0, 6000.0, dist));
    if (amt <= 0.0) {
        return 0.0;
    }
    let s0 = fd.x + fd.y;
    return (bandProfile(s0) - s0) * amt;
}

fn heightBase(xz: vec2<f32>) -> f32 {
    return fieldAt(xz).x;
}

// The height field's gradient from a uniform cubic B-spline over 4x4 texels (C2). The march uses the cheap bilinear
// surface; the shading normal must not: a bilinear patch's contours turn at every texel edge, so on a steep face each
// texel column faces a slightly different way and the cliff shades as vertical curtains one texel wide. The B-spline's
// gradient is continuous across texels, so the face turns smoothly. Returns (dh/dx, dh/dz).
fn bsplineW(f: f32) -> vec4<f32> {
    let g = 1.0 - f;
    return vec4<f32>(g * g * g, 3.0 * f * f * f - 6.0 * f * f + 4.0, -3.0 * f * f * f + 3.0 * f * f + 3.0 * f + 1.0, f * f * f) / 6.0;
}
fn bsplineD(f: f32) -> vec4<f32> {
    let g = 1.0 - f;
    return vec4<f32>(-g * g, 3.0 * f * f - 4.0 * f, -3.0 * f * f + 2.0 * f + 1.0, f * f) * 0.5;
}
fn gradientSmooth(xz: vec2<f32>) -> vec2<f32> {
    let n = i32(au.world.w);
    let t = texCoord(xz);
    let i = vec2<i32>(floor(t));
    let f = t - floor(t);
    let wx = bsplineW(f.x);
    let wy = bsplineW(f.y);
    let dx = bsplineD(f.x);
    let dy = bsplineD(f.y);
    var g = vec2<f32>(0.0);
    for (var y = 0; y < 4; y = y + 1) {
        for (var x = 0; x < 4; x = x + 1) {
            let c = clamp(i + vec2<i32>(x - 1, y - 1), vec2<i32>(0), vec2<i32>(n - 1));
            let h = textureLoad(heightTex, c, 0).r;
            g = g + h * vec2<f32>(dx[x] * wy[y], wx[x] * dy[y]);
        }
    }
    return g / au.heights.z;
}

fn masksAt(xz: vec2<f32>) -> vec4<f32> {
    let uv = (xz - au.world.xy) / au.world.z;
    return textureSampleLevel(maskTex, maskSampler, uv, 0.0);
}

// Near-field displacement: small broken rock relief, on rock only, faded out with distance.
fn detailAt(xz: vec2<f32>, dist: f32) -> f32 {
    let fade = 1.0 - smoothstep(600.0, 1800.0, dist);
    if (fade <= 0.0 || au.look.w <= 0.0) {
        return 0.0;
    }
    let m = masksAt(xz);
    var p = xz / 4.5;
    var sum = 0.0;
    var amp = 1.0;
    for (var i = 0; i < 4; i = i + 1) {
        let n = vnoise(p);
        sum = sum + n * amp;
        amp = amp * 0.45;
        p = kOct * p * 2.2;
    }
    return (sum - 0.9) * (0.25 + 0.7 * m.r) * fade * au.look.w;
}

// Fluting on the cliffs, in the cliff's own plane: vertical grooves along the face (a 2D mask can only smear them).
// `tangent` runs along the face horizontally; the grooves are long in y.
fn flutes(p: vec3<f32>, tangent: vec2<f32>) -> f32 {
    // each band weathers on its own: the pattern restarts at every band, so no groove runs the whole face
    let bi = floor(strataCoord(p) / au.strata.z);
    let u = dot(p.xz, tangent) + hash01(i32(bi), 9, 5u) * 97.0;
    let a = vnoise(vec2<f32>(u / 7.0, p.y / 55.0));
    let b = vnoise(vec2<f32>(u / 2.3, p.y / 14.0));
    return 0.65 * a + 0.35 * b; // 0..1
}

fn heightAt(xz: vec2<f32>, dist: f32) -> f32 {
    let fd = fieldAt(xz);
    return fd.x + bandCut(fd, dist) + detailAt(xz, dist);
}

fn normalAt(xz: vec2<f32>, dist: f32) -> vec3<f32> {
    // the baked field's smooth gradient, plus the near-field detail's (finite differences of the detail alone)
    let g = gradientSmooth(xz);
    let e = max(0.5, dist * 0.0012);
    let ex = vec2<f32>(e, 0.0);
    let ez = vec2<f32>(0.0, e);
    let dx = detailAt(xz + ex, dist) - detailAt(xz - ex, dist) + bandCut(fieldAt(xz + ex), dist) - bandCut(fieldAt(xz - ex), dist);
    let dz = detailAt(xz + ez, dist) - detailAt(xz - ez, dist) + bandCut(fieldAt(xz + ez), dist) - bandCut(fieldAt(xz - ez), dist);
    return normalize(vec3<f32>(-(g.x + dx / (2.0 * e)), 1.0, -(g.y + dz / (2.0 * e))));
}

fn insideWorld(xz: vec2<f32>) -> bool {
    let lo = au.world.xy;
    let hi = au.world.xy + vec2<f32>(au.world.z);
    return all(xz >= lo) && all(xz <= hi);
}

// The scene's distance at `p`: the terrain's vertical clearance, filleted (smooth-min) into the formations where they
// meet it. y = 1 when the formations are the nearer surface.
fn sceneDist(p: vec3<f32>, t: f32) -> vec2<f32> {
    let dh = p.y - heightAt(p.xz, t);
    let ds = sdFormations(p);
    let isForm = select(0.0, 1.0, ds < dh);
    if (ds < 80.0) {
        return vec2<f32>(smin(dh, ds, 22.0), isForm);
    }
    return vec2<f32>(min(dh, ds), isForm);
}

fn sceneTop() -> f32 {
    return max(au.heights.y, au.formation.z) + 6.0;
}

// Returns (hit distance or -1, 1 when the hit is on a formation).
fn march(ro: vec3<f32>, rd: vec3<f32>, tmax: f32) -> vec2<f32> {
    let top = sceneTop();
    var t = 0.5;
    if (ro.y > top) {
        if (rd.y >= 0.0) {
            return vec2<f32>(-1.0, 0.0);
        }
        t = (ro.y - top) / -rd.y;
    }
    var tPrev = t;
    for (var i = 0; i < 420; i = i + 1) {
        let p = ro + rd * t;
        if (t > tmax || (p.y > top && rd.y > 0.0) || !insideWorld(p.xz)) {
            return vec2<f32>(-1.0, 0.0);
        }
        let d = sceneDist(p, t);
        if (d.x < 0.0) {
            // overshot: bisect back to the surface
            var a = tPrev;
            var b = t;
            for (var k = 0; k < 8; k = k + 1) {
                let m = 0.5 * (a + b);
                if (sceneDist(ro + rd * m, m).x < 0.0) {
                    b = m;
                } else {
                    a = m;
                }
            }
            let tm = 0.5 * (a + b);
            return vec2<f32>(tm, sceneDist(ro + rd * tm, tm).y);
        }
        if (d.x < 0.0006 * t) {
            return vec2<f32>(t, d.y);
        }
        tPrev = t;
        // the terrain's clearance is vertical, so it under-steps by a safety factor; a formation's is a true distance
        t = t + max(d.x * select(0.38, 0.85, d.y > 0.5), 0.0015 * t + 0.05);
    }
    return vec2<f32>(-1.0, 0.0);
}

fn softShadow(p: vec3<f32>, l: vec3<f32>) -> f32 {
    var res = 1.0;
    var t = 3.0;
    let top = sceneTop();
    for (var i = 0; i < 64; i = i + 1) {
        let q = p + l * t;
        if (q.y > top || !insideWorld(q.xz)) {
            break;
        }
        let d = min(q.y - heightBase(q.xz), sdFormations(q));
        res = min(res, 10.0 * d / t);
        if (res < 0.0) {
            break;
        }
        t = t + clamp(d * 0.5, 2.0 + t * 0.02, 160.0);
    }
    res = clamp(res, 0.0, 1.0);
    return res * res * (3.0 - 2.0 * res);
}

// The normal of the full scene by central differences (the formations and their fillets).
fn sceneNormal(p: vec3<f32>, t: f32) -> vec3<f32> {
    let e = max(0.25, t * 0.0008);
    let k = vec2<f32>(1.0, -1.0);
    return normalize(k.xyy * sceneDist(p + k.xyy * e, t).x + k.yyx * sceneDist(p + k.yyx * e, t).x +
                     k.yxy * sceneDist(p + k.yxy * e, t).x + k.xxx * sceneDist(p + k.xxx * e, t).x);
}

// Ambient occlusion from the formations' distance (the terrain's comes from the baked cavity).
fn formationAO(p: vec3<f32>, n: vec3<f32>) -> f32 {
    var occ = 0.0;
    var w = 1.0;
    for (var i = 1; i <= 4; i = i + 1) {
        let h = 4.0 * f32(i);
        occ = occ + w * max(0.0, h - sdFormations(p + n * h)) / h;
        w = w * 0.6;
    }
    return clamp(1.0 - 0.45 * occ, 0.3, 1.0);
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
    let t = s / band;
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

fn shade(p: vec3<f32>, rd: vec3<f32>, t: f32, onFormation: bool) -> vec3<f32> {
    // a formation, or terrain within a fillet of one, takes the full scene's normal; open terrain the smooth one
    let nearForm = onFormation || sdFormations(p) < 30.0;
    let n0 = select(normalAt(p.xz, t), sceneNormal(p, t), nearForm);
    // bump the cliffs with flutes and the rest with grain, both fading with distance
    let near = 1.0 - smoothstep(300.0, 2500.0, t);
    let tangent = normalize(vec2<f32>(-n0.z, n0.x) + vec2<f32>(1e-4, 0.0));
    let steepN = smoothstep(0.3, 0.7, 1.0 - n0.y);
    let fe = 0.6;
    let df = flutes(p + vec3<f32>(tangent.x, 0.0, tangent.y) * fe, tangent) - flutes(p - vec3<f32>(tangent.x, 0.0, tangent.y) * fe, tangent);
    let tan3 = vec3<f32>(tangent.x, 0.0, tangent.y);
    var n = normalize(n0 - tan3 * df * 0.9 * steepN * near * au.look.w);
    let ge = 0.4;
    let gx = vnoise(p.xz / 1.7 + vec2<f32>(ge, 0.0)) - vnoise(p.xz / 1.7 - vec2<f32>(ge, 0.0));
    let gz = vnoise(p.xz / 1.7 + vec2<f32>(0.0, ge)) - vnoise(p.xz / 1.7 - vec2<f32>(0.0, ge));
    n = normalize(n - vec3<f32>(gx, 0.0, gz) * 0.35 * (1.0 - steepN) * near * au.look.w);
    var m = masksAt(p.xz);
    if (onFormation) {
        m = vec4<f32>(1.0, 0.0, 0.5, 0.5); // all rock, no playa, neutral cavity
    }
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
    // gully streaks: darker in the creases, paler on the flutes
    let fl = flutes(p, tangent);
    albedo = albedo * mix(1.0, 0.88 + 0.24 * fl, cliffAmt * mix(0.4, 1.0, near));
    // the playa: pale mineral with faint polygonal cracking
    let cracks = smoothstep(0.02, 0.0, abs(vnoise(p.xz / 7.0) - 0.5)) * 0.25;
    let playa = pal(P_PLAYA) * (0.95 - cracks + 0.08 * vnoise(p.xz / 140.0));
    albedo = mix(albedo, playa, m.g * (1.0 - rockAmt));

    // light
    let shadow = softShadow(p + n * 1.5, l);
    let dif = max(dot(n, l), 0.0) * shadow;
    let cavRaw = clamp(0.35 + 1.3 * (m.b - 0.5) + 0.55, 0.25, 1.0);
    var cav = mix(cavRaw, 0.85, smoothstep(0.25, 0.55, slope)); // a 2D mask means nothing on a face
    if (nearForm) {
        cav = cav * formationAO(p, n);
    }
    let skyAmb = (0.55 + 0.45 * n.y) * cav;
    let back = clamp(dot(n, normalize(vec3<f32>(-l.x, 0.0, -l.z))), 0.0, 1.0) * cav;
    let bounce = clamp(0.5 - 0.5 * n.y + 0.2, 0.0, 1.0) * cav;
    var light = pal(P_SUN) * 6.0 * au.sun.w * dif;
    light = light + pal(P_AMBIENT) * 0.32 * au.look2.x * skyAmb;
    light = light + pal(P_BELT) * 0.18 * au.look2.x * back;
    light = light + pal(P_BOUNCE) * 0.22 * au.look2.x * bounce * (0.4 + 0.6 * shadow);
    var col = albedo * light;
    // a soft sheen on the playa toward the sun
    let h = normalize(l - rd);
    col = col + pal(P_GLOW) * m.g * pow(max(dot(n, h), 0.0), 40.0) * 0.25 * shadow * au.sun.w;
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
    let farP = frame.invViewProj * vec4<f32>(ndc, 1.0, 1.0);
    let nearP = frame.invViewProj * vec4<f32>(ndc, 0.0, 1.0);
    let ro = frame.cameraPos.xyz;
    let rd = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    var out: WorldOut;
    let tmax = au.world.z * 1.6;
    let hit = march(ro, rd, tmax);
    let t = hit.x;
    var col: vec3<f32>;
    if (t > 0.0) {
        let p = ro + rd * t;
        col = shade(p, rd, t, hit.y > 0.5);
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
