// GPU point processing (ADR-025/029): the effector pass. One thread per InstanceRecord reads
// the object's base record buffer (uploaded once per structure change), applies the object's
// EffectorGpu list in order with EXACTLY the semantics of spatial::applyEffectorsToRecords (the
// CPU reference ADR-025 names), and writes the "live" record buffer the instanced draw reads. Runs
// every frame for objects with at least one usable effector; objects without effectors draw from
// the base buffer with no pass.
//
// ADR-1121: every (op, blend) pair is the CPU's `blendValue` / `blendRotation`. Before it, the GPU
// had its own reading for 17 of 36 pairs (Scale + Add added the target scale to the existing one;
// position ignored its blend; Replace/Mix rotations; colour, emission and density blends), and the
// parity test gave Scale a Mix blend, the one pair where the two happened to agree.
//
// Per record (p = the record's position in object space, sampled at pw = objectToWorld * p; R^-1 =
// worldToObjectRotation, the transpose of the rotation-only part of objectToWorld; s, v, c = the
// field's scalar, vector (rotated into object space) and colour readings; k = strength):
//   natural / raw per op:
//   PositionOffset : raw = v k (vector fields) or axis (s k);  natural = p + raw
//   Scale          : raw = scaleAxis (s k);                    natural = scale * (1 + raw)
//   Rotation       : delta = axisAngle(normalize(v) or normalize(axis), s k); natural = normalize(delta * q)
//                    Replace: normalize(delta); Mix: normalize(slerp(q, natural, weight)); else natural
//   Color          : raw = c.rgb;                              natural = mix(rgb, raw, c.a k)
//   Emission       : raw = (s k);                              natural = emissive * (1 + raw)
//   Density        : raw = s k;                                natural = density * raw
//   Velocity, Attribute: not run here (no such lanes in a record; ADR-422 reports them)
//   blend(existing, natural, raw, weight):
//     0 Add -> natural, 1 Multiply -> existing * raw, 2 Replace -> raw, 3 Min -> min(existing, natural),
//     4 Max -> max(existing, natural), 5 Mix -> mix(existing, natural, weight)
//
// Bindings (group 0): 0 PointsParams, 1 base records (read), 2 live records (read_write),
// 3 FieldBlock. Mirrors rendering/procedural_renderer.hpp EffectorPassUniforms.
#include "fields.wgsl"

struct InstanceRecord {
    position: vec4<f32>,  // xyz, w = density
    rotation: vec4<f32>,  // unit quaternion (x, y, z, w)
    scale: vec4<f32>,     // xyz, w = normalised index
    random: vec4<f32>,
    color: vec4<f32>,     // rgb, a = instance id
    emissive: vec4<f32>,  // rgb, a = extra lane
};

struct EffectorGpu {
    op: u32,              // EffectorOp: 0 PositionOffset, 1 Scale, 2 Rotation, 3 Velocity, 4 Color, 5 Emission, 6 Density, 7 Attribute
    blend: u32,           // EffectorBlend
    fieldSlot: i32,       // -1 = disabled
    strength: f32,
    axisWeight: vec4<f32>,   // axis.xyz, weight (Mix)
    scaleAxisPad: vec4<f32>, // scaleAxis.xyz
};

struct PointsParams {
    objectToWorld: mat4x4<f32>,
    worldToObjectRotation: mat4x4<f32>, // rotation-only inverse (3x3 in a 4x4)
    info: vec4<u32>,                    // x = record count, y = effector count
    effectors: array<EffectorGpu, 8>,
};

@group(0) @binding(0) var<uniform> pointsParams: PointsParams;
@group(0) @binding(1) var<storage, read> baseRecords: array<InstanceRecord>;
@group(0) @binding(2) var<storage, read_write> liveRecords: array<InstanceRecord>;
@group(0) @binding(3) var<uniform> fieldBlock: FieldBlock;

const EFFECTOR_POSITION_OFFSET: u32 = 0u;
const EFFECTOR_SCALE: u32 = 1u;
const EFFECTOR_ROTATION: u32 = 2u;
const EFFECTOR_COLOR: u32 = 4u;
const EFFECTOR_EMISSION: u32 = 5u;
const EFFECTOR_DENSITY: u32 = 6u;

const BLEND_ADD: u32 = 0u;
const BLEND_MULTIPLY: u32 = 1u;
const BLEND_REPLACE: u32 = 2u;
const BLEND_MIN: u32 = 3u;
const BLEND_MAX: u32 = 4u;
const BLEND_MIX: u32 = 5u;

fn blendVec3(blend: u32, existing: vec3<f32>, natural: vec3<f32>, raw: vec3<f32>, weight: f32) -> vec3<f32> {
    if (blend == BLEND_ADD) { return natural; }
    if (blend == BLEND_MULTIPLY) { return existing * raw; }
    if (blend == BLEND_REPLACE) { return raw; }
    if (blend == BLEND_MIN) { return min(existing, natural); }
    if (blend == BLEND_MAX) { return max(existing, natural); }
    return mix(existing, natural, weight);
}

fn blendF32(blend: u32, existing: f32, natural: f32, raw: f32, weight: f32) -> f32 {
    if (blend == BLEND_ADD) { return natural; }
    if (blend == BLEND_MULTIPLY) { return existing * raw; }
    if (blend == BLEND_REPLACE) { return raw; }
    if (blend == BLEND_MIN) { return min(existing, natural); }
    if (blend == BLEND_MAX) { return max(existing, natural); }
    return mix(existing, natural, weight);
}

// glm::slerp, operation for operation (the shorter arc; linear when the two are within epsilon).
fn quatSlerp(x: vec4<f32>, y: vec4<f32>, a: f32) -> vec4<f32> {
    var z = y;
    var cosTheta = dot(x, y);
    if (cosTheta < 0.0) {
        z = -y;
        cosTheta = -cosTheta;
    }
    if (cosTheta > 1.0 - 1.1920929e-7) {
        return mix(x, z, a);
    }
    let angle = acos(cosTheta);
    return (sin((1.0 - a) * angle) * x + sin(a * angle) * z) / sin(angle);
}

fn quatNormalize(q: vec4<f32>) -> vec4<f32> {
    let len = length(q);
    if (len <= 0.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    return q / len;
}

// Hamilton product a * b for (x, y, z, w) quaternions (glm order of operations).
fn quatMul(a: vec4<f32>, b: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}

fn quatAxisAngle(axis: vec3<f32>, angle: f32) -> vec4<f32> {
    let s = sin(angle * 0.5);
    return vec4<f32>(axis * s, cos(angle * 0.5));
}

@compute @workgroup_size(64)
fn cs_effectors(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= pointsParams.info.x) { return; }
    var r = baseRecords[i];
    fieldElement = r.random.w; // ADR-1116: an Element-band Spectrum field hears this record's own band
    let count = min(pointsParams.info.y, 8u);
    for (var k = 0u; k < 8u; k = k + 1u) {
        if (k >= count) { break; }
        let e = pointsParams.effectors[k];
        let slot = e.fieldSlot;
        if (slot < 0) { continue; }
        let pw = (pointsParams.objectToWorld * vec4<f32>(r.position.xyz, 1.0)).xyz;
        let vectorField = fieldTypeOf(slot) == FIELD_TYPE_VECTOR;
        let s = fieldScalar(slot, pw);
        let strength = e.strength;
        let weight = e.axisWeight.w;
        if (e.op == EFFECTOR_POSITION_OFFSET) {
            var raw: vec3<f32>;
            if (vectorField) {
                raw = (pointsParams.worldToObjectRotation * vec4<f32>(fieldVector(slot, pw), 0.0)).xyz * strength;
            } else {
                raw = e.axisWeight.xyz * (s * strength);
            }
            let p = r.position.xyz;
            r.position = vec4<f32>(blendVec3(e.blend, p, p + raw, raw, weight), r.position.w);
        } else if (e.op == EFFECTOR_SCALE) {
            let existing = r.scale.xyz;
            let raw = e.scaleAxisPad.xyz * (s * strength);
            r.scale = vec4<f32>(blendVec3(e.blend, existing, existing * (vec3<f32>(1.0) + raw), raw, weight), r.scale.w);
        } else if (e.op == EFFECTOR_ROTATION) {
            var axis: vec3<f32>;
            var len: f32;
            if (vectorField) {
                axis = (pointsParams.worldToObjectRotation * vec4<f32>(fieldVector(slot, pw), 0.0)).xyz;
            } else {
                axis = e.axisWeight.xyz;
            }
            len = length(axis);
            if (len > 1e-8) {
                axis = axis / len;
                let delta = quatAxisAngle(axis, s * strength);
                let natural = quatNormalize(quatMul(delta, r.rotation));
                if (e.blend == BLEND_REPLACE) {
                    r.rotation = quatNormalize(delta);
                } else if (e.blend == BLEND_MIX) {
                    r.rotation = quatNormalize(quatSlerp(r.rotation, natural, weight));
                } else {
                    r.rotation = natural;
                }
            }
        } else if (e.op == EFFECTOR_COLOR) {
            let c = fieldColor(slot, pw);
            let existing = r.color.rgb;
            r.color = vec4<f32>(blendVec3(e.blend, existing, mix(existing, c.rgb, c.a * strength), c.rgb, weight), r.color.w);
        } else if (e.op == EFFECTOR_EMISSION) {
            let existing = r.emissive.rgb;
            let raw = vec3<f32>(s * strength);
            r.emissive = vec4<f32>(blendVec3(e.blend, existing, existing * (vec3<f32>(1.0) + raw), raw, weight), r.emissive.w);
        } else if (e.op == EFFECTOR_DENSITY) {
            let existing = r.position.w;
            let raw = s * strength;
            r.position.w = blendF32(e.blend, existing, existing * raw, raw, weight);
        }
    }
    liveRecords[i] = r;
}
