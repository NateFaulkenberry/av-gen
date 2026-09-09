// Entity PBR pipeline: the vertex stage from common.wgsl, the shared metallic-roughness shading
// from pbr_shade.wgsl (also used by procedural.wgsl and sdf_raymarch.wgsl). Outputs scene-linear
// HDR radiance plus the auxiliary targets (ADR-035); tone mapping happens in tonemap.wgsl.
//
// Fields (ADR-025) reach this pass only through material programs (ADR-030): the Field op samples
// the frame's FieldBlock, bound here at group 2 (the material group) because the entity group 1
// holds nothing but the ObjectUniforms. procedural.wgsl and sdf_raymarch.wgsl bind their own copy
// at group(1) @binding(3); each module declares `fieldBlock` exactly once.
//
// `fs_depth` is the depth-only entry the prepass and the shadow passes use: the same vertex stage,
// so the depth it writes matches the lit pass exactly, and no fragment work beyond alpha cutout.
#include "common.wgsl"
#include "pbr_shade.wgsl"
#include "fields.wgsl"

@group(2) @binding(8) var<uniform> fieldBlock: FieldBlock;

@fragment
fn fs_main(in: VertexOut, @builtin(front_facing) frontFacing: bool) -> SceneOut {
    let screenUv = in.clip.xy * frame.targetSize.zw;
    let shaded = shadeSurface(in.worldPos, in.normal, in.uv, frontFacing, vec3<f32>(1.0), vec3<f32>(1.0),
                              materialInstanceZero(in.localPos), screenUv);
    var out: SceneOut;
    out.color = shaded.color;
    out.normalRoughness = packNormalRoughness(shaded.normal, shaded.roughness, shaded.flags);
    out.velocity = screenVelocity(in.clip, in.prevClip);
    out.emission = vec4<f32>(shaded.emission, shaded.bloomWeight);
    out.ids = packIds(object.ids.x, object.ids.y);
    return out;
}

@fragment
fn fs_depth(in: VertexOut) {
    let alphaMode = object.flags.x;
    if (alphaMode > 0.5 && alphaMode < 1.5) {
        let texMask = u32(object.flags.w + 0.5);
        var alpha = object.baseColor.a;
        if ((texMask & 1u) != 0u) {
            alpha = alpha * textureSample(baseColorTex, materialSampler, in.uv).a;
        }
        if (alpha < object.flags.y) {
            discard;
        }
    }
}
