// Entity PBR pipeline: the vertex stage from common.wgsl, the shared metallic-roughness shading
// from pbr_shade.wgsl (also used by procedural.wgsl). Outputs scene-linear HDR radiance; tone
// mapping happens in tonemap.wgsl.
#include "common.wgsl"
#include "pbr_shade.wgsl"

struct FragmentOut {
    @location(0) color: vec4<f32>,
};

@fragment
fn fs_main(in: VertexOut, @builtin(front_facing) frontFacing: bool) -> FragmentOut {
    var out: FragmentOut;
    out.color = shadePbr(in.worldPos, in.normal, in.uv, frontFacing, vec3<f32>(1.0), vec3<f32>(1.0));
    return out;
}
