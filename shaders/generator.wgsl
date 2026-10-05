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
    ground: vec4<f32>,          // height, amplitude, frequency
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
    let seed = gen.hashing.x;
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
