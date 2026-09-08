// Shared declarations for scene passes. Included by mesh.wgsl and grid.wgsl.
// Uniform layouts mirror rendering/scene_renderer.hpp (static_asserts guard the sizes).

struct FrameUniforms {
    viewProj: mat4x4<f32>,
    cameraPos: vec4<f32>,   // xyz = world position
    lightDir: vec4<f32>,    // xyz = direction the light travels (normalised)
    lightColor: vec4<f32>,  // rgb * intensity
    params: vec4<f32>,      // x = time, y = gridIntensity, z = brightness, w = unused
};

struct ObjectUniforms {
    model: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    baseColor: vec4<f32>,
    emissive: vec4<f32>,    // rgb = colour, w = intensity
    material: vec4<f32>,    // x = roughness, y = metallic
};

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

struct VertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

struct VertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    var out: VertexOut;
    let world = object.model * vec4<f32>(in.position, 1.0);
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    out.normal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    return out;
}

// Hash-based value noise, cheap and deterministic (no texture lookups).
fn hash21(p: vec2<f32>) -> f32 {
    let h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}
