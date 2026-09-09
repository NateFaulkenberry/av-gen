// Output mapping (milestone 1.2): draws the final frame onto one output with a crop, a projective
// warp given as the inverse homography (target -> source quad), soft-edge blending, brightness,
// gamma and flips. fs_blit is the identity fast path.

struct OutputMapUniforms {
    inv0: vec4<f32>,    // columns of the target -> unit-square homography (xyz used)
    inv1: vec4<f32>,
    inv2: vec4<f32>,
    crop: vec4<f32>,    // x, y, w, h in 0..1 of the source
    blend: vec4<f32>,   // left, right, top, bottom widths in quad space
    params: vec4<f32>,  // blendGamma, brightness, 1 / gamma, flags (bit 0 flipX, bit 1 flipY)
};

@group(0) @binding(0) var sourceTexture: texture_2d<f32>;
@group(0) @binding(1) var sourceSampler: sampler;
@group(0) @binding(2) var<uniform> mapping: OutputMapUniforms;

struct VertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VertexOut;
    let p = positions[index];
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 1.0 - (p.y * 0.5 + 0.5));
    return out;
}

@fragment
fn fs_blit(in: VertexOut) -> @location(0) vec4<f32> {
    return vec4<f32>(textureSampleLevel(sourceTexture, sourceSampler, in.uv, 0.0).rgb, 1.0);
}

// Soft edge: 1 without a blend width, else pow(distance / width, gamma) clamped to 0..1.
fn edgeWeight(distance: f32, width: f32, gamma: f32) -> f32 {
    let t = clamp(distance / max(width, 1e-6), 0.0, 1.0);
    return select(1.0, pow(t, gamma), width > 0.0);
}

@fragment
fn fs_map(in: VertexOut) -> @location(0) vec4<f32> {
    let h = mat3x3<f32>(mapping.inv0.xyz, mapping.inv1.xyz, mapping.inv2.xyz);
    let q = h * vec3<f32>(in.uv, 1.0);
    let w = select(q.z, 1e-6, abs(q.z) < 1e-6);
    let uv = q.xy / w;
    let inside = q.z > 0.0 && all(uv >= vec2<f32>(0.0)) && all(uv <= vec2<f32>(1.0));

    let gamma = mapping.params.x;
    let weight = edgeWeight(uv.x, mapping.blend.x, gamma) * edgeWeight(1.0 - uv.x, mapping.blend.y, gamma) *
                 edgeWeight(uv.y, mapping.blend.z, gamma) * edgeWeight(1.0 - uv.y, mapping.blend.w, gamma);

    let flags = u32(mapping.params.w + 0.5);
    let flipX = (flags & 1u) != 0u;
    let flipY = (flags & 2u) != 0u;
    let flipped = vec2<f32>(select(uv.x, 1.0 - uv.x, flipX), select(uv.y, 1.0 - uv.y, flipY));
    let src = mapping.crop.xy + clamp(flipped, vec2<f32>(0.0), vec2<f32>(1.0)) * mapping.crop.zw;

    var color = textureSampleLevel(sourceTexture, sourceSampler, src, 0.0).rgb;
    color = pow(max(color * mapping.params.y, vec3<f32>(0.0)), vec3<f32>(mapping.params.z)) * weight;
    return vec4<f32>(select(vec3<f32>(0.0), color, inside), 1.0);
}
