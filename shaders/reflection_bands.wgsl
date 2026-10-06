// ADR-1151: reflection-only light bands -- strip and ring lights in DIRECTION space, after THE ASTRAL
// FORGE prototype's env() (prototypes/astral-forge/shaders/lighting.wgsl). The radiance a band puts in a
// reflected direction; nothing else reads it, so a band lights no diffuse term and is never drawn behind
// the world. scene/reflection_bands.cpp's `reflectionBandRadiance` is the CPU twin, line for line.
//
// The includer supplies the lanes (scene::ReflectionBandLanes) through five accessors, because the lit
// path reads them from the frame block and the particle path (ADR-1153) from its own uniforms -- the
// particle pipelines bind no frame block:
//   fn bandLaneInfo() -> vec4<f32>    strips, phase, gain, rotation
//   fn bandLaneSoft() -> vec4<f32>    soft box key xyz, intensity
//   fn bandLaneSoft2() -> vec4<f32>   falloff, sky fill, 0, 1 = on
//   fn bandLaneRate() -> vec4<f32>    dash rates
//   fn bandLane(i: u32) -> vec4<f32>  [2k] axis + offset, [2k + 1] width, intensity, segments, warmth

fn bandSmooth(e0: f32, e1: f32, x: f32) -> f32 {
    let t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

fn bandAtan2(y: f32, x: f32) -> f32 {
    // atan2(0, 0) is NaN on Metal
    return select(atan2(y, x), 0.0, abs(x) + abs(y) < 1e-12);
}

// `alpha` is the reflecting lobe's angular width (the GGX alpha); `softbox` false is the strips alone.
fn reflectionBands(dirIn: vec3<f32>, alpha: f32, softbox: bool) -> vec3<f32> {
    let info = bandLaneInfo();
    let c = cos(info.w);
    let s = sin(info.w);
    let d = vec3<f32>(c * dirIn.x + s * dirIn.z, dirIn.y, -s * dirIn.x + c * dirIn.z);
    var sum = vec3<f32>(0.0);
    let count = min(u32(info.x + 0.5), 4u);
    let rates = bandLaneRate();
    for (var k = 0u; k < count; k = k + 1u) {
        let a0 = bandLane(2u * k);
        let a1 = bandLane(2u * k + 1u);
        if (a1.y <= 0.0) {
            continue;
        }
        let x = dot(d, a0.xyz) - a0.w;
        let ww = a1.x * a1.x + alpha * alpha;
        var g = exp(-x * x / (2.0 * ww)) * a1.x / sqrt(ww);
        if (a1.z > 0.5) {
            let up = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(a0.y) > 0.9);
            let e1 = normalize(cross(up, a0.xyz));
            let e2 = cross(a0.xyz, e1);
            let ph = bandAtan2(dot(d, e2), dot(d, e1));
            let u = ph * a1.z / 6.28318530718 + info.y * rates[k];
            let sf = u - floor(u);
            let soft = 0.04 + alpha;
            g = g * bandSmooth(0.0, soft, sf) * bandSmooth(0.0, soft, 0.72 - sf);
        }
        sum = sum + a1.y * g * mix(vec3<f32>(0.80, 0.90, 1.08), vec3<f32>(1.08, 0.93, 0.78), a1.w);
    }
    if (softbox) {
        let soft = bandLaneSoft();
        let soft2 = bandLaneSoft2();
        let lobe = exp((dot(d, soft.xyz) - 1.0) * soft2.x) * soft.w + soft2.y * bandSmooth(-0.3, 1.0, d.y);
        sum = sum + lobe * vec3<f32>(0.92, 0.95, 1.0);
    }
    return sum * info.z;
}

// The nearest direction on strip k to `r`: the "light direction" a groove's grating (ADR-1152) diffracts.
fn reflectionBandDirection(k: u32, r: vec3<f32>) -> vec3<f32> {
    let info = bandLaneInfo();
    let c = cos(info.w);
    let s = sin(info.w);
    let a0 = bandLane(2u * k);
    // the axis in world space (the rig turned by +rotation)
    let axis = vec3<f32>(c * a0.x - s * a0.z, a0.y, s * a0.x + c * a0.z);
    let v = r - axis * (dot(r, axis) - a0.w);
    let l = length(v);
    return select(r, v / l, l > 1e-6);
}

// Karis' analytic fit of the split sum's environment BRDF (2014, "Physically Based Shading on Mobile"):
// the scale and bias on f0 that the prefiltered term is multiplied by. The bands are analytic, so they
// need no LUT; this is what makes a rough or grazing reflection of a band carry the right energy.
fn bandEnvBrdf(f0: vec3<f32>, roughness: f32, nDotV: f32) -> vec3<f32> {
    let c0 = vec4<f32>(-1.0, -0.0275, -0.572, 0.022);
    let c1 = vec4<f32>(1.0, 0.0425, 1.04, -0.04);
    let r = roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    let ab = vec2<f32>(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}
