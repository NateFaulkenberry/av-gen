// Shared declarations for scene passes. Included by pbr.wgsl, grid.wgsl and skybox.wgsl.
// Uniform layouts mirror rendering/scene_renderer.hpp (static_asserts guard the sizes).

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265;

struct Light {
    positionType: vec4<f32>,   // xyz = position, w = type (0 directional, 1 point, 2 spot)
    directionRange: vec4<f32>, // xyz = direction the light travels (normalised), w = range (0 = inf)
    colorIntensity: vec4<f32>, // rgb = colour * intensity
    cone: vec4<f32>,           // x = cos(outer), y = 1 / max(cos(inner) - cos(outer), eps)
};

struct FrameUniforms {
    viewProj: mat4x4<f32>,
    invViewProj: mat4x4<f32>,
    cameraPos: vec4<f32>,      // xyz = world position
    params: vec4<f32>,         // x = time, y = gridIntensity, z = brightness, w = environmentIntensity
    envParams: vec4<f32>,      // x = env rotation (radians), y = prefiltered mip count - 1, z = light count, w = ibl enabled
    skyParams: vec4<f32>,      // rgb = background colour, w = skybox blur 0..1
    lights: array<Light, 8>,
};

struct ObjectUniforms {
    model: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    baseColor: vec4<f32>,      // rgb, a = opacity
    emissive: vec4<f32>,       // rgb = colour, w = intensity
    material: vec4<f32>,       // x = roughness, y = metallic, z = normalScale, w = occlusionStrength
    flags: vec4<f32>,          // x = alpha mode (0 opaque, 1 mask, 2 blend), y = alpha cutoff, z = unlit, w = texture mask
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

// Rotates a world direction into the environment map's frame (rotation about +Y).
fn envRotate(dir: vec3<f32>) -> vec3<f32> {
    let a = frame.envParams.x;
    let c = cos(a);
    let s = sin(a);
    return vec3<f32>(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);
}

// Hash-based value noise, cheap and deterministic (no texture lookups).
fn hash21(p: vec2<f32>) -> f32 {
    let h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}
