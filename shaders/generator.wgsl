// The "cells" generator (ADR-1117): one thread per cell of this frame's window writes one
// InstanceRecord into the object's record buffer -- an element, or an empty cell as a zero-scale record
// the cull pass rejects. The records then go through the existing effector pass and the existing
// prefix-scan cull (ADR-029), so ordering is deterministic and no atomics are used.
//
// This file and scene/generator.cpp are one rule written twice; every operation below has its twin
// there, in the same order (`generatorElement`). Presence is integer-only, so the two agree on which
// cells hold an element; floats agree to the last bits.
//
// Bindings (group 0): 0 GeneratorParams (uniform), 1 records (read_write). Mirrors
// rendering/procedural_renderer.hpp GeneratorUniforms.

struct InstanceRecord {
    position: vec4<f32>,
    rotation: vec4<f32>,
    scale: vec4<f32>,
    random: vec4<f32>,
    color: vec4<f32>,
    emissive: vec4<f32>,
};

struct GeneratorParams {
    genToObject: mat4x4<f32>,   // distributionTransform
    rotation: vec4<f32>,        // its rotation (x, y, z, w)
    scaleInfo: vec4<f32>,       // its scale xyz
    window: vec4<i32>,          // origin x, origin z, count x, count z
    region: vec4<i32>,          // bounded: min x, min z, max x, max z (inclusive cells)
    hashing: vec4<u32>,         // seed, presence (16-bit), cluster cells (0 = none), contrast (16-bit)
    flags: vec4<u32>,           // x = bounded, y = disc
    cell: vec4<f32>,            // cell size, jitter, size min, size max
    shape: vec4<f32>,           // tilt, value random, emissive random, emissive sparsity
    ground: vec4<f32>,          // height, amplitude, frequency, the ground seed (as f32 bits)
    disc: vec4<f32>,            // centre x, centre z, radius
};

@group(0) @binding(0) var<uniform> gen: GeneratorParams;
@group(0) @binding(1) var<storage, read_write> records: array<InstanceRecord>;

const GEN_TWO_PI: f32 = 6.283185307179586;

fn genMix(v: u32) -> u32 {
    var x = v;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}

fn genHash(ix: i32, iz: i32, seed: u32, channel: u32) -> u32 {
    let s = genMix(seed + channel * 0x9e3779b9u);
    let z = genMix((bitcast<u32>(iz) * 0xd8163841u) ^ s);
    return genMix((bitcast<u32>(ix) * 0x8da6b343u) ^ z);
}

fn genUnit(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn genFloorDiv(a: i32, b: i32) -> i32 {
    if (a < 0) {
        return (a - b + 1) / b;
    }
    return a / b;
}

fn genThreshold(ix: i32, iz: i32) -> u32 {
    let seed = gen.hashing.x;
    var fertility = 65535u;
    let cc = i32(gen.hashing.z);
    if (cc > 0) {
        let cx = genFloorDiv(ix, cc);
        let cz = genFloorDiv(iz, cc);
        let fx = u32(ix - cx * cc);
        let fz = u32(iz - cz * cc);
        let ucc = u32(cc);
        let c00 = genHash(cx, cz, seed, 7u) >> 16u;
        let c10 = genHash(cx + 1, cz, seed, 7u) >> 16u;
        let c01 = genHash(cx, cz + 1, seed, 7u) >> 16u;
        let c11 = genHash(cx + 1, cz + 1, seed, 7u) >> 16u;
        let a = (c00 * (ucc - fx) + c10 * fx) / ucc;
        let b = (c01 * (ucc - fx) + c11 * fx) / ucc;
        let v = (a * (ucc - fz) + b * fz) / ucc;
        fertility = 65535u - ((gen.hashing.w * (65535u - v)) >> 16u);
    }
    return gen.hashing.y * fertility;
}

fn genValueNoise(seed: u32, x: f32, z: f32) -> f32 {
    let xf = floor(x);
    let zf = floor(z);
    let ix = i32(xf);
    let iz = i32(zf);
    let fx = x - xf;
    let fz = z - zf;
    let sx = fx * fx * (3.0 - 2.0 * fx);
    let sz = fz * fz * (3.0 - 2.0 * fz);
    let c00 = genUnit(genHash(ix, iz, seed, 101u));
    let c10 = genUnit(genHash(ix + 1, iz, seed, 101u));
    let c01 = genUnit(genHash(ix, iz + 1, seed, 101u));
    let c11 = genUnit(genHash(ix + 1, iz + 1, seed, 101u));
    let a = c00 + (c10 - c00) * sx;
    let b = c01 + (c11 - c01) * sx;
    return a + (b - a) * sz;
}

fn genGround(x: f32, z: f32) -> f32 {
    if (gen.ground.y == 0.0) {
        return gen.ground.x;
    }
    let f = gen.ground.z;
    let seed = bitcast<u32>(gen.ground.w);
    let n = (genValueNoise(seed, x * f, z * f) + 0.5 * genValueNoise(seed, x * f * 2.0 + 17.3, z * f * 2.0 - 9.1)) / 1.5;
    return gen.ground.x + gen.ground.y * (n * 2.0 - 1.0);
}

fn genAxisAngle(axis: vec3<f32>, angle: f32) -> vec4<f32> {
    let h = angle * 0.5;
    return vec4<f32>(axis * sin(h), cos(h));
}

// Hamilton product a * b for (x, y, z, w) quaternions (glm's order of operations).
fn genQuatMul(a: vec4<f32>, b: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}

fn genPresent(ix: i32, iz: i32) -> bool {
    if (gen.flags.x != 0u) {
        if (ix < gen.region.x || ix > gen.region.z || iz < gen.region.y || iz > gen.region.w) {
            return false;
        }
        if (gen.flags.y != 0u) {
            let ccx = (f32(ix) + 0.5) * gen.cell.x;
            let ccz = (f32(iz) + 0.5) * gen.cell.x;
            let dx = ccx - gen.disc.x;
            let dz = ccz - gen.disc.y;
            if (dx * dx + dz * dz > gen.disc.z * gen.disc.z) {
                return false;
            }
        }
    }
    return genHash(ix, iz, gen.hashing.x, 0u) < genThreshold(ix, iz);
}

@compute @workgroup_size(8, 8)
fn cs_generate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let countX = u32(gen.window.z);
    let countZ = u32(gen.window.w);
    if (gid.x >= countX || gid.y >= countZ) {
        return;
    }
    let index = gid.y * countX + gid.x;
    let ix = gen.window.x + i32(gid.x);
    let iz = gen.window.y + i32(gid.y);
    var r: InstanceRecord;
    if (!genPresent(ix, iz)) {
        // An empty cell: a zero-scale record the cull pass rejects (cull.wgsl).
        r.position = vec4<f32>(0.0, 0.0, 0.0, 0.0);
        r.rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        r.scale = vec4<f32>(0.0);
        r.random = vec4<f32>(0.0);
        r.color = vec4<f32>(0.0);
        r.emissive = vec4<f32>(0.0);
        records[index] = r;
        return;
    }
    let seed = gen.hashing.x;
    let u0 = genUnit(genHash(ix, iz, seed, 1u));
    let u1 = genUnit(genHash(ix, iz, seed, 2u));
    let x = (f32(ix) + 0.5 + (u0 - 0.5) * gen.cell.y) * gen.cell.x;
    let z = (f32(iz) + 0.5 + (u1 - 0.5) * gen.cell.y) * gen.cell.x;
    let p = vec3<f32>(x, genGround(x, z), z);
    let size = gen.cell.z + (gen.cell.w - gen.cell.z) * genUnit(genHash(ix, iz, seed, 3u));
    let yaw = genUnit(genHash(ix, iz, seed, 4u)) * GEN_TWO_PI;
    let lean = genUnit(genHash(ix, iz, seed, 5u)) * gen.shape.x;
    let leanDir = genUnit(genHash(ix, iz, seed, 6u)) * GEN_TWO_PI;
    let qYaw = genAxisAngle(vec3<f32>(0.0, 1.0, 0.0), yaw);
    let qLean = genAxisAngle(vec3<f32>(cos(leanDir), 0.0, sin(leanDir)), lean);
    let local = genQuatMul(qLean, qYaw);
    let value = 1.0 + gen.shape.y * (genUnit(genHash(ix, iz, seed, 12u)) * 2.0 - 1.0);
    var emission = 1.0 + gen.shape.z * (genUnit(genHash(ix, iz, seed, 14u)) * 2.0 - 1.0);
    if (genUnit(genHash(ix, iz, seed, 13u)) < gen.shape.w) {
        emission = 0.0;
    }
    r.position = vec4<f32>((gen.genToObject * vec4<f32>(p, 1.0)).xyz, 1.0);
    r.rotation = genQuatMul(gen.rotation, local);
    r.scale = vec4<f32>(gen.scaleInfo.xyz * size, 0.0);
    r.random = vec4<f32>(genUnit(genHash(ix, iz, seed, 8u)), genUnit(genHash(ix, iz, seed, 9u)),
                         genUnit(genHash(ix, iz, seed, 10u)), genUnit(genHash(ix, iz, seed, 11u)));
    r.color = vec4<f32>(value, value, value, 0.0);
    r.emissive = vec4<f32>(emission, emission, emission, 0.0);
    records[index] = r;
}

// ---- ADR-1950: the "tiles" kernel ------------------------------------------------------------------
// Musical Geography's ground. Every cell holds its tile, at the cell centre, unrotated, at the one surface's height
// (scene/tile_terrain.cpp `tileSample`, operation for operation). A coarser ring's cell stands at the mean of the
// fine tiles it covers (ADR-1976), and a ring leaves the finer ring's window empty (the hole, in this ring's cells).
// The record's random lanes carry the material channels (water, shore, flat, path); the colour's alpha carries the
// tile's lattice hash (request 1978), so a program varies a tile by its world identity.

struct TileParams {
    a: vec4<f32>,          // continent wavelength, land threshold, shore band, inland band
    b: vec4<f32>,          // sea level, sea depth, shore rise, hill wavelength
    c: vec4<f32>,          // hill height, octaves, hill warp, warp wavelength
    d: vec4<f32>,          // hill low, hill high, crown threshold, crown flatten
    e: vec4<f32>,          // region wavelength, region variation, path width, path lift
    info: vec4<u32>,       // seed, path points, children per side, hole valid
    hole: vec4<i32>,       // min x, min z, max x, max z (inclusive, this ring's cells)
    blend: vec4<i32>,      // seam band cells (0 = off), block ratio (ADR-1976 amended)
    path: array<vec4<f32>, 8>, // two points per lane: (x0, z0, x1, z1)
};

@group(0) @binding(2) var<uniform> tile: TileParams;

const TILE_REGION_SEED: u32 = 0xA5A5A5A5u;
const TILE_CONTINENT_SEED: u32 = 0x00C0FFEEu;
const TILE_WARPX_SEED: u32 = 0x01234567u;
const TILE_WARPZ_SEED: u32 = 0x07654321u;
const TILE_HILL_SEED: u32 = 0x0000BEEFu;

fn tileSmooth(e0: f32, e1: f32, x: f32) -> f32 {
    let t = clamp((x - e0) / max(e1 - e0, 1e-6), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

fn tileValueNoise(seed: u32, x: f32, z: f32) -> f32 {
    let xf = floor(x);
    let zf = floor(z);
    let ix = i32(xf);
    let iz = i32(zf);
    let fx = x - xf;
    let fz = z - zf;
    let sx = fx * fx * (3.0 - 2.0 * fx);
    let sz = fz * fz * (3.0 - 2.0 * fz);
    let c00 = genUnit(genHash(ix, iz, seed, 101u));
    let c10 = genUnit(genHash(ix + 1, iz, seed, 101u));
    let c01 = genUnit(genHash(ix, iz + 1, seed, 101u));
    let c11 = genUnit(genHash(ix + 1, iz + 1, seed, 101u));
    let a = c00 + (c10 - c00) * sx;
    let b = c01 + (c11 - c01) * sx;
    return a + (b - a) * sz;
}

fn tileFbm(seed: u32, x: f32, z: f32, octaves: i32) -> f32 {
    var sum = 0.0;
    var norm = 0.0;
    var amp = 1.0;
    var freq = 1.0;
    let n = clamp(octaves, 1, 6);
    for (var o = 0; o < n; o = o + 1) {
        let uo = u32(o);
        let fo = f32(o);
        sum = sum + amp * tileValueNoise(seed + uo * 1013u, x * freq + fo * 17.31, z * freq - fo * 9.17);
        norm = norm + amp;
        amp = amp * 0.5;
        freq = freq * 2.0;
    }
    return sum / norm;
}

fn tilePathPoint(i: u32) -> vec2<f32> {
    let lane = tile.path[i / 2u];
    return select(lane.xy, lane.zw, (i & 1u) == 1u);
}

fn tilePathDistance(x: f32, z: f32) -> f32 {
    var best = 1e9;
    let n = min(tile.info.y, 16u);
    if (n == 1u) {
        return length(vec2<f32>(x, z) - tilePathPoint(0u));
    }
    for (var i = 0u; i + 1u < n; i = i + 1u) {
        let a = tilePathPoint(i);
        let ab = tilePathPoint(i + 1u) - a;
        let ap = vec2<f32>(x, z) - a;
        let t = clamp(dot(ap, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        best = min(best, length(ap - ab * t));
    }
    return best;
}

// (height, water, shore, flat) and the path in `pathOut`.
struct TileOut {
    height: f32,
    water: f32,
    shore: f32,
    flat: f32,
    path: f32,
};

fn tileSample(x: f32, z: f32) -> TileOut {
    let seed = tile.info.x;
    let region = tileValueNoise(seed ^ TILE_REGION_SEED, x / tile.e.x, z / tile.e.x);
    let k = 1.0 + tile.e.y * (2.0 * region - 1.0);
    let c = tileFbm(seed ^ TILE_CONTINENT_SEED, x / tile.a.x, z / tile.a.x, 3);
    let land = tileSmooth(tile.a.y - tile.a.z, tile.a.y + tile.a.z, c);
    let inland = tileSmooth(tile.a.y, tile.a.y + tile.a.w, c);
    let wx = (tileFbm(seed ^ TILE_WARPX_SEED, x / tile.c.w, z / tile.c.w, 2) * 2.0 - 1.0) * tile.c.z;
    let wz = (tileFbm(seed ^ TILE_WARPZ_SEED, x / tile.c.w + 31.7, z / tile.c.w - 11.3, 2) * 2.0 - 1.0) * tile.c.z;
    let hwl = tile.b.w * k;
    let hn = tileFbm(seed ^ TILE_HILL_SEED, (x + wx) / hwl, (z + wz) / hwl, i32(tile.c.y + 0.5));
    let shaped = tileSmooth(tile.d.x, tile.d.y, hn);
    let excess = max(shaped - tile.d.z, 0.0);
    let hill = shaped - excess * tile.d.w;
    let landHeight = tile.b.z + tile.c.x * k * hill * inland;
    var out: TileOut;
    out.height = (tile.b.x - tile.b.y) + ((tile.b.x + landHeight) - (tile.b.x - tile.b.y)) * land;
    out.water = 1.0 - land;
    out.shore = 1.0 - tileSmooth(0.0, 3.0 * tile.a.z, abs(c - tile.a.y));
    out.flat = tileSmooth(tile.d.z, tile.d.z + 0.06, shaped) * tile.d.w * inland * land;
    out.path = 0.0;
    if (tile.info.y > 0u) {
        let d = tilePathDistance(x, z);
        let p = (1.0 - tileSmooth(0.5 * tile.e.z, 0.5 * tile.e.z + 0.75, d)) * land;
        out.height = out.height + tile.e.w * p;
        out.path = p;
    }
    return out;
}

// One cell of this ring: the tile at its centre, or the mean of the fine tiles it covers (generatorTile's twin).
fn tileCell(ix: i32, iz: i32) -> TileOut {
    let cellSize = gen.cell.x;
    let m = max(tile.info.z, 1u);
    if (m == 1u) {
        return tileSample((f32(ix) + 0.5) * cellSize, (f32(iz) + 0.5) * cellSize);
    }
    let fine = cellSize / f32(m);
    var t: TileOut;
    t.height = 0.0;
    t.water = 0.0;
    t.shore = 0.0;
    t.flat = 0.0;
    t.path = 0.0;
    for (var b = 0u; b < m; b = b + 1u) {
        for (var a = 0u; a < m; a = a + 1u) {
            let s = tileSample(f32(ix) * cellSize + (f32(a) + 0.5) * fine, f32(iz) * cellSize + (f32(b) + 0.5) * fine);
            t.height = t.height + s.height;
            t.water = t.water + s.water;
            t.shore = t.shore + s.shore;
            t.flat = t.flat + s.flat;
            t.path = t.path + s.path;
        }
    }
    let inv = 1.0 / f32(m * m);
    t.height = t.height * inv;
    t.water = t.water * inv;
    t.shore = t.shore * inv;
    t.flat = t.flat * inv;
    t.path = t.path * inv;
    return t;
}

@compute @workgroup_size(8, 8)
fn cs_generate_tiles(@builtin(global_invocation_id) gid: vec3<u32>) {
    let countX = u32(gen.window.z);
    let countZ = u32(gen.window.w);
    if (gid.x >= countX || gid.y >= countZ) {
        return;
    }
    let index = gid.y * countX + gid.x;
    let ix = gen.window.x + i32(gid.x);
    let iz = gen.window.y + i32(gid.y);
    var r: InstanceRecord;
    if (tile.info.w != 0u && ix >= tile.hole.x && ix <= tile.hole.z && iz >= tile.hole.y && iz <= tile.hole.w) {
        r.position = vec4<f32>(0.0, 0.0, 0.0, 0.0);
        r.rotation = vec4<f32>(0.0, 0.0, 0.0, 1.0);
        r.scale = vec4<f32>(0.0);
        r.random = vec4<f32>(0.0);
        r.color = vec4<f32>(0.0);
        r.emissive = vec4<f32>(0.0);
        records[index] = r;
        return;
    }
    let cellSize = gen.cell.x;
    var t = tileCell(ix, iz);
    // ADR-1976 (amended): the seam band -- near this ring's window edge, ease toward the coarser block's mean.
    let band = tile.blend.x;
    let ratio = tile.blend.y;
    if (band > 0 && ratio >= 2) {
        let d = min(min(ix - gen.window.x, gen.window.x + gen.window.z - 1 - ix),
                    min(iz - gen.window.y, gen.window.y + gen.window.w - 1 - iz));
        let u = clamp(f32(d) / f32(band), 0.0, 1.0);
        let w = 1.0 - u * u * (3.0 - 2.0 * u);
        if (w > 0.0) {
            let bx = genFloorDiv(ix, ratio) * ratio;
            let bz = genFloorDiv(iz, ratio) * ratio;
            var mh = 0.0;
            var mw = 0.0;
            var ms = 0.0;
            var mf = 0.0;
            var mp = 0.0;
            for (var b = 0; b < ratio; b = b + 1) {
                for (var a = 0; a < ratio; a = a + 1) {
                    let c = tileCell(bx + a, bz + b);
                    mh = mh + c.height;
                    mw = mw + c.water;
                    ms = ms + c.shore;
                    mf = mf + c.flat;
                    mp = mp + c.path;
                }
            }
            let inv = 1.0 / f32(ratio * ratio);
            t.height = t.height + (mh * inv - t.height) * w;
            t.water = t.water + (mw * inv - t.water) * w;
            t.shore = t.shore + (ms * inv - t.shore) * w;
            t.flat = t.flat + (mf * inv - t.flat) * w;
            t.path = t.path + (mp * inv - t.path) * w;
        }
    }
    let p = vec3<f32>((f32(ix) + 0.5) * cellSize, t.height, (f32(iz) + 0.5) * cellSize);
    r.position = vec4<f32>((gen.genToObject * vec4<f32>(p, 1.0)).xyz, 1.0);
    r.rotation = gen.rotation;
    r.scale = vec4<f32>(gen.scaleInfo.xyz, 0.0);
    r.random = vec4<f32>(t.water, t.shore, t.flat, t.path);
    r.color = vec4<f32>(1.0, 1.0, 1.0, genUnit(genHash(ix, iz, gen.hashing.x, 21u)));
    r.emissive = vec4<f32>(1.0, 1.0, 1.0, 0.0);
    records[index] = r;
}
