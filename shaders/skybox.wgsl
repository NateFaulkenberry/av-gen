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
fn fs_sky(in: SkyOut) -> SceneOut {
    let near = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let far = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let dir = normalize(far.xyz / far.w - near.xyz / near.w);
    var color = frame.skyParams.rgb;
    // ADR-036: a procedural sky is an IBL source first; it only stands behind the scene when the
    // scene asks for it (skyExtra.y), so existing looks keep their flat background.
    if (frame.envParams.w >= 0.5 && frame.skyExtra.y >= 0.5) {
        let mip = frame.skyParams.w * frame.envParams.y;
        color = textureSampleLevel(prefilteredMap, iblSampler, envRotate(dir), mip).rgb * frame.params.w;
    }
    // The sky is at infinity: its velocity is the camera rotation alone, and it carries no bloom
    // weight so a bright environment does not glow through the emission target (ADR-035).
    let prevClip = frame.prevViewProj * vec4<f32>(frame.cameraPos.xyz + dir * 1.0e6, 1.0);
    var out: SceneOut;
    out.color = vec4<f32>(color, 1.0);
    out.normalRoughness = packNormalRoughness(-dir, 1.0, 8.0);
    out.velocity = screenVelocity(in.clip, prevClip);
    out.emission = vec4<f32>(0.0, 0.0, 0.0, 0.0);
    out.ids = 0u;
    return out;
}
