// Environment background: one fullscreen triangle at the far plane. The sky is whatever the IBL
// was built from -- an equirectangular HDRI read at its own resolution (ADR-049) or the procedural
// sky's prefiltered cube (ADR-036) -- and the flat background colour when there is neither.
#include "common.wgsl"

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;
@group(3) @binding(4) var skyEquirect: texture_2d<f32>;
@group(3) @binding(5) var skySampler: sampler; // as iblSampler, but wrapping in longitude

const SKY_PI: f32 = 3.14159265;

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

// The parameterisation shaders/environment.wgsl builds the cube with. One definition, so the sky
// you see and the lighting made from it cannot end up rotated differently.
fn skyEquirectUv(dir: vec3<f32>) -> vec2<f32> {
    let phi = atan2(dir.z, dir.x);
    let theta = acos(clamp(dir.y, -1.0, 1.0));
    return vec2<f32>(0.5 + phi / (2.0 * SKY_PI), theta / SKY_PI);
}

// Mip level for an equirect sampled through a view ray, chosen by hand rather than left to the
// hardware because atan2 wraps at the antimeridian: the 2x2 quad straddling the seam sees a
// full-texture derivative and would take the coarsest mip, drawing a blurred column down the sky.
fn skyEquirectLod(uv: vec2<f32>, size: vec2<f32>) -> f32 {
    var dx = dpdx(uv);
    var dy = dpdy(uv);
    if (abs(dx.x) > 0.5) { dx.x = dx.x - sign(dx.x); }
    if (abs(dy.x) > 0.5) { dy.x = dy.x - sign(dy.x); }
    let footprint = max(length(dx * size), length(dy * size));
    return max(log2(max(footprint, 1.0)), 0.0);
}

@fragment
fn fs_sky(in: SkyOut) -> SceneOut {
    let near = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let far = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    // The two unprojected points differ by the camera position, so their difference -- and every
    // sample taken along it below -- depends on the camera's orientation alone. That is what puts
    // the sky at infinity: crossing the 640 m valley does not move it.
    let dir = normalize(far.xyz / far.w - near.xyz / near.w);
    var color = frame.skyParams.rgb;
    var isSky = false;
    // ADR-036: a procedural sky is an IBL source first; it only stands behind the scene when the
    // scene asks for it (skyExtra.y), so existing looks keep their flat background.
    if (frame.envParams.w >= 0.5 && frame.skyExtra.y >= 0.5) {
        let d = envRotate(dir);
        if (frame.skyExtra.x >= 0.5) {
            // The analytic sky has no map behind it; the prefiltered cube is its only form, and
            // its own `intensity` (params.w) is what has always scaled it.
            let mip = frame.skyParams.w * frame.envParams.y;
            color = textureSampleLevel(prefilteredMap, iblSampler, d, mip).rgb * frame.params.w;
        } else {
            // ADR-049: read the HDRI itself. A 128 px prefiltered cube face is under 3 texels per
            // degree; a star is one texel of an 8K map and does not survive being resampled to
            // that, which is why the visible sky does not share the IBL's cube.
            let uv = skyEquirectUv(d);
            let levels = f32(textureNumLevels(skyEquirect) - 1u);
            let lod = min(max(skyEquirectLod(uv, vec2<f32>(textureDimensions(skyEquirect, 0))),
                              frame.skyParams.w * levels), levels);
            color = textureSampleLevel(skyEquirect, skySampler, uv, lod).rgb;
        }
        // ADR-049: the visible sky's own intensity. Shading multiplies by params.w instead, so a
        // dark sky can still cast a useful amount of light and vice versa.
        color *= frame.skyExtra.z;
        isSky = true;
    }
    if (frame.lightCounts.z > 0.5 && frame.skyExtra.x > 0.5 && isSky) {
        if (frame.envParams.z > 0.5 && frame.lights[0].positionType.w < 0.5) {
            let separation = length(dir + normalize(frame.lights[0].directionRange.xyz));
            let moon = 1.0 - smoothstep(0.014, 0.018, separation);
            color = mix(color, vec3<f32>(1.8, 2.1, 2.4), moon);
        }
        let coordinate = dir.xz / max(dir.y + 1.0, 0.02) * 190.0;
        let cell = floor(coordinate);
        let random = hash21(cell);
        let offset = vec2<f32>(hash21(cell + 17.0), hash21(cell + 43.0)) * 0.6 + 0.2;
        let radius = length(fract(coordinate) - offset);
        let footprint = max(length(fwidth(coordinate)), 0.015);
        let star = (1.0 - smoothstep(0.02, 0.02 + footprint, radius))
                   * min(0.0064 / (footprint * footprint), 1.0);
        color += vec3<f32>(0.5, 0.7, 1.0) * star * step(0.988, random)
                 * smoothstep(0.05, 0.35, dir.y) * 0.6;
    }
    // The sky is at infinity, so its velocity is the camera's rotation alone. Its bloom weight is
    // whatever the scene grants it (ADR-049 `skyBloom`); at the default 0 a bright environment
    // still cannot glow through the emission target, exactly as under ADR-035.
    let prevClip = frame.prevViewProj * vec4<f32>(frame.cameraPos.xyz + dir * 1.0e6, 1.0);
    var out: SceneOut;
    out.color = vec4<f32>(color, 1.0);
    out.normalRoughness = packNormalRoughness(-dir, 1.0, 8.0);
    out.velocity = screenVelocityAt(in.clip, prevClip);
    out.emission = vec4<f32>(select(vec3<f32>(0.0), color * frame.skyExtra.w, isSky), 0.0);
    out.ids = 0u;
    return out;
}
