// THE ASTRAL FORGE -- the matter, seen. Every particle is an oriented metal flake splatted in compute
// (Schutz-style software rasterisation, adapted to WebGPU's u32 atomics). A flake is DARK unless its normal
// reflects a light band toward the eye: most of the dust is invisible at any instant and it glitters. That is
// the defence against "glowing dots". Bound flakes take the latent normal, so a formed patch flashes as one
// plate; free flakes tumble.
//
// Accumulation per pixel (u32 fixed point, order-independent and so deterministic):
//   [0..2] dark base colour x coverage, [3] coverage, [4..6] glint + heat radiance x coverage.

@group(1) @binding(0) var<storage, read> P: array<vec4f>;
@group(1) @binding(1) var<storage, read> V: array<vec4f>;
@group(1) @binding(2) var<storage, read> A: array<vec4f>;
@group(1) @binding(3) var<storage, read_write> accum: array<atomic<u32>>;
@group(1) @binding(4) var surfDepth: texture_2d<f32>;

const SW: f32 = 4096.0;  // coverage scale
const SG: f32 = 1024.0;  // glint scale (HDR)

fn roleOfF(i: u32) -> i32 {
    let r = u01(hashu(i * 0x9e3779b9u + 0x632be5abu));
    if (r < 0.10) { return 1; }
    if (r < 0.18) { return 2; }
    if (r < 0.64) { return 3; }
    if (r < 0.91) { return 4; }
    return 5;
}

fn bandBasisF(a: vec3f) -> mat2x3f {
    let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(a.y) > 0.9);
    let e1 = normalize(cross(up, a));
    return mat2x3f(e1, cross(a, e1));
}
fn envF(dir: vec3f, alpha: f32) -> vec3f {
    var sum = vec3f(0.0);
    for (var k = 0; k < 4; k++) {
        let A0 = F.bands[2 * k];
        let A1 = F.bands[2 * k + 1];
        if (A1.y <= 0.0) { continue; }
        let x = dot(dir, A0.xyz) - A0.w;
        let ww = A1.x * A1.x + alpha * alpha;
        var g = exp(-x * x / (2.0 * ww)) * A1.x / sqrt(ww);
        if (A1.z > 0.5) {
            let bb = bandBasisF(A0.xyz);
            let ph = safeAtan2(dot(dir, bb[1]), dot(dir, bb[0]));
            let s = fract(ph * A1.z / TAU + F.rig.x * (0.07 + 0.05 * f32(k)));
            g *= smoothstep(0.0, 0.04 + alpha, s) * smoothstep(0.0, 0.04 + alpha, 0.72 - s);
        }
        sum += A1.y * g * mix(vec3f(0.80, 0.90, 1.08), vec3f(1.08, 0.93, 0.78), A1.w);
    }
    return sum * (1.0 + F.rig.w) + vec3f(F.misc.z * 1.5);
}

fn toScreen(p: vec3f) -> vec3f {
    let c = F.viewProj * vec4f(p, 1.0);
    if (c.w < 0.05) { return vec3f(-1e5); }
    let ndc = c.xy / c.w;
    return vec3f((ndc.x * 0.5 + 0.5) * F.screen.x, (0.5 - ndc.y * 0.5) * F.screen.y, c.w);
}

// `base` is the flake's reflected radiance: pixels AVERAGE it over coverage (flakes occlude each other).
// `glint` is emission (heat, hot cores): it adds.
fn put(px: vec2i, w: f32, base: vec3f, glint: vec3f) {
    if (px.x < 0 || px.y < 0 || px.x >= i32(F.screen.x) || px.y >= i32(F.screen.y) || w <= 1e-4) { return; }
    let idx = u32(px.x + px.y * i32(F.screen.x)) * 7u;
    let bc = min(base, vec3f(60.0));
    atomicAdd(&accum[idx + 0u], u32(bc.r * w * 256.0));
    atomicAdd(&accum[idx + 1u], u32(bc.g * w * 256.0));
    atomicAdd(&accum[idx + 2u], u32(bc.b * w * 256.0));
    atomicAdd(&accum[idx + 3u], u32(w * SW));
    let gl = min(glint * w, vec3f(4000.0));
    if (gl.r + gl.g + gl.b > 1e-3) {
        atomicAdd(&accum[idx + 4u], u32(gl.r * SG));
        atomicAdd(&accum[idx + 5u], u32(gl.g * SG));
        atomicAdd(&accum[idx + 6u], u32(gl.b * SG));
    }
}

fn splatPoint(c: vec2f, rpx: f32, w: f32, base: vec3f, glint: vec3f) {
    if (rpx < 1.2) {
        // sub-pixel: bilinear over 4 pixels, coverage = projected area
        let cov = min(1.0, PI * rpx * rpx) * w;
        let f = c - 0.5;
        let i0 = vec2i(floor(f));
        let fr = f - vec2f(i0);
        put(i0, (1.0 - fr.x) * (1.0 - fr.y) * cov, base, glint);
        put(i0 + vec2i(1, 0), fr.x * (1.0 - fr.y) * cov, base, glint);
        put(i0 + vec2i(0, 1), (1.0 - fr.x) * fr.y * cov, base, glint);
        put(i0 + vec2i(1, 1), fr.x * fr.y * cov, base, glint);
        return;
    }
    let R = min(rpx, 6.0);
    let ri = i32(ceil(R));
    let ci = vec2i(floor(c));
    for (var y = -ri; y <= ri; y++) {
        for (var x = -ri; x <= ri; x++) {
            let pc = vec2f(ci + vec2i(x, y)) + 0.5 - c;
            let d2 = dot(pc, pc) / (R * R);
            if (d2 > 1.0) { continue; }
            put(ci + vec2i(x, y), (1.0 - d2 * d2) * w, base, glint);
        }
    }
}

@compute @workgroup_size(256)
fn cs_flakes(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nwg: vec3u) {
    let i = gid.x + gid.y * nwg.x * 256u;
    if (i >= u32(F.grid1.w) || i32(F.flags.x) == 2) { return; }
    let p4 = P[i];
    let p = p4.xyz;
    let s0 = toScreen(p);
    if (s0.z < 0.05 || s0.x < -8.0 || s0.y < -8.0 || s0.x > F.screen.x + 8.0 || s0.y > F.screen.y + 8.0) { return; }
    let dist = length(p - F.cam.xyz);
    let pix = clamp(vec2i(s0.xy), vec2i(0), vec2i(i32(F.screen.x) - 1, i32(F.screen.y) - 1));
    let sd = textureLoad(surfDepth, pix, 0).r;
    if (dist > sd + 0.015 * dist + 0.03) { return; } // behind the surface

    let role = roleOfF(i);
    let a4 = A[i];
    let v4 = V[i];
    let b = a4.w;
    let heat = v4.w;
    let h = hashu(i * 0x165667b1u + 0x27d4eb2fu);
    let hz = u01(h);
    // world size: plates when bound, dust when free; drifters are the finest
    var rw = F.look.w * (0.55 + 0.9 * hz) * (0.8 + 0.4 * b);
    if (role == 5) { rw *= 0.45; }
    let rpx = rw / max(dist * F.camFwd.w, 1e-6);

    // orientation
    let jit = vec3f(u01(h >> 3u), u01(h >> 7u), u01(h >> 11u)) - 0.5;
    let tumbleRate = 0.4 + 4.0 * F.ent2.w;
    let ang = F.cam.w * tumbleRate * (0.5 + hz) + hz * 40.0;
    let free_ = normalize(vec3f(sin(ang + jit.x * 9.0), cos(ang * 0.7 + jit.y * 7.0), sin(ang * 1.3 + jit.z * 5.0)) + jit);
    var nf = normalize(mix(free_, normalize(a4.xyz + jit * 0.25), smoothstep(0.2, 0.8, b)));
    let Vd = normalize(F.cam.xyz - p);
    if (dot(nf, Vd) < 0.0) { nf = -nf; }
    let r = reflect(-Vd, nf);
    let cosV = clamp(dot(nf, Vd), 0.0, 1.0);
    // cold dust (unbound) is untempered steel; bound matter carries the entity's temper
    var film = thinFilm(cosV, F.ent0.w * b * (300.0 + 150.0 * hz) + 160.0 * min(heat, 1.0));
    film = mix(vec3f(dot(film, vec3f(0.2126, 0.7152, 0.0722))), film, 0.6);
    let F0 = vec3f(0.5, 0.51, 0.54) * film;
    let Fr = F0 + (vec3f(0.95) - F0) * pow(1.0 - cosV, 5.0);
    // the flake's reflected radiance: dark unless its normal finds a band (a glint)
    let base = envF(r, 0.012) * Fr * select(0.35 + 0.5 * b, 0.15, role == 5);
    // heat shows as sparks: only a third of the matter carries it visibly, so a collapse is a spray, not a fireball
    var glint = heatColor(heat) * select(0.0, 1.1, hz < 0.14);
    // a few hot cores: the field's nervous system
    if (hz > 0.992) { glint += vec3f(0.85, 0.92, 1.0) * (0.4 + 2.0 * F.audio0.w + 1.5 * F.audio1.w) * (0.3 + 0.7 * b); }

    // motion streak: sample along the screen-space path covered during the shutter
    var s1 = toScreen(p - v4.xyz * (1.0 / 60.0) * 0.9);
    var n = 1;
    if (s1.z > 0.05) {
        // cap the streak: a particle grazing the lens must not draw a line across the frame
        let d = s1.xy - s0.xy;
        let L = length(d);
        if (L > 60.0) { s1 = vec3f(s0.xy + d * (60.0 / L), s1.z); }
        n = clamp(i32(ceil(min(L, 60.0) / 1.2)), 1, 14);
    }
    // as the form sharpens, bound matter FUSES into the surface: its flakes thin out to a residual sparkle
    let fuse = 1.0 - 0.97 * F.ent0.z * smoothstep(0.6, 1.0, b);
    let w = fuse / f32(n);
    for (var k = 0; k < n; k++) {
        let c = mix(s0.xy, s1.xy, (f32(k) + 0.5) / f32(n));
        splatPoint(c, select(rpx, min(rpx, 1.1), n > 1), w, base, glint);
    }
}
