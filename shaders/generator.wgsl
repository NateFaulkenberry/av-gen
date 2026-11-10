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
    blend: vec4<i32>,      // seam band cells (0 = off), block ratio (ADR-1976 amended), children at the lowest (ADR-1953)
    water0: vec4<f32>,     // ADR-1965: river count, pond count, valley count, height scale
    skirt: vec4<f32>,      // skirt depth, the box's thickness
    path: array<vec4<f32>, 8>, // two points per lane: (x0, z0, x1, z1)
    riverInfo: array<vec4<f32>, 4>,   // ADR-1965: first point (pool index), point count, width, valley width
    riverLevel: array<vec4<f32>, 4>,  // source level, mouth level, length, 0
    riverBounds: array<vec4<f32>, 4>, // min x, min z, max x, max z, widened by half the valley
    riverPts: array<vec4<f32>, 32>,   // the rivers' points, pooled, two per lane
    ponds: array<vec4<f32>, 8>,       // centre x, centre z, radius, basin width
    pondB: array<vec4<f32>, 8>,       // level, shore noise, 0, 0
    valleyInfo: array<vec4<f32>, 4>,  // first point, point count, valley width, depth
    valleyB: array<vec4<f32>, 4>,     // min x, min z, max x, max z (widened)
    valleyFrac: vec4<f32>,            // each valley's depth fraction (x..w)
    valleyPts: array<vec4<f32>, 16>,  // the valleys' points, pooled
    // ADR-1953: the moving ground (procedural_renderer.hpp GeneratorTileUniforms).
    dyn: vec4<f32>,        // t, amplitude, wave count, event count
    breath: vec4<f32>,     // amplitude, period, wavelength, 1 when anything moves
    roll: vec4<f32>,       // rolling x, z (m/s)
    ring0: vec4<f32>,      // amplitude, speed, wavelength, width
    ring1: vec4<f32>,      // decay, crest
    waterMotion: vec4<f32>, // ADR-1965: river waves, pond waves, river drift amplitude, drift wavelength
    waterMotion2: vec4<f32>, // drift speed
    waves: array<vec4<f32>, 12>,
    events: array<vec4<f32>, 16>,
    spring0: vec4<f32>,    // ADR-1954: depth, frequency, damping, ripple speed
    spring1: vec4<f32>,    // ripple radius, spring count, ADR-1960 glow radius
    springs: array<vec4<f32>, 256>, // x, z, time, strength (scene::kMaxTileSprings)
};

@group(0) @binding(2) var<uniform> tile: TileParams;

const TILE_REGION_SEED: u32 = 0xA5A5A5A5u;
const TILE_CONTINENT_SEED: u32 = 0x00C0FFEEu;
const TILE_WARPX_SEED: u32 = 0x01234567u;
const TILE_WARPZ_SEED: u32 = 0x07654321u;
const TILE_HILL_SEED: u32 = 0x0000BEEFu;
const TILE_BREATH_SEED: u32 = 0x00B4EA7Bu; // ADR-1953
const TILE_POND_SEED: u32 = 0x00B0D1E5u;   // ADR-1965
const TILE_TWO_PI: f32 = 6.28318530717958647692;

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

// ADR-1965: a pooled river or valley point.
fn tileRiverPt(i: u32) -> vec2<f32> {
    let lane = tile.riverPts[i / 2u];
    return select(lane.xy, lane.zw, (i & 1u) == 1u);
}

fn tileValleyPt(i: u32) -> vec2<f32> {
    let lane = tile.valleyPts[i / 2u];
    return select(lane.xy, lane.zw, (i & 1u) == 1u);
}

// The distance to a pooled polyline and the arc length to its nearest point (polylineNearest's twin).
fn tileNearest(x: f32, z: f32, first: u32, count: u32, valley: bool) -> vec2<f32> {
    var best = 1e9;
    var along = 0.0;
    if (count == 0u) {
        return vec2<f32>(best, 0.0);
    }
    if (count == 1u) {
        let p0 = select(tileRiverPt(first), tileValleyPt(first), valley);
        return vec2<f32>(length(vec2<f32>(x, z) - p0), 0.0);
    }
    var walked = 0.0;
    for (var i = 0u; i + 1u < count; i = i + 1u) {
        let a = select(tileRiverPt(first + i), tileValleyPt(first + i), valley);
        let b = select(tileRiverPt(first + i + 1u), tileValleyPt(first + i + 1u), valley);
        let ab = b - a;
        let ap = vec2<f32>(x, z) - a;
        let len2 = max(dot(ab, ab), 1e-6);
        let t = clamp(dot(ap, ab) / len2, 0.0, 1.0);
        let d = length(ap - ab * t);
        let len = sqrt(len2);
        if (d < best) {
            best = d;
            along = walked + t * len;
        }
        walked = walked + len;
    }
    return vec2<f32>(best, along);
}

fn tileInBounds(x: f32, z: f32, b: vec4<f32>) -> bool {
    return x >= b.x && z >= b.y && x <= b.z && z <= b.w;
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
    crest: f32, // ADR-1953
    step: f32,  // ADR-1953: the step the motion can open to a neighbour (the moving sea's skirt)
    landing: f32, // ADR-1960: the landing glow, 0..1
    river: f32,   // ADR-1965: the river and pond shares of `water`, and the distance downstream
    pond: f32,
    riverS: f32,
};

fn tileSample(x: f32, z: f32) -> TileOut {
    let seed = tile.info.x;
    let region = tileValueNoise(seed ^ TILE_REGION_SEED, x / tile.e.x, z / tile.e.x);
    let k = 1.0 + tile.e.y * (2.0 * region - 1.0);
    let c = tileFbm(seed ^ TILE_CONTINENT_SEED, x / tile.a.x, z / tile.a.x, 3);
    let land = tileSmooth(tile.a.y - tile.a.z, tile.a.y + tile.a.z, c);
    let inland = tileSmooth(tile.a.y, tile.a.y + tile.a.w, c);
    // ADR-1953: the rolling formations (the hills' domain drifts; all 0 on a static ground: hx is x).
    let hx = x - tile.roll.x * tile.dyn.x;
    let hz = z - tile.roll.y * tile.dyn.x;
    let wx = (tileFbm(seed ^ TILE_WARPX_SEED, hx / tile.c.w, hz / tile.c.w, 2) * 2.0 - 1.0) * tile.c.z;
    let wz = (tileFbm(seed ^ TILE_WARPZ_SEED, hx / tile.c.w + 31.7, hz / tile.c.w - 11.3, 2) * 2.0 - 1.0) * tile.c.z;
    let hwl = tile.b.w * k;
    let hn = tileFbm(seed ^ TILE_HILL_SEED, (hx + wx) / hwl, (hz + wz) / hwl, i32(tile.c.y + 0.5));
    let shaped = tileSmooth(tile.d.x, tile.d.y, hn);
    let excess = max(shaped - tile.d.z, 0.0);
    let hill = shaped - excess * tile.d.w;
    // ADR-1953: the breathing (1 on a static ground).
    var breath = 1.0;
    if (tile.breath.x != 0.0) {
        let n = tileValueNoise(seed ^ TILE_BREATH_SEED, x / tile.breath.z, z / tile.breath.z);
        breath = 1.0 + tile.breath.x * sin(TILE_TWO_PI * (tile.dyn.x / tile.breath.y + n));
    }
    var landHeight = tile.b.z + tile.c.x * k * hill * inland * tile.water0.w * breath;
    // ADR-1965: the dry valleys, then the rivers, then the ponds (applyFeatures' twin)
    let valleyCount = min(u32(tile.water0.z + 0.5), 4u);
    for (var i = 0u; i < valleyCount; i = i + 1u) {
        let vi = tile.valleyInfo[i];
        if (!tileInBounds(x, z, tile.valleyB[i])) {
            continue;
        }
        let d = tileNearest(x, z, u32(vi.x + 0.5), u32(vi.y + 0.5), true).x;
        let w = 1.0 - tileSmooth(0.0, 0.5 * vi.z, d);
        if (w <= 0.0) {
            continue;
        }
        let relief = max(landHeight - tile.b.z, 0.0);
        let frac = tile.valleyFrac[i];
        let depth = select(vi.w, frac * relief, frac > 0.0);
        landHeight = max(landHeight - depth * w, min(landHeight, tile.b.z));
    }
    var river = 0.0;
    var riverS = 0.0;
    var bestRiver = 1e9;
    let riverCount = min(u32(tile.water0.x + 0.5), 4u);
    for (var i = 0u; i < riverCount; i = i + 1u) {
        let ri = tile.riverInfo[i];
        if (!tileInBounds(x, z, tile.riverBounds[i])) {
            continue;
        }
        let near = tileNearest(x, z, u32(ri.x + 0.5), u32(ri.y + 0.5), false);
        let d = near.x;
        if (d >= 0.5 * ri.w) {
            continue;
        }
        let rl = tile.riverLevel[i];
        let u = select(0.0, clamp(near.y / rl.z, 0.0, 1.0), rl.z > 1e-3);
        let ease = u * u * (3.0 - 2.0 * u);
        let level = rl.x + (rl.y - rl.x) * ease - tile.b.x;
        let v = 1.0 - tileSmooth(0.5 * ri.z, 0.5 * ri.w, d);
        landHeight = landHeight + (level - landHeight) * v;
        let channel = 1.0 - tileSmooth(0.5 * ri.z - 0.5, 0.5 * ri.z + 0.5, d);
        if (channel > river || (channel == river && d < bestRiver)) {
            river = channel;
            riverS = near.y;
            bestRiver = d;
        }
    }
    var pond = 0.0;
    let pondCount = min(u32(tile.water0.y + 0.5), 8u);
    for (var i = 0u; i < pondCount; i = i + 1u) {
        let pd = tile.ponds[i];
        let pb = tile.pondB[i];
        let d = length(vec2<f32>(x, z) - pd.xy);
        if (d >= pd.z * 1.5 + pd.w) {
            continue;
        }
        var shore = pd.z;
        if (pb.y > 0.0) {
            let scale = max(pd.z * 0.6, 1.0);
            let n = tileValueNoise((seed ^ TILE_POND_SEED) + i * 7919u, x / scale, z / scale);
            shore = pd.z * (1.0 + pb.y * 0.5 * (2.0 * n - 1.0));
        }
        let level = pb.x - tile.b.x;
        let v = 1.0 - tileSmooth(shore, shore + pd.w, d);
        landHeight = landHeight + (level - landHeight) * v;
        pond = max(pond, 1.0 - tileSmooth(shore - 0.5, shore + 0.5, d));
    }
    var out: TileOut;
    out.height = (tile.b.x - tile.b.y) + ((tile.b.x + landHeight) - (tile.b.x - tile.b.y)) * land;
    let inlandWater = max(river, pond);
    out.water = 1.0 - land + inlandWater * land;
    out.river = river * land;
    out.pond = pond * land * (1.0 - river);
    out.riverS = riverS;
    out.shore = 1.0 - tileSmooth(0.0, 3.0 * tile.a.z, abs(c - tile.a.y));
    out.flat = tileSmooth(tile.d.z, tile.d.z + 0.06, shaped) * tile.d.w * inland * land;
    out.path = 0.0;
    out.crest = 0.0;
    out.step = 0.0;
    out.landing = 0.0;
    if (tile.info.y > 0u) {
        let d = tilePathDistance(x, z);
        let p = (1.0 - tileSmooth(0.5 * tile.e.z, 0.5 * tile.e.z + 0.75, d)) * land;
        out.height = out.height + tile.e.w * p;
        out.path = p;
    }
    return out;
}

// ADR-1953: the waves and the rings at (x, z) on a tile of `water`, for a ring of `cellSize` (tileMotion's twin).
// Returns (height offset, crest, a bound on the step to a neighbour the motion can open -- the sea's skirt).
fn tileMotion(x: f32, z: f32, s: TileOut, cellSize: f32) -> vec3<f32> {
    let t = tile.dyn.x;
    let water = s.water;
    let sea = max(water - s.river - s.pond, 0.0);
    let waveWater = sea + s.river * tile.waterMotion.x + s.pond * tile.waterMotion.y; // ADR-1965
    var height = 0.0;
    var crest = 0.0;
    var step = 0.0;
    let waveCount = min(u32(tile.dyn.z + 0.5), 4u);
    for (var i = 0u; i < waveCount; i = i + 1u) {
        let w0 = tile.waves[i * 3u];
        let w1 = tile.waves[i * 3u + 1u];
        let w2 = tile.waves[i * 3u + 2u];
        var d = 0.0;
        if (w0.x > 0.5) {
            d = sqrt((x - w1.x) * (x - w1.x) + (z - w1.y) * (z - w1.y));
        } else {
            d = x * w1.x + z * w1.y;
        }
        let theta = TILE_TWO_PI * ((d - w0.w * t) / w0.z + w2.z);
        let u = 0.5 + 0.5 * sin(theta);
        let c = pow(u, 1.0 + 7.0 * w1.z);
        let mask = (w2.x * (1.0 - water) + w2.y * waveWater) * tileSmooth(2.0 * cellSize, 4.0 * cellSize, w0.z);
        height = height + w0.y * (2.0 * c - 1.0) * mask;
        crest = crest + w1.w * c * mask;
        step = step + 2.0 * abs(w0.y * mask) * min(1.0, TILE_TWO_PI * cellSize / w0.z * (1.0 + 7.0 * w1.z));
    }
    let ringFade = tileSmooth(2.0 * cellSize, 4.0 * cellSize, min(tile.ring0.z, tile.ring0.w));
    let eventCount = min(u32(tile.dyn.w + 0.5), 16u);
    for (var i = 0u; i < eventCount; i = i + 1u) {
        let e = tile.events[i];
        let age = t - e.z;
        if (age < 0.0 || age > 6.0 * tile.ring1.x) {
            continue;
        }
        let dist = sqrt((x - e.x) * (x - e.x) + (z - e.y) * (z - e.y));
        let off = dist - tile.ring0.y * age;
        let g = off / tile.ring0.w;
        let env = exp(-age / tile.ring1.x) * exp(-g * g) * e.w * ringFade;
        height = height + tile.ring0.x * env * cos(TILE_TWO_PI * off / tile.ring0.z);
        crest = crest + tile.ring1.y * env;
        step = step + 2.0 * abs(tile.ring0.x * env);
    }
    // ADR-1965: the river's drift downstream
    if (tile.waterMotion.z != 0.0 && s.river > 0.0) {
        let fade = tileSmooth(2.0 * cellSize, 4.0 * cellSize, tile.waterMotion.w);
        let dh = tile.waterMotion.z * s.river * fade *
                 sin(TILE_TWO_PI * (s.riverS - tile.waterMotion2.x * t) / max(tile.waterMotion.w, 0.5));
        height = height + dh;
        step = step + 2.0 * abs(tile.waterMotion.z * s.river * fade);
    }
    return vec3<f32>(tile.dyn.y * height, clamp(crest, 0.0, 1.0), abs(tile.dyn.y) * step);
}

// ADR-1954: the landing springs at (x, z) (tileSpring's twin), and ADR-1960 the landing glow (tileLanding's twin).
// CP-D r2: a workgroup first gathers the springs that can reach its 8x8 cells (plus the seam band's blocks), in index
// order (deterministic), so a sample loops over the few nearby landings rather than every one still ringing.
const TILE_SPRING_CAP: u32 = 256u; // scene::kTileSprings
var<workgroup> wgSpringHit: array<u32, 256>;
var<workgroup> wgSprings: array<u32, 256>;
var<workgroup> wgSpringCount: u32;

fn tileGatherSprings(lid: u32, wg: vec2<u32>) {
    let count = min(u32(tile.spring1.y + 0.5), TILE_SPRING_CAP);
    let cellSize = gen.cell.x;
    let ratio = f32(max(tile.blend.y, 1));
    // the cells this workgroup writes, widened by a coarser block (the seam band reads it) and the springs' reach
    let x0 = (f32(gen.window.x) + f32(wg.x * 8u) - ratio) * cellSize;
    let z0 = (f32(gen.window.y) + f32(wg.y * 8u) - ratio) * cellSize;
    let x1 = (f32(gen.window.x) + f32(wg.x * 8u + 8u) + ratio) * cellSize;
    let z1 = (f32(gen.window.y) + f32(wg.y * 8u + 8u) + ratio) * cellSize;
    let reach = 6.0 * max(tile.spring1.x, 1e-3); // kTileSpringReach ripple radii
    for (var i = lid; i < TILE_SPRING_CAP; i = i + 64u) {
        var hit = 0u;
        if (i < count) {
            let e = tile.springs[i];
            let dx = max(max(x0 - e.x, e.x - x1), 0.0);
            let dz = max(max(z0 - e.y, e.y - z1), 0.0);
            if (dx * dx + dz * dz < reach * reach) {
                hit = 1u;
            }
        }
        wgSpringHit[i] = hit;
    }
    workgroupBarrier();
    if (lid == 0u) {
        var n = 0u;
        for (var i = 0u; i < count; i = i + 1u) {
            if (wgSpringHit[i] != 0u) {
                wgSprings[n] = i;
                n = n + 1u;
            }
        }
        wgSpringCount = n;
    }
    workgroupBarrier();
}

// x: the springs' height offset; y: the landing glow (0..1, clamped by the caller after averaging)
fn tileSpring(x: f32, z: f32) -> vec2<f32> {
    let t = tile.dyn.x;
    let w = TILE_TWO_PI * tile.spring0.y;
    let zeta = clamp(tile.spring0.z, 0.0, 0.999);
    let wd = w * sqrt(1.0 - zeta * zeta);
    var h = 0.0;
    var l = 0.0;
    let n = wgSpringCount;
    for (var j = 0u; j < n; j = j + 1u) {
        let e = tile.springs[wgSprings[j]];
        let r = sqrt((x - e.x) * (x - e.x) + (z - e.y) * (z - e.y));
        let reach = r / max(tile.spring1.x, 1e-3);
        if (reach >= 6.0) {
            continue; // kTileSpringReach
        }
        let a = t - e.z - r / tile.spring0.w;
        if (a < 0.0) {
            continue;
        }
        let k = clamp((reach - 5.0) / (6.0 - 5.0), 0.0, 1.0); // kTileSpringReachStart .. kTileSpringReach
        let window = 1.0 - k * k * (3.0 - 2.0 * k);
        let env = exp(-zeta * w * a);
        h = h - tile.spring0.x * e.w * exp(-r / tile.spring1.x) * window * env * sin(wd * a);
        // ADR-1960: the glow on its own, tighter falloff (glowRadius <= rippleRadius), eased out by 6 of its radii
        let gr = r / tile.spring1.z;
        let gk = clamp(gr - 5.0, 0.0, 1.0);
        let gw = 1.0 - gk * gk * (3.0 - 2.0 * gk);
        l = l + e.w * exp(-gr) * gw * env * (1.0 - exp(-a / 0.06));
    }
    return vec2<f32>(h, clamp(l, 0.0, 1.0));
}

// ADR-1953: a fine sample of the moving ground -- the base plus the waves and rings at that point, faded for the fine
// pitch. Every ring averages the same fine samples (and its seam band the same blocks), so the rings agree in motion
// exactly as they do at rest.
fn tileSampleMoving(x: f32, z: f32, fine: f32) -> TileOut {
    var s = tileSample(x, z);
    if (tile.breath.w > 0.5) {
        let m = tileMotion(x, z, s, fine);
        let spring = tileSpring(x, z);
        s.height = s.height + m.x + spring.x;
        s.crest = m.y;
        s.landing = spring.y; // ADR-1960
        s.step = m.z + 2.0 * abs(spring.x); // a sprung tile steps against its neighbours too
    }
    return s;
}

// One cell of this ring: the tile at its centre, or the mean of the fine tiles it covers (generatorTile's twin).
fn tileCell(ix: i32, iz: i32) -> TileOut {
    let cellSize = gen.cell.x;
    let m = max(tile.info.z, 1u);
    if (m == 1u) {
        return tileSampleMoving((f32(ix) + 0.5) * cellSize, (f32(iz) + 0.5) * cellSize, cellSize);
    }
    let fine = cellSize / f32(m);
    var t: TileOut;
    t.height = 0.0;
    t.water = 0.0;
    t.shore = 0.0;
    t.flat = 0.0;
    t.path = 0.0;
    t.crest = 0.0;
    t.step = 0.0;
    t.landing = 0.0;
    t.river = 0.0;
    t.pond = 0.0;
    t.riverS = 0.0;
    var lowest = 3.0e38;
    for (var b = 0u; b < m; b = b + 1u) {
        for (var a = 0u; a < m; a = a + 1u) {
            let s = tileSampleMoving(f32(ix) * cellSize + (f32(a) + 0.5) * fine,
                                     f32(iz) * cellSize + (f32(b) + 0.5) * fine, fine);
            lowest = min(lowest, s.height);
            t.height = t.height + s.height;
            t.water = t.water + s.water;
            t.shore = t.shore + s.shore;
            t.flat = t.flat + s.flat;
            t.path = t.path + s.path;
            t.crest = t.crest + s.crest;
            t.landing = t.landing + s.landing;
            t.river = t.river + s.river;
            t.pond = t.pond + s.pond;
            t.riverS = t.riverS + s.riverS;
            t.step = max(t.step, s.step);
        }
    }
    let inv = 1.0 / f32(m * m);
    t.height = t.height * inv;
    t.water = t.water * inv;
    t.shore = t.shore * inv;
    t.flat = t.flat * inv;
    t.path = t.path * inv;
    t.crest = t.crest * inv;
    t.landing = t.landing * inv;
    t.river = t.river * inv;
    t.pond = t.pond * inv;
    t.riverS = t.riverS * inv;
    if (tile.blend.z != 0) {
        t.height = lowest; // ADR-1953: an underlay's cell at its lowest fine tile
    }
    return t;
}

@compute @workgroup_size(8, 8)
fn cs_generate_tiles(@builtin(global_invocation_id) gid: vec3<u32>, @builtin(local_invocation_index) lid: u32,
                     @builtin(workgroup_id) wg: vec3<u32>) {
    tileGatherSprings(lid, wg.xy); // before any thread leaves: it has barriers
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
            var mc = 0.0;
            var ml = 0.0;
            var mr = 0.0;
            var mo = 0.0;
            for (var b = 0; b < ratio; b = b + 1) {
                for (var a = 0; a < ratio; a = a + 1) {
                    let c = tileCell(bx + a, bz + b);
                    mh = mh + c.height;
                    mw = mw + c.water;
                    ms = ms + c.shore;
                    mf = mf + c.flat;
                    mp = mp + c.path;
                    mc = mc + c.crest;
                    ml = ml + c.landing;
                    mr = mr + c.river;
                    mo = mo + c.pond;
                    t.step = max(t.step, c.step);
                }
            }
            let inv = 1.0 / f32(ratio * ratio);
            t.height = t.height + (mh * inv - t.height) * w;
            t.water = t.water + (mw * inv - t.water) * w;
            t.shore = t.shore + (ms * inv - t.shore) * w;
            t.flat = t.flat + (mf * inv - t.flat) * w;
            t.path = t.path + (mp * inv - t.path) * w;
            t.crest = t.crest + (mc * inv - t.crest) * w;
            t.landing = t.landing + (ml * inv - t.landing) * w;
            t.river = t.river + (mr * inv - t.river) * w;
            t.pond = t.pond + (mo * inv - t.pond) * w;
        }
    }
    // ADR-1953: a moving sea opens steps between its slabs, which the static sea never has: it gets as much skirt as
    // its motion can step (never more than the land's), so no gap shows through the swell. 0 at rest.
    let crest = t.crest;
    let seaSkirt = min(t.step, tile.skirt.x);
    // The skirt: a land tile's box reaches `skirt` further down with its top where it was.
    // ADR-1991: the top IS t.height -- the surface bodies stand on, flight clears and the contour pass reads from
    // scale.w. The centred box (thickness tile.skirt.y) sits half its stretched height below it. It used to sit centred
    // on t.height, its top half a thickness above everything that reads the ground (0.6 m on 4 m tiles): feet sank.
    let wet = clamp(t.water, 0.0, 1.0);
    let k = tile.skirt.x * (1.0 - wet) + seaSkirt * wet;
    let stretch = (tile.skirt.y + k) / tile.skirt.y;
    let p = vec3<f32>((f32(ix) + 0.5) * cellSize, t.height - 0.5 * (tile.skirt.y + k), (f32(iz) + 0.5) * cellSize);
    r.position = vec4<f32>((gen.genToObject * vec4<f32>(p, 1.0)).xyz, 1.0);
    r.rotation = gen.rotation;
    // scale.w (a program's `instanceIndex`, meaningless for a window that moves): the tile's top, generator metres. The
    // contour pass reads its neighbours' from here (ADR-1970).
    r.scale = vec4<f32>(gen.scaleInfo.x, gen.scaleInfo.y * stretch, gen.scaleInfo.z, t.height);
    r.random = vec4<f32>(t.water, t.shore, t.flat, t.path);
    r.color = vec4<f32>(1.0, 1.0, 1.0, genUnit(genHash(ix, iz, gen.hashing.x, 21u)));
    // ADR-1953: the crest channel in the emissive's spare lane; ADR-1960: the landing glow above it, in 255ths x 32
    // (the contour pass adds 2 x its side mask between them; scene::tilePackedEmissiveW)
    // ADR-1965: and the water body's class above them (scene::tileWaterClass: the larger share), x 8192
    var waterClass = 0.0;
    if (t.river >= 0.5 * t.water && t.river > t.pond && t.river > 0.0) {
        waterClass = 1.0;
    } else if (t.pond >= 0.5 * t.water && t.pond > 0.0) {
        waterClass = 2.0;
    }
    r.emissive = vec4<f32>(1.0, 1.0, 1.0, clamp(crest, 0.0, 1.0) + 32.0 * round(clamp(t.landing, 0.0, 1.0) * 255.0) +
                           8192.0 * waterClass);
    records[index] = r;
}

// ADR-1970: the contour pass, after cs_generate_tiles. A side is a contour edge where the level
// floor((top - offset) / interval) drops to the neighbour across it (bits +x 1, -x 2, +z 4, -z 8, in the generator's
// axes); the mask joins the crest in the emissive's spare lane as crest + 2 * mask, which the `tileContour` material op
// decodes. A neighbour outside the window or in the ring's hole is no edge (the ring there is another object's).
fn tileLevel(top: f32) -> f32 {
    return floor((top - tile.ring1.w) / tile.ring1.z);
}

@compute @workgroup_size(8, 8)
fn cs_tile_contours(@builtin(global_invocation_id) gid: vec3<u32>) {
    let countX = u32(gen.window.z);
    let countZ = u32(gen.window.w);
    if (gid.x >= countX || gid.y >= countZ || !(tile.ring1.z > 0.0)) {
        return;
    }
    let index = gid.y * countX + gid.x;
    let own = records[index].scale;
    if (own.x <= 0.0) {
        return;
    }
    let level = tileLevel(own.w);
    var mask = 0u;
    if (gid.x + 1u < countX) {
        let n = records[index + 1u].scale; // the scale alone: the emissive is being written
        if (n.x > 0.0 && tileLevel(n.w) < level) { mask = mask | 1u; }
    }
    if (gid.x > 0u) {
        let n = records[index - 1u].scale; // the scale alone: the emissive is being written
        if (n.x > 0.0 && tileLevel(n.w) < level) { mask = mask | 2u; }
    }
    if (gid.y + 1u < countZ) {
        let n = records[index + countX].scale; // the scale alone: the emissive is being written
        if (n.x > 0.0 && tileLevel(n.w) < level) { mask = mask | 4u; }
    }
    if (gid.y > 0u) {
        let n = records[index - countX].scale; // the scale alone: the emissive is being written
        if (n.x > 0.0 && tileLevel(n.w) < level) { mask = mask | 8u; }
    }
    records[index].emissive.w = records[index].emissive.w + 2.0 * f32(mask);
}
