// Resolves the scene depth buffer to a linear R32Float view-space distance (ADR-035). Ambient
// occlusion and the screen-space contact-shadow march both read this instead of the depth
// attachment, which a pass may not sample while it is writing it.
#include "common.wgsl"

@group(1) @binding(0) var sceneDepth: texture_depth_2d;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_linear_depth(@builtin(vertex_index) index: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    let p = positions[index];
    var out: FsIn;
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

@fragment
fn fs_linear_depth(in: FsIn) -> @location(0) f32 {
    let texel = vec2<i32>(in.clip.xy);
    let depth = textureLoad(sceneDepth, texel, 0);
    if (depth >= 1.0) {
        return 1.0e7; // nothing was drawn here: treat it as infinitely far
    }
    let ndc = vec3<f32>(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0, depth);
    let world = frame.invViewProj * vec4<f32>(ndc, 1.0);
    let p = world.xyz / world.w;
    return max(dot(p - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
}
