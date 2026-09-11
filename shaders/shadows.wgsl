// Shadow maps, screen-space contact shadows and the ambient-occlusion fetch (ADR-033/034).
// Split out of lighting.wgsl in ADR-086 so that the half-resolution shadow-mask pass
// (shaders/shadow_mask.wgsl) can compute exactly the terms the lit pass would have computed,
// instead of a second implementation that drifts from it.
//
// Include after common.wgsl and never on its own: it uses `frame`, `octDecode` and `PI` from
// there, and the include directive does not de-duplicate.
//
// Group 0 (the frame group) bindings declared here:
//   1  sceneLights       storage   the packed scene lights (directional first, then local)
//   2  clusterLights     storage   froxel light lists: counts, then a fixed-size index block each
//   3  shadowBlock       uniform   the shadow view matrices and their biases
//   4  shadowAtlas       texture   depth_2d_array, one layer per cascade / spot map
//   5  shadowSampler     sampler   comparison, linear
//   6  aoTexture         texture   rg = octahedral bent normal, b = visibility, a = view depth
//   7  sceneLinearDepth  texture   R32F view-space depth of the prepass (contact shadows)
//   11 shadowMaskTexture texture   rgb = up to three directional lights' shadow-map visibility,
//                                    a = the view depth each was computed at (ADR-086)

const LIGHT_DIRECTIONAL: f32 = 0.0;
const LIGHT_POINT: f32 = 1.0;
const LIGHT_SPOT: f32 = 2.0;
const LIGHT_RECT: f32 = 3.0;
const LIGHT_DISK: f32 = 4.0;
const LIGHT_TUBE: f32 = 5.0;
const LIGHT_SPHERE: f32 = 6.0;

const FLAG_CASTS_SHADOW: u32 = 1u;
const FLAG_CASCADED: u32 = 2u;
const FLAG_AREA: u32 = 4u;

const MAX_LIGHTS_PER_CLUSTER: u32 = 32u;

struct GpuLight {
    positionType: vec4<f32>,   // xyz = world position, w = type code
    directionRange: vec4<f32>, // xyz = direction the light travels, w = range (0 = infinite)
    colorIntensity: vec4<f32>, // rgb = colour * temperature * intensity, w = influence radius
    cone: vec4<f32>,           // x = cos(outer), y = 1 / (cos(inner) - cos(outer)), z = shadow view, w = flags
    sizeSoft: vec4<f32>,       // x = width, y = height, z = radius, w = softness
    up: vec4<f32>,             // xyz = emitter up axis, w = shadow strength
    tangent: vec4<f32>,        // xyz = emitter right axis, w = contact-shadow strength
    extra: vec4<f32>,          // x = diffuse only, y = specular only, z = volumetric, w = shadow bias (world)
};

struct ShadowViewGpu {
    viewProj: mat4x4<f32>,
    params: vec4<f32>, // x = texel world size, y = depth bias, z = 1 when a cascade, w = far distance
};

struct ShadowUniforms {
    views: array<ShadowViewGpu, 8>,
    info: vec4<f32>,   // x = atlas resolution, y = cascade count, z = PCF taps, w = 1 when PCSS is on
    splits: vec4<f32>, // cascade far distances in view depth
};

@group(0) @binding(1) var<storage, read> sceneLights: array<GpuLight>;
@group(0) @binding(2) var<storage, read> clusterLights: array<u32>;
@group(0) @binding(3) var<uniform> shadowBlock: ShadowUniforms;
@group(0) @binding(4) var shadowAtlas: texture_depth_2d_array;
@group(0) @binding(5) var shadowSampler: sampler_comparison;
@group(0) @binding(6) var aoTexture: texture_2d<f32>;
@group(0) @binding(7) var sceneLinearDepth: texture_2d<f32>;
// ADR-086: the half-resolution screen-space shadow mask. rgb = the combined visibility (cascade
// lookup and contact march, already minned) of directional lights 0, 1 and 2; a = the view depth
// the mask texel was computed at, which is what the bilateral upsample weighs against. Bound to a
// 1x1 white stand-in when the mask is off, so the binding is always valid.
@group(0) @binding(11) var shadowMaskTexture: texture_2d<f32>;

// A rotated Poisson disc: sixteen well-spread points, rotated per pixel so the PCF footprint
// becomes fine dither instead of a visible pattern.
fn poissonDisc(i: u32) -> vec2<f32> {
    var points = array<vec2<f32>, 16>(
        vec2<f32>(-0.94201624, -0.39906216), vec2<f32>(0.94558609, -0.76890725),
        vec2<f32>(-0.09418410, -0.92938870), vec2<f32>(0.34495938, 0.29387760),
        vec2<f32>(-0.91588581, 0.45771432),  vec2<f32>(-0.81544232, -0.87912464),
        vec2<f32>(-0.38277543, 0.27676845),  vec2<f32>(0.97484398, 0.75648379),
        vec2<f32>(0.44323325, -0.97511554),  vec2<f32>(0.53742981, -0.47373420),
        vec2<f32>(-0.26496911, -0.41893023), vec2<f32>(0.79197514, 0.19090188),
        vec2<f32>(-0.24188840, 0.99706507),  vec2<f32>(-0.81409955, 0.91437590),
        vec2<f32>(0.19984126, 0.78641367),   vec2<f32>(0.14383161, -0.14100790));
    return points[i % 16u];
}

// Interleaved gradient noise (Jimenez 2014): deterministic in the pixel and the frame index, so
// two renders of the same frame agree exactly.
fn gradientNoise(pixel: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2<f32>(0.06711056, 0.00583715))));
}

// ---- ambient occlusion ---------------------------------------------------------------------------

struct AoSample {
    visibility: f32,
    bentNormal: vec3<f32>,
};

// Depth-aware (bilateral) upsample of the half-resolution AO target: the four nearest AO texels
// weighted by how well their own view depth agrees with this fragment's, which keeps the
// occlusion from bleeding across silhouettes.
fn sampleAmbientOcclusion(screenUv: vec2<f32>, viewDepth: f32, normal: vec3<f32>) -> AoSample {
    var out: AoSample;
    out.visibility = 1.0;
    out.bentNormal = normal;
    if (frame.aoParams.y < 0.5) {
        return out;
    }
    let size = frame.aoParams.zw;
    // Plain bilinear placement, and it was checked rather than assumed. A mask texel's centre falls
    // exactly between two full-resolution texels and the pass has to pick one of them, so its
    // sample arguably sits a quarter of a mask texel off where a bilinear filter puts it. Shifting
    // the lookup a quarter texel to compensate measured *worse* in both directions -- 14.7% and
    // 15.2% of the frame differing from the unmasked reference against 8.4% unshifted -- so the
    // reasoning is wrong somewhere and the number is what stands.
    let coord = screenUv * size - vec2<f32>(0.5);
    let base = floor(coord);
    let f = coord - base;
    var totalWeight = 0.0;
    var visibility = 0.0;
    var bent = vec3<f32>(0.0);
    for (var i = 0u; i < 4u; i = i + 1u) {
        let offset = vec2<f32>(f32(i & 1u), f32((i >> 1u) & 1u));
        let texel = clamp(base + offset, vec2<f32>(0.0), size - vec2<f32>(1.0));
        let sample = textureLoad(aoTexture, vec2<i32>(texel), 0);
        let bilinear = abs((1.0 - offset.x) - f.x) * abs((1.0 - offset.y) - f.y);
        // Depth agreement: a relative tolerance so the filter behaves the same near and far.
        let dz = abs(sample.a - viewDepth) / max(viewDepth, 0.05);
        let weight = bilinear * exp(-dz * 24.0) + 1e-5;
        visibility = visibility + sample.b * weight;
        bent = bent + octDecode(sample.rg) * weight;
        totalWeight = totalWeight + weight;
    }
    if (totalWeight <= 0.0) {
        return out;
    }
    out.visibility = clamp(visibility / totalWeight, 0.0, 1.0);
    let n = bent / totalWeight;
    if (dot(n, n) > 1e-6) {
        out.bentNormal = normalize(n);
    }
    return out;
}

// ---- shadow maps ---------------------------------------------------------------------------------

// Which cascade covers this view depth. Cascades are ordered near to far.
fn cascadeFor(viewDepth: f32, count: u32) -> u32 {
    var index = count - 1u;
    for (var i = 0u; i < count; i = i + 1u) {
        if (viewDepth <= shadowBlock.splits[i]) {
            index = i;
            break;
        }
    }
    return index;
}

struct ShadowLookup {
    uv: vec2<f32>,
    depth: f32,
    valid: bool,
};

fn shadowLookup(view: u32, worldPos: vec3<f32>) -> ShadowLookup {
    var out: ShadowLookup;
    out.valid = false;
    out.uv = vec2<f32>(0.0);
    out.depth = 0.0;
    let clip = shadowBlock.views[view].viewProj * vec4<f32>(worldPos, 1.0);
    if (clip.w <= 0.0) {
        return out;
    }
    let ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > vec2<f32>(1.0)) || ndc.z < 0.0 || ndc.z > 1.0) {
        return out;
    }
    out.uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    out.depth = ndc.z;
    out.valid = true;
    return out;
}

// Percentage-closer filtering over a rotated Poisson disc.
//
// The disc has sixteen points, so a seventeenth tap would land on the first one again. Past
// sixteen the disc is rotated a further half of its own angular spacing per revolution, which puts
// the second sixteen between the first sixteen instead of on top of them. That matters only to the
// shadow-mask pass (ADR-086), which runs at a quarter of the pixels and spends the difference on
// taps: a twelve-tap estimate quantises visibility to twelve levels, and at full resolution the
// per-pixel rotation dithers those levels into invisibility while at half resolution it does not,
// which reads as soft parallel bands across an open penumbra.
fn pcf(view: u32, uv: vec2<f32>, depth: f32, radiusTexels: f32, taps: u32, rotation: f32) -> f32 {
    let texel = 1.0 / max(shadowBlock.info.x, 1.0);
    var sum = 0.0;
    let n = max(taps, 1u);
    for (var i = 0u; i < n; i = i + 1u) {
        let angle = rotation + f32(i / 16u) * 0.19634954; // half of 2*pi/16
        let c = cos(angle);
        let s = sin(angle);
        let p = poissonDisc(i);
        let rotated = vec2<f32>(p.x * c - p.y * s, p.x * s + p.y * c) * radiusTexels * texel;
        sum = sum + textureSampleCompareLevel(shadowAtlas, shadowSampler, uv + rotated, view, depth);
    }
    return sum / f32(n);
}

// Percentage-closer soft shadows (Fernando 2005): find the average blocker, widen the filter with
// it, so a large emitter reads as a large emitter. Reserved for the key light.
fn pcss(view: u32, uv: vec2<f32>, depth: f32, softness: f32, taps: u32, rotation: f32) -> f32 {
    let resolution = max(shadowBlock.info.x, 1.0);
    let texel = 1.0 / resolution;
    let searchRadius = clamp(softness * 6.0, 1.0, 24.0);
    let c = cos(rotation);
    let s = sin(rotation);
    var blockerSum = 0.0;
    var blockerCount = 0.0;
    let search = min(taps, 32u);
    for (var i = 0u; i < search; i = i + 1u) {
        let searchAngle = rotation + f32(i / 16u) * 0.19634954;
        let sc = cos(searchAngle);
        let ss = sin(searchAngle);
        let p = poissonDisc(i);
        let rotated = vec2<f32>(p.x * sc - p.y * ss, p.x * ss + p.y * sc) * searchRadius * texel;
        let coord = clamp(uv + rotated, vec2<f32>(0.0), vec2<f32>(1.0)) * resolution;
        let sampled = textureLoad(shadowAtlas, vec2<i32>(coord), i32(view), 0);
        if (sampled < depth) {
            blockerSum = blockerSum + sampled;
            blockerCount = blockerCount + 1.0;
        }
    }
    if (blockerCount < 0.5) {
        return 1.0;
    }
    let blocker = blockerSum / blockerCount;
    // Penumbra grows with the ratio of receiver-to-blocker distance, in normalised depth.
    let penumbra = clamp((depth - blocker) / max(blocker, 1e-4), 0.0, 1.0);
    let radius = clamp(penumbra * searchRadius * 12.0, 1.0, 24.0);
    return pcf(view, uv, depth, radius, taps, rotation);
}

// The shadow term of one light. `bias` is applied along the surface normal (normal-offset) before
// the projection, plus a slope-scaled constant in normalised depth.
// How much of a cascade's depth extent is spent fading into the next one.
const CASCADE_BLEND: f32 = 0.12;

// The near edge of cascade `index`, in view depth. Cascade 0 starts at the camera.
//
// The index is stepped down through a guarded `select` rather than indexed as `index - 1u`,
// because WGSL evaluates both arms of a `select` and `0u - 1u` would index a vec4 at four
// billion.
fn cascadeNear(index: u32) -> f32 {
    let previous = select(0u, index - 1u, index > 0u);
    return select(0.0, shadowBlock.splits[previous], index > 0u);
}

// The visibility one shadow view reports for a point. Split out of `shadowFactor` so that a
// cascade transition can be crossfaded by evaluating two of them.
fn shadowVisibility(view: u32, light: GpuLight, worldPos: vec3<f32>, normal: vec3<f32>,
                    toLight: vec3<f32>, cascaded: bool, rotation: f32, taps: u32) -> f32 {
    if (view >= 8u) {
        return 1.0;
    }
    let nDotL = clamp(dot(normal, toLight), 0.0, 1.0);
    let texelWorld = shadowBlock.views[view].params.x;
    // Normal offset: push the sample point off the surface by a texel, more at grazing angles,
    // plus whatever the light asked for in world units (`PunctualLight::shadowBias`).
    let offset = normal * (texelWorld * (1.0 + 2.0 * (1.0 - nDotL)) * 1.4 + light.extra.w);
    let lookup = shadowLookup(view, worldPos + offset);
    if (!lookup.valid) {
        return 1.0;
    }
    // Slope-scaled constant bias on top.
    let slope = clamp(tan(acos(clamp(nDotL, 0.02, 1.0))), 0.0, 4.0);
    let depth = lookup.depth - shadowBlock.views[view].params.y * (1.0 + slope);
    if (shadowBlock.info.w > 0.5 && cascaded) {
        return pcss(view, lookup.uv, depth, max(light.sizeSoft.w, 0.05), taps, rotation);
    }
    return pcf(view, lookup.uv, depth, max(light.sizeSoft.w, 0.2) * 1.5, taps, rotation);
}

// The tap count a shading pass uses: whatever the tier asked for.
fn shadowTaps() -> u32 {
    return u32(clamp(shadowBlock.info.z, 1.0, 32.0));
}

fn shadowFactor(light: GpuLight, worldPos: vec3<f32>, normal: vec3<f32>, toLight: vec3<f32>,
                viewDepth: f32, rotation: f32, taps: u32) -> f32 {
    let flags = u32(light.cone.w + 0.5);
    if ((flags & FLAG_CASTS_SHADOW) == 0u) {
        return 1.0;
    }
    let base = u32(max(light.cone.z, 0.0));
    let cascaded = (flags & FLAG_CASCADED) != 0u;
    var visibility: f32;
    if (cascaded) {
        let count = u32(max(shadowBlock.info.y, 1.0));
        let index = cascadeFor(viewDepth, count);
        visibility = shadowVisibility(base + index, light, worldPos, normal, toLight, true, rotation, taps);

        // Crossfade the last slice of a cascade into the one behind it. Two cascades differ in
        // texel size, in normal offset (which is derived from that texel size) and in bias, so a
        // hard switch puts a discontinuity in the shadow exactly at the split plane -- a seam that
        // sweeps across the ground as the camera dollies, which is what reads as shadow popping.
        //
        // The second lookup is only taken inside the band, so the common pixel pays nothing.
        let far = shadowBlock.splits[index];
        let band = (far - cascadeNear(index)) * CASCADE_BLEND;
        if (index + 1u < count && band > 1e-4 && viewDepth > far - band) {
            let t = clamp((viewDepth - (far - band)) / band, 0.0, 1.0);
            let next = shadowVisibility(base + index + 1u, light, worldPos, normal, toLight, true,
                                        rotation, taps);
            visibility = mix(visibility, next, t);
        }
    } else {
        visibility = shadowVisibility(base, light, worldPos, normal, toLight, false, rotation, taps);
    }
    return mix(1.0, visibility, clamp(light.up.w, 0.0, 1.0));
}

// ---- screen-space contact shadows ------------------------------------------------------------------

// A short march through the depth buffer towards the light. This is what makes small parts sit
// against each other, which a cascade fitted to the whole frustum always misses.
fn contactShadow(worldPos: vec3<f32>, normal: vec3<f32>, toLight: vec3<f32>, screenUv: vec2<f32>,
                 viewDepth: f32, jitter: f32) -> f32 {
    let steps = u32(clamp(frame.shadowParams.z, 0.0, 32.0));
    if (steps == 0u) {
        return 1.0;
    }
    // The ray scales with distance so the effect keeps a constant screen size, and starts a little
    // off the surface so a grazing light does not shadow the surface with itself.
    let rayLength = frame.shadowParams.w * max(viewDepth * 0.05, 0.25);
    let stepSize = rayLength / f32(steps);
    // What counts as an occluder: anything between a quarter of a step and the ray's own length in
    // front of the marched point. Beyond that the depth buffer is showing unrelated geometry.
    let minDelta = stepSize * 0.25;
    let thickness = max(rayLength, stepSize * 12.0);
    let origin = worldPos + normal * stepSize * 0.5;
    var occlusion = 0.0;
    for (var i = 1u; i <= steps; i = i + 1u) {
        let t = (f32(i) - jitter) * stepSize;
        let p = origin + toLight * t;
        let clip = frame.viewProj * vec4<f32>(p, 1.0);
        if (clip.w <= 0.0) {
            break;
        }
        let ndc = clip.xyz / clip.w;
        if (any(abs(ndc.xy) > vec2<f32>(1.0))) {
            break;
        }
        let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        let texel = vec2<i32>(uv * frame.targetSize.xy);
        let sampled = textureLoad(sceneLinearDepth, texel, 0).r;
        let rayDepth = dot(p - frame.cameraPos.xyz, frame.cameraForward.xyz);
        let delta = rayDepth - sampled;
        if (delta > minDelta && delta < thickness) {
            // Fade the last steps so the shadow ends in a gradient, not an edge.
            occlusion = clamp(2.0 - 2.0 * f32(i) / f32(steps), 0.0, 1.0);
            break;
        }
    }
    return 1.0 - occlusion;
}

// ---- the half-resolution shadow mask (ADR-086) --------------------------------------------------

// Which mask channel a directional light's index reads. Written as nested selects rather than a
// dynamic vector index because the slot is small, known to be 0..2, and this compiles to two
// selects instead of a scratch-memory round trip.
fn maskChannel(s: vec4<f32>, slot: u32) -> f32 {
    return select(select(s.b, s.g, slot == 1u), s.r, slot == 0u);
}

struct MaskSample {
    visibility: f32,
    valid: bool,     // false = no mask texel describes this surface; shade it at full resolution
};

// Depth-aware (bilateral) upsample of the half-resolution mask, the same four-tap filter the AO
// fetch uses, plus two things AO does not need:
//
//  * a nearest-depth fallback, so a fragment whose bilinear footprint straddles a silhouette takes
//    the one tap that is on its own surface rather than a blurred average of two surfaces. A
//    penumbra survives being averaged; a contact shadow is exactly the high-frequency signal that
//    does not.
//  * an explicit `valid` flag. The mask is built from the depth prepass, which draws opaque and
//    alpha-masked geometry only, so a *blended* surface has no mask texel of its own -- the depth
//    under it belongs to whatever is behind. The same is true at a disocclusion the prepass never
//    saw. Rather than let those pixels take a neighbouring surface's shadow, they say so and the
//    caller computes the term at full resolution.
fn shadowMaskLookup(screenUv: vec2<f32>, viewDepth: f32, slot: u32) -> MaskSample {
    var out: MaskSample;
    out.visibility = 1.0;
    out.valid = false;
    let size = frame.shadowMaskParams.zw;
    if (frame.shadowMaskParams.x < 0.5 || size.x < 1.0) {
        return out;
    }
    // Plain bilinear placement, and it was checked rather than assumed. A mask texel's centre falls
    // exactly between two full-resolution texels and the pass has to pick one of them, so its
    // sample arguably sits a quarter of a mask texel off where a bilinear filter puts it. Shifting
    // the lookup a quarter texel to compensate measured *worse* in both directions -- 14.7% and
    // 15.2% of the frame differing from the unmasked reference against 8.4% unshifted -- so the
    // reasoning is wrong somewhere and the number is what stands.
    let coord = screenUv * size - vec2<f32>(0.5);
    let base = floor(coord);
    let f = coord - base;
    var total = 0.0;
    var sum = 0.0;
    var bestAgreement = 0.0;
    var bestValue = 1.0;
    for (var i = 0u; i < 4u; i = i + 1u) {
        let offset = vec2<f32>(f32(i & 1u), f32((i >> 1u) & 1u));
        let texel = clamp(base + offset, vec2<f32>(0.0), size - vec2<f32>(1.0));
        let s = textureLoad(shadowMaskTexture, vec2<i32>(texel), 0);
        let bilinear = abs((1.0 - offset.x) - f.x) * abs((1.0 - offset.y) - f.y);
        // Depth agreement: relative, so the filter behaves the same near and far.
        let dz = abs(s.a - viewDepth) / max(viewDepth, 0.05);
        let agreement = exp(-dz * 24.0);
        let value = maskChannel(s, slot);
        sum = sum + value * bilinear * agreement;
        total = total + bilinear * agreement;
        if (agreement > bestAgreement) {
            bestAgreement = agreement;
            bestValue = value;
        }
    }
    // exp(-dz * 24) = 0.2 is a relative depth difference of 6.7%: past that, no tap in the
    // footprint is on this fragment's surface and the mask has nothing to say about it.
    if (bestAgreement <= 0.2) {
        return out;
    }
    out.valid = true;
    out.visibility = clamp(select(bestValue, sum / max(total, 1e-6), total > 0.02), 0.0, 1.0);
    return out;
}
