// Shared PBR fragment shading (ADR-023): included by pbr.wgsl (entities), procedural.wgsl
// (instanced procedural geometry) and sdf_raymarch.wgsl (raymarched surfaces) so every path
// shades identically. Expects `frame` and `object` from common.wgsl; declares the material
// (group 2) and IBL (group 3) bindings.
//
// glTF metallic-roughness PBR: Cook-Torrance (GGX distribution, height-correlated Smith
// visibility, Schlick Fresnel) for punctual lights, split-sum image-based lighting, normal
// mapping via a derivative-based cotangent frame, emissive, occlusion, alpha mask/blend, then
// distance fog. Outputs scene-linear HDR radiance; tone mapping happens in tonemap.wgsl.
//
// Procedural materials (ADR-030, layered in ADR-036; material.wgsl): when the bound material names a program
// (materialSelect.program >= 0, a slot in materialPrograms) the program runs first, before any
// texture or lighting, and replaces the object's base colour, metallic, roughness, emission and
// opacity; the per-instance multipliers and the material textures then apply to its result. With
// no program the path is byte for byte the pre-ADR-030 shader. Modules that include this file
// must also include fields.wgsl (material.wgsl needs it for Field ops).
//
// Lighting (ADR-033/034) lives in lighting.wgsl: the packed light buffer, the froxel grid, area
// lights, shadow maps, contact shadows and ambient occlusion. Every path through this file uses
// it, so a mesh, an instance and an SDF surface receive light identically.
#include "material.wgsl"
#include "lighting.wgsl"

// applyFog / fogHeightIntegral moved to common.wgsl in ADR-099: water.wgsl needs the same fog and
// does not include this file, and two copies of a fog curve is how two surfaces end up in
// different weather.

// Which program the bound material runs; a 16-byte slice of the shared select buffer.
struct MaterialSelect {
    program: i32,   // material program slot, -1 = none
    pad0: i32,
    pad1: i32,
    pad2: i32,
};

@group(2) @binding(0) var materialSampler: sampler;
@group(2) @binding(1) var baseColorTex: texture_2d<f32>;
@group(2) @binding(2) var metallicRoughnessTex: texture_2d<f32>;
@group(2) @binding(3) var normalTex: texture_2d<f32>;
@group(2) @binding(4) var emissiveTex: texture_2d<f32>;
@group(2) @binding(5) var occlusionTex: texture_2d<f32>;
@group(2) @binding(6) var<storage, read> materialPrograms: MaterialProgramBlock;
@group(2) @binding(7) var<uniform> materialSelect: MaterialSelect;

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;

fn distributionGGX(nDotH: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

// Height-correlated Smith visibility term (Heitz 2014), already divided by 4 n.l n.v.
fn visibilitySmithGGX(nDotV: f32, nDotL: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    let ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

fn fresnelSchlick(cosTheta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, f0: vec3<f32>, roughness: f32) -> vec3<f32> {
    let fr = max(vec3<f32>(1.0 - roughness), f0) - f0;
    return f0 + fr * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The cotangent frame a tangent-space normal is expressed in (Schüler), from screen-space
// derivatives, so meshes need no tangent attribute. It takes derivatives, so it must be called in
// uniform control flow; `perturbNormal` below is the whole operation for callers that can.
fn cotangentFrame(n: vec3<f32>, worldPos: vec3<f32>, uv: vec2<f32>) -> mat3x3<f32> {
    let dp1 = dpdx(worldPos);
    let dp2 = dpdy(worldPos);
    let duv1 = dpdx(uv);
    let duv2 = dpdy(uv);
    let dp2perp = cross(dp2, n);
    let dp1perp = cross(n, dp1);
    let t = dp2perp * duv1.x + dp1perp * duv2.x;
    let b = dp2perp * duv1.y + dp1perp * duv2.y;
    let scale = max(dot(t, t), dot(b, b));
    if (scale < 1e-12) {
        // No usable UV gradient (procedural geometry often carries a constant UV): fall back to a
        // frame built from the position gradient, so a program's normal channel still perturbs.
        let tf = matSafeNormalize(dp1 - n * dot(n, dp1), vec3<f32>(1.0, 0.0, 0.0));
        return mat3x3<f32>(tf, cross(n, tf), n);
    }
    let invMax = inverseSqrt(scale + 1e-12);
    return mat3x3<f32>(t * invMax, b * invMax, n);
}

fn perturbNormal(n: vec3<f32>, worldPos: vec3<f32>, uv: vec2<f32>, mapNormal: vec3<f32>) -> vec3<f32> {
    return normalize(cotangentFrame(n, worldPos, uv) * mapNormal);
}

// The ADR-036 geometric inputs a fragment can derive from screen-space derivatives:
//   x = signed curvature (1/metre; positive convex, negative concave), from how fast the normal
//       turns per unit of surface travelled: (dN/dx . dP/dx + dN/dy . dP/dy) / (|dP/dx|^2 + |dP/dy|^2)
//   y = cavity: the concave part scaled by the footprint, so it only picks up crevices small
//       enough to matter at this screen size
//   z = normal variance, 0.5 (|dN/dx|^2 + |dN/dy|^2) (Kaplanyan et al. 2016), which drives the
//       roughnessFilter op's specular anti-aliasing
//   w = footprint: world units covered by one pixel, which fades micro detail before it aliases
// Call it in uniform control flow: it takes derivatives.
fn materialGeometry(worldPos: vec3<f32>, n: vec3<f32>) -> vec4<f32> {
    let dpx = dpdx(worldPos);
    let dpy = dpdy(worldPos);
    let dnx = dpdx(n);
    let dny = dpdy(n);
    let denom = max(dot(dpx, dpx) + dot(dpy, dpy), 1e-12);
    let curvature = (dot(dnx, dpx) + dot(dny, dpy)) / denom;
    let footprint = sqrt(denom);
    let variance = 0.5 * (dot(dnx, dnx) + dot(dny, dny));
    let cavity = saturate(max(-curvature, 0.0) * footprint * 8.0);
    return vec4<f32>(curvature, cavity, variance, footprint);
}

// How fast an alpha-masked material's cutoff falls as its texture is minified, per mip level.
// 0.22 halves the cutoff over about three levels, which is roughly where a Quaternius leaf card
// starts losing coverage; it is a constant rather than a knob because preserving what the artist
// authored is not a matter of taste, and a scene that wanted the erosion could only want it by
// accident.
const kAlphaCoverageFade: f32 = 0.22;

// How far down its mip chain a 2D texture is being read, from the UV derivatives -- the quantity
// the sampler computes for itself and WGSL offers no way to ask it for. Uniform control flow only.
fn textureLodFor(uv: vec2<f32>, size: vec2<f32>) -> f32 {
    let dx = dpdx(uv) * size;
    let dy = dpdy(uv) * size;
    return max(0.5 * log2(max(dot(dx, dx), dot(dy, dy))), 0.0);
}

// The material-program inputs only the caller knows: the object-space position (before the
// deformer stack), the object id and the instance record's lanes. Entities and SDF surfaces have
// no instance, so materialInstanceZero() stands in for them.
struct MaterialInstanceInfo {
    localPosition: vec3<f32>,
    objectId: f32,
    instanceIndex: f32,   // normalised index in [0, 1]
    instanceId: f32,
    instanceRandom: vec4<f32>,
    instanceColor: vec4<f32>,
    instanceEmissive: vec4<f32>,
};

fn materialInstanceZero(localPosition: vec3<f32>) -> MaterialInstanceInfo {
    var info: MaterialInstanceInfo;
    info.localPosition = localPosition;
    info.objectId = pickIndex(object.ids.x);
    info.instanceIndex = 0.0;
    info.instanceId = 0.0;
    info.instanceRandom = vec4<f32>(0.0);
    info.instanceColor = vec4<f32>(1.0);
    info.instanceEmissive = vec4<f32>(1.0);
    return info;
}

// Everything one fragment needs to fill the auxiliary targets as well as the colour (ADR-035).
struct ShadeResult {
    color: vec4<f32>,
    normal: vec3<f32>,     // the shading normal, after normal mapping
    roughness: f32,
    emission: vec3<f32>,
    bloomWeight: f32,
    flags: f32,            // 1 = lit, 2 = emissive, 4 = transparent
};

// The whole material evaluation for one fragment. `colorMul` / `emissiveMul` are per-instance
// multipliers on the base colour and emissive (vec3(1.0) for entities). May discard (alpha mask).
// Without instance information (entities before ADR-030 wiring): the object's own local position.
fn shadePbr(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
            colorMul: vec3<f32>, emissiveMul: vec3<f32>, screenUv: vec2<f32>) -> vec4<f32> {
    return shadeSurface(worldPos, normalIn, uv, frontFacing, colorMul, emissiveMul,
                        materialInstanceZero(vec3<f32>(0.0)), screenUv).color;
}

fn shadePbrInstanced(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
                     colorMul: vec3<f32>, emissiveMul: vec3<f32>, info: MaterialInstanceInfo,
                     screenUv: vec2<f32>) -> vec4<f32> {
    return shadeSurface(worldPos, normalIn, uv, frontFacing, colorMul, emissiveMul, info, screenUv).color;
}

fn shadeSurface(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
                colorMul: vec3<f32>, emissiveMul: vec3<f32>, info: MaterialInstanceInfo,
                screenUv: vec2<f32>) -> ShadeResult {
    var result: ShadeResult;
    result.normal = normalize(normalIn);
    result.roughness = 1.0;
    result.emission = vec3<f32>(0.0);
    result.bloomWeight = max(object.ids.z, 0.0);
    result.flags = 1.0;
    // ADR-133: this draw's material tier and its local-light budget. Uniform across the draw, so
    // every branch below that reads `tier` is wave-uniform -- the condition ADR-118 measured a
    // saving to need. `tier == 0` is byte for byte the pre-ADR-133 shader.
    let tier = materialTierOf();
    let tierLocalLights = materialTierLocalLights(tier);
    let texMask = u32(object.flags.w + 0.5);
    let hasBaseColor = (texMask & 1u) != 0u;
    let hasMetalRough = (texMask & 2u) != 0u;
    let hasNormal = (texMask & 4u) != 0u;
    let hasEmissive = (texMask & 8u) != 0u;
    let hasOcclusion = (texMask & 16u) != 0u;

    // The geometric normal (front-facing corrected) and the view vector: the material program's
    // `normal` / `viewDirection` / Fresnel inputs, and the frame the normal map perturbs below.
    var n = normalize(normalIn);
    if (!frontFacing) {
        n = -n;
    }
    // ADR-111: kept before anything perturbs `n`. This is the normal of the surface the depth
    // prepass and the shadow maps actually rasterised, and it is what every shadow term is biased
    // along; `n` below becomes the shading normal and is what the BRDF uses. See ShadeContext in
    // lighting.wgsl for why they have to be two different vectors.
    let geoNormal = n;
    let v = normalize(frame.cameraPos.xyz - worldPos);

    // ---- procedural material program (ADR-030, ADR-036) ----
    var matColor = object.baseColor;        // rgb = base colour, a = opacity
    var matEmissive = object.emissive;      // rgb = emissive colour, w = intensity
    var matRoughMetal = object.material.xy; // x = roughness, y = metallic
    var programNormal = vec3<f32>(0.0, 0.0, 1.0); // tangent-space perturbation the program asked for
    var programOcclusion = 1.0;
    let programIndex = materialSelect.program;
    let viewDepth = max(dot(worldPos - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
    // The screen-space geometry, the tangent frame and the ambient occlusion the program reads
    // (ADR-036). All three are hoisted out of the branches that use them because they take
    // derivatives, which WGSL only allows in uniform control flow; the guards below are uniform
    // (they read the material's select and the object's flags), so a fragment with no program and
    // no normal map pays for none of it.
    var geometry = vec4<f32>(0.0);
    var tangentFrame = mat3x3<f32>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), n);
    var occlusion: AoSample;
    occlusion.visibility = 1.0;
    occlusion.bentNormal = n;
    var sampledOcclusion = false;
    if ((programIndex >= 0 || hasNormal) && frame.lightCounts.z < 0.5) {
        tangentFrame = cotangentFrame(n, worldPos, uv);
    }
    // A leaf card's alpha is a coverage mask, and every mip level averages it towards its own mean,
    // so a fixed cutoff eats a little more of the leaf at each level: a crown that is solid up close
    // erodes with distance into a handful of specks that crawl as the camera moves. Scaling the
    // cutoff down as the texture is minified holds roughly the coverage that was authored. Castano
    // (2010) computes the exact per-level scale offline from each mip's alpha histogram; this is the
    // one-line approximation of it, and the derivatives are the same two the block below takes.
    // Hoisted here because it takes them, and the guard is uniform: both terms are per-draw.
    var alphaCutoff = object.flags.y;
    if (object.flags.x > 0.5 && object.flags.x < 1.5 && hasBaseColor) {
        let lod = textureLodFor(uv, vec2<f32>(textureDimensions(baseColorTex, 0)));
        alphaCutoff = alphaCutoff * exp2(-lod * kAlphaCoverageFade);
    }
    if (programIndex >= 0) {
        geometry = materialGeometry(worldPos, n);
        occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
        sampledOcclusion = true;
    }
    if (programIndex >= 0) {
        var ctx = materialContextZero();
        ctx.worldPosition = worldPos;
        ctx.localPosition = info.localPosition;
        ctx.normal = n;
        ctx.uv = uv;
        ctx.objectId = info.objectId;
        ctx.instanceIndex = info.instanceIndex;
        ctx.instanceId = info.instanceId;
        ctx.instanceRandom = info.instanceRandom;
        ctx.instanceColor = info.instanceColor;
        ctx.instanceEmissive = info.instanceEmissive;
        ctx.time = frame.params.x;
        ctx.audio = frame.audio;
        ctx.audioBands = frame.audioBands;
        ctx.beat = frame.beat;
        ctx.viewDirection = v;
        ctx.depth = distance(frame.cameraPos.xyz, worldPos);
        ctx.curvature = geometry.x;
        ctx.cavity = geometry.y;
        ctx.normalVariance = geometry.z;
        ctx.footprint = geometry.w;
        ctx.occlusion = occlusion.visibility;
        ctx.materialId = object.ids.y;
        var base = materialResultZero();
        base.baseColor = object.baseColor.rgb;
        base.metallic = object.material.y;
        base.roughness = object.material.x;
        base.emission = object.emissive.rgb * object.emissive.w;
        base.opacity = object.baseColor.a;
        let program = evaluateMaterialProgram(programIndex, ctx, base);
        // The program's emission is a finished radiance, so the intensity lane becomes 1.
        matColor = vec4<f32>(program.baseColor, program.opacity);
        matEmissive = vec4<f32>(program.emission, 1.0);
        matRoughMetal = vec2<f32>(program.roughness, program.metallic);
        programNormal = program.normal;
        programOcclusion = program.occlusion;
    }

    var baseColor = matColor * vec4<f32>(colorMul, 1.0);
    if (hasBaseColor) {
        if (frame.lightCounts.z < 0.5 || object.flags.z > 0.5) {
            baseColor = baseColor * textureSample(baseColorTex, materialSampler, uv);
        } else if (object.flags.x > 0.5) {
            baseColor.a *= textureSample(baseColorTex, materialSampler, uv).a;
        }
    }
    let alphaMode = object.flags.x;
    if (alphaMode > 0.5 && alphaMode < 1.5 && baseColor.a < alphaCutoff) {
        discard;
    }
    let alpha = select(1.0, baseColor.a, alphaMode > 1.5);

    let emissiveBase = matEmissive.rgb * emissiveMul;
    var emissive = emissiveBase * matEmissive.w;
    if (hasEmissive) {
        emissive = emissive * textureSample(emissiveTex, materialSampler, uv).rgb;
    }

    if (object.flags.z > 0.5) { // unlit
        result.color = vec4<f32>(applyFog(baseColor.rgb + emissive, worldPos), alpha);
        result.normal = n;
        result.emission = baseColor.rgb + emissive;
        result.flags = 2.0;
        return result;
    }

    if (frame.lightCounts.z > 0.5) {
        let roughness = clamp(matRoughMetal.x, 0.15, 1.0);
        var context: ShadeContext;
        context.worldPos = worldPos;
        context.normal = n;
        context.geoNormal = geoNormal;
        context.view = v;
        context.diffuseColor = baseColor.rgb;
        context.f0 = vec3<f32>(0.04);
        context.roughness = roughness;
        context.alpha = roughness * roughness;
        context.nDotV = max(dot(n, v), 0.0);
        context.screenUv = screenUv;
        context.viewDepth = viewDepth;
        context.rotation = gradientNoise(screenUv * frame.targetSize.xy) * 6.28318531;
        context.jitter = 0.5;
        context.maskable = alphaMode < 1.5; // ADR-087: blended surfaces are not in the depth prepass
        context.tier = tier;
        context.localLightBudget = tierLocalLights;
        let lighting = directLighting(context);
        // The flat tier's AO budget is zero: it takes the ambient unoccluded. A material program
        // may already have sampled it, in which case the read is spent whatever this says.
        if (!sampledOcclusion && tier < 2u) {
            occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
        }
        // The styled path's ambient carries most of a night landscape, so screen-space AO applied
        // to it at full depth writes its own sampling noise straight into the largest term in the
        // image -- visible as a faint lattice on open ground, and absent from the PBR control where
        // ambient is one contributor among several. frame.styledSky.w is the floor that keeps the
        // contact darkening without printing the noise; a scene that wants deeper contact should
        // reach for the ground ambient below, which is smooth, rather than for this.
        let visibility = mix(frame.styledSky.w, 1.0,
                             clamp(occlusion.visibility * programOcclusion, 0.0, 1.0));
        let hemisphere = mix(frame.styledGround.rgb, frame.styledSky.rgb, n.y * 0.5 + 0.5);
        let ambient = baseColor.rgb * hemisphere * visibility;
        let edge = pow(1.0 - context.nDotV, 4.0) * smoothstep(-0.2, 0.7, n.y);
        let rim = baseColor.rgb * vec3<f32>(0.25, 0.45, 0.5) * edge * 0.16;
        result.color = vec4<f32>(applyFog(lighting.diffuse + lighting.specular + ambient + emissive + rim,
                                         worldPos), alpha);
        result.normal = n;
        result.roughness = roughness;
        result.emission = emissive;
        result.flags = select(1.0, 3.0, dot(emissive, emissive) > 1e-6);
        if (alphaMode > 1.5) {
            result.flags = result.flags + 4.0;
        }
        return result;
    }

    var roughness = matRoughMetal.x;
    var metallic = matRoughMetal.y;
    if (hasMetalRough) {
        let mr = textureSample(metallicRoughnessTex, materialSampler, uv);
        roughness = roughness * mr.g;
        metallic = metallic * mr.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    var ao = programOcclusion; // ADR-036: the program's own occlusion channel
    if (hasOcclusion) {
        let occ = textureSample(occlusionTex, materialSampler, uv).r;
        ao = ao * (1.0 + object.material.w * (occ - 1.0));
    }

    // ADR-036: the program's tangent-space normal first, then the material's normal map over it
    // (reoriented, so the two compose rather than one replacing the other).
    var mapNormal = programNormal;
    var perturb = programNormal.z < 0.99999;
    if (hasNormal && tier < 2u) {
        var mapN = textureSample(normalTex, materialSampler, uv).xyz * 2.0 - 1.0;
        mapN = vec3<f32>(mapN.xy * object.material.z, mapN.z);
        mapNormal = matReorientNormal(mapNormal, normalize(mapN));
        perturb = true;
    }
    if (perturb) {
        n = normalize(tangentFrame * mapNormal);
    }
    let nDotV = max(dot(n, v), 1e-4);

    let albedo = baseColor.rgb;
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let diffuseColor = albedo * (1.0 - metallic);
    let alphaR = roughness * roughness;

    // ---- direct lighting (ADR-033): clustered, area-aware, shadowed ----
    var ctx: ShadeContext;
    ctx.worldPos = worldPos;
    ctx.normal = n;
    ctx.geoNormal = geoNormal;
    ctx.view = v;
    ctx.diffuseColor = diffuseColor;
    ctx.f0 = f0;
    ctx.roughness = roughness;
    ctx.alpha = alphaR;
    ctx.nDotV = nDotV;
    ctx.screenUv = screenUv;
    ctx.viewDepth = viewDepth;
    // Deterministic per-pixel rotation and ray offset: the frame index enters through the sample
    // position, never a wall clock, so the same frame renders identically twice.
    let noise = gradientNoise(screenUv * frame.targetSize.xy);
    ctx.rotation = noise * 6.28318531;
    ctx.jitter = noise;
    ctx.maskable = alphaMode < 1.5; // ADR-087: blended surfaces are not in the depth prepass
    ctx.tier = tier;
    ctx.localLightBudget = tierLocalLights;
    let lit = directLighting(ctx);
    let direct = lit.diffuse + lit.specular;

    // ---- ambient occlusion (ADR-034): applied to ambient diffuse, and to specular via the bent normal ----
    // A material program already sampled it above (it can read the visibility); otherwise sample
    // it now, on the lit path only.
    if (!sampledOcclusion && tier < 2u) {
        occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
    }
    let aoStrength = clamp(frame.aoParams.x, 0.0, 4.0);
    let visibility = clamp(mix(1.0, occlusion.visibility, aoStrength), 0.0, 1.0);
    let bentNormal = normalize(mix(n, occlusion.bentNormal, aoStrength * 0.9));
    // Frostbite's specular occlusion from the visibility cone (Lagarde & de Rousiers 2014).
    let specularOcclusion =
        clamp(pow(nDotV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility, 0.0, 1.0);

    // ---- image-based lighting (split sum) or hemispheric fallback ----
    var ambient: vec3<f32>;
    let kS = fresnelSchlickRoughness(nDotV, f0, roughness);
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);
    if (frame.envParams.w > 0.5 && tier < 2u) {
        let r = reflect(-v, n);
        let irradiance = textureSample(irradianceMap, iblSampler, envRotate(bentNormal)).rgb;
        let maxMip = frame.envParams.y;
        let prefiltered = textureSampleLevel(prefilteredMap, iblSampler, envRotate(r), roughness * maxMip).rgb;
        let brdf = textureSample(brdfLut, iblSampler, vec2<f32>(nDotV, roughness)).rg;
        let specular = prefiltered * (kS * brdf.x + brdf.y) * specularOcclusion;
        ambient = (kD * irradiance * albedo * visibility + specular) * frame.params.w;
    } else {
        let sky = vec3<f32>(0.10, 0.12, 0.20);
        let ground = vec3<f32>(0.02, 0.015, 0.03);
        let hemi = mix(ground, sky, bentNormal.y * 0.5 + 0.5);
        ambient = (kD * albedo * hemi * visibility + kS * hemi * 0.5 * specularOcclusion) * 0.8;
    }
    ambient = ambient * ao;

    // Fresnel rim tinted with the emissive colour so glowing objects read as luminous at grazing angles.
    let rim = pow(1.0 - nDotV, 3.0) * emissiveBase * (0.3 * matEmissive.w);

    result.color = vec4<f32>(applyFog(direct + ambient + emissive + rim, worldPos), alpha);
    result.normal = n;
    result.roughness = roughness;
    result.emission = emissive + rim;
    result.flags = select(1.0, 3.0, dot(emissive, emissive) > 1e-6);
    if (alphaMode > 1.5) {
        result.flags = result.flags + 4.0;
    }
    return result;
}
