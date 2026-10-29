// ADR-1864: the analytic sky's cirrus layer -- thin streaks, no volume march. scene/sky.cpp's `skyCirrus` is the CPU
// twin, line for line (an integer hash, so the lattice agrees bit for bit). Packed as four vec4s (scene::packSkyCirrus):
//   c0 = (amount, coverage, softness, altitude m)
//   c1 = (lit colour rgb, length m)
//   c2 = (shade colour rgb, width m)
//   c3 = (cos direction, sin direction, sun scatter, seed)
//   c4 = (bands, band width m, the cube's share, the sky's intensity)   (ADR-1865)
// Included by environment.wgsl (the sky cube) and sky_background.wgsl (the visible sky), so both are one sky.

fn cirrusHash(x: i32, y: i32) -> f32 {
    var h = (bitcast<u32>(x) * 0x8da6b343u) ^ (bitcast<u32>(y) * 0xd8163841u);
    h = (h ^ (h >> 15u)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12u)) * 0x297a2d39u;
    h = h ^ (h >> 15u);
    return f32(h & 0x00ffffffu) / 16777216.0;
}

fn cirrusNoise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = p - i;
    let u = f * f * (3.0 - 2.0 * f);
    let xi = i32(i.x);
    let yi = i32(i.y);
    let a = cirrusHash(xi, yi);
    let b = cirrusHash(xi + 1, yi);
    let c = cirrusHash(xi, yi + 1);
    let e = cirrusHash(xi + 1, yi + 1);
    return (a + (b - a) * u.x) + ((c + (e - c) * u.x) - (a + (b - a) * u.x)) * u.y;
}

fn cirrusFbm(pIn: vec2<f32>) -> f32 {
    var p = pIn;
    var sum = 0.0;
    var amp = 0.5;
    var norm = 0.0;
    for (var o = 0; o < 4; o = o + 1) {
        sum = sum + amp * cirrusNoise(p);
        norm = norm + amp;
        p = vec2<f32>(p.x * 2.03 + 1.7, p.y * 2.01 + 9.2);
        amp = amp * 0.5;
    }
    return sum / norm;
}

fn cirrusSmooth(e0: f32, e1: f32, x: f32) -> f32 {
    let t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// x = cover (0..1, already times amount), yzw = the streak's colour, for unit direction `d`.
fn skyCirrus(d: vec3<f32>, sunDir: vec3<f32>, c0: vec4<f32>, c1: vec4<f32>, c2: vec4<f32>, c3: vec4<f32>,
             c4: vec4<f32>) -> vec4<f32> {
    if (!(c0.x > 0.0) || d.y <= 0.0) {
        return vec4<f32>(0.0);
    }
    let y = max(d.y, 0.015);
    let plane = d.xz * (c0.w / y);
    let axis = c3.xy;
    let q = vec2<f32>(dot(plane, axis) / c1.w, dot(plane, vec2<f32>(-axis.y, axis.x)) / c2.w);
    let seeded = q + vec2<f32>(17.3, 5.1) * c3.w;
    let warp = cirrusNoise(vec2<f32>(seeded.x * 0.5, seeded.y * 0.08)) - 0.5;
    let n = cirrusFbm(vec2<f32>(seeded.x, seeded.y + 3.0 * warp));
    let horizon = cirrusSmooth(0.0, 0.04, d.y);
    var cover = cirrusSmooth(c0.y, c0.y + c0.z, n) * c0.x * horizon;
    // ADR-1865: the bands (scene/sky.cpp has the twin)
    var brightness = 1.0;
    if (c4.x > 0.0) {
        let bq = vec2<f32>(dot(plane, axis) / (3.0 * c1.w) + 3.7 * c3.w,
                           dot(plane, vec2<f32>(-axis.y, axis.x)) / c4.y + 1.3 * c3.w);
        let b = 0.65 * cirrusNoise(bq) + 0.35 * cirrusNoise(bq * 2.3 + vec2<f32>(5.1, 2.9));
        cover = cover * (1.0 - c4.x * (1.0 - cirrusSmooth(0.42, 0.62, b)));
        brightness = 1.0 + c4.x * (0.6 * b - 0.3);
    }
    var flatD = vec2<f32>(1.0, 0.0);
    if (length(d.xz) > 1e-5) { flatD = normalize(d.xz); }
    var flatS = vec2<f32>(1.0, 0.0);
    if (length(sunDir.xz) > 1e-5) { flatS = normalize(sunDir.xz); }
    let toward = 0.5 + 0.5 * dot(flatD, flatS);
    let forward = pow(max(dot(d, sunDir), 0.0), 6.0);
    let color = (c2.rgb + (c1.rgb - c2.rgb) * toward + c1.rgb * (c3.z * forward)) * brightness;
    return vec4<f32>(cover, color);
}
