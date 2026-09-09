// Volumetric atmosphere (ADR-032). Two passes, both a fullscreen triangle, encoded by
// rendering::VolumeRenderer right after the lit pass and before the post chain:
//
//   fs_volume     half-resolution raymarch into an RGBA16F target: rgb = in-scattered radiance
//                 that reached the eye, a = transmittance along the ray. Reads the scene depth
//                 so the march stops at the first surface (the fog is occluded correctly).
//   fs_composite  full resolution, into the HDR target with blending
//                 (src One, dst SrcAlpha) so the result is `scatter + hdr * transmittance`.
//                 The half-res texels are upsampled with a depth-aware bilinear filter: each of
//                 the four neighbours is weighted by how close the depth it marched (the
//                 full-res depth at 2*texel) is to this pixel's, so fog does not bleed across
//                 silhouettes.
//
// Density (the model in scene_types.hpp Environment):
//   density = volumeDensity * exp(-max(0, y - fogHeight) * fogHeightFalloff)
//             * (1 + volumeNoiseAmount * (fbm3(p * volumeNoiseScale + t * volumeNoiseSpeed) * 2 - 1))
//             * densityField(p)                                    (1 when no field is named)
// Extinction is density * volumeAbsorption, in-scatter density * volumeScattering * the key
// light through a Henyey-Greenstein phase (volumeAnisotropy), emission density * volumeEmission
// tinted by the colour field (or fogColor). Step starts are jittered by a hash of the pixel and
// the frame index - never the wall clock - so two renders of the same frame are identical.
//
// Bind groups: 0 = frame (common.wgsl) + the grid table (fields.wgsl); 1 = this pass's own
// {1 VolumeUniforms, 2 FieldBlock, 3 scene depth, 4 the half-res volume texture (composite only)}.
// Binding 0 of group 1 is deliberately empty: common.wgsl declares `object` there and no entry
// point below reads it. Mirrors rendering/volume_renderer.hpp.
#include "common.wgsl"
#include "fields.wgsl"

struct VolumeUniforms {
    params0: vec4<f32>,   // density, fogHeight, fogHeightFalloff, scattering
    params1: vec4<f32>,   // absorption, anisotropy, emission, maxDistance
    noiseParams: vec4<f32>, // noiseAmount, noiseScale, noiseSpeed, time (seconds)
    info: vec4<f32>,      // steps, density field slot (-1 none), colour field slot (-1 none), frame index
    sizes: vec4<f32>,     // half width, half height, full width, full height
    depthParams: vec4<f32>, // camera near, camera far, 0, 0
    fogColor: vec4<f32>,  // rgb = emission tint when no colour field is named
    glow: vec4<f32>,      // x = particle glow entries to read (ADR-040), yzw = 0
};

@group(1) @binding(1) var<uniform> vol: VolumeUniforms;
@group(1) @binding(2) var<uniform> fieldBlock: FieldBlock;
@group(1) @binding(3) var sceneDepth: texture_depth_2d;
@group(1) @binding(4) var volumeTex: texture_2d<f32>;
// ADR-040: two vec4 per particle system - (centre.xyz, spread radius) and (colour.rgb, power) -
// written by particles.wgsl's cs_glow_top. Slots past vol.glow.x are zero.
@group(1) @binding(5) var<storage, read> particleGlow: array<vec4<f32>>;

struct FsIn {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

// The usual oversized triangle: uv (0,0) top-left to (1,1) bottom-right.
@vertex
fn vs_volume(@builtin(vertex_index) index: u32) -> FsIn {
    var out: FsIn;
    let uv = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    out.uv = uv;
    out.pos = vec4<f32>(uv * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0), 0.0, 1.0);
    return out;
}

fn ndcOf(uv: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// World position of an NDC point at clip depth z (0 = near plane, 1 = far plane).
fn worldAt(ndc: vec2<f32>, z: f32) -> vec3<f32> {
    let p = frame.invViewProj * vec4<f32>(ndc, z, 1.0);
    return p.xyz / p.w;
}

// View-space distance a clip depth stands for (used only to weight the upsample).
fn linearDepth(z: f32) -> f32 {
    let near = vol.depthParams.x;
    let far = vol.depthParams.y;
    return near * far / max(far - z * (far - near), 1e-6);
}

// Deterministic jitter in [0, 1) from the pixel and the frame index (no wall clock).
fn stepJitter(px: vec2<i32>, frameIndex: u32) -> f32 {
    let h = pcg3d(vec3<u32>(bitcast<u32>(px.x), bitcast<u32>(px.y), frameIndex));
    return f32(h.x) * (1.0 / 4294967296.0);
}

fn volumeDensityAt(p: vec3<f32>) -> f32 {
    let heightTerm = exp(-max(0.0, p.y - vol.params0.y) * vol.params0.z);
    var base = vol.params0.x * heightTerm;
    let densitySlot = i32(vol.info.y);
    if (densitySlot >= 0) {
        base = base * max(0.0, fieldScalar(densitySlot, p));
    }
    // The noise is the expensive term (a 3-octave fBM): only evaluate it where there is fog.
    if (base <= 1e-6 || vol.noiseParams.x == 0.0) {
        return max(0.0, base);
    }
    let offset = vec3<f32>(vol.noiseParams.z * vol.noiseParams.w);
    let noiseTerm = 1.0 + vol.noiseParams.x * (fbm3(p * vol.noiseParams.y + offset, 17u) * 2.0 - 1.0);
    return max(0.0, base * noiseTerm);
}

// Henyey-Greenstein phase function; g = 0 is isotropic.
fn henyeyGreenstein(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * d * sqrt(d));
}

// Radiance the key light (the first enabled light) delivers at p, and the direction towards it.
struct KeyLight {
    radiance: vec3<f32>,
    towards: vec3<f32>,
};

fn keyLightAt(p: vec3<f32>) -> KeyLight {
    var out: KeyLight;
    out.radiance = vec3<f32>(0.0);
    out.towards = vec3<f32>(0.0, 1.0, 0.0);
    if (frame.envParams.z < 0.5) {
        return out;
    }
    let light = frame.lights[0];
    let kind = light.positionType.w;
    if (kind < 0.5) { // directional
        out.towards = -light.directionRange.xyz;
        out.radiance = light.colorIntensity.rgb;
        return out;
    }
    let toLight = light.positionType.xyz - p;
    let dist2 = max(dot(toLight, toLight), 1e-4);
    out.towards = toLight * inverseSqrt(dist2);
    var attenuation = 1.0 / dist2;
    let range = light.directionRange.w;
    if (range > 0.0) {
        let ratio = clamp(1.0 - pow(sqrt(dist2) / range, 4.0), 0.0, 1.0);
        attenuation = attenuation * ratio * ratio;
    }
    if (kind > 1.5) { // spot
        let cosAngle = dot(light.directionRange.xyz, -out.towards);
        attenuation = attenuation * clamp((cosAngle - light.cone.x) * light.cone.y, 0.0, 1.0);
    }
    out.radiance = light.colorIntensity.rgb * attenuation;
    return out;
}

// ADR-040: emissive particles light the dust around them. Each system is reduced on the GPU to
// one sphere - the emission-weighted centroid of its alive particles, the standard deviation of
// their positions, the mean colour and the total power - and the march treats it as a soft
// luminous ball whose radiance falls off as a Gaussian at the cloud's own spread. The coupling is
// one-directional and costs one Gaussian per step per system: the volume never touches the
// simulation, so determinism is untouched.
// One particle at full opacity and unit emissive intensity is treated as a point emitter of this
// radiant intensity. Small on purpose: a spark is a millimetre of hot metal, and a burst of ten
// thousand of them should read as a glow in the dust, not as a second sun.
const kParticleGlowIntensity: f32 = 0.0015;

fn particleGlowAt(p: vec3<f32>) -> vec3<f32> {
    let count = u32(vol.glow.x);
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let centre = particleGlow[i * 2u];
        let color = particleGlow[i * 2u + 1u];
        if (color.w <= 0.0) { continue; }
        let radius = max(centre.w, 0.25);
        let d = p - centre.xyz;
        let falloff = exp(-dot(d, d) / (2.0 * radius * radius));
        // Power is the summed emissive weight of the alive particles; spreading it over the
        // cloud's surface keeps a wide cloud from being as bright as a tight one.
        sum += color.rgb * (color.w * kParticleGlowIntensity * falloff / (4.0 * PI * radius * radius));
    }
    return sum;
}

@fragment
fn fs_volume(in: FsIn) -> @location(0) vec4<f32> {
    let halfPx = vec2<i32>(floor(in.pos.xy));
    // The full-resolution texel this half-res pixel marches (the composite pass uses the same
    // rule to decide which neighbour saw which surface).
    let fullPx = vec2<i32>(min(halfPx.x * 2, i32(vol.sizes.z) - 1), min(halfPx.y * 2, i32(vol.sizes.w) - 1));
    let ndc = ndcOf((vec2<f32>(fullPx) + vec2<f32>(0.5)) / vol.sizes.zw);
    let origin = worldAt(ndc, 0.0);
    let farPoint = worldAt(ndc, 1.0);
    let direction = normalize(farPoint - origin);

    let sceneZ = textureLoad(sceneDepth, fullPx, 0);
    var maxDistance = vol.params1.w;
    if (sceneZ < 1.0) {
        maxDistance = min(maxDistance, distance(worldAt(ndc, sceneZ), origin));
    }
    let steps = max(i32(vol.info.x), 1);
    let stepLength = maxDistance / f32(steps);
    if (stepLength <= 0.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let jitter = stepJitter(halfPx, u32(vol.info.w));
    let anisotropy = clamp(vol.params1.y, -0.95, 0.95);
    let colorSlot = i32(vol.info.z);

    var transmittance = 1.0;
    var scattered = vec3<f32>(0.0);
    for (var i = 0; i < steps; i = i + 1) {
        let t = (f32(i) + jitter) * stepLength;
        let p = origin + direction * t;
        let density = volumeDensityAt(p);
        if (density <= 0.0) {
            continue;
        }
        let extinction = density * vol.params1.x;
        let scattering = density * vol.params0.w;
        let key = keyLightAt(p);
        let phase = henyeyGreenstein(dot(direction, key.towards), anisotropy);
        var emission = vec3<f32>(0.0);
        if (vol.params1.z > 0.0) {
            var emissionColor = vol.fogColor.rgb;
            if (colorSlot >= 0) {
                let c = fieldColor(colorSlot, p);
                emissionColor = c.rgb * c.a;
            }
            emission = density * vol.params1.z * emissionColor;
        }
        // The particle glow arrives as light to scatter, not as fog emission, so denser dust
        // catches more of it - which is what reads as "the sparks are lighting the dust".
        let source = scattering * (phase * key.radiance + particleGlowAt(p)) + emission;
        scattered = scattered + transmittance * source * stepLength;
        transmittance = transmittance * exp(-extinction * stepLength);
        if (transmittance < 0.002) {
            break;
        }
    }
    return vec4<f32>(scattered, transmittance);
}

@fragment
fn fs_composite(in: FsIn) -> @location(0) vec4<f32> {
    let fullPx = vec2<i32>(floor(in.pos.xy));
    let myDepth = linearDepth(textureLoad(sceneDepth, fullPx, 0));
    // Bilinear footprint in half-res texel space.
    let coord = (vec2<f32>(fullPx) + vec2<f32>(0.5)) * 0.5 - vec2<f32>(0.5);
    let base = floor(coord);
    let f = coord - base;
    let bx = i32(base.x);
    let by = i32(base.y);
    let maxX = i32(vol.sizes.x) - 1;
    let maxY = i32(vol.sizes.y) - 1;
    let fullMaxX = i32(vol.sizes.z) - 1;
    let fullMaxY = i32(vol.sizes.w) - 1;

    var accum = vec4<f32>(0.0);
    var weightSum = 0.0;
    for (var j = 0; j < 2; j = j + 1) {
        for (var i = 0; i < 2; i = i + 1) {
            let hx = clamp(bx + i, 0, maxX);
            let hy = clamp(by + j, 0, maxY);
            let bilinear = (f32(1 - i) + f32(2 * i - 1) * f.x) * (f32(1 - j) + f32(2 * j - 1) * f.y);
            let sourcePx = vec2<i32>(min(hx * 2, fullMaxX), min(hy * 2, fullMaxY));
            let theirDepth = linearDepth(textureLoad(sceneDepth, sourcePx, 0));
            let w = bilinear / (1.0 + 8.0 * abs(theirDepth - myDepth) / max(myDepth, 1e-3));
            accum = accum + textureLoad(volumeTex, vec2<i32>(hx, hy), 0) * w;
            weightSum = weightSum + w;
        }
    }
    if (weightSum <= 1e-6) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let result = accum / weightSum;
    return vec4<f32>(result.rgb, clamp(result.a, 0.0, 1.0));
}
