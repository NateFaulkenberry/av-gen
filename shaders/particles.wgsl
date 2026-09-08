// GPU particle system (ADR-015): fixed pool with a dead list and a per-frame alive list.
// Passes per system per frame: cs_reset (indirect args) -> cs_emit (pop dead slots, initialise)
// -> cs_simulate (integrate, kill, append alive) -> indirect draw of camera-facing quads.
// Randomness is a hash of (seed, frameIndex, slot): deterministic for a given frame sequence.

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
    counts: vec4<u32>,      // emitCount, capacity, blend (0 additive, 1 alpha), pad
};

struct Counters {
    deadCount: atomic<u32>,
    pad0: u32,
    pad1: u32,
    pad2: u32,
};

struct Indirect {
    vertexCount: u32,
    instanceCount: atomic<u32>,
    firstVertex: u32,
    firstInstance: u32,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read_write> deadList: array<u32>;
@group(0) @binding(3) var<storage, read_write> counters: Counters;
@group(0) @binding(4) var<storage, read_write> aliveList: array<u32>;
@group(0) @binding(5) var<storage, read_write> indirect: Indirect;
// Read-only views for the render stage (same bindings, used only by vs_particle).
@group(0) @binding(1) var<storage, read> particlesRead: array<Particle>;
@group(0) @binding(4) var<storage, read> aliveRead: array<u32>;

// ---- hashing / noise ----------------------------------------------------------------------

fn pcg3d(vIn: vec3<u32>) -> vec3<u32> {
    var v = vIn * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> vec3<u32>(16u);
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

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

fn valueNoise(p: vec3<f32>) -> f32 {
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
    return vec3<f32>(valueNoise(p), valueNoise(p + vec3<f32>(31.4, 47.1, 12.9)), valueNoise(p + vec3<f32>(-17.2, 5.3, 29.8))) - vec3<f32>(0.5);
}

fn curlNoise(p: vec3<f32>) -> vec3<f32> {
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

// ---- compute passes ----------------------------------------------------------------------

@compute @workgroup_size(1)
fn cs_reset() {
    indirect.vertexCount = 6u;
    atomicStore(&indirect.instanceCount, 0u);
    indirect.firstVertex = 0u;
    indirect.firstInstance = 0u;
}

@compute @workgroup_size(64)
fn cs_emit(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= params.counts.x) { return; }
    if (atomicLoad(&counters.deadCount) == 0u) { return; }
    let old = atomicSub(&counters.deadCount, 1u);
    if (old == 0u) { atomicAdd(&counters.deadCount, 1u); return; }
    let slot = deadList[old - 1u];
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
    p.life = mix(params.speedLife.z, params.speedLife.w, r3.x);
    p.seed = r3.y;
    p.size = mix(0.7, 1.3, r3.z);
    particles[slot] = p;
}

@compute @workgroup_size(64)
fn cs_simulate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let slot = gid.x;
    if (slot >= params.counts.y) { return; }
    var p = particles[slot];
    if (p.life <= 0.0) { return; }
    let dt = params.sim.x;
    p.age += dt;
    if (p.age >= p.life) {
        p.life = 0.0;
        particles[slot] = p;
        let idx = atomicAdd(&counters.deadCount, 1u);
        if (idx < params.counts.y) { deadList[idx] = slot; }
        return;
    }
    // forces
    var force = params.gravity.xyz;
    let turbStrength = params.gravity.w;
    if (turbStrength > 0.0) {
        let np = p.position * params.turb.x + vec3<f32>(0.0, 0.0, params.sim.y * params.turb.y);
        force += curlNoise(np) * turbStrength;
    }
    let toA = params.attractor.xyz - p.position;
    let dist = length(toA) + 1e-4;
    let falloff = clamp(1.0 - dist / max(params.attractor2.x, 1e-3), 0.0, 1.0);
    let dirA = toA / dist;
    force += dirA * params.attractor.w * falloff;
    let tangent = cross(vec3<f32>(0.0, 1.0, 0.0), dirA);
    force += tangent * params.attractor2.y * falloff;
    p.velocity += force * dt;
    p.velocity *= max(0.0, 1.0 - params.direction.w * dt);
    p.position += p.velocity * dt;
    particles[slot] = p;
    let out = atomicAdd(&indirect.instanceCount, 1u);
    if (out < params.counts.y) { aliveList[out] = slot; }
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
