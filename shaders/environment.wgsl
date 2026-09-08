// Image-based lighting preprocessing (Karis 2013 split sum; Colbert & Krivanek mip-filtered
// importance sampling). Every pass draws one fullscreen triangle into one cube face (or the LUT).
// Sample sequences are fixed (Hammersley), so results are deterministic for a given GPU.

const PI: f32 = 3.14159265;

struct EnvUniforms {
    faceIndex: u32,
    mipLevel: u32,
    sampleCount: u32,
    pad0: u32,
    roughness: f32,
    sourceMipCount: f32, // mip levels of the source cube (for pdf-based mip selection)
    sourceSize: f32,     // width of the source cube face at mip 0
    pad1: f32,
};

@group(0) @binding(0) var<uniform> env: EnvUniforms;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var sourceEquirect: texture_2d<f32>;
@group(0) @binding(3) var sourceCube: texture_cube<f32>;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) index: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: FsIn;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    out.ndc = positions[index];
    return out;
}

// Direction for a cube face texel (WebGPU/Vulkan cube layer order: +X -X +Y -Y +Z -Z; the
// face UV origin is the top-left, hence the flipped v).
fn faceDirection(face: u32, ndc: vec2<f32>) -> vec3<f32> {
    let u = ndc.x;
    let v = -ndc.y;
    switch (face) {
        case 0u: { return normalize(vec3<f32>(1.0, -v, -u)); }
        case 1u: { return normalize(vec3<f32>(-1.0, -v, u)); }
        case 2u: { return normalize(vec3<f32>(u, 1.0, v)); }
        case 3u: { return normalize(vec3<f32>(u, -1.0, -v)); }
        case 4u: { return normalize(vec3<f32>(u, -v, 1.0)); }
        default: { return normalize(vec3<f32>(-u, -v, -1.0)); }
    }
}

fn equirectUv(dir: vec3<f32>) -> vec2<f32> {
    let phi = atan2(dir.z, dir.x);   // -pi..pi, 0 along +X
    let theta = acos(clamp(dir.y, -1.0, 1.0));
    return vec2<f32>(0.5 + phi / (2.0 * PI), theta / PI);
}

fn radicalInverse(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, n: u32) -> vec2<f32> {
    return vec2<f32>(f32(i) / f32(n), radicalInverse(i));
}

fn tangentFrame(n: vec3<f32>) -> mat3x3<f32> {
    let up = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(n.y) > 0.999);
    let t = normalize(cross(up, n));
    let b = cross(n, t);
    return mat3x3<f32>(t, b, n);
}

fn importanceSampleGGX(xi: vec2<f32>, roughness: f32) -> vec3<f32> {
    let a = roughness * roughness;
    let phi = 2.0 * PI * xi.x;
    let cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    let sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3<f32>(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

fn distributionGGX(nDotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

// ---- pass: equirect -> cube face at a given mip (samples the equirect's own mip chain) ----
@fragment
fn fs_equirect(in: FsIn) -> @location(0) vec4<f32> {
    let dir = faceDirection(env.faceIndex, in.ndc);
    let color = textureSampleLevel(sourceEquirect, envSampler, equirectUv(dir), f32(env.mipLevel)).rgb;
    return vec4<f32>(color, 1.0);
}

// ---- pass: diffuse irradiance (cosine-weighted hemisphere, mip-filtered) ----
@fragment
fn fs_irradiance(in: FsIn) -> @location(0) vec4<f32> {
    let n = faceDirection(env.faceIndex, in.ndc);
    let frame = tangentFrame(n);
    var acc = vec3<f32>(0.0);
    let count = env.sampleCount;
    // Read from a blurred mip so a few hundred samples converge without fireflies.
    let lod = max(env.sourceMipCount - 3.0, 0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let xi = hammersley(i, count);
        let phi = 2.0 * PI * xi.x;
        let cosTheta = sqrt(1.0 - xi.y);
        let sinTheta = sqrt(xi.y);
        let l = frame * vec3<f32>(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        acc = acc + textureSampleLevel(sourceCube, envSampler, l, lod).rgb;
    }
    return vec4<f32>(acc / f32(count), 1.0);
}

// ---- pass: GGX-prefiltered specular (one face, one roughness) ----
@fragment
fn fs_prefilter(in: FsIn) -> @location(0) vec4<f32> {
    let n = faceDirection(env.faceIndex, in.ndc);
    if (env.roughness < 1e-4) {
        return vec4<f32>(textureSampleLevel(sourceCube, envSampler, n, 0.0).rgb, 1.0);
    }
    let frame = tangentFrame(n);
    let v = n; // Karis: assume v = n = r
    var acc = vec3<f32>(0.0);
    var weight = 0.0;
    let count = env.sampleCount;
    let texelSolidAngle = 4.0 * PI / (6.0 * env.sourceSize * env.sourceSize);
    for (var i = 0u; i < count; i = i + 1u) {
        let xi = hammersley(i, count);
        let h = frame * importanceSampleGGX(xi, env.roughness);
        let l = 2.0 * dot(v, h) * h - v;
        let nDotL = dot(n, l);
        if (nDotL > 0.0) {
            let nDotH = max(dot(n, h), 0.0);
            let pdf = distributionGGX(nDotH, env.roughness) * 0.25 + 1e-6; // D * n.h / (4 v.h), v = n
            let sampleSolidAngle = 1.0 / (f32(count) * pdf);
            let lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, env.sourceMipCount - 1.0);
            acc = acc + textureSampleLevel(sourceCube, envSampler, l, lod).rgb * nDotL;
            weight = weight + nDotL;
        }
    }
    return vec4<f32>(acc / max(weight, 1e-4), 1.0);
}

// ---- pass: split-sum BRDF LUT (x = n.v, y = roughness) -> (scale, bias) ----
fn geometrySchlickIbl(nDotV: f32, roughness: f32) -> f32 {
    let k = roughness * roughness / 2.0;
    return nDotV / (nDotV * (1.0 - k) + k);
}

@fragment
fn fs_brdf(in: FsIn) -> @location(0) vec4<f32> {
    let uv = vec2<f32>(in.ndc.x * 0.5 + 0.5, 0.5 - in.ndc.y * 0.5);
    let nDotV = max(uv.x, 1e-3);
    let roughness = max(uv.y, 0.02);
    let v = vec3<f32>(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    var a = 0.0;
    var b = 0.0;
    let count = env.sampleCount;
    for (var i = 0u; i < count; i = i + 1u) {
        let xi = hammersley(i, count);
        let h = importanceSampleGGX(xi, roughness);
        let l = 2.0 * dot(v, h) * h - v;
        let nDotL = max(l.z, 0.0);
        let nDotH = max(h.z, 0.0);
        let vDotH = max(dot(v, h), 0.0);
        if (nDotL > 0.0) {
            let g = geometrySchlickIbl(nDotV, roughness) * geometrySchlickIbl(nDotL, roughness);
            let gVis = g * vDotH / (nDotH * nDotV + 1e-6);
            let fc = pow(1.0 - vDotH, 5.0);
            a = a + (1.0 - fc) * gVis;
            b = b + fc * gVis;
        }
    }
    return vec4<f32>(a / f32(count), b / f32(count), 0.0, 1.0);
}
