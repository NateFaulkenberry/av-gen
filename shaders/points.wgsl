// GPU point processing (ADR-025/029): the effector pass. One thread per InstanceRecord reads
// the object's base record buffer (uploaded once per structure change), applies the object's
// EffectorGpu list in order with the semantics of spatial::applyEffectorsToRecords, and writes
// the "live" record buffer the instanced draw reads. Runs every frame for objects with at least
// one usable effector; objects without effectors draw from the base buffer with no pass.
//
// Per record (pw = objectToWorld * position, sampled in world space; R^-1 = worldToObjectRotation,
// the transpose of the rotation-only part of objectToWorld):
//   PositionOffset : position += R^-1 (v * strength), v = fieldVector (vector fields) or
//                    fieldScalar * effector axis (scalar / colour fields)
//   Scale          : target = scale * (1 + s * strength * scaleAxis) (Replace: s * strength * scaleAxis);
//                    scale = blend(scale, target)
//   Rotation       : rotation = axisAngle(normalize(v) or axis, s * strength) * rotation
//   Color          : color.rgb = mix(color.rgb, c.rgb, c.a * strength)
//   Emission       : emissive.rgb *= 1 + s * strength (Replace: = s * strength)
//   Density        : position.w *= s * strength (Replace: = s * strength)
//   Velocity, Attribute: ignored (no such lanes in a record)
// Blend: 0 Add, 1 Multiply, 2 Replace, 3 Min, 4 Max, 5 Mix (by weight).
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

fn blendVec3(blend: u32, existing: vec3<f32>, value: vec3<f32>, weight: f32) -> vec3<f32> {
    if (blend == BLEND_ADD) { return existing + value; }
    if (blend == BLEND_MULTIPLY) { return existing * value; }
    if (blend == BLEND_MIN) { return min(existing, value); }
    if (blend == BLEND_MAX) { return max(existing, value); }
    if (blend == BLEND_MIX) { return mix(existing, value, weight); }
    return value;
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
    let count = min(pointsParams.info.y, 8u);
    for (var k = 0u; k < 8u; k = k + 1u) {
        if (k >= count) { break; }
        let e = pointsParams.effectors[k];
        let slot = e.fieldSlot;
        if (slot < 0) { continue; }
        let pw = (pointsParams.objectToWorld * vec4<f32>(r.position.xyz, 1.0)).xyz;
        let vectorField = fieldTypeOf(slot) == FIELD_TYPE_VECTOR;
        if (e.op == EFFECTOR_POSITION_OFFSET) {
            var v: vec3<f32>;
            if (vectorField) {
                v = fieldVector(slot, pw);
            } else {
                v = fieldScalar(slot, pw) * e.axisWeight.xyz;
            }
            let offset = (pointsParams.worldToObjectRotation * vec4<f32>(v * e.strength, 0.0)).xyz;
            r.position = vec4<f32>(r.position.xyz + offset, r.position.w);
        } else if (e.op == EFFECTOR_SCALE) {
            let s = fieldScalar(slot, pw);
            let scale = r.scale.xyz;
            var newScale = scale * (1.0 + s * e.strength * e.scaleAxisPad.xyz);
            if (e.blend == BLEND_REPLACE) {
                newScale = s * e.strength * e.scaleAxisPad.xyz;
            }
            r.scale = vec4<f32>(blendVec3(e.blend, scale, newScale, e.axisWeight.w), r.scale.w);
        } else if (e.op == EFFECTOR_ROTATION) {
            var axis: vec3<f32>;
            if (vectorField) {
                axis = fieldNormalize(fieldVector(slot, pw));
            } else {
                axis = fieldNormalize(e.axisWeight.xyz);
            }
            if (dot(axis, axis) >= 0.5) {
                let angle = fieldScalar(slot, pw) * e.strength;
                r.rotation = quatMul(quatAxisAngle(axis, angle), r.rotation);
            }
        } else if (e.op == EFFECTOR_COLOR) {
            let c = fieldColor(slot, pw);
            r.color = vec4<f32>(mix(r.color.rgb, c.rgb, c.a * e.strength), r.color.w);
        } else if (e.op == EFFECTOR_EMISSION) {
            let s = fieldScalar(slot, pw);
            var em = r.emissive.rgb * (1.0 + s * e.strength);
            if (e.blend == BLEND_REPLACE) {
                em = vec3<f32>(s * e.strength);
            }
            r.emissive = vec4<f32>(em, r.emissive.w);
        } else if (e.op == EFFECTOR_DENSITY) {
            let s = fieldScalar(slot, pw);
            var d = r.position.w * (s * e.strength);
            if (e.blend == BLEND_REPLACE) {
                d = s * e.strength;
            }
            r.position.w = d;
        }
    }
    liveRecords[i] = r;
}
