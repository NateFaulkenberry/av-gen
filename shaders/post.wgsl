// Built-in post-processing chain (ADR-016, image formation ADR-039). Every pass is a fullscreen
// triangle over `source` (and optionally `second` / `third`) with `depth` for depth-aware effects,
// `emission` and `identifier` for selective post (ADR-035; 1x1 placeholders when the renderer has
// no such targets, in which case the `available` flags in the uniforms are 0 and every effect
// falls back to its luminance-only behaviour). Uniforms carry per-pass data.
//
// Order (docs/image-formation.md): exposure, defocus (depth of field and the ADR-079 tilt-shift
// band, which share one gather), motion blur, lens distortion and chromatic aberration, bloom /
// halation / anamorphic, colour grade, sharpen; the tone map, vignette and grain follow in
// shaders/tonemap.wgsl.

struct PostUniforms {
    texelSize: vec2<f32>,    // 1 / source size
    outputSize: vec2<f32>,
    params0: vec4<f32>,
    params1: vec4<f32>,
    params2: vec4<f32>,
    params3: vec4<f32>,
    params4: vec4<f32>,
    lift: vec4<f32>,
    gamma: vec4<f32>,
    gain: vec4<f32>,
    tintA: vec4<f32>,
    tintB: vec4<f32>,
    cameraPos: vec4<f32>,
    prevViewProj: mat4x4<f32>,
    invViewProj: mat4x4<f32>,
    // ADR-038 depth layers, as (start, end, contrast, saturation); the count is params2.y.
    depthLayers: array<vec4<f32>, 6>,
};

@group(0) @binding(0) var<uniform> post: PostUniforms;
@group(0) @binding(1) var linearSampler: sampler;
@group(0) @binding(2) var source: texture_2d<f32>;
@group(0) @binding(3) var second: texture_2d<f32>;
@group(0) @binding(4) var depthTex: texture_depth_2d;
@group(0) @binding(5) var third: texture_2d<f32>;
@group(0) @binding(6) var emissionTex: texture_2d<f32>;
@group(0) @binding(7) var identifierTex: texture_2d<u32>;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) i: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: FsIn;
    let p = positions[i];
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 1.0 - (p.y * 0.5 + 0.5));
    return out;
}

fn luminance(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }

// ---- exposure (ADR-037): the first stage, so every threshold below is in exposed units --------

@fragment
fn fs_exposure(in: FsIn) -> @location(0) vec4<f32> {
    let scale = post.params0.x;
    return vec4<f32>(textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb * scale, 1.0);
}

// ---- automatic metering: a centre-weighted average of the pre-exposure image's luminance -------
// Each texel carries (sum of weight * luminance, sum of weight); the 1x1 result divides one by the
// other. ADR-037 allows an average or a histogram; the arithmetic average is the one that keeps a
// mostly-black frame with a blazing centre (Hyperspace's core) from being *brightened*, which is
// exactly what a log-average would do once the empty background dominates the pixel count.

@fragment
fn fs_meter_prefilter(in: FsIn) -> @location(0) vec4<f32> {
    let centerWeight = post.params0.x;
    let aspect = post.params0.y;
    let t = post.texelSize;
    var sum = vec3<f32>(0.0);
    for (var y = -1; y <= 1; y = y + 2) {
        for (var x = -1; x <= 1; x = x + 2) {
            sum += textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(f32(x), f32(y)) * t, 0.0).rgb;
        }
    }
    // Clamped well inside the half-float range so one runaway texel cannot make the sum infinite.
    let lum = clamp(luminance(sum * 0.25), 0.0, 4096.0);
    // Centre weighting: 1 in the middle, falling to (1 - centerWeight) at the edges.
    let d = length((in.uv - vec2<f32>(0.5)) * vec2<f32>(aspect, 1.0)) * 2.0;
    let w = max(1.0 - centerWeight * smoothstep(0.25, 1.15, d), 1e-3);
    return vec4<f32>(lum * w, w, 0.0, 1.0);
}

@fragment
fn fs_meter_reduce(in: FsIn) -> @location(0) vec4<f32> {
    // 4x4 box over the source, so a 1080p image reaches 1x1 in six passes.
    let t = post.texelSize;
    var sum = vec2<f32>(0.0);
    for (var y = 0; y < 4; y = y + 1) {
        for (var x = 0; x < 4; x = x + 1) {
            let o = (vec2<f32>(f32(x), f32(y)) - vec2<f32>(1.5)) * t;
            sum += textureSampleLevel(source, linearSampler, in.uv + o, 0.0).xy;
        }
    }
    return vec4<f32>(sum * (1.0 / 16.0), 0.0, 1.0);
}

// ---- bloom (Jimenez 2014 downsample, energy-conserving upsample) -------------------------------

// Soft-knee threshold on luminance. The weight is the fraction of the pixel's energy that passes,
// so with threshold 0 the prefilter is the identity and the pyramid carries the whole image's
// energy; that is what makes the bloom tier measurable rather than a look-dependent hard cut.
fn thresholdWeight(lum: f32, threshold: f32, kneeFraction: f32) -> f32 {
    let knee = max(threshold * kneeFraction, 1e-4);
    let soft = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    let softContribution = soft * soft / (4.0 * knee);
    let contribution = max(softContribution, lum - threshold);
    return clamp(contribution, 0.0, lum) / max(lum, 1e-5);
}

@fragment
fn fs_prefilter(in: FsIn) -> @location(0) vec4<f32> {
    // 4-tap box of the full-res exposed scene, weighted by luminance rather than evenly, then the
    // soft-knee threshold.
    //
    // The weighting (Karis) is what stops a sub-pixel highlight from becoming a light source. Water
    // sparkle is a dense field of near-pixel-sized speculars; each one entered the pyramid as a
    // firefly, and the flare stage then reads that pyramid *magnified* -- `mix(0.5, mirrored, 0.75)`
    // and `0.40`, so 1.33x and 2.5x -- and mirrored through the frame centre. A sparkling river
    // therefore printed a lattice of bright dots across unrelated parts of the image, at any ghost
    // strength, because the structure was already in the pyramid before the ghosts read it.
    //
    // Measured on the reported case: the lattice is visibly dimmer with this and unchanged
    // elsewhere. It does not remove it. The rest is the pyramid's own texel grid being magnified,
    // and integrating that away needs the *source's* texel size, which this pass is not given --
    // `post.texelSize` here is the output's. Tried with the output's and measured a 2% change for
    // eight extra samples, so it is not in.
    let t = post.texelSize;
    let s0 = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(-0.5, -0.5) * t, 0.0).rgb;
    let s1 = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(0.5, -0.5) * t, 0.0).rgb;
    let s2 = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(-0.5, 0.5) * t, 0.0).rgb;
    let s3 = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(0.5, 0.5) * t, 0.0).rgb;
    let w0 = 1.0 / (1.0 + luminance(s0));
    let w1 = 1.0 / (1.0 + luminance(s1));
    let w2 = 1.0 / (1.0 + luminance(s2));
    let w3 = 1.0 / (1.0 + luminance(s3));
    let c = (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / max(w0 + w1 + w2 + w3, 1e-4);
    let threshold = post.params0.x;
    let knee = post.params0.y;
    let emissionWeight = post.params0.z;
    let emissionAvailable = post.params0.w;
    var weight = thresholdWeight(luminance(c), threshold, knee);
    // Selective bloom (ADR-039): weight by the emission target when the renderer wrote one, so a
    // merely bright lit surface stops glowing like a light source.
    if (emissionAvailable > 0.5 && emissionWeight > 0.0) {
        // The emission target is pre-exposure and `c` is post-exposure, so the emission is brought
        // into the same space before the ratio is taken. Guarded because a zero here would mask
        // everything away rather than nothing.
        let exposureScale = max(post.params1.x, 1e-4);
        let e = textureSampleLevel(emissionTex, linearSampler, in.uv, 0.0).rgb * exposureScale;
        let mask = clamp(luminance(e) / max(luminance(c), 1e-4), 0.0, 1.0);
        weight *= mix(1.0, mask, emissionWeight);
    }
    return vec4<f32>(c * weight, 1.0);
}

// Halation (ADR-039): the same threshold, restricted to highlights that are already warm, fed into
// a pyramid that starts coarser so the halo is wider than bloom's.
@fragment
fn fs_halation_prefilter(in: FsIn) -> @location(0) vec4<f32> {
    let t = post.texelSize;
    var c = vec3<f32>(0.0);
    for (var y = -1; y <= 1; y = y + 2) {
        for (var x = -1; x <= 1; x = x + 2) {
            c += textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(f32(x), f32(y)) * t, 0.0).rgb;
        }
    }
    c *= 0.25;
    let threshold = post.params0.x;
    let knee = post.params0.y;
    let warmth = post.params0.z;
    let lum = luminance(c);
    var weight = thresholdWeight(lum, threshold, knee);
    // Warmth: how much redder than neutral this highlight already is.
    let warm = clamp((c.r - 0.5 * (c.g + c.b)) / max(lum, 1e-4), 0.0, 1.0);
    weight *= mix(1.0, warm, warmth);
    return vec4<f32>(c * weight, 1.0);
}

@fragment
fn fs_downsample(in: FsIn) -> @location(0) vec4<f32> {
    let t = post.texelSize;
    let a = textureSample(source, linearSampler, in.uv + vec2<f32>(-2.0, -2.0) * t).rgb;
    let b = textureSample(source, linearSampler, in.uv + vec2<f32>(0.0, -2.0) * t).rgb;
    let c = textureSample(source, linearSampler, in.uv + vec2<f32>(2.0, -2.0) * t).rgb;
    let d = textureSample(source, linearSampler, in.uv + vec2<f32>(-2.0, 0.0) * t).rgb;
    let e = textureSample(source, linearSampler, in.uv).rgb;
    let f = textureSample(source, linearSampler, in.uv + vec2<f32>(2.0, 0.0) * t).rgb;
    let g = textureSample(source, linearSampler, in.uv + vec2<f32>(-2.0, 2.0) * t).rgb;
    let h = textureSample(source, linearSampler, in.uv + vec2<f32>(0.0, 2.0) * t).rgb;
    let i = textureSample(source, linearSampler, in.uv + vec2<f32>(2.0, 2.0) * t).rgb;
    let j = textureSample(source, linearSampler, in.uv + vec2<f32>(-1.0, -1.0) * t).rgb;
    let k = textureSample(source, linearSampler, in.uv + vec2<f32>(1.0, -1.0) * t).rgb;
    let l = textureSample(source, linearSampler, in.uv + vec2<f32>(-1.0, 1.0) * t).rgb;
    let m = textureSample(source, linearSampler, in.uv + vec2<f32>(1.0, 1.0) * t).rgb;
    var sum = e * 0.125;
    sum += (a + c + g + i) * 0.03125;
    sum += (b + d + f + h) * 0.0625;
    sum += (j + k + l + m) * 0.125;
    return vec4<f32>(sum, 1.0);
}

// Energy-conserving upsample (ADR-039): the coarse level's 9-tap tent (weights summing to 1) is
// *blended* with this level rather than added to it, so the pyramid's mean equals the prefiltered
// image's mean whatever the level count. Adding, as the chain did before, multiplied a highlight's
// energy by the number of levels, which is why everything glowed.
@fragment
fn fs_upsample(in: FsIn) -> @location(0) vec4<f32> {
    let t = post.texelSize * post.params0.x; // spread
    let blend = clamp(post.params0.y, 0.0, 1.0);
    var sum = textureSample(source, linearSampler, in.uv + vec2<f32>(-1.0, -1.0) * t).rgb;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(0.0, -1.0) * t).rgb * 2.0;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(1.0, -1.0) * t).rgb;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(-1.0, 0.0) * t).rgb * 2.0;
    sum += textureSample(source, linearSampler, in.uv).rgb * 4.0;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(1.0, 0.0) * t).rgb * 2.0;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(-1.0, 1.0) * t).rgb;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(0.0, 1.0) * t).rgb * 2.0;
    sum += textureSample(source, linearSampler, in.uv + vec2<f32>(1.0, 1.0) * t).rgb;
    sum *= 1.0 / 16.0;
    let fine = textureSample(second, linearSampler, in.uv).rgb;
    return vec4<f32>(mix(fine, sum, blend), 1.0);
}

// ---- the wide tier: halation and anamorphic streaks, combined into one texture ------------------
// source = a coarse bloom level (the streak's input), second = the halation pyramid's result.
// tintA = the halation tint * intensity, tintB = the anamorphic tint * intensity.

@fragment
fn fs_wide(in: FsIn) -> @location(0) vec4<f32> {
    let stretch = post.params0.x;
    let ghosts = post.params0.y;
    let anamorphicOn = post.params0.z;
    let halationOn = post.params0.w;
    var out = vec3<f32>(0.0);
    if (halationOn > 0.5) {
        out += textureSampleLevel(second, linearSampler, in.uv, 0.0).rgb * post.tintA.rgb;
    }
    if (anamorphicOn > 0.5) {
        // A horizontal gaussian whose reach is `stretch` times this pass's texel size.
        //
        // The tap *count* is derived from the reach rather than fixed, and that is the whole of the
        // lattice bug. Eight taps a side spanning `texelSize.x * stretch * 8` puts them 4.6% of the
        // screen apart at stretch 10 -- hundreds of times the source texel. Each tap then point
        // samples the bloom texture and a compact bright feature is *copied* once per tap, so a
        // sparkling river printed a row of evenly spaced dots across the frame. The spacing of the
        // artefact was the spacing of the taps, which is why lowering `ghosts` never touched it:
        // this is the streak, and the ghosts were innocent.
        //
        // Consecutive taps have to overlap in the source for the gaussian to be a blur instead of a
        // comb, so the step is capped at one source texel and the count grows to keep the reach.
        // Capped at 48 a side: past that the streak is wider than any authored `stretch` reaches and
        // the cost stops being worth it, and the residual under-sampling is then at a spacing too
        // fine to read as a lattice.
        let srcTexel = 1.0 / f32(textureDimensions(source, 0).x);
        let reach = post.texelSize.x * stretch * 8.0;
        let taps = clamp(i32(ceil(reach / max(srcTexel, 1e-6))), 8, 48);
        let step = reach / f32(taps);
        let sigma = f32(taps) / 2.5;
        var streak = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb * 0.20;
        var weightSum = 0.20;
        for (var i = 1; i <= taps; i = i + 1) {
            let w = exp(-0.5 * pow(f32(i) / sigma, 2.0));
            let o = vec2<f32>(step * f32(i), 0.0);
            streak += textureSampleLevel(source, linearSampler, clamp(in.uv + o, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).rgb * w;
            streak += textureSampleLevel(source, linearSampler, clamp(in.uv - o, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).rgb * w;
            weightSum += 2.0 * w;
        }
        streak /= weightSum;
        if (ghosts > 0.0) {
            // Two flare ghosts mirrored through the frame centre, as an anamorphic lens gives.
            //
            // Each is a *magnified* read of a coarse bloom level -- 1/0.75 and 1/0.40, so 1.33x and
            // 2.5x. A single bilinear tap into a magnified coarse texture puts that texture's own
            // texel grid on the screen, and over a sparkling river that is a regular lattice of
            // bright dots printed across unrelated parts of the frame. Lowering `ghosts` only made
            // it dimmer, because the structure is in the source rather than in the strength.
            //
            // So integrate over the source texel instead of point-sampling it. The step is the
            // *source's* texel scaled by the magnification, which is why it is asked of the texture
            // rather than taken from `post.texelSize` -- that is the output's, and using it spreads
            // the taps by a fraction of a source texel and does nothing at all.
            let srcTexel = 1.0 / vec2<f32>(textureDimensions(source, 0));
            let mirrored = vec2<f32>(1.0) - in.uv;
            let uv1 = mix(vec2<f32>(0.5), mirrored, 0.75);
            let uv2 = mix(vec2<f32>(0.5), mirrored, 0.40);
            let step1 = srcTexel / 0.75;
            let step2 = srcTexel / 0.40;
            var g1 = vec3<f32>(0.0);
            var g2 = vec3<f32>(0.0);
            // A 3x3 of half-texel offsets: nine taps spanning one source texel in each direction,
            // which is the period of the lattice being removed. Averaging over the period is what
            // removes a lattice; more resolution is not available here and would not help.
            for (var y = -1; y <= 1; y = y + 1) {
                for (var x = -1; x <= 1; x = x + 1) {
                    let o = vec2<f32>(f32(x), f32(y));
                    g1 += textureSampleLevel(source, linearSampler, uv1 + o * step1, 0.0).rgb;
                    g2 += textureSampleLevel(source, linearSampler, uv2 + o * step2, 0.0).rgb;
                }
            }
            g1 *= 1.0 / 9.0;
            g2 *= 1.0 / 9.0;
            streak += (g1 * 0.6 + g2 * 0.35) * ghosts;
        }
        out += streak * post.tintB.rgb;
    }
    return vec4<f32>(out, 1.0);
}

// ---- lens: distortion and chromatic aberration --------------------------------------------------

fn distort(uv: vec2<f32>, amount: f32) -> vec2<f32> {
    let c = uv - vec2<f32>(0.5);
    let r2 = dot(c, c);
    return vec2<f32>(0.5) + c * (1.0 + amount * r2 * 2.0);
}

@fragment
fn fs_lens(in: FsIn) -> @location(0) vec4<f32> {
    let chromatic = post.params0.x;
    let distortion = post.params0.y;
    var uv = in.uv;
    if (abs(distortion) > 1e-4) {
        uv = clamp(distort(uv, distortion), vec2<f32>(0.0), vec2<f32>(1.0));
    }
    if (chromatic > 1e-4) {
        let dir = (uv - vec2<f32>(0.5)) * chromatic * 0.03;
        return vec4<f32>(textureSampleLevel(source, linearSampler, clamp(uv + dir, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).r,
                         textureSampleLevel(source, linearSampler, uv, 0.0).g,
                         textureSampleLevel(source, linearSampler, clamp(uv - dir, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).b,
                         1.0);
    }
    return vec4<f32>(textureSampleLevel(source, linearSampler, uv, 0.0).rgb, 1.0);
}

// ---- depth helpers -----------------------------------------------------------------------------

fn worldFromDepth(uv: vec2<f32>, depth: f32) -> vec3<f32> {
    let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    let w = post.invViewProj * ndc;
    return w.xyz / w.w;
}

fn sampleDepth(uv: vec2<f32>) -> f32 {
    let size = vec2<f32>(textureDimensions(depthTex));
    let coord = vec2<i32>(clamp(uv * size, vec2<f32>(0.0), size - vec2<f32>(1.0)));
    return textureLoad(depthTex, coord, 0);
}

fn viewDistance(uv: vec2<f32>) -> f32 {
    let d = sampleDepth(uv);
    if (d >= 1.0) { return 1e6; }
    return length(worldFromDepth(uv, d) - post.cameraPos.xyz);
}

// ---- depth layers: atmospheric perspective (ADR-038) ---------------------------------------------
// A layer is a distance band with its own contrast and saturation. `layerAt()` on the CPU answers
// the discrete question -- which band an *instance* is in -- but a per-pixel grade cannot switch
// at a band edge without drawing a line across the image, so the grade treats each layer's values
// as sitting at its band's midpoint and interpolates between midpoints, clamped at the ends. One
// layer therefore grades its whole range uniformly, which is what a single band should mean.
fn depthGrade(distance: f32, count: u32) -> vec2<f32> {
    if (count == 0u) {
        return vec2<f32>(1.0, 1.0);
    }
    var prevMid = 0.0;
    var prevValue = vec2<f32>(1.0, 1.0);
    for (var i = 0u; i < 6u; i = i + 1u) {
        if (i >= count) { break; }
        let layer = post.depthLayers[i];
        let mid = (layer.x + layer.y) * 0.5;
        let value = layer.zw;
        if (i == 0u && distance <= mid) {
            return value;
        }
        if (i > 0u && distance <= mid) {
            let span = max(mid - prevMid, 1e-4);
            return mix(prevValue, value, clamp((distance - prevMid) / span, 0.0, 1.0));
        }
        prevMid = mid;
        prevValue = value;
    }
    return prevValue;
}

// ---- composite: bloom and the wide tier mixed in, then colour grading ---------------------------

fn hueRotate(c: vec3<f32>, angle: f32) -> vec3<f32> {
    // YIQ rotation
    let yiq = mat3x3<f32>(vec3<f32>(0.299, 0.596, 0.211), vec3<f32>(0.587, -0.274, -0.523), vec3<f32>(0.114, -0.322, 0.312)) * c;
    let cs = cos(angle);
    let sn = sin(angle);
    let rot = vec3<f32>(yiq.x, yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return mat3x3<f32>(vec3<f32>(1.0, 1.0, 1.0), vec3<f32>(0.956, -0.272, -1.106), vec3<f32>(0.621, -0.647, 1.703)) * rot;
}

@fragment
fn fs_composite(in: FsIn) -> @location(0) vec4<f32> {
    let bloomIntensity = post.params0.x;
    let bloomOn = post.params0.y;
    let wideOn = post.params0.z;
    let contrast = post.params1.x;
    let saturation = post.params1.y;
    let temperature = post.params1.z;
    let tint = post.params1.w;
    let hueShift = post.params2.x;
    let layerCount = u32(post.params2.y + 0.5);

    var color = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb;
    if (bloomOn > 0.5) {
        color += textureSampleLevel(second, linearSampler, in.uv, 0.0).rgb * bloomIntensity;
    }
    if (wideOn > 0.5) {
        color += textureSampleLevel(third, linearSampler, in.uv, 0.0).rgb;
    }

    // White balance: warm shifts red up / blue down, tint shifts green.
    color *= vec3<f32>(1.0 + 0.15 * temperature, 1.0 - 0.1 * tint, 1.0 - 0.15 * temperature);
    if (abs(hueShift) > 1e-4) {
        color = hueRotate(color, hueShift);
    }
    // Contrast about mid grey in log space, saturation about luminance. The depth layers scale
    // both per pixel, so distance can flatten and desaturate on its own.
    let grade = depthGrade(viewDistance(in.uv), layerCount);
    let grey = 0.18;
    color = grey * pow(max(color, vec3<f32>(1e-5)) / grey, vec3<f32>(contrast * grade.x));
    let lum = luminance(color);
    color = mix(vec3<f32>(lum), color, saturation * grade.y);
    // Lift / gamma / gain.
    color = pow(max(color * post.gain.rgb + post.lift.rgb, vec3<f32>(0.0)), vec3<f32>(1.0) / max(post.gamma.rgb, vec3<f32>(1e-3)));
    return vec4<f32>(color, 1.0);
}

// ---- output: contrast-adaptive sharpening, optionally masked by the identifier target -----------

@fragment
fn fs_sharpen(in: FsIn) -> @location(0) vec4<f32> {
    let amount = post.params0.x;
    let maskId = u32(post.params0.y);
    let identifierAvailable = post.params0.z;
    let c = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb;
    var mask = 1.0;
    if (identifierAvailable > 0.5 && maskId != 0u) {
        let size = vec2<f32>(textureDimensions(identifierTex));
        let coord = vec2<i32>(clamp(in.uv * size, vec2<f32>(0.0), size - vec2<f32>(1.0)));
        mask = select(0.0, 1.0, textureLoad(identifierTex, coord, 0).r == maskId);
    }
    if (mask <= 0.0) {
        return vec4<f32>(c, 1.0);
    }
    let t = post.texelSize;
    let n = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(0.0, -t.y), 0.0).rgb;
    let s = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(0.0, t.y), 0.0).rgb;
    let w = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(-t.x, 0.0), 0.0).rgb;
    let e = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(t.x, 0.0), 0.0).rgb;
    let lo = min(c, min(min(n, s), min(w, e)));
    let hi = max(c, max(max(n, s), max(w, e)));
    let sharpened = c + (c * 4.0 - (n + s + w + e)) * amount * 0.25;
    return vec4<f32>(mix(c, clamp(sharpened, lo, hi), mask), 1.0);
}

// ---- defocus: one circle-of-confusion gather, two ways of deciding the circle -------------------
// Depth of field asks "how far is this point from the focus *distance*"; a tilt-shift lens, whose
// focal plane is swung away from parallel with the sensor, asks "how far is it from the in-focus
// *band* across the frame" (ADR-079). Only the circle-of-confusion function differs, so both drive
// the same gather: same taps, same reach test, same energy.
//
// params0 = (focus distance m, focus range m, max radius px, physical flag)
// params1 = (focal length mm, f-number, sensor height mm, image height px)
// params2 = (band centre x, band centre y, cos rotation, sin rotation)
// params3 = (band half-width, falloff, band max radius px, band flag)
// params4 = (depth-of-field flag, aspect, 0, 0)

fn depthCircleOfConfusion(dist: f32) -> f32 {
    let focus = post.params0.x;
    let maxRadius = post.params0.z;
    if (post.params0.w > 0.5) {
        // ADR-037: the lens's own circle of confusion, c = f^2 |d - s| / (N d (s - f)) millimetres
        // on the sensor, converted to pixels; `maxRadius` now only clamps it.
        let f = max(post.params1.x, 1e-3);
        let n = max(post.params1.y, 0.05);
        let s = max(focus * 1000.0, f * 1.0001 + 1e-3);
        let d = max(min(dist, 1e5) * 1000.0, 1e-3);
        let cMm = abs(f * f * (d - s) / (n * d * (s - f)));
        let pixelsPerMm = post.params1.w / max(post.params1.z, 1e-3);
        return min(cMm * pixelsPerMm * 0.5, maxRadius); // diameter -> gather radius
    }
    let range = post.params0.y;
    let offset = max(abs(dist - focus) - range, 0.0);
    return clamp(offset / max(focus, 1e-3), 0.0, 1.0) * maxRadius;
}

// The twin of scene::tiltShiftCoverage in src/scene/post_settings.cpp, which is where the unit
// tests pin this shape. Scaling x by the aspect ratio puts both axes in units of frame height, so
// the rotation is an angle on screen rather than in texture space -- measured in raw uv, a 45
// degree band on a 16:9 frame comes out at 28 degrees and changes width as it turns.
fn tiltShiftCoverage(uv: vec2<f32>) -> f32 {
    let p = (uv - post.params2.xy) * vec2<f32>(post.params4.y, 1.0);
    let normal = vec2<f32>(-post.params2.w, post.params2.z); // normal to the band's axis
    let d = abs(dot(p, normal));
    let t = clamp((d - post.params3.x) / max(post.params3.y, 1e-4), 0.0, 1.0);
    // Smoothstep, not a linear ramp: the band's edge is exactly where the eye looks for a seam and
    // a linear ramp creases there, because its slope jumps from zero to the full falloff.
    return t * t * (3.0 - 2.0 * t);
}

fn circleOfConfusion(uv: vec2<f32>) -> f32 {
    var coc = 0.0;
    if (post.params3.w > 0.5) {
        coc = tiltShiftCoverage(uv) * post.params3.z;
    }
    if (post.params4.x > 0.5) {
        // The larger circle wins rather than the sum: two ways of being out of focus are not two
        // defocus energies to add, and the wider blur is the only one you can see anyway. Reading
        // depth stays behind this flag so a tilt-shift with no depth of field pays for no samples.
        coc = max(coc, depthCircleOfConfusion(viewDistance(uv)));
    }
    return coc;
}

// 24 taps is what the depth path has always used and is kept exactly, so no existing render moves.
// A tilt-shift wants a much wider circle than a depth blur usually does -- a miniature fake is 20+
// pixels -- and 24 taps thrown over a 12 pixel disc is one sample per 12 square pixels, which does
// not read as defocus at all: it reads as noise, because neighbouring pixels average different
// samples. So when the band is in play the count follows the disc's *area*, at roughly one tap per
// two square pixels, which bilinear filtering then closes up. The ceiling is where the cost stops
// being worth it: 192 taps covers a 20 pixel radius, and a blur wider than that wants a downsampled
// pyramid rather than a bigger spiral (ADR-079).
fn defocusTaps(coc: f32) -> u32 {
    if (post.params3.w < 0.5) {
        return 24u;
    }
    return clamp(u32(coc * coc * 0.5), 24u, 192u);
}

@fragment
fn fs_dof(in: FsIn) -> @location(0) vec4<f32> {
    let coc = circleOfConfusion(in.uv);
    let centre = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb;
    if (coc < 0.5) {
        return vec4<f32>(centre, 1.0);
    }
    var sum = centre;
    var weight = 1.0;
    let taps = defocusTaps(coc);
    let golden = 2.39996323;
    // The spiral is deliberately *not* rotated per pixel. Interleaved gradient noise is the usual
    // answer to a sparse gather's rings, and it was tried here: it turns the rings into noise, and
    // on a broadband test pattern that noise was the largest artefact left in the blurred region
    // (the worst pixel-to-pixel step went from 2.6 to 3.1 out of an original 13.7, measured in
    // tests/rendering/test_tilt_shift_gpu.cpp). Unrotated, every pixel applies the same irregular
    // kernel, which is a filter rather than an estimator, and neighbours differ only by their
    // offset. With the tap count following the disc's area there is nothing left for a dither to
    // hide.
    for (var i = 1u; i <= taps; i = i + 1u) {
        let r = sqrt(f32(i) / f32(taps)) * coc;
        let a = f32(i) * golden;
        let offset = vec2<f32>(cos(a), sin(a)) * r * post.texelSize;
        let uv = in.uv + offset;
        let tapCoc = circleOfConfusion(uv);
        // Only taps whose own blur radius reaches this pixel contribute (avoids sharp halos).
        let w = clamp(tapCoc - r + 1.0, 0.0, 1.0);
        sum += textureSampleLevel(source, linearSampler, uv, 0.0).rgb * w;
        weight += w;
    }
    return vec4<f32>(sum / weight, 1.0);
}

// ---- motion blur: tile-based reconstruction over the velocity target (ADR-035/040) -----------
// Replaces the old depth-reprojection blur, which could only see *camera* motion. The velocity
// target already holds the screen motion of every shading path - camera, object, instance,
// deformation and particle - so reconstructing from it blurs all of them for the same cost.
//
// Three passes, following McGuire et al., "A Reconstruction Filter for Plausible Motion Blur"
// (2012):
//   fs_velocity_tile_max      one texel per k x k block: the longest velocity in the block, in
//                             pixels, already scaled by the shutter and clamped to the maximum
//                             radius (a moving object must be able to smear *outside* its own
//                             silhouette, which a per-pixel filter can never do).
//   fs_velocity_neighbour_max 3 x 3 maximum over those tiles, so a tile knows about the fast
//                             thing about to sweep into it.
//   fs_motion_blur            samples along the neighbourhood velocity, weighting each tap by a
//                             soft depth comparison (does the tap's surface blur *over* this
//                             pixel, or is it behind it?) and by whether the tap's own velocity
//                             reaches this pixel.
//
// Blur length is `velocity * shutterAngle / 360` (ADR-037), passed in as params0.x, so a 0 degree
// shutter produces no smear at all and 360 degrees smears a whole frame of travel.
//
// Determinism and temporal stability: the tap offset jitter is interleaved gradient noise of the
// *pixel coordinate only*. It never reads the frame index or the clock, so the same frame renders
// identically every time and a static image does not shimmer between frames.

// params0 = (blur scale, tile size px, max radius px, 0); params1 = (full width, full height, 0, 0)
@fragment
fn fs_velocity_tile_max(in: FsIn) -> @location(0) vec4<f32> {
    let tile = max(i32(post.params0.y), 1);
    let fullSize = vec2<i32>(post.params1.xy);
    let origin = vec2<i32>(floor(in.clip.xy)) * tile;
    var best = vec2<f32>(0.0);
    var bestLen = 0.0;
    for (var y = 0; y < tile; y = y + 1) {
        for (var x = 0; x < tile; x = x + 1) {
            let coord = min(origin + vec2<i32>(x, y), fullSize - vec2<i32>(1));
            let v = textureLoad(source, coord, 0).xy * post.params1.xy * post.params0.x;
            let l = length(v);
            if (l > bestLen) {
                bestLen = l;
                best = v;
            }
        }
    }
    if (bestLen > post.params0.z) {
        best *= post.params0.z / bestLen;
    }
    return vec4<f32>(best, 0.0, 0.0);
}

@fragment
fn fs_velocity_neighbour_max(in: FsIn) -> @location(0) vec4<f32> {
    let size = vec2<i32>(textureDimensions(source));
    let center = vec2<i32>(floor(in.clip.xy));
    var best = vec2<f32>(0.0);
    var bestLen = 0.0;
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let coord = clamp(center + vec2<i32>(x, y), vec2<i32>(0), size - vec2<i32>(1));
            let v = textureLoad(source, coord, 0).xy;
            let l = length(v);
            if (l > bestLen) {
                bestLen = l;
                best = v;
            }
        }
    }
    return vec4<f32>(best, 0.0, 0.0);
}

// 1 when `near` really is in front of `far`, fading to 0 as it falls behind. Both are view
// distances in metres, so the soft edge is scaled by the nearer of the two.
fn softDepthCompare(near: f32, far: f32) -> f32 {
    let soft = 0.05 * max(min(near, far), 1.0);
    return clamp(1.0 - (near - far) / soft, 0.0, 1.0);
}
fn blurCone(dist: f32, len: f32) -> f32 { return clamp(1.0 - dist / max(len, 1e-4), 0.0, 1.0); }
fn blurCylinder(dist: f32, len: f32) -> f32 {
    // smoothstep with equal edges is undefined in WGSL, and a still surface has len == 0 - which
    // is exactly the case that must return 0, or every static pixel inside a moving tile's
    // neighbourhood would average its surroundings and erode.
    let l = max(len, 1e-4);
    return 1.0 - smoothstep(0.95 * l, 1.05 * l, dist);
}
// Interleaved gradient noise (Jimenez 2014). A function of the pixel alone: stable over time.
fn blurJitter(pixel: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2<f32>(0.06711056, 0.00583715))));
}

// source = colour, second = the neighbourhood-max tiles, third = the velocity target,
// params0 = (blur scale, samples, max radius px, tile size px)
@fragment
fn fs_motion_blur(in: FsIn) -> @location(0) vec4<f32> {
    let center = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb;
    let tileSize = vec2<i32>(textureDimensions(second));
    let tileCoord = clamp(vec2<i32>(in.uv * vec2<f32>(tileSize)), vec2<i32>(0), tileSize - vec2<i32>(1));
    let neighbour = textureLoad(second, tileCoord, 0).xy; // pixels
    let neighbourLen = length(neighbour);
    // Half a pixel of motion is invisible; skipping it keeps still frames bit-identical to the
    // unblurred image and costs nothing where nothing moves.
    if (neighbourLen < 0.5) {
        return vec4<f32>(center, 1.0);
    }
    let fullSize = vec2<f32>(textureDimensions(source));
    let ownVel = textureSampleLevel(third, linearSampler, in.uv, 0.0).xy * fullSize * post.params0.x;
    var ownLen = length(ownVel);
    if (ownLen > post.params0.z) { ownLen = post.params0.z; }
    let centerDepth = viewDistance(in.uv);

    let samples = max(i32(post.params0.y), 2);
    let jitter = blurJitter(in.clip.xy) - 0.5;
    // The centre tap's weight: a pixel that is itself still keeps most of its own colour, a fast
    // one spreads its energy over the whole streak.
    var weight = 1.0 / max(ownLen, 0.5);
    var sum = center * weight;
    for (var i = 0; i < samples; i = i + 1) {
        // Symmetric taps about the pixel, jittered inside their own step so the sample pattern
        // does not band.
        let t = (f32(i) + 0.5 + jitter) / f32(samples) - 0.5;
        let offsetPx = neighbour * t;
        let uv = clamp(in.uv + offsetPx / fullSize, vec2<f32>(0.0), vec2<f32>(1.0));
        let dist = length(offsetPx);
        let tapDepth = viewDistance(uv);
        var tapLen = length(textureSampleLevel(third, linearSampler, uv, 0.0).xy * fullSize * post.params0.x);
        if (tapLen > post.params0.z) { tapLen = post.params0.z; }
        // The tap's surface is in front of this pixel and moving: it smears over us. This is the
        // case that lets a moving object blur *outside* its own silhouette.
        let foreground = softDepthCompare(tapDepth, centerDepth) * blurCone(dist, tapLen);
        // The tap is behind us and *we* are moving: we uncover it.
        let background = softDepthCompare(centerDepth, tapDepth) * blurCone(dist, ownLen);
        // Both moving at a similar rate: the usual blur of a coherent surface.
        let alongBoth = 2.0 * blurCylinder(dist, tapLen) * blurCylinder(dist, ownLen);
        let w = foreground + background + alongBoth;
        sum += textureSampleLevel(source, linearSampler, uv, 0.0).rgb * w;
        weight += w;
    }
    return vec4<f32>(sum / max(weight, 1e-4), 1.0);
}

// ---- edge antialiasing (ADR-059) ---------------------------------------------------------------
//
// This renderer has no MSAA -- `multisample.count` is 1 everywhere, because Apple's TBDR pays for
// it in tile memory -- and no TAA. Its worlds are full of alpha-tested foliage, which is the worst
// case for both: a leaf edge is a one-pixel feature, so any sub-pixel movement of the camera flips
// it fully on or off and the crown appears to crawl.
//
// That this is aliasing rather than popping is measured, not assumed. Two consecutive frames of
// Glowmere with the wind switched off, so the only thing moving is five centimetres of camera:
// 5.775% of pixels jumped by more than 24/255 at native resolution, and 3.711% when the same two
// frames were rendered at twice the linear resolution and box-filtered back down. Culling and LOD
// do not care how many samples you take; aliasing does. Better than a third of the churn is
// therefore aliasing, and supersampling the whole frame to remove it would cost four times the
// fill on a frame that is already a third fill-bound.
//
// So: Lottes's FXAA. It runs inside the HDR chain on a tone-mapped luminance proxy, because every
// threshold below is perceptual and unbounded scene radiance would either never reach them or
// always exceed them.

const kFxaaAbsolute: f32 = 0.0312;   // below this local contrast, leave the pixel alone
const kFxaaRelative: f32 = 0.125;    // ...or below this fraction of the neighbourhood's maximum
const kFxaaSearchSteps: i32 = 12;

// Reinhard, so the thresholds above are in display terms rather than in scene radiance. Monotone,
// so it cannot invert an edge, and two taps cheaper than an actual tone map.
fn fxaaLuma(c: vec3<f32>) -> f32 {
    let l = dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
    return l / (1.0 + l);
}

fn fxaaLumaAt(uv: vec2<f32>) -> f32 {
    return fxaaLuma(textureSampleLevel(source, linearSampler, uv, 0.0).rgb);
}

@fragment
fn fs_fxaa(in: FsIn) -> @location(0) vec4<f32> {
    let texel = post.texelSize;
    let uv = in.uv;
    let rgbM = textureSampleLevel(source, linearSampler, uv, 0.0).rgb;

    let lumaM = fxaaLuma(rgbM);
    let lumaN = fxaaLumaAt(uv + vec2<f32>(0.0, -texel.y));
    let lumaS = fxaaLumaAt(uv + vec2<f32>(0.0,  texel.y));
    let lumaW = fxaaLumaAt(uv + vec2<f32>(-texel.x, 0.0));
    let lumaE = fxaaLumaAt(uv + vec2<f32>( texel.x, 0.0));

    let lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    let lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    let range = lumaMax - lumaMin;
    // Flat enough to leave alone. The absolute floor is what keeps the filter out of the deep
    // shadows, which on a night landscape is most of the frame and is where a luminance-relative
    // test alone would happily smear sensor-grade noise.
    if (range < max(kFxaaAbsolute, lumaMax * kFxaaRelative)) {
        return vec4<f32>(rgbM, 1.0);
    }

    let lumaNW = fxaaLumaAt(uv + vec2<f32>(-texel.x, -texel.y));
    let lumaNE = fxaaLumaAt(uv + vec2<f32>( texel.x, -texel.y));
    let lumaSW = fxaaLumaAt(uv + vec2<f32>(-texel.x,  texel.y));
    let lumaSE = fxaaLumaAt(uv + vec2<f32>( texel.x,  texel.y));

    // Which way the edge runs, from the second differences across the 3x3. The centre row and
    // column are weighted double because they pass through the pixel being shaded.
    let edgeHorz = abs((lumaNW + lumaNE) - 2.0 * lumaN) +
                   abs((lumaW  + lumaE ) - 2.0 * lumaM) * 2.0 +
                   abs((lumaSW + lumaSE) - 2.0 * lumaS);
    let edgeVert = abs((lumaNW + lumaSW) - 2.0 * lumaW) +
                   abs((lumaN  + lumaS ) - 2.0 * lumaM) * 2.0 +
                   abs((lumaNE + lumaSE) - 2.0 * lumaE);
    let horizontal = edgeHorz >= edgeVert;

    // The two neighbours across the edge, and which side of it has the steeper gradient: the blend
    // is towards that side, which is where the geometric edge actually lies.
    var luma1 = select(lumaW, lumaN, horizontal);
    var luma2 = select(lumaE, lumaS, horizontal);
    let grad1 = luma1 - lumaM;
    let grad2 = luma2 - lumaM;
    let steeper1 = abs(grad1) >= abs(grad2);
    let gradScaled = 0.25 * max(abs(grad1), abs(grad2));

    var stepLength = select(texel.x, texel.y, horizontal);
    if (steeper1) { stepLength = -stepLength; } else { luma1 = luma2; }
    // Half a texel towards the edge, and the local average across it: the value the edge would have
    // had if the pixel had been sampled with any area at all.
    let lumaLocal = (luma1 + lumaM) * 0.5;

    var currentUv = uv;
    if (horizontal) { currentUv.y += stepLength * 0.5; } else { currentUv.x += stepLength * 0.5; }

    // Walk along the edge in both directions until its contrast dies out. How far it runs sets how
    // much of the neighbour to blend in: a long clean edge gets a strong blend, a two-pixel speckle
    // barely any, which is what stops the filter softening genuine detail.
    let offset = select(vec2<f32>(0.0, texel.y), vec2<f32>(texel.x, 0.0), horizontal);
    var uv1 = currentUv - offset;
    var uv2 = currentUv + offset;
    var lumaEnd1 = fxaaLumaAt(uv1) - lumaLocal;
    var lumaEnd2 = fxaaLumaAt(uv2) - lumaLocal;
    var done1 = abs(lumaEnd1) >= gradScaled;
    var done2 = abs(lumaEnd2) >= gradScaled;
    if (!done1) { uv1 -= offset; }
    if (!done2) { uv2 += offset; }

    for (var i = 2; i < kFxaaSearchSteps; i = i + 1) {
        if (done1 && done2) { break; }
        // The step grows once the search is past the first few texels, so a long edge is still
        // found inside a bounded number of taps.
        let scale = select(1.0, 2.0, i > 5);
        if (!done1) {
            lumaEnd1 = fxaaLumaAt(uv1) - lumaLocal;
            done1 = abs(lumaEnd1) >= gradScaled;
            if (!done1) { uv1 -= offset * scale; }
        }
        if (!done2) {
            lumaEnd2 = fxaaLumaAt(uv2) - lumaLocal;
            done2 = abs(lumaEnd2) >= gradScaled;
            if (!done2) { uv2 += offset * scale; }
        }
    }

    let distance1 = select(abs(uv.y - uv1.y), abs(uv.x - uv1.x), horizontal);
    let distance2 = select(abs(uv.y - uv2.y), abs(uv.x - uv2.x), horizontal);
    let nearer1 = distance1 < distance2;
    let distanceFinal = min(distance1, distance2);
    let edgeLength = distance1 + distance2;
    var pixelOffset = -distanceFinal / max(edgeLength, 1e-6) + 0.5;

    // If the end we are nearest to sits on the same side of the local average as this pixel, then
    // this pixel is not on the edge after all and blending it would be wrong.
    let lumaCentreSmaller = lumaM < lumaLocal;
    let correctVariation = ((select(lumaEnd2, lumaEnd1, nearer1) < 0.0) != lumaCentreSmaller);
    if (!correctVariation) { pixelOffset = 0.0; }

    // The sub-pixel term: a feature smaller than a pixel produces no long edge for the search to
    // find, but it does move the pixel away from its own neighbourhood's mean. This is what
    // actually catches thin foliage.
    let lumaAverage = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    let subPixel1 = clamp(abs(lumaAverage - lumaM) / max(range, 1e-6), 0.0, 1.0);
    let subPixel2 = (-2.0 * subPixel1 + 3.0) * subPixel1 * subPixel1; // smoothstep
    let subPixelOffset = subPixel2 * subPixel2 * post.params0.x;

    let finalOffset = max(pixelOffset, subPixelOffset);
    var finalUv = uv;
    if (horizontal) { finalUv.y += finalOffset * stepLength; } else { finalUv.x += finalOffset * stepLength; }
    return vec4<f32>(textureSampleLevel(source, linearSampler, finalUv, 0.0).rgb, 1.0);
}
