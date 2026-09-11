// Clustered forward lighting, area lights, shadows, contact shadows and ambient occlusion
// (ADR-033, ADR-034). Included by pbr_shade.wgsl, so meshes, procedural instances and raymarched
// SDF surfaces all take exactly this path.
//
// Group 0 (the frame group) carries everything a shading pass needs beyond its own material:
//   1  sceneLights       storage   the packed scene lights (directional first, then local)
//   2  clusterLights     storage   froxel light lists: counts, then a fixed-size index block each
//   3  shadowBlock       uniform   the shadow view matrices and their biases
//   4  shadowAtlas       texture   depth_2d_array, one layer per cascade / spot map
//   5  shadowSampler     sampler   comparison, linear
//   6  aoTexture         texture   rg = octahedral bent normal, b = visibility, a = view depth
//   7  sceneLinearDepth  texture   R32F view-space depth of the prepass (contact shadows)
//   8  ltc1 / 9 ltc2     texture   the linearly-transformed-cone table (rebuilt at startup)
//   10 ltcSampler        sampler   filtering

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
@group(0) @binding(8) var ltc1: texture_2d<f32>;
@group(0) @binding(9) var ltc2: texture_2d<f32>;
@group(0) @binding(10) var ltcSampler: sampler;

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
fn pcf(view: u32, uv: vec2<f32>, depth: f32, radiusTexels: f32, taps: u32, rotation: f32) -> f32 {
    let texel = 1.0 / max(shadowBlock.info.x, 1.0);
    let c = cos(rotation);
    let s = sin(rotation);
    var sum = 0.0;
    let n = max(taps, 1u);
    for (var i = 0u; i < n; i = i + 1u) {
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
    let search = min(taps, 16u);
    for (var i = 0u; i < search; i = i + 1u) {
        let p = poissonDisc(i);
        let rotated = vec2<f32>(p.x * c - p.y * s, p.x * s + p.y * c) * searchRadius * texel;
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
                    toLight: vec3<f32>, cascaded: bool, rotation: f32) -> f32 {
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
    let taps = u32(clamp(shadowBlock.info.z, 1.0, 24.0));
    if (shadowBlock.info.w > 0.5 && cascaded) {
        return pcss(view, lookup.uv, depth, max(light.sizeSoft.w, 0.05), taps, rotation);
    }
    return pcf(view, lookup.uv, depth, max(light.sizeSoft.w, 0.2) * 1.5, taps, rotation);
}

fn shadowFactor(light: GpuLight, worldPos: vec3<f32>, normal: vec3<f32>, toLight: vec3<f32>,
                viewDepth: f32, rotation: f32) -> f32 {
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
        visibility = shadowVisibility(base + index, light, worldPos, normal, toLight, true, rotation);

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
                                        rotation);
            visibility = mix(visibility, next, t);
        }
    } else {
        visibility = shadowVisibility(base, light, worldPos, normal, toLight, false, rotation);
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

// ---- linearly transformed cones (area lights) ---------------------------------------------------

// The clamped-cosine integral of one polygon edge (Heitz et al. 2016).
fn integrateEdge(v1: vec3<f32>, v2: vec3<f32>) -> f32 {
    let cosTheta = clamp(dot(v1, v2), -0.9999, 0.9999);
    let theta = acos(cosTheta);
    return cross(v1, v2).z * (theta / max(sin(theta), 1e-4));
}

// Integrates a quadrilateral emitter through the linear transform `minv`. With the identity it is
// the exact Lambertian irradiance of the polygon; with the fitted matrix it is the GGX lobe.
fn ltcEvaluate(n: vec3<f32>, v: vec3<f32>, p: vec3<f32>, minv: mat3x3<f32>, q0: vec3<f32>, q1: vec3<f32>,
               q2: vec3<f32>, q3: vec3<f32>, twoSided: bool) -> f32 {
    // Shading frame with the view projected into the tangent plane, matching the table's fit.
    let t1 = normalize(v - n * dot(v, n));
    let t2 = cross(n, t1);
    let world = transpose(mat3x3<f32>(t1, t2, n));
    let m = minv * world;

    var l0 = m * (q0 - p);
    var l1 = m * (q1 - p);
    var l2 = m * (q2 - p);
    var l3 = m * (q3 - p);
    // Everything below the horizon contributes nothing; a cheap clamp instead of a full clip keeps
    // the branchless form and is what the reference implementation ships.
    if (l0.z < 0.0 && l1.z < 0.0 && l2.z < 0.0 && l3.z < 0.0) {
        return 0.0;
    }
    l0 = normalize(l0);
    l1 = normalize(l1);
    l2 = normalize(l2);
    l3 = normalize(l3);
    var sum = integrateEdge(l0, l1) + integrateEdge(l1, l2) + integrateEdge(l2, l3) + integrateEdge(l3, l0);
    sum = sum / (2.0 * PI);
    if (twoSided) {
        return abs(sum);
    }
    return max(sum, 0.0);
}

struct AreaCorners {
    p0: vec3<f32>,
    p1: vec3<f32>,
    p2: vec3<f32>,
    p3: vec3<f32>,
};

// A rect emitter's four corners, or the quad inscribing a disk (the ellipse solution reduces to
// this at the sizes worlds actually use, and it keeps one code path).
fn areaCorners(light: GpuLight) -> AreaCorners {
    let isDisk = light.positionType.w > LIGHT_RECT + 0.5;
    // A disk of radius r integrates like a square of side r * sqrt(pi), matching its area.
    let halfW = select(light.sizeSoft.x * 0.5, light.sizeSoft.z * 0.8862269, isDisk);
    let halfH = select(light.sizeSoft.y * 0.5, light.sizeSoft.z * 0.8862269, isDisk);
    let hx = light.tangent.xyz * halfW;
    let hy = light.up.xyz * halfH;
    let c = light.positionType.xyz;
    var out: AreaCorners;
    out.p0 = c - hx - hy;
    out.p1 = c + hx - hy;
    out.p2 = c + hx + hy;
    out.p3 = c - hx + hy;
    return out;
}

// ---- light evaluation ----------------------------------------------------------------------------

struct LightSample {
    diffuse: vec3<f32>,  // already multiplied by the light's radiance
    specular: vec3<f32>,
};

struct ShadeContext {
    worldPos: vec3<f32>,
    normal: vec3<f32>,
    view: vec3<f32>,       // towards the eye
    diffuseColor: vec3<f32>,
    f0: vec3<f32>,
    roughness: f32,
    alpha: f32,            // roughness squared
    nDotV: f32,
    screenUv: vec2<f32>,
    viewDepth: f32,
    rotation: f32,         // per-pixel PCF rotation
    jitter: f32,           // per-pixel contact-shadow offset
};

fn distributionGgxL(nDotH: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

fn visibilitySmithL(nDotV: f32, nDotL: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    let ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

fn fresnelSchlickL(cosTheta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Punctual (and representative-point) shading of one light.
fn painterlyRamp(illumination: f32) -> f32 {
    return 0.18 + 0.42 * smoothstep(0.08, 0.38, illumination)
                + 0.40 * smoothstep(0.58, 0.88, illumination);
}

fn shadePainterly(ctx: ShadeContext, direction: vec3<f32>, radiance: vec3<f32>) -> LightSample {
    var result: LightSample;
    let incidence = dot(ctx.normal, direction);
    let diffuse = painterlyRamp(incidence * 0.7 + 0.15);
    let halfway = matSafeNormalize(direction + ctx.view, ctx.normal);
    let highlight = smoothstep(mix(0.92, 0.72, ctx.roughness), 0.99,
                               max(dot(ctx.normal, halfway), 0.0));
    result.diffuse = ctx.diffuseColor * radiance * (diffuse / PI);
    result.specular = radiance * (highlight * (1.0 - ctx.roughness) * 0.12)
                     * smoothstep(0.0, 0.25, incidence);
    return result;
}

fn shadePunctual(light: GpuLight, ctx: ShadeContext, l: vec3<f32>, attenuation: f32,
                 alphaOverride: f32) -> LightSample {
    if (frame.lightCounts.z > 0.5) {
        return shadePainterly(ctx, l, light.colorIntensity.rgb * attenuation);
    }
    var out: LightSample;
    out.diffuse = vec3<f32>(0.0);
    out.specular = vec3<f32>(0.0);
    let nDotL = dot(ctx.normal, l);
    if (nDotL <= 0.0 || attenuation <= 0.0) {
        return out;
    }
    let h = normalize(l + ctx.view);
    let nDotH = max(dot(ctx.normal, h), 0.0);
    let vDotH = max(dot(ctx.view, h), 0.0);
    let f = fresnelSchlickL(vDotH, ctx.f0);
    let spec = distributionGgxL(nDotH, alphaOverride) * visibilitySmithL(ctx.nDotV, nDotL, alphaOverride) * f;
    let diff = (vec3<f32>(1.0) - f) * ctx.diffuseColor / PI;
    let radiance = light.colorIntensity.rgb * attenuation * nDotL;
    out.diffuse = diff * radiance;
    out.specular = spec * radiance;
    return out;
}

// Rect and Disk through linearly transformed cones.
fn shadeArea(light: GpuLight, ctx: ShadeContext) -> LightSample {
    var out: LightSample;
    out.diffuse = vec3<f32>(0.0);
    out.specular = vec3<f32>(0.0);
    let corners = areaCorners(light);
    let identity = mat3x3<f32>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0));
    let diffuse = ltcEvaluate(ctx.normal, ctx.view, ctx.worldPos, identity, corners.p0, corners.p1, corners.p2,
                              corners.p3, false);
    if (diffuse <= 0.0) {
        return out;
    }
    // The table is indexed by (roughness, n.v) with a half-texel inset so the edges do not clamp.
    let uv = vec2<f32>(ctx.nDotV, ctx.roughness) * (31.0 / 32.0) + vec2<f32>(0.5 / 32.0);
    let t1 = textureSampleLevel(ltc1, ltcSampler, uv, 0.0);
    let t2 = textureSampleLevel(ltc2, ltcSampler, uv, 0.0);
    let minv = mat3x3<f32>(vec3<f32>(t1.x, 0.0, t1.w), vec3<f32>(0.0, t1.z, 0.0), vec3<f32>(t1.y, 0.0, 1.0));
    let specular = ltcEvaluate(ctx.normal, ctx.view, ctx.worldPos, minv, corners.p0, corners.p1, corners.p2,
                               corners.p3, false);
    // Range window so a rect light still respects its authored reach.
    var window = 1.0;
    if (light.directionRange.w > 0.0) {
        let d = distance(light.positionType.xyz, ctx.worldPos) / light.directionRange.w;
        let w = clamp(1.0 - d * d * d * d, 0.0, 1.0);
        window = w * w;
    }
    let radiance = light.colorIntensity.rgb * window;
    out.diffuse = ctx.diffuseColor * radiance * diffuse;
    out.specular = radiance * specular * (ctx.f0 * t2.x + vec3<f32>(t2.y));
    return out;
}

// Tube and Sphere through the representative point (Karis 2013).
fn shadeRepresentative(light: GpuLight, ctx: ShadeContext) -> LightSample {
    let r = max(light.sizeSoft.z, 1e-3);
    let reflected = reflect(-ctx.view, ctx.normal);
    var center = light.positionType.xyz;
    if (light.positionType.w > LIGHT_TUBE - 0.5 && light.positionType.w < LIGHT_TUBE + 0.5) {
        // Closest point on the tube's axis to the reflection ray.
        let halfLength = max(light.sizeSoft.x * 0.5, 1e-4);
        let axis = light.tangent.xyz;
        let a = center - axis * halfLength;
        let b = center + axis * halfLength;
        let ab = b - a;
        let t = clamp(dot(ctx.worldPos - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        center = a + ab * t;
    }
    let toCenter = center - ctx.worldPos;

    // Sphere: the closest point on the emitter to the reflection ray.
    let toRay = dot(toCenter, reflected) * reflected - toCenter;
    let representative = toCenter + toRay * clamp(r / max(length(toRay), 1e-4), 0.0, 1.0);
    let l = normalize(representative);
    let dist2 = max(dot(toCenter, toCenter), 1e-4);
    var attenuation = 1.0 / dist2;
    if (light.directionRange.w > 0.0) {
        let d = sqrt(dist2) / light.directionRange.w;
        let w = clamp(1.0 - d * d * d * d, 0.0, 1.0);
        attenuation = attenuation * w * w;
    }
    // The emitter's area turns its radiance into an intensity (the CPU packs nits for area kinds).
    var area = PI * r * r;
    if (light.positionType.w > LIGHT_TUBE - 0.5 && light.positionType.w < LIGHT_TUBE + 0.5) {
        area = 2.0 * r * max(light.sizeSoft.x, 1e-3);
    }
    // Sphere-light normalisation: widen the lobe by the emitter's angular size.
    let widened = clamp(ctx.alpha + r / (3.0 * sqrt(dist2)), 0.0, 1.0);
    let energy = ctx.alpha / max(widened, 1e-4);
    var sample = shadePunctual(light, ctx, l, attenuation * area, widened);
    sample.specular = sample.specular * energy * energy;
    // Diffuse comes from the emitter centre, which is stable at grazing angles.
    let diffuseSample = shadePunctual(light, ctx, normalize(toCenter), attenuation * area, ctx.alpha);
    sample.diffuse = diffuseSample.diffuse;
    return sample;
}

// One light, whatever kind, including its shadow and contact shadow.
fn evaluateLight(index: u32, ctx: ShadeContext) -> LightSample {
    let light = sceneLights[index];
    var sample: LightSample;
    sample.diffuse = vec3<f32>(0.0);
    sample.specular = vec3<f32>(0.0);
    var toLight = vec3<f32>(0.0, 1.0, 0.0);

    let kind = light.positionType.w;
    if (kind < LIGHT_POINT - 0.5) {
        toLight = -light.directionRange.xyz;
        sample = shadePunctual(light, ctx, toLight, 1.0, ctx.alpha);
    } else if (kind < LIGHT_SPOT + 0.5) {
        let delta = light.positionType.xyz - ctx.worldPos;
        let dist2 = max(dot(delta, delta), 1e-4);
        let dist = sqrt(dist2);
        toLight = delta / dist;
        var attenuation = 1.0 / dist2;
        if (light.directionRange.w > 0.0) {
            let d = dist / light.directionRange.w;
            let w = clamp(1.0 - d * d * d * d, 0.0, 1.0);
            attenuation = attenuation * w * w;
        }
        if (kind > LIGHT_POINT + 0.5) {
            let cosAngle = dot(-toLight, light.directionRange.xyz);
            let spot = clamp((cosAngle - light.cone.x) * light.cone.y, 0.0, 1.0);
            attenuation = attenuation * spot * spot;
        }
        sample = shadePunctual(light, ctx, toLight, attenuation, ctx.alpha);
    } else if (kind < LIGHT_DISK + 0.5) {
        toLight = normalize(light.positionType.xyz - ctx.worldPos);
        sample = shadeArea(light, ctx);
    } else {
        toLight = normalize(light.positionType.xyz - ctx.worldPos);
        sample = shadeRepresentative(light, ctx);
    }

    if (light.extra.x > 0.5) {
        sample.specular = vec3<f32>(0.0);
    }
    if (light.extra.y > 0.5) {
        sample.diffuse = vec3<f32>(0.0);
    }
    let lit = max(max(sample.diffuse.r + sample.diffuse.g + sample.diffuse.b,
                      sample.specular.r + sample.specular.g + sample.specular.b), 0.0);
    if (lit <= 0.0) {
        return sample;
    }

    var visibility = shadowFactor(light, ctx.worldPos, ctx.normal, toLight, ctx.viewDepth, ctx.rotation);
    // The contact march is per-light and independent of the map: a point or area light never gets
    // a map at all, and this is the only shadow it has (ADR-034). Combined by minimum, so whichever
    // says "occluded" wins.
    if (light.tangent.w > 0.5 && light.up.w > 0.0) {
        let contact = contactShadow(ctx.worldPos, ctx.normal, toLight, ctx.screenUv, ctx.viewDepth, ctx.jitter);
        visibility = min(visibility, mix(1.0, contact, clamp(light.up.w, 0.0, 1.0)));
    }
    sample.diffuse = sample.diffuse * visibility;
    sample.specular = sample.specular * visibility;
    return sample;
}

// The uniform fallback path: the original punctual evaluation over `frame.lights`.
fn uniformLightRadiance(light: Light, worldPos: vec3<f32>) -> vec4<f32> {
    let kind = light.positionType.w;
    if (kind < 0.5) {
        return vec4<f32>(-light.directionRange.xyz, 1.0);
    }
    let toLight = light.positionType.xyz - worldPos;
    let dist2 = max(dot(toLight, toLight), 1e-4);
    let dist = sqrt(dist2);
    let l = toLight / dist;
    var attenuation = 1.0 / dist2;
    let range = light.directionRange.w;
    if (range > 0.0) {
        let ratio = dist / range;
        let window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
        attenuation = attenuation * window * window;
    }
    if (kind > 1.5) {
        let cosAngle = dot(-l, light.directionRange.xyz);
        let spot = clamp((cosAngle - light.cone.x) * light.cone.y, 0.0, 1.0);
        attenuation = attenuation * spot * spot;
    }
    return vec4<f32>(l, attenuation);
}

fn shadeUniform(light: Light, ctx: ShadeContext, l: vec3<f32>, attenuation: f32) -> LightSample {
    if (frame.lightCounts.z > 0.5) {
        return shadePainterly(ctx, l, light.colorIntensity.rgb * attenuation);
    }
    var out: LightSample;
    out.diffuse = vec3<f32>(0.0);
    out.specular = vec3<f32>(0.0);
    let nDotL = dot(ctx.normal, l);
    if (nDotL <= 0.0 || attenuation <= 0.0) {
        return out;
    }
    let h = normalize(l + ctx.view);
    let nDotH = max(dot(ctx.normal, h), 0.0);
    let vDotH = max(dot(ctx.view, h), 0.0);
    let f = fresnelSchlickL(vDotH, ctx.f0);
    let spec = distributionGgxL(nDotH, ctx.alpha) * visibilitySmithL(ctx.nDotV, nDotL, ctx.alpha) * f;
    let diff = (vec3<f32>(1.0) - f) * ctx.diffuseColor / PI;
    let radiance = light.colorIntensity.rgb * attenuation * nDotL;
    out.diffuse = diff * radiance;
    out.specular = spec * radiance;
    return out;
}

// The froxel this fragment belongs to (ADR-033).
fn clusterIndexFor(screenUv: vec2<f32>, viewDepth: f32) -> u32 {
    let dims = vec3<u32>(u32(frame.clusterParams.x), u32(frame.clusterParams.y), u32(frame.clusterParams.z));
    let ix = min(u32(clamp(screenUv.x, 0.0, 0.9999) * f32(dims.x)), dims.x - 1u);
    // The grid's y runs bottom-up (NDC order), the screen's runs top-down.
    let iy = min(u32(clamp(1.0 - screenUv.y, 0.0, 0.9999) * f32(dims.y)), dims.y - 1u);
    let depth = max(viewDepth, frame.clusterDepth.z);
    let slice = i32(floor(log2(depth) * frame.clusterDepth.x + frame.clusterDepth.y));
    let iz = u32(clamp(slice, 0, i32(dims.z) - 1));
    return (iz * dims.y + iy) * dims.x + ix;
}

// Direct lighting: every directional light, then the local lights of this fragment's froxel (or
// all of them on the uniform fallback tier, where the packed buffer holds at most eight).
fn directLighting(ctx: ShadeContext) -> LightSample {
    var total: LightSample;
    total.diffuse = vec3<f32>(0.0);
    total.specular = vec3<f32>(0.0);
    let directional = u32(frame.lightCounts.x + 0.5);
    let totalLights = u32(frame.lightCounts.y + 0.5);
    if (frame.clusterParams.w > 0.5) {
        // Directional lights reach every fragment, so they never enter the grid.
        for (var i = 0u; i < directional; i = i + 1u) {
            let s = evaluateLight(i, ctx);
            total.diffuse = total.diffuse + s.diffuse;
            total.specular = total.specular + s.specular;
        }
        let cluster = clusterIndexFor(ctx.screenUv, ctx.viewDepth);
        let clusterCount = u32(frame.clusterParams.x * frame.clusterParams.y * frame.clusterParams.z);
        let count = min(clusterLights[cluster], MAX_LIGHTS_PER_CLUSTER);
        let base = clusterCount + cluster * MAX_LIGHTS_PER_CLUSTER;
        for (var i = 0u; i < count; i = i + 1u) {
            let index = directional + clusterLights[base + i];
            if (index >= totalLights) {
                continue;
            }
            let s = evaluateLight(index, ctx);
            total.diffuse = total.diffuse + s.diffuse;
            total.specular = total.specular + s.specular;
        }
    } else {
        // The pre-ADR-033 fallback tier: the eight lights of the uniform array, no clusters, no
        // shadows and no area kinds, so a frame rendered on this tier matches the old renderer.
        let uniformCount = u32(frame.envParams.z + 0.5);
        for (var i = 0u; i < MAX_LIGHTS; i = i + 1u) {
            if (i >= uniformCount) { break; }
            let light = frame.lights[i];
            let lr = uniformLightRadiance(light, ctx.worldPos);
            let s = shadeUniform(light, ctx, lr.xyz, lr.w);
            total.diffuse = total.diffuse + s.diffuse;
            total.specular = total.specular + s.specular;
        }
    }
    return total;
}
