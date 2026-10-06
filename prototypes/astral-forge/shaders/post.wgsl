// THE ASTRAL FORGE -- combine (surface + flakes), bloom chain, filmic composite.
// The bloom chain is the GPU-world spike's (Karis-weighted first level, 13-tap down, tent up).

struct PostU { a: vec4f, b: vec4f, };  // a: exposure, bloom, vignette, grain; b: frame, contrast, saturation, flake opacity
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var<uniform> P: PostU;
@group(0) @binding(3) var bloomTex: texture_2d<f32>;
@group(0) @binding(4) var<storage, read> accum: array<u32>;

struct FOut { @builtin(position) clip: vec4f, @location(0) uv: vec2f, };
@vertex fn vs_full(@builtin(vertex_index) vi: u32) -> FOut {
    let xy = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
    var o: FOut;
    o.clip = vec4f(xy * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2f(xy.x, 1.0 - xy.y);
    return o;
}
fn hashp(x0: u32) -> u32 {
    var x = x0;
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}

// surface + flakes: the flakes' dark bodies occlude the surface behind them (average colour, Beer
// coverage), and their glints add.
@fragment fn fs_combine(i: FOut) -> @location(0) vec4f {
    let px = vec2i(i.clip.xy);
    let dim = vec2i(textureDimensions(src));
    let surf = textureLoad(src, px, 0).rgb;
    let idx = u32(px.x + px.y * dim.x) * 7u;
    let W = f32(accum[idx + 3u]) / 4096.0;
    var c = surf;
    if (W > 1e-5) {
        let base = vec3f(f32(accum[idx]), f32(accum[idx + 1u]), f32(accum[idx + 2u])) / 256.0 / W;
        let alpha = 1.0 - exp(-W * P.b.w);
        c = surf * (1.0 - alpha) + base * alpha;
    }
    c += vec3f(f32(accum[idx + 4u]), f32(accum[idx + 5u]), f32(accum[idx + 6u])) / 1024.0;
    return vec4f(c, 1.0);
}

fn tap(uv: vec2f) -> vec3f { return textureSampleLevel(src, samp, uv, 0.0).rgb; }
fn karis(c: vec3f) -> vec3f { return c / (1.0 + dot(c, vec3f(0.2126, 0.7152, 0.0722)) * 0.25); }
@fragment fn fs_down(i: FOut) -> @location(0) vec4f {
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let a = tap(uv + t * vec2f(-2, -2)); let b = tap(uv + t * vec2f(0, -2)); let c = tap(uv + t * vec2f(2, -2));
    let d = tap(uv + t * vec2f(-2, 0));  let e = tap(uv);                    let f = tap(uv + t * vec2f(2, 0));
    let g = tap(uv + t * vec2f(-2, 2));  let h = tap(uv + t * vec2f(0, 2));  let k = tap(uv + t * vec2f(2, 2));
    let j = tap(uv + t * vec2f(-1, -1)); let l = tap(uv + t * vec2f(1, -1));
    let m = tap(uv + t * vec2f(-1, 1));  let n = tap(uv + t * vec2f(1, 1));
    let o = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (j + l + m + n) * 0.125;
    return vec4f(min(o, vec3f(200.0)), 1.0);
}
@fragment fn fs_down_first(i: FOut) -> @location(0) vec4f {
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let a = karis(tap(uv + t * vec2f(-1, -1))); let b = karis(tap(uv + t * vec2f(1, -1)));
    let c = karis(tap(uv + t * vec2f(-1, 1)));  let d = karis(tap(uv + t * vec2f(1, 1)));
    var o = (a + b + c + d) * 0.25;
    o = o / max(1.0 - dot(o, vec3f(0.2126, 0.7152, 0.0722)) * 0.25, 0.05);
    return vec4f(o, 1.0);
}
@fragment fn fs_up(i: FOut) -> @location(0) vec4f {
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let s = (tap(uv + t * vec2f(-1, -1)) + tap(uv + t * vec2f(1, -1)) + tap(uv + t * vec2f(-1, 1)) + tap(uv + t * vec2f(1, 1)))
          + (tap(uv + t * vec2f(0, -1)) + tap(uv + t * vec2f(-1, 0)) + tap(uv + t * vec2f(1, 0)) + tap(uv + t * vec2f(0, 1))) * 2.0
          + tap(uv) * 4.0;
    return vec4f(s / 16.0, 1.0);
}
// AgX-like filmic curve (a smooth sigmoid in log space): keeps the metal's highlights from going to flat white.
fn filmic(x: vec3f) -> vec3f {
    let lx = clamp(log2(max(x, vec3f(1e-7))), vec3f(-12.47), vec3f(4.026));
    let n = (lx + 12.47) / 16.5;
    let n2 = n * n; let n4 = n2 * n2;
    return clamp(15.5 * n4 * n2 - 40.14 * n4 * n + 31.96 * n4 - 6.868 * n2 * n + 0.4298 * n2 + 0.1191 * n - 0.00232, vec3f(0.0), vec3f(1.0));
}
@fragment fn fs_composite(i: FOut) -> @location(0) vec4f {
    let hdr = textureSampleLevel(src, samp, i.uv, 0.0).rgb;
    let bl = textureSampleLevel(bloomTex, samp, i.uv, 0.0).rgb;
    var c = (hdr + bl * P.a.y) * P.a.x;
    let l = dot(c, vec3f(0.2126, 0.7152, 0.0722));
    c = mix(vec3f(l), c, P.b.z);
    c = filmic(c);              // display-encoded already (AgX-style)
    c = pow(c, vec3f(P.b.y));
    let q = i.uv - 0.5;
    c *= 1.0 - P.a.z * dot(q, q) * 1.6;
    let px = vec2u(i.clip.xy);
    let g = f32(hashp(px.x * 1973u + px.y * 9277u + u32(P.b.x) * 26699u) >> 8u) / 16777216.0 - 0.5;
    c += g * P.a.w;
    return vec4f(max(c, vec3f(0.0)), 1.0);
}
