// Environment background: a fullscreen triangle at the far plane sampling the prefiltered
// environment cube (blur selects the mip). Falls back to the background colour without an env map.
#include "common.wgsl"

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;

struct SkyOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_sky(@builtin(vertex_index) index: u32) -> SkyOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: SkyOut;
    let p = positions[index];
    out.clip = vec4<f32>(p, 1.0, 1.0); // z = far plane
    out.ndc = p;
    return out;
}

@fragment
fn fs_sky(in: SkyOut) -> @location(0) vec4<f32> {
    if (frame.envParams.w < 0.5) {
        return vec4<f32>(frame.skyParams.rgb, 1.0);
    }
    let near = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let far = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let dir = normalize(far.xyz / far.w - near.xyz / near.w);
    let mip = frame.skyParams.w * frame.envParams.y;
    let color = textureSampleLevel(prefilteredMap, iblSampler, envRotate(dir), mip).rgb * frame.params.w;
    return vec4<f32>(color, 1.0);
}
