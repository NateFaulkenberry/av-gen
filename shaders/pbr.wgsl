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
// ADR-138: whether this module's draws are the procedural scatter, which is the share tier
// assignment could actually reach on Glowmere (authored entities are 77% of coverage and the
// terrain can never be demoted). `pbr_shade.wgsl` reads it to pick which tier applies. Every
// includer must define it -- a missing one is a compile error rather than a silent wrong tier.
const kProceduralDraw: bool = false;
// ADR-155: this draw's rung tier, or 0 (Full) for a path that has no rungs. Defined by
// every includer of pbr_shade.wgsl, so a missing one is a compile error.
fn proceduralRungTier() -> f32 { return 0.0; }
#include "pbr_shade.wgsl"
#include "fields.wgsl"

@group(2) @binding(8) var<uniform> fieldBlock: FieldBlock;

@fragment
fn fs_main(in: VertexOut, @builtin(front_facing) frontFacing: bool) -> SceneOut {
    let screenUv = in.clip.xy * frame.targetSize.zw;
    let shaded = shadeSurface(in.worldPos, in.normal, in.uv, frontFacing, vec3<f32>(1.0), vec3<f32>(1.0),
                              materialInstanceZero(in.localPos), screenUv);
    // ADR-376: the tree's own light. Added to both the radiance and the emission target, because
    // an emissive term that reaches the frame but not the AOV is invisible to bloom -- which is
    // most of what makes a conducted pulse read.
    let energy = treeEnergyAt(in.worldPos);
    var out: SceneOut;
    out.color = shaded.color + vec4<f32>(energy, 0.0);
    out.normalRoughness = packNormalRoughness(shaded.normal, shaded.roughness, shaded.flags);
    out.velocity = screenVelocityAt(in.clip, in.prevClip);
    out.emission = vec4<f32>(shaded.emission + energy * max(object.energy2.w, 0.0),
                             shaded.bloomWeight);
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
