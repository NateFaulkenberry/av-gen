// Skinned entities (ADR-086). The whole of this file is the *vertex* stage: it includes pbr.wgsl
// unchanged, so `fs_main` and `fs_depth` -- and therefore every line of shading in pbr_shade.wgsl
// -- are literally the same code the static pipeline runs. A skinned surface and a static one with
// the same material cannot diverge, because there is nothing here for them to diverge in.
//
// The joint palette is a read-only storage buffer at group(1) binding(1), bound with a dynamic
// offset to this draw's rig. One rig's slice holds its matrices twice: the current pose first, then
// the pose the rig was drawn with last frame, which is what the velocity target needs so a running
// character's limbs blur along their own motion instead of the body's. `object.ids.w` carries the
// joint count, so the second half starts at `jointCount`.
#include "pbr.wgsl"

@group(1) @binding(1) var<storage, read> jointMatrices: array<mat4x4<f32>>;

struct SkinnedVertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) joints: vec4<u32>,
    @location(4) weights: vec4<f32>,
};

// The linear blend of four joint matrices. `base` selects the current palette (0) or the previous
// frame's (jointCount). Weights are normalised on import; a vertex that still arrives weightless
// falls back to the identity so it stays in the bind pose rather than collapsing to the origin.
fn skinMatrix(base: u32, count: u32, joints: vec4<u32>, weights: vec4<f32>) -> mat4x4<f32> {
    let total = weights.x + weights.y + weights.z + weights.w;
    if (count == 0u || total < 1.0e-5) {
        return mat4x4<f32>(vec4<f32>(1.0, 0.0, 0.0, 0.0), vec4<f32>(0.0, 1.0, 0.0, 0.0),
                           vec4<f32>(0.0, 0.0, 1.0, 0.0), vec4<f32>(0.0, 0.0, 0.0, 1.0));
    }
    let last = count - 1u;
    let i = vec4<u32>(min(joints.x, last), min(joints.y, last), min(joints.z, last), min(joints.w, last));
    let w = weights / total;
    return w.x * jointMatrices[base + i.x] + w.y * jointMatrices[base + i.y] +
           w.z * jointMatrices[base + i.z] + w.w * jointMatrices[base + i.w];
}

@vertex
fn vs_skinned(in: SkinnedVertexIn) -> VertexOut {
    let count = u32(max(object.ids.w, 0.0) + 0.5);
    let skin = skinMatrix(0u, count, in.joints, in.weights);
    let prevSkin = skinMatrix(count, count, in.joints, in.weights);

    var out: VertexOut;
    let posed = skin * vec4<f32>(in.position, 1.0);
    let world = object.model * posed;
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    // The joint blend is affine and, for a rig without non-uniform scale in it, rigid: its upper
    // 3x3 rotates the normal correctly and the object's own normal matrix finishes the job.
    let skinRot = mat3x3<f32>(skin[0].xyz, skin[1].xyz, skin[2].xyz);
    out.normal = normalize((object.normalMatrix * vec4<f32>(skinRot * in.normal, 0.0)).xyz);
    out.uv = in.uv;
    // The ADR-030 `localPosition` material input stays in *bind* space deliberately: a pattern
    // authored on the surface should travel with the surface, not swim through it as the character
    // moves.
    out.localPos = in.position;
    out.prevClip = frame.prevViewProj * (object.prevModel * (prevSkin * vec4<f32>(in.position, 1.0)));
    return out;
}
