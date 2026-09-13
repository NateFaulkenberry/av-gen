// Clustered forward lighting and area lights (ADR-033). Included by pbr_shade.wgsl, so meshes,
// procedural instances and raymarched SDF surfaces all take exactly this path.
//
// The shadow half of what used to live here -- shadow maps, PCSS, the screen-space contact march
// and the ambient-occlusion fetch -- is in shadows.wgsl, which this file includes. It was split
// out so shaders/shadow_mask.wgsl can compute the same terms in its own pass (ADR-087).
//
// Group 0 (the frame group) carries everything a shading pass needs beyond its own material:
// bindings 1-7 and 11 are declared by shadows.wgsl; this file adds
//   8  ltc1 / 9 ltc2     texture   the linearly-transformed-cone table (rebuilt at startup)
//   10 ltcSampler        sampler   filtering
#include "shadows.wgsl"

@group(0) @binding(8) var ltc1: texture_2d<f32>;
@group(0) @binding(9) var ltc2: texture_2d<f32>;
@group(0) @binding(10) var ltcSampler: sampler;


// Normalises, or returns the fallback when the vector is too short to have a direction. This used
// to call material.wgsl's matSafeNormalize, which meant this file could only be included by a
// module that had already included the material interpreter -- an undeclared dependency that
// water.wgsl (ADR-099), which wants the lighting and has no material program in it, tripped over
// immediately. One copy of four lines is cheaper than that coupling.
fn lightSafeNormalize(v: vec3<f32>, fallback: vec3<f32>) -> vec3<f32> {
    let len2 = dot(v, v);
    if (len2 > 1e-12) {
        return v * inverseSqrt(len2);
    }
    return fallback;
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
    // ADR-111: the *geometric* normal -- the interpolated vertex normal, front-facing corrected,
    // before the material program's perturbation and before the material's normal map. `normal`
    // above is the shading normal and is what every BRDF term here uses; this one is what the
    // shadow terms use, and the two are deliberately different vectors.
    //
    // A shadow map holds the depth of *rasterised geometry*, and the screen-space contact march
    // reads the depth buffer, which holds the same thing. Neither has ever heard of a normal map.
    // The normal offset and the slope-scaled bias exist to move a sample point off the surface the
    // shadow map recorded, so they have to be computed from the normal of that surface: offsetting
    // along a normal-mapped normal slides the lookup sideways across the surface by an amount that
    // tracks the texture rather than the geometry, which is acne in the troughs and detachment on
    // the peaks. Holbert's normal-offset shadows (2011) uses the vertex normal for exactly this
    // reason.
    //
    // It is also the only normal the half-resolution shadow mask can have. That pass runs before
    // the scene pass, over the linear depth of the prepass, so it reconstructs a geometric normal
    // from the depth buffer and has no way to learn about a material's normal map. Handing the
    // full-resolution path a shading normal here made the two paths compute different shadows for
    // the same fragment, so the frame changed when the mask was switched on -- which is the defect
    // ADR-111 is about.
    geoNormal: vec3<f32>,
    // ADR-087: whether this fragment may read the half-resolution shadow mask. False for a
    // blended surface, which the depth prepass never drew, so the mask under it describes
    // whatever is behind rather than the surface itself.
    maskable: bool,
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
    let halfway = lightSafeNormalize(direction + ctx.view, ctx.normal);
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

    // ADR-087: the leading directional lights have had their *shadow map* term computed for them at
    // half resolution, in shaders/shadow_mask.wgsl, before this pass started. Reading it back costs
    // four texel loads; computing it costs a PCSS blocker search and a filtered cascade lookup. The
    // mask is consulted only where it has something to say about *this* surface -- everywhere else
    // the full computation runs unchanged, which is what keeps blended geometry (never drawn into
    // the depth prepass), disocclusions and every local light correct.
    // The normal every shadow term below is biased along. Guarded rather than used raw: a
    // ShadeContext is zero-initialised, so a future construction site that forgets to fill
    // `geoNormal` would otherwise silently bias along the zero vector -- an offset of nothing and a
    // slope scale pinned at its maximum, which reads as a scene-wide shadow bias bug with no
    // obvious cause. Falling back to the shading normal restores the old behaviour instead.
    let shadowNormal = select(ctx.normal, ctx.geoNormal, dot(ctx.geoNormal, ctx.geoNormal) > 0.5);

    var visibility = -1.0;
    if (ctx.maskable && index < u32(frame.shadowMaskParams.y + 0.5)) {
        let masked = shadowMaskLookup(ctx.screenUv, ctx.viewDepth, index);
        if (masked.valid) {
            visibility = masked.visibility;
        }
    }
    if (visibility < 0.0) {
        visibility = shadowFactor(light, ctx.worldPos, shadowNormal, toLight, ctx.viewDepth, ctx.rotation,
                                  shadowTaps());
    }
    // The contact march is per-light and independent of the map: a point or area light never gets
    // a map at all, and this is the only shadow it has (ADR-034). Combined by minimum, so whichever
    // says "occluded" wins. It stays at full resolution whether or not the map term was masked --
    // it is the one term that does not survive being computed once per 2x2 (see shadow_mask.wgsl).
    // ADR-118: and it is skipped where its answer cannot matter. The march is combined by
    // `min(visibility, mix(1, contact, strength))`, and `mix(1, contact, strength)` is bounded
    // below by `1 - strength` whatever the march finds. So a fragment the shadow map already
    // reports at or under `1 - strength` takes the same value either way, and the twelve depth
    // loads are twelve depth loads spent on a number that is discarded. This is an algebraic
    // identity on the combine, not a threshold anybody tuned: at full shadow strength it says
    // "a fully shadowed fragment cannot be shadowed further", and it is exact in both directions.
    let contactStrength = clamp(light.up.w, 0.0, 1.0);
    if (light.tangent.w > 0.5 && contactStrength > 0.0 && visibility > 1.0 - contactStrength) {
        let contact = contactShadow(ctx.worldPos, shadowNormal, toLight, ctx.screenUv, ctx.viewDepth, ctx.jitter);
        visibility = min(visibility, mix(1.0, contact, contactStrength));
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
