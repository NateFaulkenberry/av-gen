// Output stage: HDR scene-linear -> tone-mapping operator -> vignette, film grain -> sRGB encode.

struct TonemapUniforms {
    exposure: f32,
    operatorId: f32,   // 0 ACES fitted, 1 AgX, 2 Reinhard, 3 Khronos PBR Neutral, 4 clamp
    vignette: f32,
    grain: f32,
    size: vec2<f32>,
    seed: f32,
    chromaRetention: f32,  // 0 = the operator's own highlight rolloff, 1 = hold the source hue
};

@group(0) @binding(0) var hdrTexture: texture_2d<f32>;
// ADR-137: the HDR target may be smaller than the output when `renderScale < 1`, so this samples
// rather than loads. A `textureLoad` upscale is nearest-neighbour, which is not a quality tier --
// it is stair-stepped silhouettes and a shimmer under camera motion, and §50 rejects it. At a
// matched size the bilinear footprint collapses to weight 1 on one texel, so the 1:1 frame is
// unchanged; that is asserted rather than assumed (byte-identical capture at renderScale = 1).
@group(0) @binding(2) var hdrSampler: sampler;
@group(0) @binding(1) var<uniform> tonemap: TonemapUniforms;

struct VertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VertexOut;
    let p = positions[index];
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 1.0 - (p.y * 0.5 + 0.5));
    return out;
}

fn acesFitted(x: vec3<f32>) -> vec3<f32> {
    let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

// AgX (Troy Sobotka; polynomial fit by Benjamin Wende), base look.
fn agxDefaultContrastApprox(xIn: vec3<f32>) -> vec3<f32> {
    let x = xIn;
    let x2 = x * x;
    let x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

fn agx(val: vec3<f32>) -> vec3<f32> {
    let agxInset = mat3x3<f32>(vec3<f32>(0.842479062253094, 0.0423282422610123, 0.0423756549057051),
                               vec3<f32>(0.0784335999999992, 0.878468636469772, 0.0784336),
                               vec3<f32>(0.0792237451477643, 0.0791661274605434, 0.879142973793104));
    let agxOutset = mat3x3<f32>(vec3<f32>(1.19687900512017, -0.0528968517574562, -0.0529716355144438),
                                vec3<f32>(-0.0980208811401368, 1.15190312990417, -0.0980434501171241),
                                vec3<f32>(-0.0990297440797205, -0.0989611768448433, 1.15107367264116));
    let minEv = -12.47393;
    let maxEv = 4.026069;
    var v = agxInset * val;
    v = clamp(log2(max(v, vec3<f32>(1e-10))), vec3<f32>(minEv), vec3<f32>(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    v = agxDefaultContrastApprox(v);
    v = agxOutset * v;
    // AgX output is sRGB-encoded; return linear so the shared encode applies.
    return pow(clamp(v, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(2.2));
}

fn reinhardExtended(c: vec3<f32>) -> vec3<f32> {
    let white = 4.0;
    let l = dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
    let lm = l * (1.0 + l / (white * white)) / (1.0 + l);
    return clamp(c * (lm / max(l, 1e-5)), vec3<f32>(0.0), vec3<f32>(1.0));
}

// Khronos PBR Neutral (reference implementation).
fn pbrNeutral(colorIn: vec3<f32>) -> vec3<f32> {
    let startCompression = 0.8 - 0.04;
    let desaturation = 0.15;
    var color = colorIn;
    let x = min(color.r, min(color.g, color.b));
    let offset = select(0.04, x - 6.25 * x * x, x < 0.08);
    color -= vec3<f32>(offset);
    let peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return clamp(color, vec3<f32>(0.0), vec3<f32>(1.0));
    }
    let d = 1.0 - startCompression;
    let newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    let g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(color, vec3<f32>(newPeak), g), vec3<f32>(0.0), vec3<f32>(1.0));
}

// Every per-channel curve drives all three channels to 1.0 together, so a bright narrow-band
// colour turns white exactly where it is most visible -- the failure mode for a light that is
// meant to read as cyan or violet. This restates the source ratio at the brightness the curve
// chose: the pixel keeps the operator's exposure and the emitter's hue. In a curve's linear
// region the two are identical, so the ramp below confines the effect to compressed highlights.
fn retainChroma(hdr: vec3<f32>, mapped: vec3<f32>, amount: f32) -> vec3<f32> {
    let peakHdr = max(hdr.r, max(hdr.g, hdr.b));
    let peakMapped = max(mapped.r, max(mapped.g, mapped.b));
    if (amount <= 0.0 || peakHdr < 1e-5 || peakMapped < 1e-5) {
        return mapped;
    }
    let hue = hdr * (peakMapped / peakHdr);
    let overWhite = smoothstep(0.8, 3.0, peakHdr); // below scene white the operator's look stands
    return clamp(mix(mapped, hue, amount * overWhite), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linearToSrgb(c: vec3<f32>) -> vec3<f32> {
    let lo = c * 12.92;
    let hi = 1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - 0.055;
    return select(hi, lo, c <= vec3<f32>(0.0031308));
}

fn hash12(p: vec2<f32>) -> f32 {
    let h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    // Kept for the vignette below, which needs the frame's aspect. It is the *scene* target's size,
    // which is the right one: the vignette is a property of the framing, not of the output buffer.
    let size = vec2<f32>(textureDimensions(hdrTexture));
    let hdr = textureSampleLevel(hdrTexture, hdrSampler, in.uv, 0.0).rgb * tonemap.exposure;
    var mapped: vec3<f32>;
    let op = i32(tonemap.operatorId + 0.5);
    if (op == 1) {
        mapped = agx(hdr);
    } else if (op == 2) {
        mapped = reinhardExtended(hdr);
    } else if (op == 3) {
        mapped = pbrNeutral(hdr);
    } else if (op == 4) {
        mapped = clamp(hdr, vec3<f32>(0.0), vec3<f32>(1.0));
    } else {
        mapped = acesFitted(hdr);
    }
    mapped = retainChroma(hdr, mapped, tonemap.chromaRetention);
    if (tonemap.vignette > 0.0) {
        let d = length((in.uv - vec2<f32>(0.5)) * vec2<f32>(1.0, size.y / max(size.x, 1.0)) * 2.0);
        mapped *= 1.0 - tonemap.vignette * smoothstep(0.35, 1.25, d);
    }
    if (tonemap.grain > 0.0) {
        let n = hash12(in.uv * size + vec2<f32>(tonemap.seed * 17.0, tonemap.seed * 3.0)) - 0.5;
        mapped = clamp(mapped + n * tonemap.grain * 0.12, vec3<f32>(0.0), vec3<f32>(1.0));
    }
    return vec4<f32>(linearToSrgb(mapped), 1.0);
}
