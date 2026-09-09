// Entity PBR pipeline: the vertex stage from common.wgsl, the shared metallic-roughness shading
// from pbr_shade.wgsl (also used by procedural.wgsl and sdf_raymarch.wgsl). Outputs scene-linear
// HDR radiance; tone mapping happens in tonemap.wgsl.
//
// Fields (ADR-025) reach this pass only through material programs (ADR-030): the Field op samples
// the frame's FieldBlock, bound here at group 2 (the material group) because the entity group 1
// holds nothing but the ObjectUniforms. procedural.wgsl and sdf_raymarch.wgsl bind their own copy
// at group(1) @binding(3); each module declares `fieldBlock` exactly once.
#include "common.wgsl"
#include "pbr_shade.wgsl"
#include "fields.wgsl"

@group(2) @binding(8) var<uniform> fieldBlock: FieldBlock;

struct FragmentOut {
    @location(0) color: vec4<f32>,
};

@fragment
fn fs_main(in: VertexOut, @builtin(front_facing) frontFacing: bool) -> FragmentOut {
    var out: FragmentOut;
    out.color = shadePbrInstanced(in.worldPos, in.normal, in.uv, frontFacing, vec3<f32>(1.0), vec3<f32>(1.0),
                                  materialInstanceZero(in.localPos));
    return out;
}
