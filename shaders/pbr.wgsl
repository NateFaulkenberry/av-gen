// glTF metallic-roughness PBR: Cook-Torrance (GGX distribution, height-correlated Smith
// visibility, Schlick Fresnel) for punctual lights, split-sum image-based lighting, normal
// mapping via a derivative-based cotangent frame, emissive, occlusion, alpha mask/blend.
// Outputs scene-linear HDR radiance; tone mapping happens in tonemap.wgsl.
#include "common.wgsl"

@group(2) @binding(0) var materialSampler: sampler;
@group(2) @binding(1) var baseColorTex: texture_2d<f32>;
@group(2) @binding(2) var metallicRoughnessTex: texture_2d<f32>;
@group(2) @binding(3) var normalTex: texture_2d<f32>;
@group(2) @binding(4) var emissiveTex: texture_2d<f32>;
@group(2) @binding(5) var occlusionTex: texture_2d<f32>;

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

fn lightRadiance(light: Light, worldPos: vec3<f32>) -> vec4<f32> { // xyz = L (towards light), w = attenuation
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

struct FragmentOut {
    @location(0) color: vec4<f32>,
};

@fragment
fn fs_main(in: VertexOut, @builtin(front_facing) frontFacing: bool) -> FragmentOut {
    let texMask = u32(object.flags.w + 0.5);
    let hasBaseColor = (texMask & 1u) != 0u;
    let hasMetalRough = (texMask & 2u) != 0u;
    let hasNormal = (texMask & 4u) != 0u;
    let hasEmissive = (texMask & 8u) != 0u;
    let hasOcclusion = (texMask & 16u) != 0u;

    var baseColor = object.baseColor;
    if (hasBaseColor) {
        baseColor = baseColor * textureSample(baseColorTex, materialSampler, in.uv);
    }
    let alphaMode = object.flags.x;
    if (alphaMode > 0.5 && alphaMode < 1.5 && baseColor.a < object.flags.y) {
        discard;
    }
    let alpha = select(1.0, baseColor.a, alphaMode > 1.5);

    var emissive = object.emissive.rgb * object.emissive.w;
    if (hasEmissive) {
        emissive = emissive * textureSample(emissiveTex, materialSampler, in.uv).rgb;
    }

    if (object.flags.z > 0.5) { // unlit
        var out: FragmentOut;
        out.color = vec4<f32>(baseColor.rgb + emissive, alpha);
        return out;
    }

    var roughness = object.material.x;
    var metallic = object.material.y;
    if (hasMetalRough) {
        let mr = textureSample(metallicRoughnessTex, materialSampler, in.uv);
        roughness = roughness * mr.g;
        metallic = metallic * mr.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    var ao = 1.0;
    if (hasOcclusion) {
        let occ = textureSample(occlusionTex, materialSampler, in.uv).r;
        ao = 1.0 + object.material.w * (occ - 1.0);
    }

    var n = normalize(in.normal);
    if (!frontFacing) {
        n = -n;
    }
    if (hasNormal) {
        var mapN = textureSample(normalTex, materialSampler, in.uv).xyz * 2.0 - 1.0;
        mapN = vec3<f32>(mapN.xy * object.material.z, mapN.z);
        n = perturbNormal(n, in.worldPos, in.uv, normalize(mapN));
    }
    let v = normalize(frame.cameraPos.xyz - in.worldPos);
    let nDotV = max(dot(n, v), 1e-4);

    let albedo = baseColor.rgb;
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let diffuseColor = albedo * (1.0 - metallic);
    let alphaR = roughness * roughness;

    // ---- punctual lights ----
    var direct = vec3<f32>(0.0);
    let lightCount = u32(frame.envParams.z + 0.5);
    for (var i = 0u; i < MAX_LIGHTS; i = i + 1u) {
        if (i >= lightCount) { break; }
        let light = frame.lights[i];
        let lr = lightRadiance(light, in.worldPos);
        let l = lr.xyz;
        let nDotL = max(dot(n, l), 0.0);
        if (nDotL <= 0.0 || lr.w <= 0.0) { continue; }
        let h = normalize(l + v);
        let nDotH = max(dot(n, h), 0.0);
        let vDotH = max(dot(v, h), 0.0);
        let f = fresnelSchlick(vDotH, f0);
        let spec = distributionGGX(nDotH, alphaR) * visibilitySmithGGX(nDotV, nDotL, alphaR) * f;
        let diff = (vec3<f32>(1.0) - f) * diffuseColor / PI;
        direct = direct + (diff + spec) * light.colorIntensity.rgb * lr.w * nDotL;
    }

    // ---- image-based lighting (split sum) or hemispheric fallback ----
    var ambient: vec3<f32>;
    let kS = fresnelSchlickRoughness(nDotV, f0, roughness);
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);
    if (frame.envParams.w > 0.5) {
        let r = reflect(-v, n);
        let irradiance = textureSample(irradianceMap, iblSampler, envRotate(n)).rgb;
        let maxMip = frame.envParams.y;
        let prefiltered = textureSampleLevel(prefilteredMap, iblSampler, envRotate(r), roughness * maxMip).rgb;
        let brdf = textureSample(brdfLut, iblSampler, vec2<f32>(nDotV, roughness)).rg;
        let specular = prefiltered * (kS * brdf.x + brdf.y);
        ambient = (kD * irradiance * albedo + specular) * frame.params.w;
    } else {
        let sky = vec3<f32>(0.10, 0.12, 0.20);
        let ground = vec3<f32>(0.02, 0.015, 0.03);
        let hemi = mix(ground, sky, n.y * 0.5 + 0.5);
        ambient = (kD * albedo * hemi + kS * hemi * 0.5) * 0.8;
    }
    ambient = ambient * ao;

    // Fresnel rim tinted with the emissive colour so glowing objects read as luminous at grazing angles.
    let rim = pow(1.0 - nDotV, 3.0) * object.emissive.rgb * (0.3 * object.emissive.w);

    var out: FragmentOut;
    out.color = vec4<f32>(direct + ambient + emissive + rim, alpha);
    return out;
}
