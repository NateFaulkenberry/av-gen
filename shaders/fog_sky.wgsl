// The surface fog's view of the sky (ADR-918): a small map of the sky's radiance by direction,
// rebuilt each frame the fog asks for it, and sampled by `applyFog` (common.wgsl) along each ray.
//
// Aerial perspective is light the air scatters towards the eye, and what lights the air is the sky
// around it. The fog used to fade every surface towards one constant colour, so a ridge seen
// against a bright horizon -- the aurora behind the valley rim -- faded towards navy and stood as a
// dark cut-out against the sky it should have been dissolving into. Taking the colour from the sky
// in the ray's own direction is the standard cheap form of the effect: the far rim fades into the
// sky behind it.
//
// What the map holds, texel by texel, is exactly what the camera would see there -- the background
// through `skyBackgroundAt` (the pass's own code, shared with skybox.wgsl) plus the atmospheric
// layer through `atmosphereSkyAt` (the aurora and comets, shared with atmosphere.wgsl) -- averaged
// over the texel's footprint. Not the stars or the crisp moon disc: points of light are not
// something air scatters into a fog colour.
//
// The parameterisation, which common.wgsl's `fogSkyUv` inverts:
//   u = 0.5 + azimuth / tau    the sky's own equirect convention (sky_background.wgsl)
//   v = sqrt(elevation sine)   upper hemisphere only, twice as many rows near the horizon as a
//                              linear map, because that is where the sky changes fastest and where
//                              a fogged ridge sits
// Below the horizon the fog reads row 0, the horizon: the air between the eye and the valley floor
// is lit by the sky above it, not by the ground colour the sky draws beneath its horizon.
//
// Deterministic: a pure function of the frame block -- the sky's parameters, the atmospheric
// effects' state (packed from the transport second) and the camera's position.

#include "common.wgsl"
#include "sky_background.wgsl"
#include "atmosphere_fx.wgsl"

// Mirrors SceneRenderer::kFogSkyWidth / kFogSkyHeight. The map's own texture is the placeholder in
// this pass (a pass may not sample what it renders), so its size cannot be asked for.
const kFogSkyWidth: f32 = 128.0;
const kFogSkyHeight: f32 = 32.0;
const kFogSkyTau: f32 = 6.28318530718;
// Samples a side per texel: the average that makes the map a low-pass of the sky rather than a
// point sample of it.
const kFogSkySubSamples: i32 = 4;

struct FogSkyOut {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_fog_sky(@builtin(vertex_index) index: u32) -> FogSkyOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: FogSkyOut;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

// The direction a point of the map stands for. `uv` has (0, 0) at the texture's top left, so row 0
// is the horizon and the last row the zenith.
fn fogSkyDirection(uv: vec2<f32>) -> vec3<f32> {
    let phi = (uv.x - 0.5) * kFogSkyTau;
    let y = clamp(uv.y * uv.y, 0.0, 1.0);
    let r = sqrt(max(1.0 - y * y, 0.0));
    return vec3<f32>(cos(phi) * r, y, sin(phi) * r);
}

@fragment
fn fs_fog_sky(in: FogSkyOut) -> @location(0) vec4<f32> {
    let texel = floor(in.clip.xy);
    let size = vec2<f32>(kFogSkyWidth, kFogSkyHeight);
    let n = f32(kFogSkySubSamples);
    // The angle one sub-sample spans in azimuth: the comets' anti-aliasing width and the analytic
    // sun disc's floor, so neither is sampled finer than this pass samples the sky.
    let sampleAngle = kFogSkyTau / (kFogSkyWidth * n);
    // The HDRI's mip whose texel is about one sub-sample, and never finer than the visible sky's
    // own blur asks for.
    let equirectSize = vec2<f32>(textureDimensions(skyEquirect, 0));
    let equirectLevels = f32(textureNumLevels(skyEquirect) - 1u);
    let equirectLod = clamp(max(log2(max(equirectSize.x / (kFogSkyWidth * n), 1.0)),
                                frame.skyParams.w * equirectLevels), 0.0, equirectLevels);
    // Whether the sky is drawn behind the world at all (SceneRenderer, `frame.fogSky.y`): with the
    // skybox off the background is the flat colour, whatever the IBL would have shown.
    let skyDrawn = frame.fogSky.y > 0.5;
    var sum = vec3<f32>(0.0);
    for (var j = 0; j < kFogSkySubSamples; j = j + 1) {
        for (var i = 0; i < kFogSkySubSamples; i = i + 1) {
            let uv = (texel + (vec2<f32>(f32(i), f32(j)) + 0.5) / n) / size;
            let dir = fogSkyDirection(uv);
            var radiance = frame.skyParams.rgb;
            if (skyDrawn) {
                radiance = skyBackgroundAt(dir, equirectLod, sampleAngle);
            }
            radiance = radiance + atmosphereSkyAt(frame.cameraPos.xyz, dir, sampleAngle).radiance;
            sum = sum + radiance;
        }
    }
    return vec4<f32>(sum / (n * n), 1.0);
}
