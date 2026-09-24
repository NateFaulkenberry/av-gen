// Shared hashing and noise (ADR-023/025): the WGSL transliteration of core/noise.hpp. Every
// function here is a pure function of its arguments and evaluates the same expressions in the
// same order as the CPU so results agree within float rounding (tests compare them).
//
//   pcg3d       Jarzynski & Olano hash
//   hash01      one uniform in [0, 1) from an integer cell + seed (per-axis seed mixing)
//   valueNoise  trilinear value noise with a smoothstep fade, in [0, 1)
//   fbm3        3-octave fBM of valueNoise, normalised to [0, 1)
//   fbm3Vec     three decorrelated channels (offsets 31.7 / 67.3), each in [-1, 1)
//   curlNoise   curl of the fbm3Vec potential by central differences (eps 0.01), divergence-free
//   voronoiF1   distance to the nearest jittered cell point (Worley F1)
//   worleyF1F2  F1, F2 (the second-nearest) and a hash of the nearest cell; F2 - F1 is the cell edges
//
// Included by procedural.wgsl (deformers) and fields.wgsl (field kinds); include this file only
// once per module (fields.wgsl already includes it).

fn pcg3d(vIn: vec3<u32>) -> vec3<u32> {
    var v = vIn * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> vec3<u32>(16u);
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

fn hash01(cell: vec3<i32>, seed: u32) -> f32 {
    let h = pcg3d(vec3<u32>(bitcast<u32>(cell.x) + seed * 7919u, bitcast<u32>(cell.y) + seed * 104729u,
                            bitcast<u32>(cell.z) + seed * 1299709u));
    return f32(h.x) * (1.0 / 4294967296.0);
}

// Value noise: trilinear (smoothstep-weighted) interpolation of hash01 at the 8 cell corners.
fn valueNoise(p: vec3<f32>, seed: u32) -> f32 {
    let c = floor(p);
    let f = p - c;
    let u = f * f * (3.0 - 2.0 * f);
    let ci = vec3<i32>(c);
    let n000 = hash01(ci, seed);
    let n100 = hash01(ci + vec3<i32>(1, 0, 0), seed);
    let n010 = hash01(ci + vec3<i32>(0, 1, 0), seed);
    let n110 = hash01(ci + vec3<i32>(1, 1, 0), seed);
    let n001 = hash01(ci + vec3<i32>(0, 0, 1), seed);
    let n101 = hash01(ci + vec3<i32>(1, 0, 1), seed);
    let n011 = hash01(ci + vec3<i32>(0, 1, 1), seed);
    let n111 = hash01(ci + vec3<i32>(1, 1, 1), seed);
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// Three-octave value fBM in [0, 1].
fn fbm3(p: vec3<f32>, seed: u32) -> f32 {
    return (0.5 * valueNoise(p, seed) + 0.25 * valueNoise(p * 2.03 + vec3<f32>(17.0), seed) +
            0.125 * valueNoise(p * 4.11 + vec3<f32>(31.0), seed)) / 0.875;
}

// Three decorrelated fBM channels, each in [-1, 1).
fn fbm3Vec(p: vec3<f32>, seed: u32) -> vec3<f32> {
    return vec3<f32>(fbm3(p, seed) * 2.0 - 1.0, fbm3(p + vec3<f32>(31.7), seed) * 2.0 - 1.0,
                     fbm3(p + vec3<f32>(67.3), seed) * 2.0 - 1.0);
}

// Curl of the fbm3Vec potential by central differences (Bridson 2007); epsilon 0.01 like the CPU.
fn curlNoise(p: vec3<f32>, seed: u32) -> vec3<f32> {
    let e = 0.01;
    let dx = vec3<f32>(e, 0.0, 0.0);
    let dy = vec3<f32>(0.0, e, 0.0);
    let dz = vec3<f32>(0.0, 0.0, e);
    let px1 = fbm3Vec(p + dx, seed);
    let px0 = fbm3Vec(p - dx, seed);
    let py1 = fbm3Vec(p + dy, seed);
    let py0 = fbm3Vec(p - dy, seed);
    let pz1 = fbm3Vec(p + dz, seed);
    let pz0 = fbm3Vec(p - dz, seed);
    let inv = 1.0 / (2.0 * e);
    // curl F = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
    return vec3<f32>((py1.z - py0.z) - (pz1.y - pz0.y), (pz1.x - pz0.x) - (px1.z - px0.z),
                     (px1.y - px0.y) - (py1.x - py0.x)) * inv;
}

// Worley F1: distance to the nearest of 27 jittered neighbouring cell points.
fn voronoiF1(p: vec3<f32>, seed: u32) -> f32 {
    let c = floor(p);
    let f = p - c;
    let ci = vec3<i32>(c);
    var best = 8.0;
    for (var z = -1; z <= 1; z = z + 1) {
        for (var y = -1; y <= 1; y = y + 1) {
            for (var x = -1; x <= 1; x = x + 1) {
                let cell = ci + vec3<i32>(x, y, z);
                let jitter = vec3<f32>(hash01(cell, seed), hash01(cell, seed + 1u), hash01(cell, seed + 2u));
                let d = vec3<f32>(f32(x), f32(y), f32(z)) + jitter - f;
                best = min(best, dot(d, d));
            }
        }
    }
    return sqrt(best);
}

// Worley F1 and F2 together, and a hash in [0, 1) of the nearest cell (Effect Library Wave 2: cell
// patterns and vein networks). The same lattice and jitter as `voronoiF1`, so x is exactly its F1; y
// is the distance to the second-nearest point, so y >= x everywhere and y - x falls to 0 on the
// boundary between two cells -- the edges a vein network or a cell wall is drawn on. The CPU twin
// is `world::worleyF1F2` (entity_fx.cpp). 27 cells, as F1: a jitter in [0, 1) keeps both nearest
// points inside the 3x3x3 neighbourhood of the cell the point is in.
fn worleyF1F2(p: vec3<f32>, seed: u32) -> vec3<f32> {
    let c = floor(p);
    let f = p - c;
    let ci = vec3<i32>(c);
    var best = 8.0;
    var second = 8.0;
    var nearest = 0.0;
    for (var z = -1; z <= 1; z = z + 1) {
        for (var y = -1; y <= 1; y = y + 1) {
            for (var x = -1; x <= 1; x = x + 1) {
                let cell = ci + vec3<i32>(x, y, z);
                let jitter = vec3<f32>(hash01(cell, seed), hash01(cell, seed + 1u), hash01(cell, seed + 2u));
                let d = vec3<f32>(f32(x), f32(y), f32(z)) + jitter - f;
                let dd = dot(d, d);
                if (dd < best) {
                    second = best;
                    best = dd;
                    nearest = hash01(cell, seed + 3u);
                } else if (dd < second) {
                    second = dd;
                }
            }
        }
    }
    return vec3<f32>(sqrt(best), sqrt(second), nearest);
}

// ---- Effect Library (DF): an animated, divergence-free flow, cheap enough for a screen-space field --
//
// `curlNoise` above is the reference curl and costs six fBM vectors -- 432 hashes a call -- which is
// a compute-pass price, not a per-fragment one over a quarter of the screen. The two functions below
// are the per-fragment form: the value noise's gradient taken ANALYTICALLY (no finite differences),
// and the flow built as the cross product of two such gradients, which is divergence-free by
// identity (div(grad a x grad b) = 0), so it swirls rather than sources or sinks -- 32 hashes a call.
// Additive: nothing above changed, and nothing above calls these.

// Value noise (as `valueNoise`, same lattice and fade) with its analytic gradient: xyz = d/dp, w = value.
fn valueNoiseGrad(p: vec3<f32>, seed: u32) -> vec4<f32> {
    let c = floor(p);
    let f = p - c;
    let u = f * f * (3.0 - 2.0 * f);
    let du = 6.0 * f * (1.0 - f);
    let ci = vec3<i32>(c);
    let n000 = hash01(ci, seed);
    let n100 = hash01(ci + vec3<i32>(1, 0, 0), seed);
    let n010 = hash01(ci + vec3<i32>(0, 1, 0), seed);
    let n110 = hash01(ci + vec3<i32>(1, 1, 0), seed);
    let n001 = hash01(ci + vec3<i32>(0, 0, 1), seed);
    let n101 = hash01(ci + vec3<i32>(1, 0, 1), seed);
    let n011 = hash01(ci + vec3<i32>(0, 1, 1), seed);
    let n111 = hash01(ci + vec3<i32>(1, 1, 1), seed);
    let k1 = n100 - n000;
    let k2 = n010 - n000;
    let k3 = n001 - n000;
    let k4 = n000 - n100 - n010 + n110;
    let k5 = n000 - n010 - n001 + n011;
    let k6 = n000 - n100 - n001 + n101;
    let k7 = -n000 + n100 + n010 - n110 + n001 - n101 - n011 + n111;
    let value = n000 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x +
                k7 * u.x * u.y * u.z;
    let grad = du * vec3<f32>(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                              k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                              k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
    return vec4<f32>(grad, value);
}

// A divergence-free flow at `p`, animated by `t`: grad(a) x grad(b) for two decorrelated two-octave
// value noises. The two potentials slide in DIFFERENT directions as `t` advances, so the field
// evolves in place instead of translating as a whole -- temporally coherent (a smooth function of t)
// without the crawl of a single scrolled texture. Magnitude is O(1); callers scale it.
fn flowCurl(p: vec3<f32>, t: f32, seed: u32) -> vec3<f32> {
    let da = vec3<f32>(0.31, 0.17, -0.23) * t;
    let db = vec3<f32>(-0.19, 0.27, 0.13) * t;
    let ga = valueNoiseGrad(p + da, seed).xyz + 0.5 * valueNoiseGrad(p * 2.03 + vec3<f32>(17.0) - da, seed).xyz;
    let gb = valueNoiseGrad(p + vec3<f32>(31.7) + db, seed + 1u).xyz +
             0.5 * valueNoiseGrad(p * 2.03 + vec3<f32>(47.3) - db, seed + 1u).xyz;
    return cross(ga, gb);
}
