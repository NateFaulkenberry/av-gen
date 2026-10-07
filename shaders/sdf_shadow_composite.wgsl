// ADR-1165: a raymarched SDF's shadow, marched at a fraction of the shadow map's resolution and written into it.
//
// The raymarched casters of one shadow view are marched into a low-resolution depth layer by the ordinary shadow
// pipelines (sdf_raymarch.wgsl, `fs_sdf_shadow`), with the same view-projection, so a low-resolution texel holds
// exactly the depth a full-resolution texel at its centre would have. This pass then covers the view's full-resolution
// layer and copies each texel's nearest low-resolution depth into it, under the ordinary depth test, so the meshes
// already drawn there and the SDF casters keep whichever is nearer the light.
//
// Nearest, not interpolated: a depth map is not a colour, and blending a caster's depth with the cleared far plane
// across a silhouette would invent a surface halfway to the light.

@group(0) @binding(0) var lowDepth: texture_depth_2d;

struct CompositeUniforms {
    fullSize: vec4<f32>, // xy = the full-resolution layer's size in texels
};
@group(0) @binding(1) var<uniform> composite: CompositeUniforms;

@vertex
fn vs_composite(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
    // One triangle over the whole layer.
    let p = vec2<f32>(f32((vertexIndex << 1u) & 2u), f32(vertexIndex & 2u));
    return vec4<f32>(p * 2.0 - 1.0, 0.0, 1.0);
}

struct CompositeOut {
    @builtin(frag_depth) depth: f32,
};

@fragment
fn fs_composite(@builtin(position) position: vec4<f32>) -> CompositeOut {
    let low = vec2<f32>(textureDimensions(lowDepth));
    let texel = vec2<i32>(clamp(floor(position.xy * low / composite.fullSize.xy), vec2<f32>(0.0), low - 1.0));
    let d = textureLoad(lowDepth, texel, 0);
    if (d >= 1.0) {
        discard; // no SDF caster here: the meshes' depth stands
    }
    var out: CompositeOut;
    out.depth = d;
    return out;
}
