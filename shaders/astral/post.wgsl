// THE ASTRAL FORGE -- combine (surface + flakes), bloom chain, filmic composite.
// The bloom chain is the GPU-world spike's (Karis-weighted first level, 13-tap down, tent up).

struct PostU { a: vec4f, b: vec4f, c: vec4f, d: vec4f, e: vec4f, f: vec4f, };
// a: exposure, bloom, vignette, grain; b: frame, contrast, saturation, flake opacity
// v2 atmosphere: c: light uv xy, light in front of the camera, god-ray strength; d: atmosphere rgb, strength;
// e: ray colour rgb, source radius (uv)
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var<uniform> P: PostU;
@group(0) @binding(3) var bloomTex: texture_2d<f32>;
@group(0) @binding(4) var<storage, read> accum: array<u32>;
@group(0) @binding(5) var surfDepth: texture_2d<f32>;

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
fn combineAt(px: vec2i) -> vec4f {
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
    // v2 ATMOSPHERE. The void gets depth: a palette-tinted ambient glow around an off-screen source behind/above the
    // god, faint vertical depth falloff, and GOD RAYS -- light from that source scattered by the haze, blocked by the
    // god's surface and by dense matter, so shafts cut through the gaps in forming matter (screen-space light
    // scattering, Mitchell, GPU Gems 3 ch. 13). No ground, no horizon, nothing architectural.
    if (P.d.w > 0.0 || P.c.w > 0.0) {
        let uv = (vec2f(px) + 0.5) / vec2f(dim);
        let aspect = f32(dim.x) / f32(dim.y);
        let lv = P.c.xy;
        let dl = (uv - lv) * vec2f(aspect, 1.0);
        let occHere = select(0.0, 1.0, textureLoad(surfDepth, px, 0).r < 1e8);
        let glow = exp(-dot(dl, dl) * 1.6) * 0.55 + 0.06 * smoothstep(1.2, -0.2, uv.y);
        c += P.d.rgb * glow * P.d.w * 0.11 * (1.0 - 0.85 * occHere); // the void stays near-black: a faint tint
        // ADR-1222: f.x = the tier's tap count (0 = 20, the prototype's)
        let n = select(20, i32(P.f.x), P.f.x > 0.5);
        if (P.c.w > 0.0 && P.c.z > 0.5 && n > 0) {
            // v2 perf: 20 jittered taps (was 48; the same Riemann sum, decay per tap rescaled)
            let stepv = (lv - uv) / f32(n);
            var q = uv;
            var acc = 0.0;
            var wsum = 0.0;
            var w = 1.0;
            let jit = fract(sin(dot(vec2f(px), vec2f(12.9898, 78.233))) * 43758.5453);
            q += stepv * jit;
            for (var k = 0; k < n; k++) {
                q += stepv;
                let qp = vec2i(clamp(q, vec2f(0.0), vec2f(0.999)) * vec2f(dim));
                var occ = select(0.0, 1.0, textureLoad(surfDepth, qp, 0).r < 1e8);
                let qi = u32(qp.x + qp.y * dim.x) * 7u;
                occ = max(occ, clamp(f32(accum[qi + 3u]) / 4096.0 * 0.6, 0.0, 1.0));
                let ds = (q - lv) * vec2f(aspect, 1.0);
                let src = exp(-dot(ds, ds) / (P.e.w * P.e.w));
                acc += w * src * (1.0 - occ);
                wsum += w;
                w *= pow(0.975, 48.0 / f32(n)); // 0.975^(48/20) at 20 taps
            }
            let rays = acc / max(wsum, 1e-3);
            c += P.e.rgb * rays * P.c.w * 0.13 * (1.0 - 0.7 * occHere);
        }
    }
    return vec4f(c, 1.0);
}
@fragment fn fs_combine(i: FOut) -> @location(0) vec4f { return combineAt(vec2i(i.clip.xy)); }

// ADR-1221: production composites straight into the scene's HDR target (already exposed: AV Gen's own bloom and
// tonemap follow) and depth buffer: the surface writes its own depth, the void writes the far plane (so it shows
// only where nothing of the scene stands in front).
struct SceneOut { @location(0) color: vec4f, @builtin(frag_depth) depth: f32, };
@fragment fn fs_combine_scene(i: FOut) -> SceneOut {
    let px = vec2i(i.clip.xy);
    var o: SceneOut;
    o.color = vec4f(combineAt(px).rgb * P.a.x, 1.0);
    let d = textureLoad(surfDepth, px, 0);
    o.depth = select(1.0, d.g, d.r < 1e8);
    return o;
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
