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
// diffusion sweeps started from (read-only; unused by the other kernels), 4 = THIS STEP's field
// block (dynamic offset: packed at the step's own second, ADR-1119), 5 = the agents (ADR-1120),
// 6 = the agents' u32 fixed-point deposits, one per float of the table (ADR-1120), 15 = the grid table
// (declared by fields.wgsl; a velocity or injection field may itself be a Grid field, which then reads
// the previous frame's state). Mirrors rendering/simulation.hpp.
//
// ADR-1120, agents: the one non-gather kernel. Deposits are atomicAdd on u32 fixed point (1/4096), so
// their order cannot change the sum and the step stays bit-exact run to run. Sensing reads only the
// previous step's trail; randomness is a hash of (agent, step); the step index is the field block's
// pad0.
#include "fields.wgsl"

struct SimUniforms {
    res: vec4<u32>,       // resolution.xyz, components per cell
    layout0: vec4<u32>,   // grid offset in floats, cell count, wrap (0 clamp, 1 wrap), pad
    bounds0: vec4<f32>,   // boundsMin.xyz, dt (seconds)
    bounds1: vec4<f32>,   // boundsMax.xyz, time (seconds)
    params0: vec4<f32>,   // injectRate, advect, diffusion, dissipation
    params1: vec4<f32>,   // feed, kill, diffusionA, diffusionB
    slots: vec4<i32>,     // injection field slot, velocity field slot, mode, deposit field slot
    agents: vec4<u32>,    // ADR-1120: count, species, offset (agents), seed
    agentSense: vec4<f32>,   // sensorAngle, sensorDistance (cells), turnAngle, stepSize (cells)
    agentDeposit: vec4<f32>, // depositAmount, repel, fixed-point scale, w = a scalar grid's ceiling (ADR-1163; 0 = none)
};

const SIM_MODE_SCALAR: i32 = 0;
const SIM_MODE_VECTOR: i32 = 1;
const SIM_MODE_REACTION: i32 = 2;
const SIM_MODE_AGENTS: i32 = 3;
const SIM_TWO_PI: f32 = 6.283185307179586;

@group(0) @binding(0) var<uniform> sim: SimUniforms;
@group(0) @binding(1) var<storage, read_write> dst: array<f32>;
@group(0) @binding(2) var<storage, read> src: array<f32>;
@group(0) @binding(3) var<storage, read> initial: array<f32>;
@group(0) @binding(4) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(5) var<storage, read_write> agents: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read_write> deposits: array<atomic<u32>>;

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
    var injected = dst[base + channel] + amount * max(0.0, fieldScalar(slot, p));
    // ADR-1163: a scalar grid saturates at its ceiling (0 = unbounded).
    if (sim.slots.z == SIM_MODE_SCALAR && sim.agentDeposit.w > 0.0) {
        injected = min(injected, sim.agentDeposit.w);
    }
    dst[base + channel] = injected;
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

// ---- agents (ADR-1120) ---------------------------------------------------------------------------
// An agents grid is a plane (res.y = 1); an agent is (x, z in cells, heading in radians, species).

fn simMix(v: u32) -> u32 {
    var x = v;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}

fn simHash(a: u32, b: u32, c: u32) -> u32 {
    return simMix((a * 0x8da6b343u) ^ simMix((b * 0xd8163841u) ^ simMix(c)));
}

fn simUnit(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

// Wraps a continuous cell coordinate into [0, n) (wrap grids) or clamps it (clamp grids).
fn agentWrap(x: f32, n: f32) -> f32 {
    if (sim.layout0.z == 1u) {
        return x - floor(x / n) * n;
    }
    return clamp(x, 0.0, n - 0.001);
}

// What an agent of `species` smells at (x, z): its own trail, less `repel` of the others'.
fn agentSense(x: f32, z: f32, species: u32) -> f32 {
    let nx = f32(sim.res.x);
    let nz = f32(sim.res.z);
    let i = i32(floor(agentWrap(x, nx)));
    let k = i32(floor(agentWrap(z, nz)));
    let base = simBase(vec3<i32>(i, 0, k));
    var others = 0.0;
    for (var c = 0u; c < 3u; c = c + 1u) {
        if (c < sim.agents.y && c != species) {
            others = others + src[base + c];
        }
    }
    return src[base + species] - sim.agentDeposit.y * others;
}

fn agentWorld(x: f32, z: f32) -> vec3<f32> {
    let extent = sim.bounds1.xyz - sim.bounds0.xyz;
    return vec3<f32>(sim.bounds0.x + x / f32(sim.res.x) * extent.x, sim.bounds0.y + 0.5 * extent.y,
                     sim.bounds0.z + z / f32(sim.res.z) * extent.z);
}

// Seeds every agent from a hash of its index and the grid's seed: anywhere on the plane, any heading.
@compute @workgroup_size(64)
fn cs_agents_init(@builtin(global_invocation_id) gid: vec3<u32>) {
    let a = gid.x;
    if (a >= sim.agents.x) {
        return;
    }
    let seed = sim.agents.w;
    let x = simUnit(simHash(a, seed, 1u)) * f32(sim.res.x);
    let z = simUnit(simHash(a, seed, 2u)) * f32(sim.res.z);
    let heading = simUnit(simHash(a, seed, 3u)) * SIM_TWO_PI;
    agents[sim.agents.z + a] = vec4<f32>(x, z, heading, f32(a % max(sim.agents.y, 1u)));
}

// Sense, turn, move, deposit (Jones 2010's rule, three species that attract their own and avoid the others).
@compute @workgroup_size(64)
fn cs_agents_move(@builtin(global_invocation_id) gid: vec3<u32>) {
    let a = gid.x;
    if (a >= sim.agents.x) {
        return;
    }
    let index = sim.agents.z + a;
    let agent = agents[index];
    var x = agent.x;
    var z = agent.y;
    var h = agent.z;
    let species = u32(agent.w + 0.5);
    let sa = sim.agentSense.x;
    let sd = sim.agentSense.y;
    let turn = sim.agentSense.z;
    let front = agentSense(x + cos(h) * sd, z + sin(h) * sd, species);
    let left = agentSense(x + cos(h + sa) * sd, z + sin(h + sa) * sd, species);
    let right = agentSense(x + cos(h - sa) * sd, z + sin(h - sa) * sd, species);
    let step = fieldBlock.pad0;
    let r = simUnit(simHash(a, step, sim.agents.w));
    if (front >= left && front >= right) {
        // straight on
    } else if (left > right) {
        h = h + turn;
    } else if (right > left) {
        h = h - turn;
    } else {
        h = h + select(-turn, turn, r < 0.5);
    }
    let dt = sim.bounds0.w;
    // A vector field steers: the heading turns towards its direction by advect * dt, at most `turn`.
    let velocitySlot = sim.slots.y;
    if (velocitySlot >= 0 && sim.params0.y != 0.0) {
        let v = fieldVector(velocitySlot, agentWorld(x, z));
        let side = cos(h) * v.z - sin(h) * v.x;
        h = h + clamp(side * sim.params0.y * dt, -turn, turn);
    }
    h = h - floor(h / SIM_TWO_PI) * SIM_TWO_PI;
    let nx = f32(sim.res.x);
    let nz = f32(sim.res.z);
    var nxp = x + cos(h) * sim.agentSense.w;
    var nzp = z + sin(h) * sim.agentSense.w;
    if (sim.layout0.z != 1u && (nxp < 0.0 || nxp >= nx || nzp < 0.0 || nzp >= nz)) {
        h = h + 3.14159265; // clamp grids: turn back at the edge
        h = h - floor(h / SIM_TWO_PI) * SIM_TWO_PI;
    }
    x = agentWrap(nxp, nx);
    z = agentWrap(nzp, nz);
    // The deposit, scaled by the deposit field (an audio field, typically) heard in this species' band.
    var amount = sim.agentDeposit.x;
    let depositSlot = sim.slots.w;
    if (depositSlot >= 0) {
        fieldElement = (f32(species) + 0.5) / 3.0;
        amount = amount * max(0.0, fieldScalar(depositSlot, agentWorld(x, z)));
    }
    let q = u32(clamp(amount, 0.0, 64.0) * sim.agentDeposit.z + 0.5);
    let base = simBase(vec3<i32>(i32(floor(x)), 0, i32(floor(z))));
    atomicAdd(&deposits[base + species], q);
    agents[index] = vec4<f32>(x, z, h, agent.w);
}

// The trail: blur by `diffusion` (3 x 3 in the plane), fade by `dissipation`, add this step's deposits,
// clear them; channel 3 is the sum of the species' trails.
@compute @workgroup_size(64)
fn cs_agents_resolve(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= simCells()) {
        return;
    }
    let c = simCoords(i);
    let base = simBase(c);
    let keep = max(0.0, 1.0 - sim.params0.w * sim.bounds0.w);
    var total = 0.0;
    for (var ch = 0; ch < 3; ch = ch + 1) {
        var v = 0.0;
        if (u32(ch) < sim.agents.y) {
            var blur = 0.0;
            for (var dz = -1; dz <= 1; dz = dz + 1) {
                for (var dx = -1; dx <= 1; dx = dx + 1) {
                    blur = blur + simRead(c.x + dx, 0, c.z + dz, ch);
                }
            }
            blur = blur / 9.0;
            let here = simRead(c.x, 0, c.z, ch);
            let deposited = f32(atomicLoad(&deposits[base + u32(ch)])) / sim.agentDeposit.z;
            v = (here + (blur - here) * sim.params0.z) * keep + deposited;
            atomicStore(&deposits[base + u32(ch)], 0u);
        }
        dst[base + u32(ch)] = v;
        total = total + v;
    }
    dst[base + 3u] = total;
}
