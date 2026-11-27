// Volumetric lighting (ADR-2210): a camera-aligned froxel grid that carries the environment's air and
// lights it with the scene's own lights and shadow atlas. Two compute entry points, encoded by
// rendering::VolumetricLightRenderer after the lit pass; the apply is one trilinear tap in
// `fs_composite` (volume.wgsl), so no extra full-screen tile round trip is added (ADR-2217).
//
//   cs_inject     per froxel: the medium (ADR-2211: the SAME height-fog profile, horizon term and noise
//                 the march and the surface fog read), and the light it scatters toward the eye --
//                 the eight frame lights by `volumetricStrength` with their shadow-map visibility
//                 (cascades, spot maps, cubes; ADR-2214: the surface's own views, no volumetric-only
//                 maps), the froxel's clustered local lights, the particle glow, and emission.
//                 Output rgba16f: rgb = in-scattered radiance per metre, a = extinction per metre.
//   cs_integrate  per column, front to back (Hillaire 2015's energy-conserving step):
//                 rgb = in-scatter reaching the eye from the near plane to the FAR end of slice z,
//                 a = transmittance over the same span.
//
// No history at any tier (ADR-2212). The sample offsets inside a froxel are a fixed function of the
// froxel's index and the subsample number -- not of the frame -- so a still frame is a still picture
// and a frame is a pure function of the scene, the time and the camera.
//
// Group 0 is the frame group's resources re-bound for compute (ADR-2210's compute-visible binding):
// 0 frame, 1 the packed scene lights, 2 the froxel light lists, 3 ShadowUniforms, 4 the shadow atlas,
// 5 its comparison sampler, 12 the terrain height. The pipelines use automatic layouts, so each entry
// point binds exactly what it reads. Group 1 is this pass's own; binding 0 stays free because
// common.wgsl declares `object` there.
#include "common.wgsl"
#include "shadows.wgsl"
#include "noise.wgsl"

struct VolumetricLightUniforms {
    grid: vec4<f32>,      // x, y, z = froxel counts, w = inject subsamples (1..4)
    depth: vec4<f32>,     // x = view depth where slice 0 starts, y = far (the hand-over distance), z = log(y/x), w = intensity
    medium0: vec4<f32>,   // volumeDensity, fogHeight, fogHeightFalloff, volumeScattering
    medium1: vec4<f32>,   // volumeAbsorption, volumeAnisotropy, volumeEmission, horizonDensity
    shape: vec4<f32>,     // fogUpperDensity, fogHeightCurve, fogGroundFollow (0 without terrain), 0
    noise: vec4<f32>,     // volumeNoise, volumeNoiseScale, volumeNoiseSpeed, render time
    fogColor: vec4<f32>,  // rgb (emission colour), w = volumeLocalLights
    info: vec4<f32>,      // x = particle glow systems, y = shadow taps (1 or 4), z = local lights per froxel, w = 0
    // Per frame light: x = its index in the packed light buffer (the shadow view and flags live there), -1 = none;
    // y = 1 when the light shadows the air (castsShadow and volumetricShadow), zw = 0.
    lightShadow: array<vec4<f32>, 8>,
};

@group(1) @binding(1) var<uniform> vl: VolumetricLightUniforms;
@group(1) @binding(2) var<storage, read> particleGlow: array<vec4<f32>>;
@group(1) @binding(3) var scatterOut: texture_storage_3d<rgba16float, write>;
@group(1) @binding(4) var scatterIn: texture_3d<f32>;
@group(1) @binding(5) var integratedOut: texture_storage_3d<rgba16float, write>;

// The view depth at slice coordinate w in [0, 1]: exponential, so froxels stay roughly cubic.
fn vlSliceDepth(w: f32) -> f32 {
    return vl.depth.x * exp(clamp(w, 0.0, 1.0) * vl.depth.z);
}

// A world-space ray through screen position uv, scaled so that dot(ray, cameraForward) == 1: a point
// at view depth d is cameraPos + ray * d.
fn vlRay(uv: vec2<f32>) -> vec3<f32> {
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let h = frame.invViewProj * vec4<f32>(ndc, 0.5, 1.0);
    let ray = h.xyz / h.w - frame.cameraPos.xyz;
    return ray / max(dot(ray, frame.cameraForward.xyz), 1e-6);
}

fn vlPhase(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * d * sqrt(d));
}

// ADR-2211: the environment's air, exactly as `volumeDensityAt` (volume.wgsl) states it -- the shared
// profile from height_fog.wgsl, the ground follow and pooling through the frame's own lanes, the noise
// term -- times the horizon growth the march applies per sample. A density FIELD is not carried here;
// a scene that names one keeps its air in the march and says so (VolumetricLightRenderer).
fn vlDensityAt(p: vec3<f32>, t: f32) -> f32 {
    var heightTerm = fogHeightProfile(p.y - vl.medium0.y, vl.medium0.z, vl.shape.x, vl.shape.y);
    if ((vl.shape.z > 0.0 || frame.fogPool.x > 0.0) && frame.terrainMap1.w > 0.5) {
        heightTerm = fogGroundProfileAt(terrainHeightTex, frame.terrainMap0, frame.terrainMap1, p, vl.medium0.y,
                                        vl.shape.z, frame.fogPool.x, vl.medium0.z, vl.shape.x, vl.shape.y);
    }
    var d = vl.medium0.x * heightTerm;
    if (d > 1e-6 && vl.noise.x != 0.0) {
        let offset = vec3<f32>(vl.noise.z * vl.noise.w);
        d = d * (1.0 + vl.noise.x * (fbm3(p * vl.noise.y + offset, 17u) * 2.0 - 1.0));
    }
    if (vl.medium1.w > 0.0) {
        d = d * (1.0 + vl.medium1.w * t * 0.001); // ADR-705's horizon density, by distance from the eye
    }
    return max(d, 0.0);
}

// Shadow-map visibility of packed light `index` at a point in the air (ADR-2214): its own views, a
// plain comparison tap (or a 2x2 box at High and above), no normal offset (there is no surface), the
// cascade picked by view depth and faded out at the end of the shadowed range as shadowFactor fades.
fn vlTap(view: u32, p: vec3<f32>, bias: f32, taps: u32) -> f32 {
    let look = shadowLookup(view, p);
    if (!look.valid) {
        return 1.0;
    }
    let z = look.depth - bias;
    if (taps <= 1u) {
        return textureSampleCompareLevel(shadowAtlas, shadowSampler, look.uv, view, z);
    }
    let texel = 1.0 / max(shadowBlock.info.x, 1.0);
    var s = 0.0;
    for (var i = 0u; i < 4u; i = i + 1u) {
        let o = (vec2<f32>(f32(i & 1u), f32(i >> 1u)) - vec2<f32>(0.5)) * texel;
        s = s + textureSampleCompareLevel(shadowAtlas, shadowSampler, look.uv + o, view, z);
    }
    return s * 0.25;
}

fn vlShadow(index: u32, p: vec3<f32>, viewDepth: f32) -> f32 {
    let light = sceneLights[index];
    let flags = u32(light.cone.w + 0.5);
    if ((flags & FLAG_CASTS_SHADOW) == 0u) {
        return 1.0;
    }
    let base = u32(max(light.cone.z, 0.0));
    let taps = u32(vl.info.y + 0.5);
    var visibility = 1.0;
    if ((flags & FLAG_CASCADED) != 0u) {
        let count = u32(max(shadowBlock.info.y, 1.0));
        let c = cascadeFor(viewDepth, count);
        visibility = vlTap(base + c, p, shadowBlock.views[base + c].params.y, taps);
        let lastSplit = shadowBlock.splits[count - 1u];
        let fadeBand = lastSplit * SHADOW_RANGE_FADE;
        if (fadeBand > 1e-4 && viewDepth > lastSplit - fadeBand) {
            let t = clamp((viewDepth - (lastSplit - fadeBand)) / fadeBand, 0.0, 1.0);
            visibility = mix(visibility, 1.0, t * t);
        }
    } else if ((flags & FLAG_CUBE) != 0u) {
        let face = cubeFace(p - light.positionType.xyz);
        visibility = vlTap(base + face, p, shadowBlock.views[base + face].params.y, taps);
    } else {
        visibility = vlTap(base, p, shadowBlock.views[base].params.y, taps);
    }
    return mix(1.0, visibility, clamp(light.up.w, 0.0, 1.0));
}

// The eight frame lights, as the march's `inScatterAt` takes them (strength by `volumetricStrength`,
// `lightRadiance`'s falloff and cone) -- plus the shadow the march never had.
fn vlFrameLights(p: vec3<f32>, dir: vec3<f32>, viewDepth: f32, g: f32) -> vec3<f32> {
    var total = vec3<f32>(0.0);
    let count = min(i32(frame.envParams.z), 8);
    for (var i = 0; i < count; i = i + 1) {
        let light = frame.lights[i];
        let strength = light.cone.z;
        if (strength <= 0.0) {
            continue;
        }
        var radiance = light.colorIntensity.rgb;
        var towards = -light.directionRange.xyz;
        if (light.positionType.w >= 0.5) {
            let toLight = light.positionType.xyz - p;
            let dist2 = max(dot(toLight, toLight), 1e-4);
            var attenuation = 1.0 / dist2;
            let range = light.directionRange.w;
            if (range > 0.0) {
                let ratio = clamp(1.0 - pow(sqrt(dist2) / range, 4.0), 0.0, 1.0);
                attenuation = attenuation * ratio * ratio;
            }
            towards = toLight * inverseSqrt(dist2);
            if (light.positionType.w > 1.5) {
                attenuation = attenuation * clamp((dot(light.directionRange.xyz, -towards) - light.cone.x) * light.cone.y,
                                                  0.0, 1.0);
            }
            if (attenuation <= 0.0) {
                continue;
            }
            radiance = radiance * attenuation;
        }
        var visibility = 1.0;
        let packed = vl.lightShadow[i];
        if (packed.x >= 0.0 && packed.y > 0.5) {
            visibility = vlShadow(u32(packed.x), p, viewDepth);
        }
        total = total + strength * vlPhase(dot(dir, towards), g) * visibility * radiance;
    }
    return total;
}

// The froxel's clustered local lights, as the march's `localInScatterAt` takes them (point-like,
// unshadowed in B1; the cap per froxel is the tier's).
fn vlLocalLights(p: vec3<f32>, uv: vec2<f32>, viewDepth: f32, dir: vec3<f32>, g: f32) -> vec3<f32> {
    let gain = vl.fogColor.w;
    if (frame.clusterParams.w <= 0.5 || gain <= 0.0) {
        return vec3<f32>(0.0);
    }
    let dims = vec3<u32>(u32(frame.clusterParams.x), u32(frame.clusterParams.y), u32(frame.clusterParams.z));
    let ix = min(u32(clamp(uv.x, 0.0, 0.9999) * f32(dims.x)), dims.x - 1u);
    let iy = min(u32(clamp(1.0 - uv.y, 0.0, 0.9999) * f32(dims.y)), dims.y - 1u);
    let slice = i32(floor(log2(max(viewDepth, frame.clusterDepth.z)) * frame.clusterDepth.x + frame.clusterDepth.y));
    let iz = u32(clamp(slice, 0, i32(dims.z) - 1));
    let cluster = (iz * dims.y + iy) * dims.x + ix;
    let directional = u32(frame.lightCounts.x + 0.5);
    let totalLights = u32(frame.lightCounts.y + 0.5);
    let clusterCount = dims.x * dims.y * dims.z;
    let count = min(min(clusterLights[cluster], MAX_LIGHTS_PER_CLUSTER), u32(vl.info.z + 0.5));
    let base = clusterCount + cluster * MAX_LIGHTS_PER_CLUSTER;
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let index = directional + clusterLights[base + i];
        if (index >= totalLights) {
            continue;
        }
        let light = sceneLights[index];
        let toLight = light.positionType.xyz - p;
        let d2 = max(dot(toLight, toLight), 1e-4);
        let range = light.directionRange.w;
        if (range > 0.0 && d2 > range * range) {
            continue;
        }
        let dist = sqrt(d2);
        var attenuation = 1.0 / d2;
        if (range > 0.0) {
            let r = dist / range;
            let t = clamp(1.0 - r * r * r * r, 0.0, 1.0);
            attenuation = attenuation * t * t;
        }
        sum = sum + light.colorIntensity.rgb * (attenuation * vlPhase(dot(dir, toLight / dist), g));
    }
    return sum * gain;
}

const kVlParticleGlowIntensity: f32 = 0.0015; // volume.wgsl kParticleGlowIntensity

fn vlParticleGlow(p: vec3<f32>) -> vec3<f32> {
    let count = u32(vl.info.x + 0.5);
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let centre = particleGlow[i * 2u];
        let color = particleGlow[i * 2u + 1u];
        if (color.w <= 0.0) {
            continue;
        }
        let radius = max(centre.w, 0.25);
        let d = p - centre.xyz;
        let falloff = exp(-dot(d, d) / (2.0 * radius * radius));
        sum = sum + color.rgb * (color.w * kVlParticleGlowIntensity * falloff / (4.0 * PI * radius * radius));
    }
    return sum;
}

// A fixed in-froxel offset per subsample: a 4-point rotated grid in xy and a stratified depth, the
// rotation chosen per froxel from its index so neighbouring froxels do not alias together. No time.
fn vlOffset(id: vec3<u32>, k: u32, n: u32) -> vec3<f32> {
    let h = pcg3d(id);
    let r = f32(h.x & 0xFFFFu) * (1.0 / 65536.0);
    var pattern = array<vec2<f32>, 4>(vec2<f32>(0.375, 0.125), vec2<f32>(0.875, 0.375), vec2<f32>(0.125, 0.625),
                                      vec2<f32>(0.625, 0.875));
    let xy = select(vec2<f32>(0.5), pattern[(k + (h.y & 3u)) & 3u], n > 1u);
    let z = fract((f32(k) + 0.5) / f32(n) + r);
    return vec3<f32>(xy, z);
}

@compute @workgroup_size(4, 4, 4)
fn cs_inject(@builtin(global_invocation_id) id: vec3<u32>) {
    let dims = vec3<u32>(vl.grid.xyz);
    if (any(id >= dims)) {
        return;
    }
    let n = max(u32(vl.grid.w + 0.5), 1u);
    let g = clamp(vl.medium1.y, -0.95, 0.95);
    var source = vec3<f32>(0.0);
    var extinction = 0.0;
    for (var k = 0u; k < n; k = k + 1u) {
        let uvw = (vec3<f32>(id) + vlOffset(id, k, n)) / vec3<f32>(dims);
        let ray = vlRay(uvw.xy);
        let viewDepth = vlSliceDepth(uvw.z);
        let p = frame.cameraPos.xyz + ray * viewDepth;
        let t = viewDepth * length(ray);
        let density = vlDensityAt(p, t);
        if (density <= 0.0) {
            continue;
        }
        let dir = normalize(ray);
        let lit = vlFrameLights(p, dir, viewDepth, g) + vlLocalLights(p, uvw.xy, viewDepth, dir, g) + vlParticleGlow(p);
        source = source + density * vl.medium0.w * lit * vl.depth.w + density * vl.medium1.z * vl.fogColor.rgb;
        extinction = extinction + density * vl.medium1.x;
    }
    let inv = 1.0 / f32(n);
    textureStore(scatterOut, id, vec4<f32>(source * inv, extinction * inv));
}

@compute @workgroup_size(8, 8, 1)
fn cs_integrate(@builtin(global_invocation_id) id: vec3<u32>) {
    let dims = vec3<u32>(vl.grid.xyz);
    if (id.x >= dims.x || id.y >= dims.y) {
        return;
    }
    let uv = (vec2<f32>(id.xy) + vec2<f32>(0.5)) / vec2<f32>(dims.xy);
    let rayScale = length(vlRay(uv)); // metres along the ray per metre of view depth
    var scattered = vec3<f32>(0.0);
    var transmittance = 1.0;
    // Slice 0 starts at the eye, not at the grid's near depth: the air in front of the first slice is
    // carried by the first froxel, so no metre between the camera and the hand-over is uncounted.
    var previous = 0.0;
    for (var z = 0u; z < dims.z; z = z + 1u) {
        let s = textureLoad(scatterIn, vec3<i32>(vec3<u32>(id.xy, z)), 0);
        let next = vlSliceDepth(f32(z + 1u) / f32(dims.z));
        let segment = (next - previous) * rayScale;
        previous = next;
        let sigma = s.a;
        var tr = 1.0;
        var through = segment;
        if (sigma > 1e-7) {
            tr = exp(-sigma * segment);
            through = (1.0 - tr) / sigma; // Hillaire 2015: the exact integral over a constant slice
        }
        scattered = scattered + transmittance * s.rgb * through;
        transmittance = transmittance * tr;
        textureStore(integratedOut, vec3<u32>(id.xy, z), vec4<f32>(scattered, transmittance));
    }
}
