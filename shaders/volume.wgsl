// Volumetric atmosphere (ADR-032). Two passes, both a fullscreen triangle, encoded by
// rendering::VolumeRenderer right after the lit pass and before the post chain:
//
//   fs_volume     raymarch into an RGBA16F target at `QualitySettings::volumeResolutionScale` of
//                 the scene's resolution (ADR-139; 0.5 is the shipped default, 1.0 is per pixel
//                 and is what Offline renders): rgb = in-scattered radiance
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
// ADR-388. After fields.wgsl, which is what brings in noise.wgsl's fbm3; the include directive
// does not de-duplicate, so this file must not include noise.wgsl itself.
#include "vortex.wgsl"

struct VolumeUniforms {
    params0: vec4<f32>,   // density, fogHeight, fogHeightFalloff, scattering
    params1: vec4<f32>,   // absorption, anisotropy, emission, maxDistance
    noiseParams: vec4<f32>, // noiseAmount, noiseScale, noiseSpeed, time (seconds)
    info: vec4<f32>,      // steps, density field slot (-1 none), colour field slot (-1 none), frame index
    sizes: vec4<f32>,     // march width, march height, full width, full height (ADR-139)
    depthParams: vec4<f32>, // camera near, camera far, 0, 0
    fogColor: vec4<f32>,  // rgb = emission tint when no colour field is named
    glow: vec4<f32>,      // x = particle glow entries to read (ADR-040), yzw = 0
    // ADR-371: the cosmic vortex. All zero -- specifically vortex0.w (the radius) at zero -- means
    // every function below returns before it does any work, so a scene that does not ask for one
    // marches exactly what it always marched.
    vortex0: vec4<f32>,   // centre xyz, radius (0 = no vortex)
    vortex1: vec4<f32>,   // thickness, swirl, rotationSpeed, density
    vortex2: vec4<f32>,   // innerVoid, contrast, turbulence, turbulenceScale
    vortex3: vec4<f32>,   // breathAmount, breathSpeed, emission, filaments
    vortexA: vec4<f32>,   // deep colour
    vortexB: vec4<f32>,   // mid colour
    vortexAccent: vec4<f32>, // luminous accent
    vortex4: vec4<f32>,   // ADR-374: funnel depth, throat radius fraction, throat density, 0
    vortex5: vec4<f32>,   // ADR-381/388: comet response, reach, scene scattering, 0
    vortex6: vec4<f32>,   // ADR-389: smokeWarp, smokeBillow, detail, 0
    // Vortex 2.0 §7-§11, the macro structure -- eye, eye wall and spiral bands. All zero means
    // the field evaluates ADR-389's envelope exactly, so this is additive in the same sense
    // `vortex0.w == 0` is: a scene that asks for nothing gets the frame it got before.
    vortex7: vec4<f32>,   // eyeWallWidth, eyeWallGain, cloudNoise, 0
    vortex8: vec4<f32>,   // bandArms, cot(bandPitch), bandDepth, bandHarmonic
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
//
// ADR-461: scaled by `Environment::volumeJitter` (depthParams.z), and centred on the middle of the
// step so that lowering it converges on the step's midpoint rather than on its start -- the mean
// sample position must not move, or the medium's integrated density moves with it and every
// per-metre coefficient calibrated against it is wrong (ADR-374/379/381/389's family).
//
// Why it is a control at all: jitter turns banding into noise, which is a good trade when the
// medium varies LITTLE across one step. The cosmic vortex is the opposite -- at 4000 m over 32
// steps a step is 125 metres and the funnel changes completely across one, so a full-step offset
// between neighbouring pixels is 125 metres of uncorrelated displacement and it reads as
// salt-and-pepper. ADR-460 measured that on a field with the noise switched off entirely and the
// grain still fell 61% when the steps went up eight times; this is the same artifact from the
// other side, for nothing.
//
// An ORDERED offset was tried first and is not the answer: interleaved gradient noise at the same
// amplitude moved the grain by -11% where the field is smooth and **+8% on the shipped frame**,
// where the field is aliased noise and a structured sample pattern exposes error that an
// independent one averages away. That is ADR-389's weight-based band-limit finding again, in a
// different mechanism. The amount is the lever; the arrangement is not.
fn stepJitter(px: vec2<i32>, frameIndex: u32) -> f32 {
    let h = pcg3d(vec3<u32>(bitcast<u32>(px.x), bitcast<u32>(px.y), frameIndex));
    let u = f32(h.x) * (1.0 / 4294967296.0);
    return 0.5 + clamp(vol.depthParams.z, 0.0, 1.0) * (u - 0.5);
}

// ADR-371: the cosmic vortex, as a world-space density field inside the volumetric march.
//
// WHY HERE and not a new pass or a skybox. The brief is emphatic that this is not a backdrop: it
// must sit physically below the island, be seen in perspective, be occluded by the rock, gain depth
// as the camera drops and flatten as it rises. The volumetric pass already gives every one of those
// for free -- it marches world space, it stops at the depth buffer, and ADR-139's half-resolution
// march with a depth-aware upsample is the scalable quality knob the brief asks for. A second
// raymarcher would be a second set of all of that, drifting.
//
// The model is the polar construction the brief describes: radius and angle about the centre, the
// angle sheared by radius so the structure winds, then domain-warped noise at three spatial and
// three TEMPORAL rates. The last part is what stops it reading as a screensaver -- a single rate
// makes everything move together, which the eye reads instantly as procedural.
// ADR-388: the funnel's shape comes from `shaders/vortex.wgsl` now, which is the transliteration
// of `core/vortex.cpp`. It used to live here, in this file, which meant the volumetric march was
// the only thing in the engine that could ask where the vortex was -- particles approximated it
// with an attractor and an orbit force (ADR-380), and anything else had to guess again.
//
// This wrapper exists so the march reads the five uniform slots it already uploads. The numbers
// and their order are unchanged, which is what makes the Tree of Life byte-identical across this
// move rather than something to re-tune.
// ADR-389: the march passes its own step length so the field can drop the octaves this many
// samples cannot resolve. `info.x` is the step count and `params1.w` the far distance, which is the
// same pair `fs_march` divides to get `stepLength` -- kept in one expression here so the two cannot
// disagree about how finely this frame is being sampled.
fn vortexFilterWidth() -> f32 {
    return vol.params1.w / max(vol.info.x, 1.0);
}

fn vortexShape(p: vec3<f32>, t: f32) -> f32 {
    return vortexShapeAt(VortexUniformsWgsl(vol.vortex0, vol.vortex1, vol.vortex2, vol.vortex3,
                                            vol.vortex4, vol.vortex6, vol.vortex7, vol.vortex8),
                         p, t, vortexFilterWidth());
}

// The vortex's own light. It is emissive rather than lit: nothing in this scene could illuminate
// something that size, and the brief's reference is a nebula, which glows.
fn vortexEmissionAt(p: vec3<f32>, shape: f32, t: f32) -> vec3<f32> {
    if (shape <= 0.0 || vol.vortex3.z <= 0.0) {
        return vec3<f32>(0.0);
    }
    // Colour hierarchy, which is the brief's section on this almost verbatim: DARK -> MID ->
    // LUMINOUS ACCENT, keyed on density, so the bright colour appears only in the dense filaments
    // and the bulk of the cloud stays deep. Saturating everything is the failure mode named.
    var c = mix(vol.vortexA.rgb, vol.vortexB.rgb, smoothstep(0.0, 0.45, shape));
    let filament = smoothstep(0.62, 0.95, shape) * clamp(vol.vortex3.w, 0.0, 4.0);
    c = c + vol.vortexAccent.rgb * filament;
    return c * (shape * vol.vortex3.z);
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

// The fog and the vortex are one medium as far as the march is concerned: one density, one
// transmittance. Keeping them separate would mean two marches or a composite, and a composite of
// two participating media is wrong wherever they overlap.
fn volumeTotalDensityAt(p: vec3<f32>, t: f32) -> f32 {
    return volumeDensityAt(p) + vortexShape(p, t) * vol.vortex1.w;
}

// Henyey-Greenstein phase function; g = 0 is isotropic.
fn henyeyGreenstein(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * d * sqrt(d));
}

// In-scattered radiance at `p` for a ray travelling along `direction`.
//
// Every enabled light contributes in proportion to its `volumetricStrength` (packed into
// `cone.z` by rendering::SceneRenderer; 0 means "this light does not light the air"), so a rig
// can put a warm practical in the haze without the key washing the whole volume out. Lights with
// zero strength cost one comparison. There is no shadowing in the fog: a beam is the falloff of a
// local emitter, not an occluded shaft.
fn lightRadiance(index: i32, p: vec3<f32>) -> vec4<f32> {
    let light = frame.lights[index];
    let kind = light.positionType.w;
    if (kind < 0.5) { // directional
        return vec4<f32>(light.colorIntensity.rgb, 0.0);
    }
    let toLight = light.positionType.xyz - p;
    let dist2 = max(dot(toLight, toLight), 1e-4);
    var attenuation = 1.0 / dist2;
    let range = light.directionRange.w;
    if (range > 0.0) {
        let ratio = clamp(1.0 - pow(sqrt(dist2) / range, 4.0), 0.0, 1.0);
        attenuation = attenuation * ratio * ratio;
    }
    if (kind > 1.5) { // spot
        let towards = toLight * inverseSqrt(dist2);
        let cosAngle = dot(light.directionRange.xyz, -towards);
        attenuation = attenuation * clamp((cosAngle - light.cone.x) * light.cone.y, 0.0, 1.0);
    }
    return vec4<f32>(light.colorIntensity.rgb * attenuation, 1.0);
}

fn lightTowards(index: i32, p: vec3<f32>) -> vec3<f32> {
    let light = frame.lights[index];
    if (light.positionType.w < 0.5) {
        return -light.directionRange.xyz;
    }
    let toLight = light.positionType.xyz - p;
    return toLight * inverseSqrt(max(dot(toLight, toLight), 1e-4));
}

fn inScatterAt(p: vec3<f32>, direction: vec3<f32>, anisotropy: f32) -> vec3<f32> {
    var total = vec3<f32>(0.0);
    let count = min(i32(frame.envParams.z), 8);
    for (var i = 0; i < count; i = i + 1) {
        let strength = frame.lights[i].cone.z;
        if (strength <= 0.0) {
            continue;
        }
        let towards = lightTowards(i, p);
        let phase = henyeyGreenstein(dot(direction, towards), anisotropy);
        total = total + strength * phase * lightRadiance(i, p).rgb;
    }
    return total;
}

// ADR-053: the clustered local lights, so the air near a glowing thing takes its colour. The
// march reads the same froxel list the surface shading reads (group 0 bindings 1 and 2 of the
// shared frame layout); the structs are declared here rather than by including lighting.wgsl,
// which would drag the shadow atlas, the LTC tables and the whole BRDF into a pass that needs
// none of them.
struct VolumeGpuLight {
    positionType: vec4<f32>,
    directionRange: vec4<f32>,
    colorIntensity: vec4<f32>,
    cone: vec4<f32>,
    sizeSoft: vec4<f32>,
    up: vec4<f32>,
    tangent: vec4<f32>,
    extra: vec4<f32>,
};
@group(0) @binding(1) var<storage, read> volumeLights: array<VolumeGpuLight>;
@group(0) @binding(2) var<storage, read> volumeClusters: array<u32>;
const VOLUME_MAX_PER_CLUSTER: u32 = 32u;
// The march samples at most this many of a froxel's lights. Fog has no detail to resolve: what it
// needs is the colour and rough strength of the light in the air, and taking every one of 32
// candidates at every step of every half-res pixel cost more than the whole ecology light field
// did on the surfaces it actually lit.
const VOLUME_LIGHT_SAMPLES: u32 = 6u;

fn volumeClusterIndex(screenUv: vec2<f32>, viewDepth: f32) -> u32 {
    let dims = vec3<u32>(u32(frame.clusterParams.x), u32(frame.clusterParams.y), u32(frame.clusterParams.z));
    let ix = min(u32(clamp(screenUv.x, 0.0, 0.9999) * f32(dims.x)), dims.x - 1u);
    let iy = min(u32(clamp(1.0 - screenUv.y, 0.0, 0.9999) * f32(dims.y)), dims.y - 1u);
    let depth = max(viewDepth, frame.clusterDepth.z);
    let slice = i32(floor(log2(depth) * frame.clusterDepth.x + frame.clusterDepth.y));
    let iz = u32(clamp(slice, 0, i32(dims.z) - 1));
    return (iz * dims.y + iy) * dims.x + ix;
}

// The local lights of one froxel, in-scattered. Point-like only: an area emitter contributes
// through its centre here, because the difference between a disk and a point is not visible in
// fog and the LTC integration is not affordable per march step.
fn localInScatterAt(p: vec3<f32>, screenUv: vec2<f32>, viewDepth: f32, direction: vec3<f32>,
                    anisotropy: f32) -> vec3<f32> {
    let gain = vol.glow.y;
    if (frame.clusterParams.w <= 0.5 || gain <= 0.0) {
        return vec3<f32>(0.0);
    }
    let directional = u32(frame.lightCounts.x + 0.5);
    let totalLights = u32(frame.lightCounts.y + 0.5);
    let cluster = volumeClusterIndex(screenUv, viewDepth);
    let clusterCount = u32(frame.clusterParams.x * frame.clusterParams.y * frame.clusterParams.z);
    let count = min(min(volumeClusters[cluster], VOLUME_MAX_PER_CLUSTER), VOLUME_LIGHT_SAMPLES);
    let base = clusterCount + cluster * VOLUME_MAX_PER_CLUSTER;
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let index = directional + volumeClusters[base + i];
        if (index >= totalLights) { continue; }
        let light = volumeLights[index];
        let toLight = light.positionType.xyz - p;
        let d2 = max(dot(toLight, toLight), 1e-4);
        let range = light.directionRange.w;
        if (range > 0.0 && d2 > range * range) { continue; }
        let dist = sqrt(d2);
        let towards = toLight / dist;
        // Inverse square with the same smooth window the surface shading uses, so a light does
        // not end at a hard edge in the fog where it faded out on the ground.
        var attenuation = 1.0 / d2;
        if (range > 0.0) {
            let t = clamp(1.0 - (dist / range) * (dist / range) * (dist / range) * (dist / range), 0.0, 1.0);
            attenuation = attenuation * t * t;
        }
        let phase = henyeyGreenstein(dot(direction, towards), anisotropy);
        sum = sum + light.colorIntensity.rgb * (attenuation * phase);
    }
    return sum * gain;
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
    let marchPx = vec2<i32>(floor(in.pos.xy));
    // The full-resolution texel this march pixel marches (the composite pass uses the same rule
    // to decide which neighbour saw which surface). `ratio` is 2 at the default half-resolution
    // scale, where this is exactly the `marchPx * 2` it has always been, and 1 at scale 1.0,
    // where every march pixel is its own full-res pixel.
    let ratio = vol.sizes.zw / vol.sizes.xy;
    let mapped = vec2<i32>(floor(vec2<f32>(marchPx) * ratio));
    let fullPx = vec2<i32>(min(mapped.x, i32(vol.sizes.z) - 1), min(mapped.y, i32(vol.sizes.w) - 1));
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
    let jitter = stepJitter(marchPx, u32(vol.info.w));
    let screenUv = (vec2<f32>(fullPx) + vec2<f32>(0.5)) / vol.sizes.zw;
    // Froxel depth is measured along the camera axis, not along this pixel's ray, or a sample at
    // the edge of a wide frame lands a slice or two deep and reads the wrong light list.
    let depthAlongRay = dot(direction, frame.cameraForward.xyz);
    let anisotropy = clamp(vol.params1.y, -0.95, 0.95);
    let colorSlot = i32(vol.info.z);

    var transmittance = 1.0;
    var scattered = vec3<f32>(0.0);
    for (var i = 0; i < steps; i = i + 1) {
        let t = (f32(i) + jitter) * stepLength;
        let p = origin + direction * t;
        // ADR-371: the vortex is part of the same medium, so it shares one density and one
        // transmittance with the fog. `vortexShape` returns before doing any work when no vortex is
        // authored, which is what keeps this loop the cost it was.
        let vortex = vortexShape(p, vol.noiseParams.w);
        let fogDensity = volumeDensityAt(p);
        let vortexDensity = vortex * vol.vortex1.w;
        let density = fogDensity + vortexDensity;
        if (density <= 0.0) {
            continue;
        }
        let extinction = density * vol.params1.x;
        // ADR-371: the FOG scatters the scene's lights; the VORTEX does not. This is the single
        // most important line in the effect, and the first version did not have it. A nebula four
        // hundred metres below an island is not lit by that island's key light, and letting it be
        // turned the frame into an even wash: at density 0.0015 with the key at intensity 22, a
        // 2.6 km march accumulated so much in-scattered key light that the picture came back mean
        // luminance 131 of 255 with the vortex's own emission set to ZERO. That is precisely the
        // flat haze ADR-358 predicted when it refused to build a volumetric beam without shadow
        // sampling -- the same defect, arrived at from the other direction.
        //
        // So the vortex contributes extinction and emission and nothing else. It is self-luminous,
        // which is both what a nebula is and what keeps it out of the scene's lighting entirely.
        //
        // ADR-388 adds the controlled way back in, and leaves the refusal above standing because it
        // is still the right default. `vortex5.z` is 0 unless a scene asks otherwise, so every
        // frame rendered before this line changed renders identically after it. What it is FOR is
        // one thing: an upward spotlight aimed through the funnel lights the surfaces it reaches
        // and the ordinary fog, and its beam stops dead at the funnel's edge, because the medium
        // the beam is supposed to be visible in does not scatter. Above 0 it does.
        //
        // The 131-of-255 above was measured on the PRE-FUNNEL slab; against the shape ADR-374 left
        // -- a throat, a void and a rim -- full scattering is worth +15 luminance levels on the
        // shipped frame, not a wash. The refusal is still the right default. The number is no
        // longer what this knob does, and it is left above because it is why the knob starts at 0.
        let scattering = (fogDensity + vortexDensity * vol.vortex5.z) * vol.params0.w;
        var emission = vec3<f32>(0.0);
        if (vol.params1.z > 0.0) {
            var emissionColor = vol.fogColor.rgb;
            if (colorSlot >= 0) {
                let c = fieldColor(colorSlot, p);
                emissionColor = c.rgb * c.a;
            }
            emission = density * vol.params1.z * emissionColor;
        }
        // ...and the vortex brings its own light. Emissive rather than lit, because nothing in this
        // scene could illuminate something that size and because the reference is a nebula.
        emission = emission + vortexEmissionAt(p, vortex, vol.noiseParams.w);
        // ADR-381, phase 13: the COMET, and only the comet, is allowed to light the vortex.
        //
        // ADR-374 established that letting the scene's lights scatter in this medium turns the
        // frame into flat haze -- the key at intensity 22 accumulated over a 2.6 km march returned
        // mean luminance 131 of 255 with the vortex's own emission at zero. So this is not a
        // relaxation of that: it is one small, moving, distance-limited source, taken from the same
        // `skyGroundPoint` description the SURFACES are lit from, so the rock and the fog brighten
        // from one account of where the comet is rather than two that can disagree.
        //
        // Weighted by the vortex's own density, so it reads as the comet finding the cloud rather
        // than as a light in empty space, and gated by a parameter that is zero by default.
        if (vol.vortex5.x > 0.0 && vortex > 0.0) {
            let lit = frame.skyGroundPointColor;
            if (lit.r + lit.g + lit.b > 0.0) {
                // XZ ONLY, and a wider reach than the surfaces get. `atmosphere_ground.wgsl` says
                // why for the first: the comet is kilometres up, so the distance that matters is
                // how far a point is from the spot *under* it, not from the spot itself. My first
                // version used the 3D distance and measured nothing at all -- the ground pool's
                // 220 m radius is the right size for the island's rock and the vortex is hundreds
                // of metres below the ground plane, so every sample fell outside a sphere of it.
                //
                // And the second: the fog is kilometres across where the pool is metres, so the
                // radius is scaled by `vortex5.y`. A light that reaches the rock and stops dead at
                // the cloud beside it is the discontinuity ADR-369 was about, one object along.
                let d = length(p.xz - frame.skyGroundPoint.xz) /
                        max(frame.skyGroundPoint.w * max(vol.vortex5.y, 1.0), 1.0);
                let fall = pow(clamp(1.0 - d, 0.0, 1.0), max(lit.w, 0.5));
                // ADR-389 is the fourth member of this family and the first that is not about
                // metres: replacing the vortex's contrast curve changed the MEAN of its shape by
                // six times, and `density` and `emission` were calibrated against the old one. The
                // general form, for whoever meets the fifth: a coefficient tuned against a quantity
                // is invalidated by any change to that quantity's DISTRIBUTION, not only by a
                // change to its units.
                //
                // The 0.005 is the per-metre conversion, and it is the THIRD time in this branch
                // that adding a radiance to a march without it has produced a number two orders of
                // magnitude wrong (ADR-374's density, ADR-379's spill). `lit.rgb` is a surface
                // radiance; this loop integrates over metres. Without it, cometResponse 0.6 lifted
                // the whole frame by 125 luminance levels.
                emission = emission + lit.rgb * (fall * vortex * vol.vortex5.x * 0.005);
            }
        }
        // The particle glow arrives as light to scatter, not as fog emission, so denser dust
        // catches more of it - which is what reads as "the sparks are lighting the dust".
        let local = localInScatterAt(p, screenUv, max(t * depthAlongRay, 1e-3), direction, anisotropy);
        let source = scattering * (inScatterAt(p, direction, anisotropy) + particleGlowAt(p) + local) + emission;
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
    // Bilinear footprint in march-texel space. `invRatio` is 0.5 at the default half-resolution
    // scale and 1.0 when the march ran per pixel -- and at 1.0 `coord` lands exactly on an
    // integer, so `f` is zero, the (0,0) neighbour takes the whole weight and the filter is a
    // copy of the pixel's own march rather than a blur of four.
    let ratio = vol.sizes.zw / vol.sizes.xy;
    let invRatio = vol.sizes.xy / vol.sizes.zw;
    let coord = (vec2<f32>(fullPx) + vec2<f32>(0.5)) * invRatio - vec2<f32>(0.5);
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
            let mapped = vec2<i32>(floor(vec2<f32>(f32(hx), f32(hy)) * ratio));
            let sourcePx = vec2<i32>(min(mapped.x, fullMaxX), min(mapped.y, fullMaxY));
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
