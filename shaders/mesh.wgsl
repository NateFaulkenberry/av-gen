// Lit opaque mesh: Lambert diffuse + GGX-style specular + hemispheric ambient + fresnel rim +
// emissive. Outputs scene-linear HDR radiance; tone mapping happens in tonemap.wgsl.
#include "common.wgsl"

fn distributionGGX(nDotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d + 1e-5);
}

fn fresnelSchlick(cosTheta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let n = normalize(in.normal);
    let v = normalize(frame.cameraPos.xyz - in.worldPos);
    let l = normalize(-frame.lightDir.xyz);
    let h = normalize(l + v);

    let roughness = clamp(object.material.x, 0.04, 1.0);
    let metallic = clamp(object.material.y, 0.0, 1.0);
    let albedo = object.baseColor.rgb;
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);

    let nDotL = max(dot(n, l), 0.0);
    let nDotV = max(dot(n, v), 1e-4);
    let nDotH = max(dot(n, h), 0.0);

    let diffuse = albedo * (1.0 - metallic) / 3.14159265;
    let spec = distributionGGX(nDotH, roughness) * fresnelSchlick(max(dot(h, v), 0.0), f0) /
               (4.0 * nDotV + 1e-3);
    let direct = (diffuse + spec) * frame.lightColor.rgb * nDotL;

    // Hemispheric ambient: cool sky above, dark ground below.
    let sky = vec3<f32>(0.10, 0.12, 0.20);
    let ground = vec3<f32>(0.02, 0.015, 0.03);
    let ambient = albedo * mix(ground, sky, n.y * 0.5 + 0.5) * 0.6;

    // Fresnel rim tinted with the emissive colour so the orb reads as luminous at grazing angles.
    let rim = pow(1.0 - nDotV, 3.0) * object.emissive.rgb * (0.4 + 0.3 * object.emissive.w);
    let emissive = object.emissive.rgb * object.emissive.w;

    let color = direct + ambient + rim + emissive;
    return vec4<f32>(color, 1.0);
}
