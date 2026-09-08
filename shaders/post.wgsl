// Built-in post-processing chain (ADR-016). Every pass is a fullscreen triangle over `source`
// (and optionally `second`) with `depth` for depth-aware effects. Uniforms carry per-pass data.

struct PostUniforms {
    texelSize: vec2<f32>,    // 1 / source size
    outputSize: vec2<f32>,
    params0: vec4<f32>,
    params1: vec4<f32>,
    params2: vec4<f32>,
    params3: vec4<f32>,
    lift: vec4<f32>,
    gamma: vec4<f32>,
    gain: vec4<f32>,
    cameraPos: vec4<f32>,
    prevViewProj: mat4x4<f32>,
    invViewProj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> post: PostUniforms;
@group(0) @binding(1) var linearSampler: sampler;
@group(0) @binding(2) var source: texture_2d<f32>;
@group(0) @binding(3) var second: texture_2d<f32>;
@group(0) @binding(4) var depthTex: texture_depth_2d;

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

// ---- bloom (Jimenez 2014 style: soft threshold, 13-tap downsample, tent upsample) ----------

@fragment
fn fs_prefilter(in: FsIn) -> @location(0) vec4<f32> {
    // 4-tap box of the full-res scene, then soft knee threshold.
    let t = post.texelSize;
    var c = textureSample(source, linearSampler, in.uv + vec2<f32>(-0.5, -0.5) * t).rgb;
    c += textureSample(source, linearSampler, in.uv + vec2<f32>(0.5, -0.5) * t).rgb;
    c += textureSample(source, linearSampler, in.uv + vec2<f32>(-0.5, 0.5) * t).rgb;
    c += textureSample(source, linearSampler, in.uv + vec2<f32>(0.5, 0.5) * t).rgb;
    c *= 0.25;
    let threshold = post.params0.x;
    let knee = threshold * post.params0.y;
    let br = max(max(c.r, c.g), c.b);
    let soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    let softContribution = soft * soft / (4.0 * knee + 1e-5);
    let inKnee = soft > 0.0 && br <= threshold + knee;
    let contribution = max(select(0.0, softContribution, inKnee), br - threshold);
    let weight = max(contribution, 0.0) / max(br, 1e-5);
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

@fragment
fn fs_upsample(in: FsIn) -> @location(0) vec4<f32> {
    // 9-tap tent filter of the coarser level (source) added to this level (second).
    let t = post.texelSize * post.params0.x; // radius
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
    return vec4<f32>(sum + textureSample(second, linearSampler, in.uv).rgb, 1.0);
}

// ---- composite: lens (distortion, chromatic aberration), bloom mix, colour grading ---------

fn distort(uv: vec2<f32>, amount: f32) -> vec2<f32> {
    let c = uv - vec2<f32>(0.5);
    let r2 = dot(c, c);
    return vec2<f32>(0.5) + c * (1.0 + amount * r2 * 2.0);
}

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
    let chromatic = post.params0.y;
    let distortion = post.params0.z;
    let contrast = post.params1.x;
    let saturation = post.params1.y;
    let temperature = post.params1.z;
    let tint = post.params1.w;
    let hueShift = post.params2.x;
    let bloomOn = post.params2.y;

    var uv = in.uv;
    if (abs(distortion) > 1e-4) {
        uv = clamp(distort(uv, distortion), vec2<f32>(0.0), vec2<f32>(1.0));
    }
    var scene: vec3<f32>;
    if (chromatic > 1e-4) {
        let dir = (uv - vec2<f32>(0.5)) * chromatic * 0.03;
        scene = vec3<f32>(textureSampleLevel(source, linearSampler, uv + dir, 0.0).r,
                          textureSampleLevel(source, linearSampler, uv, 0.0).g,
                          textureSampleLevel(source, linearSampler, uv - dir, 0.0).b);
    } else {
        scene = textureSampleLevel(source, linearSampler, uv, 0.0).rgb;
    }
    var color = scene;
    if (bloomOn > 0.5) {
        color += textureSampleLevel(second, linearSampler, uv, 0.0).rgb * bloomIntensity;
    }

    // White balance: warm shifts red up / blue down, tint shifts green.
    color *= vec3<f32>(1.0 + 0.15 * temperature, 1.0 - 0.1 * tint, 1.0 - 0.15 * temperature);
    if (abs(hueShift) > 1e-4) {
        color = hueRotate(color, hueShift);
    }
    // Contrast about mid grey in log space, saturation about luminance.
    let grey = 0.18;
    color = grey * pow(max(color, vec3<f32>(1e-5)) / grey, vec3<f32>(contrast));
    let lum = luminance(color);
    color = mix(vec3<f32>(lum), color, saturation);
    // Lift / gamma / gain.
    color = pow(max(color * post.gain.rgb + post.lift.rgb, vec3<f32>(0.0)), vec3<f32>(1.0) / max(post.gamma.rgb, vec3<f32>(1e-3)));
    return vec4<f32>(color, 1.0);
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

// ---- depth of field: circle-of-confusion gather ----------------------------------------------

fn circleOfConfusion(dist: f32) -> f32 {
    let focus = post.params0.x;
    let range = post.params0.y;
    let maxRadius = post.params0.z;
    let offset = max(abs(dist - focus) - range, 0.0);
    return clamp(offset / max(focus, 1e-3), 0.0, 1.0) * maxRadius;
}

@fragment
fn fs_dof(in: FsIn) -> @location(0) vec4<f32> {
    let centreDist = viewDistance(in.uv);
    let coc = circleOfConfusion(centreDist);
    let centre = textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb;
    if (coc < 0.5) {
        return vec4<f32>(centre, 1.0);
    }
    var sum = centre;
    var weight = 1.0;
    let taps = 24u;
    let golden = 2.39996323;
    for (var i = 1u; i <= taps; i = i + 1u) {
        let r = sqrt(f32(i) / f32(taps)) * coc;
        let a = f32(i) * golden;
        let offset = vec2<f32>(cos(a), sin(a)) * r * post.texelSize;
        let uv = in.uv + offset;
        let tapCoc = circleOfConfusion(viewDistance(uv));
        // Only taps whose own blur radius reaches this pixel contribute (avoids sharp halos).
        let w = clamp(tapCoc - r + 1.0, 0.0, 1.0);
        sum += textureSampleLevel(source, linearSampler, uv, 0.0).rgb * w;
        weight += w;
    }
    return vec4<f32>(sum / weight, 1.0);
}

// ---- motion blur from depth reprojection (camera motion) ---------------------------------------

fn velocityAt(uv: vec2<f32>, amount: f32) -> vec2<f32> {
    let d = sampleDepth(uv);
    if (d >= 1.0) { return vec2<f32>(0.0); }
    let world = worldFromDepth(uv, d);
    let prevClip = post.prevViewProj * vec4<f32>(world, 1.0);
    let prevNdc = prevClip.xy / max(prevClip.w, 1e-5);
    let prevUv = vec2<f32>(prevNdc.x * 0.5 + 0.5, 1.0 - (prevNdc.y * 0.5 + 0.5));
    var v = (uv - prevUv) * amount;
    let maxLen = 0.06;
    let len = length(v);
    if (len > maxLen) { v *= maxLen / len; }
    return v;
}

@fragment
fn fs_motion_blur(in: FsIn) -> @location(0) vec4<f32> {
    let amount = post.params0.x;
    let samples = max(i32(post.params0.y), 2);
    // Neighbourhood max (a cheap stand-in for McGuire's tile max): lets moving objects smear over
    // their static surroundings instead of only inside their own silhouette.
    var best = velocityAt(in.uv, amount);
    let r = 0.03;
    var offsets = array<vec2<f32>, 8>(vec2<f32>(r, 0.0), vec2<f32>(-r, 0.0), vec2<f32>(0.0, r), vec2<f32>(0.0, -r),
                                      vec2<f32>(r, r), vec2<f32>(-r, r), vec2<f32>(r, -r), vec2<f32>(-r, -r));
    for (var i = 0; i < 8; i = i + 1) {
        let nv = velocityAt(clamp(in.uv + offsets[i], vec2<f32>(0.0), vec2<f32>(1.0)), amount);
        if (dot(nv, nv) > dot(best, best)) { best = nv; }
    }
    if (dot(best, best) < 1e-10) {
        return vec4<f32>(textureSampleLevel(source, linearSampler, in.uv, 0.0).rgb, 1.0);
    }
    var sum = vec3<f32>(0.0);
    for (var i = 0; i < samples; i = i + 1) {
        let t = (f32(i) / f32(samples - 1)) - 0.5;
        sum += textureSampleLevel(source, linearSampler, clamp(in.uv + best * t, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).rgb;
    }
    return vec4<f32>(sum / f32(samples), 1.0);
}
