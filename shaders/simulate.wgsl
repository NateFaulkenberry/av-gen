// Grid field simulation (ADR-032): the GPU twin of spatial::GridField::step(). One kernel per
// stage, in the order inject -> advect -> {reaction | snapshot -> diffuse x N -> dissipate}.
// A frame's whole sub-step chain is encoded into one compute pass: in a compute pass the usage
// scope is a single dispatch, so the ping-pong buffers may swap between readable and writable
// from one dispatch to the next (that is why the diffusion snapshot is a kernel and not a buffer
// copy - a copy would have to split the pass). Every
// kernel is a gather: a thread writes only its own cell and reads whatever it likes, so there
// are no atomics and no ordering between threads - the result is a pure function of the input
// state, the parameters and the fixed sub-step. rendering::Simulation drives the passes and
// copies the final state into the shared grid table fields.wgsl samples.
//
// Bindings (group 0): 0 = SimUniforms (dynamic offset, one 256-byte slot per grid), 1 = the
// destination buffer (read_write storage), 2 = the source buffer (read-only), 3 = the state the
// diffusion sweeps started from (read-only; unused by the other kernels), 4 = the field block,
// 15 = the grid table (declared by fields.wgsl; a velocity or injection field may itself be a
// Grid field, which then reads the previous frame's state). Mirrors rendering/simulation.hpp.
#include "fields.wgsl"

struct SimUniforms {
    res: vec4<u32>,       // resolution.xyz, components per cell
    layout0: vec4<u32>,   // grid offset in floats, cell count, wrap (0 clamp, 1 wrap), pad
    bounds0: vec4<f32>,   // boundsMin.xyz, dt (seconds)
    bounds1: vec4<f32>,   // boundsMax.xyz, time (seconds)
    params0: vec4<f32>,   // injectRate, advect, diffusion, dissipation
    params1: vec4<f32>,   // feed, kill, diffusionA, diffusionB
    slots: vec4<i32>,     // injection field slot, velocity field slot, mode, pad
};

const SIM_MODE_SCALAR: i32 = 0;
const SIM_MODE_VECTOR: i32 = 1;
const SIM_MODE_REACTION: i32 = 2;

@group(0) @binding(0) var<uniform> sim: SimUniforms;
@group(0) @binding(1) var<storage, read_write> dst: array<f32>;
@group(0) @binding(2) var<storage, read> src: array<f32>;
@group(0) @binding(3) var<storage, read> initial: array<f32>;
@group(0) @binding(4) var<uniform> fieldBlock: FieldBlock;

fn simCells() -> u32 {
    return sim.res.x * sim.res.y * sim.res.z;
}

fn simCoords(i: u32) -> vec3<i32> {
    let plane = sim.res.x * sim.res.y;
    let z = i / plane;
    let rest = i - z * plane;
    let y = rest / sim.res.x;
    let x = rest - y * sim.res.x;
    return vec3<i32>(i32(x), i32(y), i32(z));
}

fn simBase(c: vec3<i32>) -> u32 {
    let nx = i32(sim.res.x);
    let ny = i32(sim.res.y);
    return sim.layout0.x + u32(((c.z * ny + c.y) * nx + c.x) * i32(sim.res.w));
}

fn simCellSize() -> vec3<f32> {
    return (sim.bounds1.xyz - sim.bounds0.xyz) / vec3<f32>(f32(sim.res.x), f32(sim.res.y), f32(sim.res.z));
}

fn simCellCenter(c: vec3<i32>) -> vec3<f32> {
    return sim.bounds0.xyz + (vec3<f32>(c) + vec3<f32>(0.5)) * simCellSize();
}

fn simWrapAxis(i: i32, n: i32) -> i32 {
    if (sim.layout0.z == 1u) {
        let m = i % n;
        return select(m, m + n, m < 0);
    }
    return clamp(i, 0, n - 1);
}

// One component of the source buffer with the wrap rule (spatial::GridField::at).
fn simRead(i: i32, j: i32, k: i32, c: i32) -> f32 {
    if (c < 0 || c >= i32(sim.res.w)) {
        return 0.0;
    }
    let cc = vec3<i32>(simWrapAxis(i, i32(sim.res.x)), simWrapAxis(j, i32(sim.res.y)), simWrapAxis(k, i32(sim.res.z)));
    return src[simBase(cc) + u32(c)];
}

fn simReadInitial(i: i32, j: i32, k: i32, c: i32) -> f32 {
    if (c < 0 || c >= i32(sim.res.w)) {
        return 0.0;
    }
    let cc = vec3<i32>(simWrapAxis(i, i32(sim.res.x)), simWrapAxis(j, i32(sim.res.y)), simWrapAxis(k, i32(sim.res.z)));
    return initial[simBase(cc) + u32(c)];
}

// Continuous cell coordinate of a point (cell i is centred at i), as spatial::gridCoord.
fn simCoord(p: vec3<f32>) -> vec3<f32> {
    let extent = max(sim.bounds1.xyz - sim.bounds0.xyz, vec3<f32>(1e-6));
    let inv = vec3<f32>(1.0) / extent;
    return (p - sim.bounds0.xyz) * inv * vec3<f32>(f32(sim.res.x), f32(sim.res.y), f32(sim.res.z)) - vec3<f32>(0.5);
}

fn simTrilinear(coord: vec3<f32>, c: i32) -> f32 {
    let base = floor(coord);
    let f = coord - base;
    let i = i32(base.x);
    let j = i32(base.y);
    let k = i32(base.z);
    let c000 = simRead(i, j, k, c);
    let c100 = simRead(i + 1, j, k, c);
    let c010 = simRead(i, j + 1, k, c);
    let c110 = simRead(i + 1, j + 1, k, c);
    let c001 = simRead(i, j, k + 1, c);
    let c101 = simRead(i + 1, j, k + 1, c);
    let c011 = simRead(i, j + 1, k + 1, c);
    let c111 = simRead(i + 1, j + 1, k + 1, c);
    let x00 = c000 + (c100 - c000) * f.x;
    let x10 = c010 + (c110 - c010) * f.x;
    let x01 = c001 + (c101 - c001) * f.x;
    let x11 = c011 + (c111 - c011) * f.x;
    let y0 = x00 + (x10 - x00) * f.y;
    let y1 = x01 + (x11 - x01) * f.y;
    return y0 + (y1 - y0) * f.z;
}

// ---- kernels -----------------------------------------------------------------------------------

// dst = src. Snapshots the state the diffusion sweeps relax towards (cs_diffuse's `initial`).
@compute @workgroup_size(64)
fn cs_copy(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let base = simBase(simCoords(i));
    let comps = i32(sim.res.w);
    for (var k = 0; k < comps; k = k + 1) {
        dst[base + u32(k)] = src[base + u32(k)];
    }
}

// dst = src + injectRate * dt * field(cellCentre). Scalar / reaction grids take the field's
// scalar reading (clamped at 0) into the pattern channel; vector grids take its vector.
@compute @workgroup_size(64)
fn cs_inject(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let c = simCoords(i);
    let base = simBase(c);
    let comps = i32(sim.res.w);
    for (var k = 0; k < comps; k = k + 1) {
        dst[base + u32(k)] = src[base + u32(k)];
    }
    let slot = sim.slots.x;
    if (slot < 0) {
        return;
    }
    let p = simCellCenter(c);
    let amount = sim.params0.x * sim.bounds0.w;
    if (sim.slots.z == SIM_MODE_VECTOR) {
        let v = fieldVector(slot, p);
        dst[base] = dst[base] + amount * v.x;
        dst[base + 1u] = dst[base + 1u] + amount * v.y;
        dst[base + 2u] = dst[base + 2u] + amount * v.z;
        return;
    }
    let channel = select(0u, 1u, sim.slots.z == SIM_MODE_REACTION);
    dst[base + channel] = dst[base + channel] + amount * max(0.0, fieldScalar(slot, p));
}

// Semi-Lagrangian advection: back-trace the cell centre by the velocity field and gather.
@compute @workgroup_size(64)
fn cs_advect(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let c = simCoords(i);
    let base = simBase(c);
    let comps = i32(sim.res.w);
    let slot = sim.slots.y;
    if (slot < 0) {
        for (var k = 0; k < comps; k = k + 1) {
            dst[base + u32(k)] = src[base + u32(k)];
        }
        return;
    }
    let centre = simCellCenter(c);
    let v = fieldVector(slot, centre) * sim.params0.y;
    let source = simCoord(centre - v * sim.bounds0.w);
    for (var k = 0; k < comps; k = k + 1) {
        dst[base + u32(k)] = simTrilinear(source, k);
    }
}

// One Jacobi sweep: (initial + a * sum(6 neighbours of src)) / (1 + 6a), a = diffusion * dt.
@compute @workgroup_size(64)
fn cs_diffuse(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let c = simCoords(i);
    let base = simBase(c);
    let comps = i32(sim.res.w);
    let a = sim.params0.z * sim.bounds0.w;
    for (var k = 0; k < comps; k = k + 1) {
        let sum = simRead(c.x - 1, c.y, c.z, k) + simRead(c.x + 1, c.y, c.z, k) +
                  simRead(c.x, c.y - 1, c.z, k) + simRead(c.x, c.y + 1, c.z, k) +
                  simRead(c.x, c.y, c.z - 1, k) + simRead(c.x, c.y, c.z + 1, k);
        dst[base + u32(k)] = (simReadInitial(c.x, c.y, c.z, k) + a * sum) / (1.0 + 6.0 * a);
    }
}

// dst = src * max(0, 1 - dissipation * dt).
@compute @workgroup_size(64)
fn cs_dissipate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let base = simBase(simCoords(i));
    let keep = max(0.0, 1.0 - sim.params0.w * sim.bounds0.w);
    let comps = i32(sim.res.w);
    for (var k = 0; k < comps; k = k + 1) {
        dst[base + u32(k)] = src[base + u32(k)] * keep;
    }
}

// Gray-Scott, explicit Euler with the 6-neighbour Laplacian (Pearson 1993).
@compute @workgroup_size(64)
fn cs_reaction(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let c = simCoords(i);
    let base = simBase(c);
    let dt = sim.bounds0.w;
    let a = simRead(c.x, c.y, c.z, 0);
    let b = simRead(c.x, c.y, c.z, 1);
    let la = simRead(c.x - 1, c.y, c.z, 0) + simRead(c.x + 1, c.y, c.z, 0) + simRead(c.x, c.y - 1, c.z, 0) +
             simRead(c.x, c.y + 1, c.z, 0) + simRead(c.x, c.y, c.z - 1, 0) + simRead(c.x, c.y, c.z + 1, 0) -
             6.0 * a;
    let lb = simRead(c.x - 1, c.y, c.z, 1) + simRead(c.x + 1, c.y, c.z, 1) + simRead(c.x, c.y - 1, c.z, 1) +
             simRead(c.x, c.y + 1, c.z, 1) + simRead(c.x, c.y, c.z - 1, 1) + simRead(c.x, c.y, c.z + 1, 1) -
             6.0 * b;
    let reaction = a * b * b;
    dst[base] = clamp(a + (sim.params1.z * la - reaction + sim.params1.x * (1.0 - a)) * dt, 0.0, 1.0);
    dst[base + 1u] =
        clamp(b + (sim.params1.w * lb + reaction - (sim.params1.y + sim.params1.x) * b) * dt, 0.0, 1.0);
}
