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
// Procedural materials (ADR-030, material.wgsl): when the bound material names a program
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
@group(2) @binding(6) var<uniform> materialPrograms: MaterialProgramBlock;
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

// Perturbs the interpolated normal with a tangent-space normal map using screen-space
// derivatives (Schüler's cotangent frame), so meshes need no tangent attribute.
fn perturbNormal(n: vec3<f32>, worldPos: vec3<f32>, uv: vec2<f32>, mapNormal: vec3<f32>) -> vec3<f32> {
    let dp1 = dpdx(worldPos);
    let dp2 = dpdy(worldPos);
    let duv1 = dpdx(uv);
    let duv2 = dpdy(uv);
    let dp2perp = cross(dp2, n);
    let dp1perp = cross(n, dp1);
    let t = dp2perp * duv1.x + dp1perp * duv2.x;
    let b = dp2perp * duv1.y + dp1perp * duv2.y;
    let invMax = inverseSqrt(max(dot(t, t), dot(b, b)) + 1e-12);
    let tbn = mat3x3<f32>(t * invMax, b * invMax, n);
    return normalize(tbn * mapNormal);
}

// Exponential-squared distance fog towards frame.fogParams.rgb; density 0 leaves the colour
// untouched (the branch keeps the no-fog output bit-identical to the pre-fog shader).
fn applyFog(color: vec3<f32>, worldPos: vec3<f32>) -> vec3<f32> {
    let density = frame.fogParams.w;
    if (density <= 0.0) {
        return color;
    }
    let d = distance(frame.cameraPos.xyz, worldPos) * density;
    let f = exp(-d * d);
    return mix(frame.fogParams.rgb, color, f);
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
    info.objectId = object.ids.x;
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
    let v = normalize(frame.cameraPos.xyz - worldPos);

    // ---- procedural material program (ADR-030) ----
    var matColor = object.baseColor;        // rgb = base colour, a = opacity
    var matEmissive = object.emissive;      // rgb = emissive colour, w = intensity
    var matRoughMetal = object.material.xy; // x = roughness, y = metallic
    let programIndex = materialSelect.program;
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
        var base: MaterialResult;
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
    }

    var baseColor = matColor * vec4<f32>(colorMul, 1.0);
    if (hasBaseColor) {
        baseColor = baseColor * textureSample(baseColorTex, materialSampler, uv);
    }
    let alphaMode = object.flags.x;
    if (alphaMode > 0.5 && alphaMode < 1.5 && baseColor.a < object.flags.y) {
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

    var roughness = matRoughMetal.x;
    var metallic = matRoughMetal.y;
    if (hasMetalRough) {
        let mr = textureSample(metallicRoughnessTex, materialSampler, uv);
        roughness = roughness * mr.g;
        metallic = metallic * mr.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    var ao = 1.0;
    if (hasOcclusion) {
        let occ = textureSample(occlusionTex, materialSampler, uv).r;
        ao = 1.0 + object.material.w * (occ - 1.0);
    }

    if (hasNormal) {
        var mapN = textureSample(normalTex, materialSampler, uv).xyz * 2.0 - 1.0;
        mapN = vec3<f32>(mapN.xy * object.material.z, mapN.z);
        n = perturbNormal(n, worldPos, uv, normalize(mapN));
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
    ctx.view = v;
    ctx.diffuseColor = diffuseColor;
    ctx.f0 = f0;
    ctx.roughness = roughness;
    ctx.alpha = alphaR;
    ctx.nDotV = nDotV;
    ctx.screenUv = screenUv;
    ctx.viewDepth = max(dot(worldPos - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
    // Deterministic per-pixel rotation and ray offset: the frame index enters through the sample
    // position, never a wall clock, so the same frame renders identically twice.
    let noise = gradientNoise(screenUv * frame.targetSize.xy);
    ctx.rotation = noise * 6.28318531;
    ctx.jitter = noise;
    let lit = directLighting(ctx);
    let direct = lit.diffuse + lit.specular;

    // ---- ambient occlusion (ADR-034): applied to ambient diffuse, and to specular via the bent normal ----
    let occlusion = sampleAmbientOcclusion(screenUv, ctx.viewDepth, n);
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
    if (frame.envParams.w > 0.5) {
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
