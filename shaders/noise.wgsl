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
