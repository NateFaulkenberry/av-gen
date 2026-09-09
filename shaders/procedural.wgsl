// Procedural geometry (ADR-023): one source mesh drawn `instanceCount` times; the vertex stage
// reads its InstanceRecord from a storage buffer by instance_index, runs the deformer stack and
// recomputes the normal by finite differences of the whole chain; the fragment stage is the
// shared PBR shading (pbr_shade.wgsl) with the per-instance colour/emissive multipliers.
//
// Per vertex (p = source position in object space, all deformers in stack order):
//   p = deformLocal(p)           local-space deformers (kind < 8)
//   p = instance(p)              position + rotate(quaternion, p * scale)
//   p = object.model * p         the object/parent matrix (identity in this phase)
//   p = deformWorld(p)           world-space deformers (kind >= 8)
// The normal is cross(chain(p + eps t1) - chain(p), chain(p + eps t2) - chain(p)) for a tangent
// basis (t1, t2) of the source normal, flipped into the hemisphere of the transformed source
// normal; eps = timeInfo.z (1e-3 x source bounds radius, set by the renderer).
//
// Bind groups: 0 frame (common.wgsl), 1 = {0 ObjectUniforms (dynamic offset; model = object
// matrix, material fields), 1 instances (read-only storage), 2 ProceduralUniforms}, 2 material,
// 3 IBL (both declared in pbr_shade.wgsl). Mirrors rendering/procedural_renderer.hpp.
#include "common.wgsl"
#include "pbr_shade.wgsl"

struct InstanceRecord {
    position: vec4<f32>,  // xyz, w = uniform scale hint (unused here)
    rotation: vec4<f32>,  // unit quaternion (x, y, z, w)
    scale: vec4<f32>,     // xyz, w = normalised index
    random: vec4<f32>,    // four hashed randoms in [0, 1)
    color: vec4<f32>,     // rgb = base colour multiplier, a = instance id
    emissive: vec4<f32>,  // rgb = emissive multiplier
};

// One deformer slot (64 bytes). Kinds: 0 bend, 1 twist, 2 sine, 3 noise, 4 displacement;
// + 8 when the deformer acts in world space; < 0 = disabled slot.
struct DeformerUniform {
    axisKind: vec4<f32>,     // xyz = unit axis, w = kind code (see above)
    centerAmount: vec4<f32>, // xyz = center, w = amount
    params: vec4<f32>,       // x = frequency (sine) | spatial scale (noise, displacement), y = speed, z = phase, w = falloff
    extra: vec4<f32>,        // xyz = displacement axis (sine) | bend direction (bend) | axis mask (noise); w = bitcast<u32> seed
};

struct ProceduralUniforms {
    timeInfo: vec4<f32>,  // x = render time, y = deformer count, z = normal epsilon, w = instance count
    deformers: array<DeformerUniform, 8>,
};

@group(1) @binding(1) var<storage, read> instances: array<InstanceRecord>;
@group(1) @binding(2) var<uniform> proc: ProceduralUniforms;

const DEFORM_BEND: i32 = 0;
const DEFORM_TWIST: i32 = 1;
const DEFORM_SINE: i32 = 2;
const DEFORM_NOISE: i32 = 3;
const DEFORM_DISPLACEMENT: i32 = 4;
const DEFORM_WORLD: i32 = 8;

// ---- hashing / noise (reference implementation; scene::fbm3 on the CPU must match) ----------

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

// ---- transforms ------------------------------------------------------------------------------

fn quatRotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Rodrigues rotation of p about the axis through c. angle 0 returns p exactly.
fn rotateAbout(p: vec3<f32>, c: vec3<f32>, axis: vec3<f32>, angle: f32) -> vec3<f32> {
    let q = p - c;
    let s = sin(angle);
    let co = cos(angle);
    return c + q * co + cross(axis, q) * s + axis * (dot(axis, q) * (1.0 - co));
}

// Any unit vector perpendicular to the unit vector a.
fn perpendicularTo(a: vec3<f32>) -> vec3<f32> {
    var helper = vec3<f32>(0.0, 1.0, 0.0);
    if (abs(a.y) > 0.9) {
        helper = vec3<f32>(1.0, 0.0, 0.0);
    }
    return normalize(cross(helper, a));
}

// ---- deformers -------------------------------------------------------------------------------
// Semantics (scene/procedural.hpp): axis is unit, c = center in the deformer's space, a = amount,
// t = render time. falloff > 0 ramps the effect 0..1 with |dot(p - c, axis)| / falloff for
// twist, sine, noise and displacement; for bend it clamps the bent extent to +-falloff.

// Classic Barr bend: the coordinate y along `axis` maps to an arc of radius R = 1/k (k = amount,
// radians per unit) that bends towards `dir` (extra.xyz projected perpendicular to axis, +X by
// default): x' = R - (R - x) cos(theta), y' = (R - x) sin(theta), theta = k * y; the component
// along cross(axis, dir) is unchanged. Beyond +-falloff (when falloff > 0) the remainder of y
// continues rigidly along the end tangent. |k| < 1e-6 is the identity. The arc terms are
// evaluated as x cos(theta) + 2 sin^2(theta/2) / k and sin(theta) / k - x sin(theta), which are
// the same expressions without the catastrophic cancellation of R - R cos(theta) for small k.
fn bendPoint(p: vec3<f32>, c: vec3<f32>, axis: vec3<f32>, k: f32, dirIn: vec3<f32>, falloff: f32) -> vec3<f32> {
    if (abs(k) < 1e-6) {
        return p;
    }
    var dir = dirIn - axis * dot(dirIn, axis);
    if (dot(dir, dir) < 1e-12) {
        dir = perpendicularTo(axis);
    } else {
        dir = normalize(dir);
    }
    let b = cross(axis, dir);
    let rel = p - c;
    let x = dot(rel, dir);
    let y = dot(rel, axis);
    let z = dot(rel, b);
    var yb = y;
    if (falloff > 0.0) {
        yb = clamp(y, -falloff, falloff);
    }
    let theta = k * yb;
    let st = sin(theta);
    let ct = cos(theta);
    let sh = sin(theta * 0.5);
    let rest = y - yb;
    let xp = x * ct + 2.0 * sh * sh / k + rest * st;
    let yp = st / k - x * st + rest * ct;
    return c + dir * xp + axis * yp + b * z;
}

fn falloffRamp(y: f32, falloff: f32) -> f32 {
    if (falloff > 0.0) {
        return clamp(abs(y) / falloff, 0.0, 1.0);
    }
    return 1.0;
}

// Applies one deformer (kind without the world flag) to p; n is the source normal expressed in
// the deformer's space (used by displacement only).
fn applyDeformer(d: DeformerUniform, kind: i32, p: vec3<f32>, n: vec3<f32>, t: f32) -> vec3<f32> {
    let axis = d.axisKind.xyz;
    let c = d.centerAmount.xyz;
    let amount = d.centerAmount.w;
    let falloff = d.params.w;
    let speed = d.params.y;
    let phase = d.params.z;
    let y = dot(p - c, axis);
    if (kind == DEFORM_BEND) {
        return bendPoint(p, c, axis, amount, d.extra.xyz, falloff);
    }
    let ramp = falloffRamp(y, falloff);
    if (kind == DEFORM_TWIST) {
        // rotation about axis by amount * distance along the axis (+ speed * t + phase)
        let angle = amount * y * ramp + speed * t + phase;
        return rotateAbout(p, c, axis, angle);
    }
    if (kind == DEFORM_SINE) {
        let arg = y * d.params.x + phase + speed * t;
        return p + d.extra.xyz * (amount * ramp * sin(arg));
    }
    let seed = bitcast<u32>(d.extra.w);
    let q = p * d.params.x + vec3<f32>(t * speed);
    if (kind == DEFORM_NOISE) {
        // three decorrelated channels: the input offset per axis; the seed selects the hash lattice
        let nz = vec3<f32>(fbm3(q, seed), fbm3(q + vec3<f32>(31.7), seed), fbm3(q + vec3<f32>(67.3), seed)) * 2.0 -
                 vec3<f32>(1.0);
        return p + nz * d.extra.xyz * (amount * ramp);
    }
    if (kind == DEFORM_DISPLACEMENT) {
        return p + n * (amount * ramp * (fbm3(q, seed) * 2.0 - 1.0));
    }
    return p;
}

fn deformerCode(d: DeformerUniform) -> i32 {
    return i32(floor(d.axisKind.w + 0.5)); // -1 stays -1 (disabled)
}

// The whole chain for one source point: local deformers, instance, object, world deformers.
fn deformChain(pIn: vec3<f32>, nLocal: vec3<f32>, nWorld: vec3<f32>, inst: InstanceRecord) -> vec3<f32> {
    let count = u32(proc.timeInfo.y + 0.5);
    let t = proc.timeInfo.x;
    var p = pIn;
    for (var i = 0u; i < 8u; i = i + 1u) {
        if (i >= count) { break; }
        let d = proc.deformers[i];
        let code = deformerCode(d);
        if (code < 0 || code >= DEFORM_WORLD) { continue; }
        p = applyDeformer(d, code, p, nLocal, t);
    }
    p = inst.position.xyz + quatRotate(inst.rotation, p * inst.scale.xyz);
    p = (object.model * vec4<f32>(p, 1.0)).xyz;
    for (var i = 0u; i < 8u; i = i + 1u) {
        if (i >= count) { break; }
        let d = proc.deformers[i];
        let code = deformerCode(d);
        if (code < DEFORM_WORLD) { continue; }
        p = applyDeformer(d, code - DEFORM_WORLD, p, nWorld, t);
    }
    return p;
}

// ---- vertex / fragment -------------------------------------------------------------------------

struct ProcVertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) colorMul: vec3<f32>,
    @location(4) emissiveMul: vec3<f32>,
};

@vertex
fn vs_proc(in: VertexIn, @builtin(instance_index) instanceIndex: u32) -> ProcVertexOut {
    let inst = instances[instanceIndex];
    let n = normalize(in.normal);
    // Tangent basis around the source normal: cross(t1, t2) == n.
    let t1 = perpendicularTo(n);
    let t2 = cross(n, t1);
    // The source normal carried through instance scale/rotation and the object's normal matrix:
    // the reference hemisphere for the finite-difference normal and the direction world-space
    // displacement pushes along.
    let s = inst.scale.xyz;
    let safeScale = select(s, vec3<f32>(1.0), abs(s) < vec3<f32>(1e-8));
    let nInst = quatRotate(inst.rotation, n / safeScale);
    let nRef = normalize((object.normalMatrix * vec4<f32>(nInst, 0.0)).xyz);

    let eps = proc.timeInfo.z;
    let p0 = deformChain(in.position, n, nRef, inst);
    let p1 = deformChain(in.position + t1 * eps, n, nRef, inst);
    let p2 = deformChain(in.position + t2 * eps, n, nRef, inst);
    var nw = cross(p1 - p0, p2 - p0);
    if (dot(nw, nw) < 1e-30) {
        nw = nRef;
    } else {
        nw = normalize(nw);
        if (dot(nw, nRef) < 0.0) {
            nw = -nw;
        }
    }

    var out: ProcVertexOut;
    out.clip = frame.viewProj * vec4<f32>(p0, 1.0);
    out.worldPos = p0;
    out.normal = nw;
    out.uv = in.uv;
    out.colorMul = inst.color.rgb;
    out.emissiveMul = inst.emissive.rgb;
    return out;
}

@fragment
fn fs_proc(in: ProcVertexOut, @builtin(front_facing) frontFacing: bool) -> @location(0) vec4<f32> {
    return shadePbr(in.worldPos, in.normal, in.uv, frontFacing, in.colorMul, in.emissiveMul);
}
