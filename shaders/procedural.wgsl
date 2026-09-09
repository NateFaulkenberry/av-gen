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
// Fields (ADR-025, fields.wgsl): the Field deformer (kind 5) samples slot params.x at the
// vertex's current world position and displaces in the deformer's space (vector fields: v *
// amount, rotated into object space for local deformers; scalar fields: along the normal or the
// axis by s * amount). fieldInfo.x/y multiply the emission by 1 + amount * fieldScalar(slot,
// worldPos) in the fragment. Point sources (fieldInfo.z) are camera-facing quads: the centre is
// the instance origin through the object matrix and the world deformers, the quad spans the
// camera right/up axes scaled by the instance scale, the normal faces the camera.
//
// Splines (ADR-026, spline.wgsl): the Path deformer (kind 6, local space only) is a curve
// deform: the object-space coordinate along the deformer axis (from its centre) maps to arc
// length d = pathOffset + coord * pathScale on spline slot params.x, and the perpendicular
// components (u along cross(up, axis), v along cross(axis, u)) are placed in the spline frame:
// p' = S(d).position + (binormal * u + normal * v) * S.scale after rotating (u, v) by pathRoll;
// result = mix(p, p', amount). The renderer resolves pathScale to units per object unit (the
// "fit" mode divides the spline length by the source extent along the axis).
//
// Culling and LOD (ADR-029, cull.wgsl): when fieldInfo.w is 1 the draw is indirect and reads its
// instance through visibleIndices[instance_index] (binding 5), the compacted list of the LOD level
// being drawn; the ProceduralUniforms slot is per level, so LOD2/LOD3 billboards set fieldInfo.z
// and take the same camera-facing Point path as Point sources. fieldInfo.w = 0 keeps the original
// path byte for byte.
//
// Bind groups: 0 frame (common.wgsl), 1 = {0 ObjectUniforms (dynamic offset; model = object
// matrix, material fields), 1 instances (read-only storage; the live buffer when the object has
// effectors), 2 ProceduralUniforms, 3 FieldBlock, 4 SplineTable, 5 visible list}, 2 material,
// 3 IBL (both declared in pbr_shade.wgsl). Mirrors rendering/procedural_renderer.hpp.
#include "common.wgsl"
#include "pbr_shade.wgsl"
#include "fields.wgsl"
#include "spline.wgsl"

struct InstanceRecord {
    position: vec4<f32>,  // xyz, w = density
    rotation: vec4<f32>,  // unit quaternion (x, y, z, w)
    scale: vec4<f32>,     // xyz, w = normalised index
    random: vec4<f32>,    // four hashed randoms in [0, 1)
    color: vec4<f32>,     // rgb = base colour multiplier, a = instance id
    emissive: vec4<f32>,  // rgb = emissive multiplier, a = extra lane
};

// One deformer slot (64 bytes). Kinds: 0 bend, 1 twist, 2 sine, 3 noise, 4 displacement,
// 5 field, 6 path; + 8 when the deformer acts in world space; < 0 = disabled slot.
struct DeformerUniform {
    axisKind: vec4<f32>,     // xyz = unit axis, w = kind code (see above)
    centerAmount: vec4<f32>, // xyz = center, w = amount
    params: vec4<f32>,       // x = frequency (sine) | spatial scale (noise, displacement) | field slot (field, -1 none) | spline slot (path), y = speed | alongNormal (field) | pathScale (path), z = phase | pathOffset (path), w = falloff | pathRoll (path)
    extra: vec4<f32>,        // xyz = displacement axis (sine) | bend direction (bend) | axis mask (noise); w = bitcast<u32> seed
};

struct ProceduralUniforms {
    timeInfo: vec4<f32>,  // x = render time, y = deformer count, z = normal epsilon, w = instance count
    fieldInfo: vec4<f32>, // x = emissive field slot (-1 none), y = emissive field amount, z = point source or LOD billboard (1/0), w = indirection enabled (1/0)
    prevInfo: vec4<f32>,  // x = last frame's render time (ADR-035 velocity: deformation motion), yzw = 0
    // Step 1 of the chain: the source mesh's own placement, applied to the vertex before any
    // deformer runs. Mirrors ProceduralGeometry::instanceMatrix()'s trailing sourceTransform.
    sourceMatrix: mat4x4<f32>,
    sourceNormalMatrix: mat4x4<f32>,
    deformers: array<DeformerUniform, 8>,
};

@group(1) @binding(1) var<storage, read> instances: array<InstanceRecord>;
@group(1) @binding(2) var<uniform> proc: ProceduralUniforms;
@group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
@group(1) @binding(4) var<storage, read> splineTable: SplineTable;
// Culling / LOD (ADR-029, cull.wgsl): the compacted visible list of this draw's LOD level, bound
// at the level's slice of the object's visible buffer. Read-only storage is allowed in a vertex
// stage. When fieldInfo.w is 0 the object is not culled, this binding is an inert placeholder and
// instance_index addresses `instances` directly - exactly the pre-culling path.
@group(1) @binding(5) var<storage, read> visibleIndices: array<u32>;

const DEFORM_BEND: i32 = 0;
const DEFORM_TWIST: i32 = 1;
const DEFORM_SINE: i32 = 2;
const DEFORM_NOISE: i32 = 3;
const DEFORM_DISPLACEMENT: i32 = 4;
const DEFORM_FIELD: i32 = 5;
const DEFORM_PATH: i32 = 6;
const DEFORM_WORLD: i32 = 8;

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

// The instance transform of an object-space point.
fn instancePoint(inst: InstanceRecord, p: vec3<f32>) -> vec3<f32> {
    return inst.position.xyz + quatRotate(inst.rotation, p * inst.scale.xyz);
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
// the deformer's space (used by displacement only). Field deformers are handled by the chain.
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

// Field deformer on a world-space point: samples slot params.x at p and displaces p (vector
// fields by v * amount; scalar fields along the world normal or the axis by s * amount).
fn applyWorldFieldDeformer(d: DeformerUniform, p: vec3<f32>, nWorld: vec3<f32>) -> vec3<f32> {
    let slot = i32(floor(d.params.x + 0.5));
    if (slot < 0) {
        return p;
    }
    let amount = d.centerAmount.w;
    if (fieldTypeOf(slot) == FIELD_TYPE_VECTOR) {
        return p + fieldVector(slot, p) * amount;
    }
    let s = fieldScalar(slot, p);
    var dir = d.axisKind.xyz;
    if (d.params.y > 0.5) {
        dir = nWorld;
    }
    return p + dir * (s * amount);
}

// Field deformer on an object-space point: samples at the point's current world position and
// brings vector results back through the inverse of the object's linear part (the transpose of
// its normal matrix) and the instance rotation and scale.
fn applyLocalFieldDeformer(d: DeformerUniform, p: vec3<f32>, nLocal: vec3<f32>, inst: InstanceRecord) -> vec3<f32> {
    let slot = i32(floor(d.params.x + 0.5));
    if (slot < 0) {
        return p;
    }
    let amount = d.centerAmount.w;
    let pw = (object.model * vec4<f32>(instancePoint(inst, p), 1.0)).xyz;
    if (fieldTypeOf(slot) == FIELD_TYPE_VECTOR) {
        let s = inst.scale.xyz;
        let safeScale = select(s, vec3<f32>(1.0), abs(s) < vec3<f32>(1e-8));
        let nm = object.normalMatrix;
        let objInv = transpose(mat3x3<f32>(nm[0].xyz, nm[1].xyz, nm[2].xyz));
        let conj = vec4<f32>(-inst.rotation.xyz, inst.rotation.w);
        let vObj = objInv * fieldVector(slot, pw);
        return p + (quatRotate(conj, vObj) / safeScale) * amount;
    }
    let sv = fieldScalar(slot, pw);
    var dir = d.axisKind.xyz;
    if (d.params.y > 0.5) {
        dir = nLocal;
    }
    return p + dir * (sv * amount);
}

// Path deformer (object space): see the header comment. Mirrors scene::applyPathDeformer.
fn applyPathDeformer(d: DeformerUniform, p: vec3<f32>) -> vec3<f32> {
    let slot = i32(floor(d.params.x + 0.5));
    if (slot < 0) {
        return p;
    }
    let axis = d.axisKind.xyz;
    var up = vec3<f32>(0.0, 1.0, 0.0);
    if (abs(axis.y) > 0.999) {
        up = vec3<f32>(1.0, 0.0, 0.0);
    }
    let u = normalize(cross(up, axis));
    let v = cross(axis, u);
    let q = p - d.centerAmount.xyz;
    let coord = dot(q, axis);
    let cu = dot(q, u);
    let cv = dot(q, v);
    let s = splineSample(slot, d.params.z + coord * d.params.y);
    let cr = cos(d.params.w);
    let sr = sin(d.params.w);
    let ru = cu * cr - cv * sr;
    let rv = cu * sr + cv * cr;
    let bent = s.position + (s.binormal * ru + s.normal * rv) * s.scale;
    return mix(p, bent, d.centerAmount.w);
}

fn deformerCode(d: DeformerUniform) -> i32 {
    return i32(floor(d.axisKind.w + 0.5)); // -1 stays -1 (disabled)
}

// The world-space deformers applied to a world-space point (the second half of the chain).
fn deformWorld(pIn: vec3<f32>, nWorld: vec3<f32>, t: f32) -> vec3<f32> {
    let count = u32(proc.timeInfo.y + 0.5);
    var p = pIn;
    for (var i = 0u; i < 8u; i = i + 1u) {
        if (i >= count) { break; }
        let d = proc.deformers[i];
        let code = deformerCode(d);
        if (code < DEFORM_WORLD) { continue; }
        if (code - DEFORM_WORLD == DEFORM_FIELD) {
            p = applyWorldFieldDeformer(d, p, nWorld);
        } else {
            p = applyDeformer(d, code - DEFORM_WORLD, p, nWorld, t);
        }
    }
    return p;
}

// The whole chain for one source point: local deformers, instance, object, world deformers.
fn deformChain(pIn: vec3<f32>, nLocal: vec3<f32>, nWorld: vec3<f32>, inst: InstanceRecord, t: f32,
               model: mat4x4<f32>) -> vec3<f32> {
    let count = u32(proc.timeInfo.y + 0.5);
    var p = pIn;
    for (var i = 0u; i < 8u; i = i + 1u) {
        if (i >= count) { break; }
        let d = proc.deformers[i];
        let code = deformerCode(d);
        if (code < 0 || code >= DEFORM_WORLD) { continue; }
        if (code == DEFORM_FIELD) {
            p = applyLocalFieldDeformer(d, p, nLocal, inst);
        } else if (code == DEFORM_PATH) {
            p = applyPathDeformer(d, p);
        } else {
            p = applyDeformer(d, code, p, nLocal, t);
        }
    }
    p = instancePoint(inst, p);
    p = (model * vec4<f32>(p, 1.0)).xyz;
    return deformWorld(p, nWorld, t);
}

// ---- vertex / fragment -------------------------------------------------------------------------

struct ProcVertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) instColor: vec4<f32>,    // rgb = base colour multiplier, a = instance id
    @location(4) instEmissive: vec4<f32>, // rgb = emissive multiplier, a = extra lane
    // ADR-030 material inputs: the source position before the deformer stack and the instance record.
    @location(5) localPos: vec3<f32>,
    @location(6) instRandom: vec4<f32>,
    @location(7) instIndex: f32,          // normalised instance index in [0, 1]
    @location(8) prevClip: vec4<f32>,     // last frame's clip position, for the velocity target
};

@vertex
fn vs_proc(in: VertexIn, @builtin(instance_index) instanceIndex: u32) -> ProcVertexOut {
    // Indirection is per draw (uniform across the whole draw), so both paths stay coherent.
    var recordIndex = instanceIndex;
    if (proc.fieldInfo.w > 0.5) { recordIndex = visibleIndices[instanceIndex]; }
    let inst = instances[recordIndex];
    var out: ProcVertexOut;
    out.uv = in.uv;
    out.instColor = inst.color;
    out.instEmissive = inst.emissive;
    out.localPos = in.position;
    out.instRandom = inst.random;
    out.instIndex = inst.scale.w;

    if (proc.fieldInfo.z > 0.5) {
        // Point source: a camera-facing quad around the instance centre. The centre goes through
        // the object matrix and the world deformers; the quad offsets skip the deformer stack.
        let srcOffset = (proc.sourceMatrix * vec4<f32>(0.0, 0.0, 0.0, 1.0)).xyz;
        var c = (object.model * vec4<f32>(instancePoint(inst, srcOffset), 1.0)).xyz;
        let toCamera = normalize(frame.cameraPos.xyz - c + vec3<f32>(0.0, 0.0, 1e-6));
        c = deformWorld(c, toCamera, proc.timeInfo.x);
        let right = frame.cameraRight.xyz;
        let up = frame.cameraUp.xyz;
        let p = c + right * (in.position.x * inst.scale.x) + up * (in.position.y * inst.scale.y);
        out.clip = frame.viewProj * vec4<f32>(p, 1.0);
        out.worldPos = p;
        out.normal = normalize(frame.cameraPos.xyz - c + vec3<f32>(0.0, 0.0, 1e-6));
        var cPrev = (object.prevModel * vec4<f32>(instancePoint(inst, srcOffset), 1.0)).xyz;
        cPrev = deformWorld(cPrev, toCamera, proc.prevInfo.x);
        let pPrev = cPrev + right * (in.position.x * inst.scale.x) + up * (in.position.y * inst.scale.y);
        out.prevClip = frame.prevViewProj * vec4<f32>(pPrev, 1.0);
        return out;
    }

    // The source transform is step 1 of the chain: the vertex and its normal enter the deformer
    // stack already placed.
    let srcPos = (proc.sourceMatrix * vec4<f32>(in.position, 1.0)).xyz;
    let srcNormal = (proc.sourceNormalMatrix * vec4<f32>(in.normal, 0.0)).xyz;
    let n = normalize(select(srcNormal, in.normal, dot(srcNormal, srcNormal) < 1e-20));
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
    let now = proc.timeInfo.x;
    let p0 = deformChain(srcPos, n, nRef, inst, now, object.model);
    let p1 = deformChain(srcPos + t1 * eps, n, nRef, inst, now, object.model);
    let p2 = deformChain(srcPos + t2 * eps, n, nRef, inst, now, object.model);
    var nw = cross(p1 - p0, p2 - p0);
    if (dot(nw, nw) < 1e-30) {
        nw = nRef;
    } else {
        nw = normalize(nw);
        if (dot(nw, nRef) < 0.0) {
            nw = -nw;
        }
    }

    out.clip = frame.viewProj * vec4<f32>(p0, 1.0);
    out.worldPos = p0;
    out.normal = nw;
    // Velocity covers camera, object, instance and deformation motion: the same chain evaluated
    // with last frame's time and last frame's object matrix (ADR-035).
    let pPrev = deformChain(srcPos, n, nRef, inst, proc.prevInfo.x, object.prevModel);
    out.prevClip = frame.prevViewProj * vec4<f32>(pPrev, 1.0);
    return out;
}

@fragment
fn fs_proc(in: ProcVertexOut, @builtin(front_facing) frontFacing: bool) -> SceneOut {
    var emissiveMul = in.instEmissive.rgb;
    let emissiveSlot = i32(floor(proc.fieldInfo.x + 0.5));
    if (emissiveSlot >= 0) {
        emissiveMul = emissiveMul * (1.0 + proc.fieldInfo.y * fieldScalar(emissiveSlot, in.worldPos));
    }
    var info: MaterialInstanceInfo;
    info.localPosition = in.localPos;
    info.objectId = object.ids.x;
    info.instanceIndex = in.instIndex;
    info.instanceId = in.instColor.a;
    info.instanceRandom = in.instRandom;
    info.instanceColor = in.instColor;
    info.instanceEmissive = in.instEmissive;
    let screenUv = in.clip.xy * frame.targetSize.zw;
    let shaded = shadeSurface(in.worldPos, in.normal, in.uv, frontFacing, in.instColor.rgb, emissiveMul, info,
                              screenUv);
    var out: SceneOut;
    out.color = shaded.color;
    out.normalRoughness = packNormalRoughness(shaded.normal, shaded.roughness, shaded.flags);
    out.velocity = screenVelocity(in.clip, in.prevClip);
    out.emission = vec4<f32>(shaded.emission, shaded.bloomWeight);
    out.ids = packIds(object.ids.x, object.ids.y);
    return out;
}

// Depth-only entry for the prepass and the shadow passes: the same vertex stage, so the depth
// matches the lit pass and instanced procedural geometry casts shadows (ADR-034).
@fragment
fn fs_proc_depth(in: ProcVertexOut) {
}
