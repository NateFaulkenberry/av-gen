// GPU particle system (ADR-015, revision 2026-09-08: deterministic compaction).
//
// Fixed pool of `capacity` slots. Every list this shader produces is a pure function of
// (slot index, frame index, parameters): there are no atomics anywhere, so which slot a spawn
// takes, its random seeds, and the draw order are bit-identical from run to run.
//
// Pass order per system per frame (one compute pass; dispatches in a pass are ordered and their
// storage writes are visible to later dispatches):
//   1. cs_emit          thread i < min(emitCount, counters.deadCount) initialises slot
//                       deadList[i]. deadList/deadCount are the previous frame's compaction
//                       output (or the CPU reset: deadList = 0..capacity-1, deadCount = capacity).
//   2. cs_simulate      every slot: age, kill, integrate. Writes flags[slot] = 1 if alive else 0.
//   3. cs_scan_reduce   one workgroup per block of kScanBlock slots: blockSums[b] = alive count.
//   4. cs_scan_top      one workgroup: exclusive scan of blockSums in place (looping over chunks
//                       of kScanBlock); thread 0 writes counters.aliveCount, counters.deadCount
//                       = capacity - aliveCount, and the indirect draw args.
//   5. cs_scan_scatter  one workgroup per block: local exclusive scan + blockSums[b] gives every
//                       alive slot its rank r; aliveList[r] = slot, and every dead slot its rank
//                       slot - r; deadList[slot - r] = slot. Both lists are therefore in slot order.
//   Render: vs_particle reads aliveList[instance_index], so instances are drawn in slot order.
// Randomness is a hash of (slot, frameIndex, seed, salt) - deterministic for a frame sequence.
//
// Field forces (ADR-025): params.fieldForces holds up to 4 (mode, slot, strength, mix) + (axis, 0)
// pairs sampled from the FieldBlock (binding 8, fields.wgsl) at the particle position after the
// built-in forces: Force / Turbulence add fieldVector * strength * dt to the velocity, Velocity
// blends the velocity towards fieldVector * strength by `mix`, Kill removes the particle where
// fieldScalar >= 0.5. Scalar fields act along the force's axis (fieldScalar * axis).
#include "fields.wgsl"

struct Particle {
    position: vec3<f32>,
    age: f32,
    velocity: vec3<f32>,
    life: f32,       // 0 = dead
    seed: f32,
    size: f32,
    pad: vec2<f32>,
};

struct Params {
    viewProj: mat4x4<f32>,
    cameraRight: vec4<f32>,
    cameraUp: vec4<f32>,
    emitterPos: vec4<f32>,  // xyz, w = shape (0 point, 1 sphere, 2 disc, 3 box)
    extent: vec4<f32>,      // xyz, w = spread
    direction: vec4<f32>,   // xyz, w = drag
    speedLife: vec4<f32>,   // speedMin, speedMax, lifeMin, lifeMax
    gravity: vec4<f32>,     // xyz, w = turbulence strength
    turb: vec4<f32>,        // scale, speed, softness, emissive
    attractor: vec4<f32>,   // xyz, w = strength
    attractor2: vec4<f32>,  // radius, orbit, sizeStart, sizeEnd
    colorStart: vec4<f32>,
    colorEnd: vec4<f32>,
    sim: vec4<f32>,         // dt, time, frameIndex, seed
    counts: vec4<u32>,      // emitCount, capacity, blend (0 additive, 1 alpha), scan blocks
    fieldInfo: vec4<u32>,   // x = field force count
    fieldForces: array<vec4<f32>, 8>, // per force: (mode, slot, strength, mix), (axis.xyz, 0)
};

// Plain values: written by one thread of cs_scan_top, read by cs_emit the next frame.
struct Counters {
    deadCount: u32,
    aliveCount: u32,
    pad0: u32,
    pad1: u32,
};

struct Indirect {
    vertexCount: u32,
    instanceCount: u32,
    firstVertex: u32,
    firstInstance: u32,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read_write> deadList: array<u32>;
@group(0) @binding(3) var<storage, read_write> counters: Counters;
@group(0) @binding(4) var<storage, read_write> aliveList: array<u32>;
@group(0) @binding(5) var<storage, read_write> indirect: Indirect;
@group(0) @binding(6) var<storage, read_write> flags: array<u32>;     // 1 = alive after simulate
@group(0) @binding(7) var<storage, read_write> blockSums: array<u32>; // per scan block
@group(0) @binding(8) var<uniform> fieldBlock: FieldBlock;
// Read-only views for the render stage (same bindings, used only by vs_particle).
@group(0) @binding(1) var<storage, read> particlesRead: array<Particle>;
@group(0) @binding(4) var<storage, read> aliveRead: array<u32>;

// ---- hashing / noise ----------------------------------------------------------------------

// pcg3d comes from noise.wgsl (through fields.wgsl); the built-in turbulence keeps its own
// hash3-based value noise (turbValueNoise / turbCurl) so existing scenes stay bit-identical.
fn rand3(slot: u32, frame: u32, salt: u32) -> vec3<f32> {
    let h = pcg3d(vec3<u32>(slot, frame + u32(params.sim.w) * 7919u, salt));
    return vec3<f32>(h) * (1.0 / 4294967296.0);
}

fn hash3(p: vec3<f32>) -> f32 {
    let q = fract(p * vec3<f32>(0.1031, 0.1030, 0.0973));
    let d = dot(q, vec3<f32>(q.y + 33.33, q.z + 33.33, q.x + 33.33));
    let r = q + vec3<f32>(d);
    return fract((r.x + r.y) * r.z);
}

fn turbValueNoise(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let n000 = hash3(i);
    let n100 = hash3(i + vec3<f32>(1.0, 0.0, 0.0));
    let n010 = hash3(i + vec3<f32>(0.0, 1.0, 0.0));
    let n110 = hash3(i + vec3<f32>(1.0, 1.0, 0.0));
    let n001 = hash3(i + vec3<f32>(0.0, 0.0, 1.0));
    let n101 = hash3(i + vec3<f32>(1.0, 0.0, 1.0));
    let n011 = hash3(i + vec3<f32>(0.0, 1.0, 1.0));
    let n111 = hash3(i + vec3<f32>(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// Three decorrelated potentials; curl of the potential field is divergence-free (Bridson 2007).
fn potential(p: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(turbValueNoise(p), turbValueNoise(p + vec3<f32>(31.4, 47.1, 12.9)), turbValueNoise(p + vec3<f32>(-17.2, 5.3, 29.8))) - vec3<f32>(0.5);
}

fn turbCurl(p: vec3<f32>) -> vec3<f32> {
    let e = 0.05;
    let dx = potential(p + vec3<f32>(e, 0.0, 0.0)) - potential(p - vec3<f32>(e, 0.0, 0.0));
    let dy = potential(p + vec3<f32>(0.0, e, 0.0)) - potential(p - vec3<f32>(0.0, e, 0.0));
    let dz = potential(p + vec3<f32>(0.0, 0.0, e)) - potential(p - vec3<f32>(0.0, 0.0, e));
    let inv = 1.0 / (2.0 * e);
    return vec3<f32>(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) * inv;
}

fn sphereDir(r: vec2<f32>) -> vec3<f32> {
    let z = 1.0 - 2.0 * r.x;
    let s = sqrt(max(0.0, 1.0 - z * z));
    let phi = 6.28318530 * r.y;
    return vec3<f32>(s * cos(phi), s * sin(phi), z);
}

// ---- emit / simulate --------------------------------------------------------------------------

@compute @workgroup_size(64)
fn cs_emit(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    // Spawn i takes the i-th free slot (lowest slot first). Clamped to last frame's dead count.
    if (i >= params.counts.x || i >= counters.deadCount) { return; }
    let slot = deadList[i];
    let frame = u32(params.sim.z);
    let r1 = rand3(slot, frame, 1u);
    let r2 = rand3(slot, frame, 2u);
    let r3 = rand3(slot, frame, 3u);

    var p: Particle;
    let shape = params.emitterPos.w;
    var offset = vec3<f32>(0.0);
    if (shape > 2.5) {
        offset = (r1 * 2.0 - 1.0) * params.extent.xyz;
    } else if (shape > 1.5) {
        let ang = 6.28318530 * r1.x;
        let rad = sqrt(r1.y) * params.extent.x;
        offset = vec3<f32>(cos(ang) * rad, 0.0, sin(ang) * rad);
    } else if (shape > 0.5) {
        offset = sphereDir(r1.xy) * pow(r1.z, 1.0 / 3.0) * params.extent.x;
    }
    p.position = params.emitterPos.xyz + offset;
    let baseDir = normalize(params.direction.xyz + vec3<f32>(1e-5, 0.0, 0.0));
    let randomDir = sphereDir(r2.xy);
    let dir = normalize(mix(baseDir, randomDir, params.extent.w) + vec3<f32>(1e-5));
    let speed = mix(params.speedLife.x, params.speedLife.y, r2.z);
    p.velocity = dir * speed;
    p.age = 0.0;
    // lifeMin + (lifeMax - lifeMin) * r rather than mix(): exact when lifeMin == lifeMax, so a
    // fixed lifetime dies on a predictable frame.
    p.life = params.speedLife.z + (params.speedLife.w - params.speedLife.z) * r3.x;
    p.seed = r3.y;
    p.size = mix(0.7, 1.3, r3.z);
    particles[slot] = p;
}

@compute @workgroup_size(64)
fn cs_simulate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let slot = gid.x;
    if (slot >= params.counts.y) { return; }
    var p = particles[slot];
    if (p.life <= 0.0) {
        flags[slot] = 0u;
        return;
    }
    let dt = params.sim.x;
    p.age += dt;
    if (p.age >= p.life) {
        p.life = 0.0;
        particles[slot] = p;
        flags[slot] = 0u;
        return;
    }
    // forces
    var force = params.gravity.xyz;
    let turbStrength = params.gravity.w;
    if (turbStrength > 0.0) {
        let np = p.position * params.turb.x + vec3<f32>(0.0, 0.0, params.sim.y * params.turb.y);
        force += turbCurl(np) * turbStrength;
    }
    let toA = params.attractor.xyz - p.position;
    let dist = length(toA) + 1e-4;
    let falloff = clamp(1.0 - dist / max(params.attractor2.x, 1e-3), 0.0, 1.0);
    let dirA = toA / dist;
    force += dirA * params.attractor.w * falloff;
    let tangent = cross(vec3<f32>(0.0, 1.0, 0.0), dirA);
    force += tangent * params.attractor2.y * falloff;
    p.velocity += force * dt;
    // field forces (in order; a Kill force ends the particle here)
    let fieldCount = min(params.fieldInfo.x, 4u);
    for (var k = 0u; k < 4u; k = k + 1u) {
        if (k >= fieldCount) { break; }
        let a = params.fieldForces[k * 2u];
        let axis = params.fieldForces[k * 2u + 1u].xyz;
        let mode = u32(a.x + 0.5);
        let fslot = i32(floor(a.y + 0.5));
        if (fslot < 0) { continue; }
        if (mode == 3u) { // kill
            if (fieldScalar(fslot, p.position) >= 0.5) {
                p.life = 0.0;
                particles[slot] = p;
                flags[slot] = 0u;
                return;
            }
            continue;
        }
        var fv: vec3<f32>;
        if (fieldTypeOf(fslot) == FIELD_TYPE_VECTOR) {
            fv = fieldVector(fslot, p.position);
        } else {
            fv = fieldScalar(fslot, p.position) * axis;
        }
        if (mode == 1u) { // velocity
            p.velocity = mix(p.velocity, fv * a.z, a.w);
        } else {          // force / turbulence
            p.velocity += fv * (a.z * dt);
        }
    }
    p.velocity *= max(0.0, 1.0 - params.direction.w * dt);
    p.position += p.velocity * dt;
    particles[slot] = p;
    flags[slot] = 1u;
}

// ---- stable stream compaction -------------------------------------------------------------------
// Workgroups of kScanThreads threads, kScanElems consecutive slots per thread: kScanBlock slots
// per block. Integer sums are associative, so the result does not depend on scheduling.

const kScanThreads: u32 = 256u;
const kScanElems: u32 = 4u;
const kScanBlock: u32 = 1024u; // kScanThreads * kScanElems

var<workgroup> scanShared: array<u32, 256>;

// Workgroup-wide exclusive prefix sum of one value per thread (Hillis-Steele, 8 rounds).
// Must be called in uniform control flow. Returns (exclusive prefix, workgroup total).
fn workgroupScan(tid: u32, value: u32) -> vec2<u32> {
    workgroupBarrier(); // previous call's readers are done with scanShared
    scanShared[tid] = value;
    workgroupBarrier();
    for (var offset = 1u; offset < kScanThreads; offset = offset << 1u) {
        var v = scanShared[tid];
        if (tid >= offset) { v += scanShared[tid - offset]; }
        workgroupBarrier();
        scanShared[tid] = v;
        workgroupBarrier();
    }
    let inclusive = scanShared[tid];
    let total = scanShared[kScanThreads - 1u];
    return vec2<u32>(inclusive - value, total);
}

// Loads this thread's kScanElems flags (0 beyond capacity) and returns their sum.
fn loadFlags(base: u32, out: ptr<function, array<u32, 4>>) -> u32 {
    var sum = 0u;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        var f = 0u;
        if (i < params.counts.y) { f = flags[i]; }
        (*out)[k] = f;
        sum += f;
    }
    return sum;
}

@compute @workgroup_size(256)
fn cs_scan_reduce(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    var f: array<u32, 4>;
    let local = loadFlags(wid.x * kScanBlock + tid * kScanElems, &f);
    let r = workgroupScan(tid, local);
    if (tid == 0u) { blockSums[wid.x] = r.y; }
}

@compute @workgroup_size(256)
fn cs_scan_top(@builtin(local_invocation_id) lid: vec3<u32>) {
    let tid = lid.x;
    let blocks = params.counts.w;
    var carry = 0u;
    for (var chunk = 0u; chunk < blocks; chunk += kScanBlock) {
        let base = chunk + tid * kScanElems;
        var v: array<u32, 4>;
        var local = 0u;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            var s = 0u;
            if (i < blocks) { s = blockSums[i]; }
            v[k] = s;
            local += s;
        }
        let r = workgroupScan(tid, local);
        var run = carry + r.x;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            if (i < blocks) { blockSums[i] = run; }
            run += v[k];
        }
        carry += r.y;
    }
    if (tid == 0u) {
        let alive = min(carry, params.counts.y);
        counters.aliveCount = alive;
        counters.deadCount = params.counts.y - alive;
        indirect.vertexCount = 6u;
        indirect.instanceCount = alive;
        indirect.firstVertex = 0u;
        indirect.firstInstance = 0u;
    }
}

@compute @workgroup_size(256)
fn cs_scan_scatter(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    let base = wid.x * kScanBlock + tid * kScanElems;
    var f: array<u32, 4>;
    let local = loadFlags(base, &f);
    let r = workgroupScan(tid, local);
    var rank = blockSums[wid.x] + r.x; // alive slots before slot `base`
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        if (i < params.counts.y) {
            if (f[k] != 0u) {
                aliveList[rank] = i;
            } else {
                deadList[i - rank] = i;
            }
            rank += f[k];
        }
    }
}

// ---- rendering --------------------------------------------------------------------------------

struct VsOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
};

@vertex
fn vs_particle(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VsOut {
    let slot = aliveRead[ii];
    let p = particlesRead[slot];
    let t = clamp(p.age / max(p.life, 1e-4), 0.0, 1.0);
    let size = mix(params.attractor2.z, params.attractor2.w, t) * p.size;
    var corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(-1.0, 1.0),
                                      vec2<f32>(-1.0, 1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0));
    let c = corners[vi];
    let world = p.position + (params.cameraRight.xyz * c.x + params.cameraUp.xyz * c.y) * size;
    var out: VsOut;
    out.clip = params.viewProj * vec4<f32>(world, 1.0);
    out.uv = c;
    var color = mix(params.colorStart, params.colorEnd, t);
    // Per-particle tint variation from the seed keeps clouds from looking flat.
    color = vec4<f32>(color.rgb * (0.85 + 0.3 * p.seed), color.a);
    out.color = color;
    if (p.life <= 0.0) { out.clip = vec4<f32>(0.0, 0.0, 2.0, 1.0); } // cull dead (never drawn)
    return out;
}

@fragment
fn fs_particle(in: VsOut) -> @location(0) vec4<f32> {
    let r2 = dot(in.uv, in.uv);
    if (r2 > 1.0) { discard; }
    let falloff = (1.0 - r2) * (1.0 - r2);
    let alpha = in.color.a * falloff;
    let emissive = params.turb.w;
    if (params.counts.z == 0u) {
        return vec4<f32>(in.color.rgb * alpha * emissive, alpha); // additive: premultiplied
    }
    return vec4<f32>(in.color.rgb * emissive, alpha);
}
