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

// ADR-345: the analytic sky, evaluated here rather than sampled from a cube.
//
// This is shaders/environment.wgsl's `skyRadiance` reading the frame block instead of the
// environment processor's, because the two passes cannot see each other's uniforms. It is
// deliberately the same maths: the sky the camera sees and the sky the IBL was built from have to
// be the same sky, and the way that goes wrong is two implementations drifting. The one difference
// is the trailing intensity multiply, which is left to the caller here -- the background pass
// already applies `skyExtra.z`, the visible sky's own intensity, and applying both would square it.
fn skySmoothstepF(e0: f32, e1: f32, x: f32) -> f32 {
    if (e0 == e1) { return select(1.0, 0.0, x < e0); }
    let t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}

fn skyRadianceFrame(dir: vec3<f32>, minRadius: f32) -> vec3<f32> {
    let d = normalize(dir);
    let hazeWidth = max(frame.skyZenithColor.w, 1e-3);
    let haze = exp(-saturate(d.y) / hazeWidth);
    let gradient = mix(frame.skyZenithColor.rgb, frame.skyHorizonColor.rgb, haze);
    let band = skySmoothstepF(-0.03, 0.03, d.y);
    let base = mix(frame.skyGroundColor.rgb, gradient, band);

    let cosTheta = clamp(dot(d, frame.skySun.xyz), -1.0, 1.0);
    let theta = acos(cosTheta);
    let sunRadius = max(frame.skyHorizonColor.w, 1e-3);
    let radius = max(sunRadius, max(minRadius, 0.0));
    let energy = (sunRadius / radius) * (sunRadius / radius);
    let disc = (1.0 - skySmoothstepF(radius * 0.85, radius * 1.15, theta)) * energy;
    let glow = exp(-theta / max(frame.skyGroundColor.w, 1e-3)) * 0.02; // SKY_AUREOLE, scene/sky.cpp
    let sun = frame.skySunRadiance.rgb * (disc + glow) * band;
    return base + sun;
}

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

// ---- Effect Library Wave 2: the Stars effect (world/effects/star_field.hpp) ---------------------
// The fixed field's cell layout and footprint anti-aliasing, with the effect's controls: a density,
// a power-law magnitude per star, a temperature per star, scintillation strongest near the horizon,
// a galactic band, and a bright sky hiding what is behind it. Pure in (direction, second).
fn starSmoothNoise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = hash21(i);
    let b = hash21(i + vec2<f32>(1.0, 0.0));
    let c = hash21(i + vec2<f32>(0.0, 1.0));
    let d = hash21(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn effectStars(dir: vec3<f32>, sky: vec3<f32>) -> vec3<f32> {
    let brightness = frame.starsA.z;
    let horizon = frame.starsB.w;
    let fade = smoothstep(horizon * 0.15, horizon, dir.y);
    let hide = 1.0 - frame.starsC.z * smoothstep(0.02, 0.4, dot(sky, vec3<f32>(0.2126, 0.7152, 0.0722)));
    let coordinate = dir.xz / max(dir.y + 1.0, 0.02) * 190.0;
    let footprint = max(length(fwidth(coordinate)), 0.015);
    // The band: a great circle tilted `bandTilt` from the horizon, Gaussian across.
    let tilt = frame.starsC.y;
    let across = dot(dir, vec3<f32>(0.0, cos(tilt), sin(tilt)));
    let band = exp(-(across * across) / (0.14 * 0.14)) * frame.starsC.x;

    let cell = floor(coordinate);
    let exists = step(1.0 - frame.starsA.y * (1.0 + 3.0 * band), hash21(cell));
    let offset = vec2<f32>(hash21(cell + 17.0), hash21(cell + 43.0)) * 0.6 + 0.2;
    let radius = length(fract(coordinate) - offset);
    let star = (1.0 - smoothstep(0.02, 0.02 + footprint, radius)) * min(0.0064 / (footprint * footprint), 1.0);
    let magnitude = pow(hash21(cell + 71.0), frame.starsA.w);
    let temperature = hash21(cell + 97.0);
    let tint = mix(vec3<f32>(0.5, 0.7, 1.0),
                   mix(vec3<f32>(0.55, 0.72, 1.0), vec3<f32>(1.0, 0.7, 0.42), temperature), frame.starsB.x);
    // Scintillation: deepest at the horizon, where the light crosses the most air. Each star's rate is
    // snapped to whole cycles per 256 s, so the wrapped second the frame carries never jumps.
    let air = 1.0 - smoothstep(0.0, 0.6, max(dir.y, 0.0));
    let depth = frame.starsB.y * mix(0.35, 1.0, air);
    let rate = frame.starsB.z * (0.7 + 0.6 * hash21(cell + 131.0));
    let cycles = round(rate * 256.0 / 6.2831853);
    let phase = 6.2831853 * fract(cycles * frame.starsC.w / 256.0 + hash21(cell + 151.0));
    let twinkle = 1.0 - depth * (0.5 + 0.5 * sin(phase));

    let points = tint * (brightness * magnitude * star * exists * twinkle);
    // The band's glow is mottled at two scales and split by a dark rift, as a galaxy's dust lanes do.
    let mottle = 0.6 * starSmoothNoise(coordinate * 0.03) + 0.4 * starSmoothNoise(coordinate * 0.11 + 13.0);
    let rift = smoothstep(0.3, 0.65, starSmoothNoise(coordinate * 0.018 + 71.0));
    let glow = vec3<f32>(0.34, 0.36, 0.46) * band * brightness * 0.02 * (0.25 + 0.75 * mottle) * (0.35 + 0.65 * rift);
    return (points + glow) * fade * max(hide, 0.0);
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
        if (frame.skySunRadiance.w >= 0.5) {
            // ADR-345: an HDRI is lighting the scene and the scene has asked for the procedural
            // sky behind it. One pixel of sky is a few ALU here against a cube fetch, and it is
            // the only way to get a background whose colours move with a day/night cycle while
            // the lighting comes from a map.
            color = skyRadianceFrame(d, 0.0);
        } else if (frame.skyExtra.x >= 0.5) {
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
        // Where the *sky* says its sun or moon is, not where the first light in the scene happens
        // to point. Those are the same thing only when the key light is also light zero, and when
        // they were not, the sky's own soft disc and this crisp one sat in different parts of the
        // sky -- two moons, one of them glowing.
        if (frame.skySun.w > 0.5) {
            let separation = length(dir - normalize(frame.skySun.xyz));
            let moon = 1.0 - smoothstep(0.014, 0.018, separation);
            color = mix(color, vec3<f32>(1.8, 2.1, 2.4), moon);
        }
        if (frame.starsA.x < 0.5) {
            // The fixed field, drawn when no Stars effect owns the sky. Untouched by Wave 2.
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
    }
    // Effect Library Wave 2: a Stars effect owns the star field, on any sky (the uniform `on` flag
    // is the gate: with none the frame is exactly the fixed field's above).
    if (frame.starsA.x > 0.5 && isSky) {
        color += effectStars(dir, color);
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
