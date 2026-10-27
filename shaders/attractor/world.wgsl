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
};

@group(0) @binding(1) var<uniform> au: AttractorUniforms;
@group(0) @binding(2) var heightTex: texture_2d<f32>;
@group(0) @binding(3) var maskTex: texture_2d<f32>;
@group(0) @binding(4) var maskSampler: sampler;

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

fn heightBase(xz: vec2<f32>) -> f32 {
    let n = i32(au.world.w);
    let t = clamp(texCoord(xz), vec2<f32>(0.0), vec2<f32>(f32(n - 1)));
    let i0 = min(vec2<i32>(t), vec2<i32>(n - 2));
    let f = t - vec2<f32>(i0);
    let a = textureLoad(heightTex, i0, 0).r;
    let b = textureLoad(heightTex, i0 + vec2<i32>(1, 0), 0).r;
    let c = textureLoad(heightTex, i0 + vec2<i32>(0, 1), 0).r;
    let d = textureLoad(heightTex, i0 + vec2<i32>(1, 1), 0).r;
    // smoothstep-weighted bilinear: hides the texel grid on the faces without a 16-tap filter
    let s = f * f * (3.0 - 2.0 * f);
    let w = mix(f, s, 0.6);
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
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
    var p = xz / 9.0;
    var sum = 0.0;
    var amp = 1.0;
    for (var i = 0; i < 4; i = i + 1) {
        let n = 1.0 - abs(vnoise(p) * 2.0 - 1.0);
        sum = sum + n * n * amp;
        amp = amp * 0.45;
        p = kOct * p * 2.2;
    }
    return (sum - 0.45) * (0.6 + 2.6 * m.r) * fade * au.look.w;
}

// Fluting on the cliffs, in the cliff's own plane: vertical grooves along the face (a 2D mask can only smear them).
// `tangent` runs along the face horizontally; the grooves are long in y.
fn flutes(p: vec3<f32>, tangent: vec2<f32>) -> f32 {
    let u = dot(p.xz, tangent);
    let a = vnoise(vec2<f32>(u / 7.0, p.y / 55.0));
    let b = vnoise(vec2<f32>(u / 2.3, p.y / 14.0));
    return 0.65 * a + 0.35 * b; // 0..1
}

fn heightAt(xz: vec2<f32>, dist: f32) -> f32 {
    return heightBase(xz) + detailAt(xz, dist);
}

fn normalAt(xz: vec2<f32>, dist: f32) -> vec3<f32> {
    let e = max(au.heights.z * 0.35, dist * 0.0012);
    let hx = heightAt(xz + vec2<f32>(e, 0.0), dist) - heightAt(xz - vec2<f32>(e, 0.0), dist);
    let hz = heightAt(xz + vec2<f32>(0.0, e), dist) - heightAt(xz - vec2<f32>(0.0, e), dist);
    return normalize(vec3<f32>(-hx, 2.0 * e, -hz));
}

fn insideWorld(xz: vec2<f32>) -> bool {
    let lo = au.world.xy;
    let hi = au.world.xy + vec2<f32>(au.world.z);
    return all(xz >= lo) && all(xz <= hi);
}

// Returns the hit distance, or -1.
fn march(ro: vec3<f32>, rd: vec3<f32>, tmax: f32) -> f32 {
    let top = au.heights.y + 6.0;
    var t = 0.5;
    if (ro.y > top) {
        if (rd.y >= 0.0) {
            return -1.0;
        }
        t = (ro.y - top) / -rd.y;
    }
    var tPrev = t;
    var dPrev = 1e9;
    for (var i = 0; i < 400; i = i + 1) {
        let p = ro + rd * t;
        if (t > tmax || (p.y > top && rd.y > 0.0) || !insideWorld(p.xz)) {
            return -1.0;
        }
        let d = p.y - heightAt(p.xz, t);
        if (d < 0.0) {
            // overshot: bisect back to the surface
            var a = tPrev;
            var b = t;
            for (var k = 0; k < 7; k = k + 1) {
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
        dPrev = d;
        t = t + max(d * 0.38, 0.0015 * t + 0.05);
    }
    return -1.0;
}

fn softShadow(p: vec3<f32>, l: vec3<f32>) -> f32 {
    var res = 1.0;
    var t = 3.0;
    let top = au.heights.y + 6.0;
    for (var i = 0; i < 56; i = i + 1) {
        let q = p + l * t;
        if (q.y > top || !insideWorld(q.xz)) {
            break;
        }
        let d = q.y - heightBase(q.xz);
        res = min(res, 10.0 * d / t);
        if (res < 0.0) {
            break;
        }
        t = t + clamp(d * 0.5, 2.0 + t * 0.02, 160.0);
    }
    res = clamp(res, 0.0, 1.0);
    return res * res * (3.0 - 2.0 * res);
}

// ---- strata ------------------------------------------------------------------------------------------------------

fn strataCoord(p: vec3<f32>) -> f32 {
    let tilt = vec2<f32>(cos(au.strata.x), sin(au.strata.x));
    let nseed = bitcast<u32>(au.look2.z);
    let warp = (10.0 + 60.0 * au.view.w) * fbm(p.xz / 1300.0, 3, nseed + 3u);
    let fd = vec2<f32>(cos(au.fold.x), sin(au.fold.x));
    let fold = au.fold.z * sin(dot(p.xz, fd) * (6.28318530718 / max(au.fold.y, 1.0)));
    return p.y + au.strata.y * dot(p.xz, tilt) + warp + fold;
}

fn pal(i: u32) -> vec3<f32> {
    return au.pal[i].rgb;
}

// The colour of a stratum, in three scales (02-plan.md §strata):
//   * a FORMATION, five or so terrace bands thick, picks one strata colour: rust and coral low in the stack,
//     sandstone in the middle, a pale bone caprock high up, with the seed's hash shuffling neighbours;
//   * each BAND (one terrace step) shifts that colour's value a little, with a soft darker bedding plane on top;
//   * fine LAMINATIONS inside a band, barely there.
fn strataColour(s: f32, xz: vec2<f32>) -> vec3<f32> {
    let seed = bitcast<u32>(au.look2.z);
    let band = au.strata.z;
    // formations: irregular thickness from a slow wobble of the coordinate
    let fs = (s + 0.6 * band * sin(s / (band * 3.1)) ) / (band * 4.5);
    let fi = floor(fs);
    let ff = fs - fi;
    let h = hash01(i32(fi), 3, seed);
    let level = clamp(fi * band * 4.5 / max(au.heights.y, 1.0), 0.0, 1.0); // 0 low in the stack .. 1 high
    var idx = 0u;                                   // sandstone
    let r = h + (0.5 - level) * 0.5;                // low formations lean rust/coral, high ones bone
    if (r > 0.62) { idx = 1u; }                     // rust
    if (r > 0.86) { idx = 3u; }                     // the accent (coral)
    if (r < 0.22) { idx = 2u; }                     // bone
    var c = pal(P_STRATA0 + idx);
    // a soft blend into the next formation over its lowest tenth
    let hn = hash01(i32(fi) - 1, 3, seed);
    var idxN = 0u;
    let rn = hn + (0.5 - level) * 0.5;
    if (rn > 0.62) { idxN = 1u; }
    if (rn > 0.86) { idxN = 3u; }
    if (rn < 0.22) { idxN = 2u; }
    c = mix(pal(P_STRATA0 + idxN), c, smoothstep(0.0, 0.12, ff));
    // bands: a value shift per terrace step and a darker bedding plane
    let t = s / band;
    let bi = floor(t);
    let bf = t - bi;
    c = c * (0.9 + 0.16 * hash01(i32(bi), 5, seed));
    c = c * mix(0.78, 1.0, smoothstep(0.0, 0.05, bf));
    // laminations
    let lam = 0.5 + 0.5 * sin((bf * 6.0 + hash01(i32(bi), 4, seed) * 6.0) * 6.2831);
    c = c * (0.95 + 0.05 * lam);
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
    let cav = clamp(0.35 + 1.3 * (m.b - 0.5) + 0.55, 0.25, 1.0);
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
    let t = march(ro, rd, tmax);
    var col: vec3<f32>;
    if (t > 0.0) {
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
