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
// so the depth it writes matches the lit pass exactly, and no fragment work beyond deciding
// whether the fragment blocks at all -- an alpha cutout for MASK, and an ordered dither against
// alpha for BLEND, which is how a half-transparent caster throws half a shadow (ADR-701).
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

// A 4x4 ordered (Bayer) threshold in (0, 1), indexed by the depth target's own pixel.
//
// Ordered rather than hashed, on purpose. A depth map stores one occluder per texel and has
// nowhere to put "half blocked", so the only way a caster can throw a partial shadow is by
// claiming a *fraction of the texels* it covers -- and whichever fraction it claims has to be the
// same every time the same frame is drawn, or an offline render stops reproducing (the project
// checks that by hashing captured frames). A Bayer matrix is a pure function of the texel; a hash
// of the world position is not, once the caster moves. Sixteen levels, which is also the tap count
// of the Poisson disc in shadows.wgsl that averages them back into a smooth term.
fn orderedDither(pixel: vec2<f32>) -> f32 {
    var m = array<u32, 16>(0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
    let p = vec2<u32>(pixel);
    return (f32(m[(p.y & 3u) * 4u + (p.x & 3u)]) + 0.5) / 16.0;
}

@fragment
fn fs_depth(in: VertexOut) {
    let alphaMode = object.flags.x;
    let texMask = u32(object.flags.w + 0.5);
    var alpha = object.baseColor.a;
    // Uniform control flow, which `textureSample` requires: both conditions read the object
    // uniform, so every fragment of a draw takes the same branch.
    if (alphaMode > 0.5 && (texMask & 1u) != 0u) {
        alpha = alpha * textureSample(baseColorTex, materialSampler, in.uv).a;
    }
    if (alphaMode > 0.5 && alphaMode < 1.5) {
        if (alpha < object.flags.y) {
            discard;
        }
    } else if (alphaMode > 1.5) {
        // BLEND, and this arm is the whole of the second half of the abduction defect. Without it
        // a blended surface writes depth like an opaque one -- and `SceneRenderer` compensated by
        // not making it a caster at all, so ADR-385's fade lost its shadow on the FIRST frame its
        // opacity fell below 1, a full second before the body itself was gone. Stochastic
        // transparency instead: the caster claims `alpha` of the depth texels it covers, so the
        // term the PCF disc averages falls with the body rather than switching off ahead of it. At
        // alpha 1 nothing is discarded and the shadow is the opaque one; at 0 everything is and
        // there is no shadow left to pop.
        if (alpha <= 0.0 || orderedDither(in.clip.xy) >= alpha) {
            discard;
        }
    }
}
