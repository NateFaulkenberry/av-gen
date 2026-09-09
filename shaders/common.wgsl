// Shared declarations for scene passes. Included by pbr.wgsl, grid.wgsl and skybox.wgsl.
// Uniform layouts mirror rendering/scene_renderer.hpp (static_asserts guard the sizes).

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265;

struct Light {
    positionType: vec4<f32>,   // xyz = position, w = type (0 directional, 1 point, 2 spot)
    directionRange: vec4<f32>, // xyz = direction the light travels (normalised), w = range (0 = inf)
    colorIntensity: vec4<f32>, // rgb = colour * intensity
    cone: vec4<f32>,           // x = cos(outer), y = 1 / max(cos(inner) - cos(outer), eps),
                               // z = volumetric strength (volume.wgsl), w = 0
};

struct FrameUniforms {
    viewProj: mat4x4<f32>,
    invViewProj: mat4x4<f32>,
    prevViewProj: mat4x4<f32>, // ADR-035: last frame's, for velocity and temporal reprojection
    cameraPos: vec4<f32>,      // xyz = world position
    cameraRight: vec4<f32>,    // xyz = camera right axis in world space (billboards)
    cameraUp: vec4<f32>,       // xyz = camera up axis in world space
    cameraForward: vec4<f32>,  // xyz = camera view axis in world space (froxel depth)
    params: vec4<f32>,         // x = time, y = gridIntensity, z = brightness, w = environmentIntensity
    envParams: vec4<f32>,      // x = env rotation (radians), y = prefiltered mip count - 1, z = light count, w = ibl enabled
    skyParams: vec4<f32>,      // rgb = background colour, w = skybox blur 0..1
    skyExtra: vec4<f32>,       // ADR-036: x = 1 when the IBL is the procedural sky, y = draw it as
                               // the background instead of the flat colour, zw = 0
    fogParams: vec4<f32>,      // rgb = fog colour, w = fog density (0 = off; exp2 fog by view distance)
    audio: vec4<f32>,          // ADR-030 material inputs: rms, bass, mid, treble
    audioBands: vec4<f32>,     // lowMid, highMid, spectral centroid, flux
    beat: vec4<f32>,           // beat phase 0..1, pulse (1 - phase), onset strength, bar phase
    clusterParams: vec4<f32>,  // xyz = froxel grid dimensions, w = 1 when the clustered path is on
    clusterDepth: vec4<f32>,   // x = slice scale, y = slice bias, z = near, w = far
    lightCounts: vec4<f32>,    // x = directional lights (always shaded), y = total lights
    shadowParams: vec4<f32>,   // x = atlas resolution, y = PCF radius (texels), z = contact steps, w = contact length
    aoParams: vec4<f32>,       // x = strength, y = 1 when AO is on, zw = the AO texture size
    targetSize: vec4<f32>,     // x = width, y = height, z = 1 / width, w = 1 / height
    lights: array<Light, 8>,
};

struct ObjectUniforms {
    model: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    prevModel: mat4x4<f32>,    // ADR-035: last frame's model matrix, for the velocity target
    baseColor: vec4<f32>,      // rgb, a = opacity
    emissive: vec4<f32>,       // rgb = colour, w = intensity
    material: vec4<f32>,       // x = roughness, y = metallic, z = normalScale, w = occlusionStrength
    flags: vec4<f32>,          // x = alpha mode (0 opaque, 1 mask, 2 blend), y = alpha cutoff, z = unlit, w = texture mask
    ids: vec4<f32>,            // x = object id (ADR-030 `objectId`), y = material id, z = bloom weight, w = 0
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
    @location(3) localPos: vec3<f32>, // object space, for the ADR-030 `localPosition` material input
    @location(4) prevClip: vec4<f32>, // last frame's clip position, for the velocity target
};

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    var out: VertexOut;
    let world = object.model * vec4<f32>(in.position, 1.0);
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    out.normal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.localPos = in.position;
    out.prevClip = frame.prevViewProj * (object.prevModel * vec4<f32>(in.position, 1.0));
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

// ---- auxiliary targets (ADR-035) ---------------------------------------------------------------
//
// Every pipeline in the scene pass writes the same five colour targets, so the auxiliary buffers
// agree at silhouettes: 0 = HDR radiance, 1 = normal (octahedral) + roughness + flags,
// 2 = velocity in UV units, 3 = emission with a bloom weight in alpha, 4 = packed identifiers.

struct SceneOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec2<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

// Cigolle et al. 2014: a unit vector in two components with no visible loss at 16 bits.
fn octEncode(nIn: vec3<f32>) -> vec2<f32> {
    let n = nIn / max(abs(nIn.x) + abs(nIn.y) + abs(nIn.z), 1e-8);
    if (n.z >= 0.0) {
        return n.xy;
    }
    let signs = vec2<f32>(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
    return (vec2<f32>(1.0) - abs(vec2<f32>(n.y, n.x))) * signs;
}

fn octDecode(e: vec2<f32>) -> vec3<f32> {
    var n = vec3<f32>(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        let signs = vec2<f32>(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
        let xy = (vec2<f32>(1.0) - abs(vec2<f32>(n.y, n.x))) * signs;
        n = vec3<f32>(xy, n.z);
    }
    return normalize(n);
}

// Screen-space motion in UV units, from this frame's and last frame's clip positions. Both must
// be genuine clip-space vectors: a fragment's @builtin(position) is *not* one (it is in
// framebuffer pixels with w = 1 / clip.w), so use screenVelocityAt() for that.
fn screenVelocity(clip: vec4<f32>, prevClip: vec4<f32>) -> vec2<f32> {
    let now = clip.xy / max(abs(clip.w), 1e-6) * sign(max(clip.w, 1e-6));
    let before = prevClip.xy / max(abs(prevClip.w), 1e-6) * sign(max(prevClip.w, 1e-6));
    // NDC -> UV: x maps directly, y flips.
    return vec2<f32>((now.x - before.x) * 0.5, (before.y - now.y) * 0.5);
}

// Screen-space motion in UV units when the current position comes from the fragment stage's
// @builtin(position). Dividing that by its own w is meaningless - it yields pixel * clip.w, which
// is what made the velocity target read hundreds of screens per frame - so the UV comes straight
// from the framebuffer coordinate instead.
fn screenVelocityAt(fragCoord: vec4<f32>, prevClip: vec4<f32>) -> vec2<f32> {
    let nowUv = fragCoord.xy * frame.targetSize.zw;
    let before = prevClip.xy / max(abs(prevClip.w), 1e-6) * sign(max(prevClip.w, 1e-6));
    let beforeUv = vec2<f32>(before.x * 0.5 + 0.5, 0.5 - before.y * 0.5);
    return nowUv - beforeUv;
}

fn packIds(objectId: f32, materialId: f32) -> u32 {
    return (u32(clamp(objectId, 0.0, 65535.0)) & 0xFFFFu) | ((u32(clamp(materialId, 0.0, 65535.0)) & 0xFFFFu) << 16u);
}

// `flags` in the normal target's alpha: 1 = lit surface, 2 = emissive, 4 = transparent, 8 = sky.
fn packNormalRoughness(normal: vec3<f32>, roughness: f32, flags: f32) -> vec4<f32> {
    return vec4<f32>(octEncode(normal), clamp(roughness, 0.0, 1.0), flags);
}
