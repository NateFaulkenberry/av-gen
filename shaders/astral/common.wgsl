// THE ASTRAL FORGE -- shared frame block, hashing, noise, spectral helpers.
// The layout is mirrored by `FrameU` in astral_forge.cpp (vec4s and mat4s only).

struct Frame {
    viewProj: mat4x4f,
    invViewProj: mat4x4f,
    cam: vec4f,      // eye xyz, time
    camFwd: vec4f,   // forward xyz, pixel angle (2 tan(fov/2) / height)
    screen: vec4f,   // w, h, 1/w, 1/h
    ent0: vec4f,     // coherence, coherence at the previous step, sharpness, temper
    ent1: vec4f,     // archetype A, archetype B, morph, breath
    ent2: vec4f,     // mass, face flash, flow, shimmer
    ent3: vec4f,     // blast speed, heat inject, chaos amplitude, smooth-union k
    fold0: vec4f,    // twist, eye depth, mouth tunnel, inversion
    fold1: vec4f,    // bend, fold phase, choir sync, choir merge
    grid0: vec4f,    // origin xyz, cell size
    grid1: vec4f,    // res, 1/res, iso threshold, particle count
    sim: vec4f,      // dt, step index, seed, approach (0..4 = A..E)
    bands: array<vec4f, 8>, // band k: [2k] axis xyz + offset, [2k+1] width, intensity, segments, warmth
    rig: vec4f,      // band phase, sweep position, sweep strength, flicker
    audio0: vec4f,   // bass, lowMid, mid, high
    audio1: vec4f,   // air, rms, kickEnv, snareEnv
    look: vec4f,     // exposure, bloom, haze, flake size
    flags: vec4f,    // debug view, density weight, density norm, grating spacing (um)
    entity: vec4f,   // centre xyz, scale
    misc: vec4f,     // fall (abyss sink), escape rate, strobe, appendage density weight
    fp0: vec4f,      // the PREVIOUS step's fold0 (for advecting matter through the warp)
    fp1: vec4f,      // the previous step's fold1
    fp2: vec4f,      // previous step: time, breath, warp-advection gain; tendon flow speed
    ext: vec4f,      // sharpness spread (1 = only the anatomy's centre sharpens, 0 = everywhere), tendon density weight, meta-field radius, meta-face strength
    it2: vec4f,      // iteration 2: latent cache on, shards on, shard threshold (px), -
    cbox: vec4f,     // iteration 2: the latent cache's box (origin xyz, edge length), framed on the shot
    it3: vec4f,      // iteration 3: half-resolution march on, seconds since the last collapse (-1 none), -, -
    pal0: vec4f,     // v2: key-light tint (unit luminance), palette strength
    pal1: vec4f,     // rim tint, rim strength
    pal2: vec4f,     // eye tint, eye glow
    pal3: vec4f,     // zone A (features) tint, raking key strength
    pal4: vec4f,     // zone B (periphery) tint, dust absorption (0..1)
    atm0: vec4f,     // atmosphere colour, strength
    lg0: vec4f,      // v2 legibility at a formed peak (0..1), -, -, -
};

@group(0) @binding(0) var<uniform> F: Frame;

const PI: f32 = 3.14159265;
const TAU: f32 = 6.2831853;

fn hashu(x0: u32) -> u32 {
    var x = x0;
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
fn u01(h: u32) -> f32 { return f32(h >> 8u) / 16777216.0; }
fn hash3i(c: vec3i, salt: u32) -> u32 {
    return hashu((bitcast<u32>(c.x) * 0x8da6b343u) ^ hashu((bitcast<u32>(c.y) * 0xd8163841u) ^ hashu((bitcast<u32>(c.z) * 0xcb1ab31fu) ^ salt)));
}
fn hash31(c: vec3i, salt: u32) -> f32 { return u01(hash3i(c, salt)); }

// value noise, 3-D
fn vnoise3(p: vec3f, salt: u32) -> f32 {
    let i = vec3i(floor(p));
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = mix(mix(hash31(i, salt), hash31(i + vec3i(1, 0, 0), salt), u.x),
                mix(hash31(i + vec3i(0, 1, 0), salt), hash31(i + vec3i(1, 1, 0), salt), u.x), u.y);
    let b = mix(mix(hash31(i + vec3i(0, 0, 1), salt), hash31(i + vec3i(1, 0, 1), salt), u.x),
                mix(hash31(i + vec3i(0, 1, 1), salt), hash31(i + vec3i(1, 1, 1), salt), u.x), u.y);
    return mix(a, b, u.z);
}

// A cheap vector potential and its curl (finite differences of three noise channels): divergence-free flow.
fn potential(p: vec3f) -> vec3f {
    return vec3f(vnoise3(p, 11u) + 0.5 * vnoise3(p * 2.03, 17u),
                 vnoise3(p + vec3f(31.4, 7.1, 2.9), 23u) + 0.5 * vnoise3(p * 2.03 + 3.3, 29u),
                 vnoise3(p + vec3f(5.7, 41.2, 13.3), 37u) + 0.5 * vnoise3(p * 2.03 + 7.7, 41u));
}
fn curl(p: vec3f) -> vec3f {
    let e = 0.08;
    let dx = vec3f(e, 0.0, 0.0); let dy = vec3f(0.0, e, 0.0); let dz = vec3f(0.0, 0.0, e);
    let px0 = potential(p - dx); let px1 = potential(p + dx);
    let py0 = potential(p - dy); let py1 = potential(p + dy);
    let pz0 = potential(p - dz); let pz1 = potential(p + dz);
    return vec3f((py1.z - py0.z) - (pz1.y - pz0.y),
                 (pz1.x - pz0.x) - (px1.z - px0.z),
                 (px1.y - px0.y) - (py1.x - py0.x)) / (2.0 * e);
}

fn rot2(a: f32) -> mat2x2f { let c = cos(a); let s = sin(a); return mat2x2f(c, s, -s, c); }
// atan2(0,0) is NaN on Metal (the spike found it the hard way): guard every use.
fn safeAtan2(y: f32, x: f32) -> f32 { return select(atan2(y, x), 0.0, abs(x) + abs(y) < 1e-12); }

// ---- spectral helpers -------------------------------------------------------------------------
// Wyman, Sloan and Shirley (2013) multi-lobe fit of the CIE 1931 colour-matching functions.
fn cieXYZ(lambda: f32) -> vec3f {
    let t1 = (lambda - 442.0) * select(0.0374, 0.0624, lambda < 442.0);
    let t2 = (lambda - 599.8) * select(0.0323, 0.0264, lambda < 599.8);
    let t3 = (lambda - 501.1) * select(0.0382, 0.0490, lambda < 501.1);
    let x = 0.362 * exp(-0.5 * t1 * t1) + 1.056 * exp(-0.5 * t2 * t2) - 0.065 * exp(-0.5 * t3 * t3);
    let u1 = (lambda - 568.8) * select(0.0247, 0.0213, lambda < 568.8);
    let u2 = (lambda - 530.9) * select(0.0322, 0.0613, lambda < 530.9);
    let y = 0.821 * exp(-0.5 * u1 * u1) + 0.286 * exp(-0.5 * u2 * u2);
    let v1 = (lambda - 437.0) * select(0.0278, 0.0845, lambda < 437.0);
    let v2 = (lambda - 459.0) * select(0.0725, 0.0385, lambda < 459.0);
    let z = 1.217 * exp(-0.5 * v1 * v1) + 0.681 * exp(-0.5 * v2 * v2);
    return vec3f(x, y, z);
}
fn xyzToRgb(c: vec3f) -> vec3f {
    return vec3f(3.2406 * c.x - 1.5372 * c.y - 0.4986 * c.z,
                -0.9689 * c.x + 1.8758 * c.y + 0.0415 * c.z,
                 0.0557 * c.x - 0.2040 * c.y + 1.0570 * c.z);
}
// One wavelength as linear RGB (unit luminance-ish), clamped to the gamut.
fn spectralRgb(lambda: f32) -> vec3f { return max(xyzToRgb(cieXYZ(lambda)), vec3f(0.0)); }

// Thin-film interference (oxide on steel: the temper colours). Airy reflectance of air|oxide|metal at
// eight wavelengths, folded to RGB and normalised by the bare metal's reflectance, so a zero film is
// neutral and a thick one tints only through interference. Bhadeshia: straw -> brown -> purple -> blue.
fn thinFilm(cosI: f32, thicknessNm: f32) -> vec3f {
    if (thicknessNm < 1.0) { return vec3f(1.0); }
    let n = 2.4;                    // iron oxide, approximately
    let sinT2 = (1.0 - cosI * cosI) / (n * n);
    let cosT = sqrt(max(1.0 - sinT2, 0.0));
    let r1 = (1.0 - n) / (1.0 + n); // air -> oxide
    let r2 = -0.62;                 // oxide -> metal, approximated as real with a phase flip
    var acc = vec3f(0.0);
    var norm = vec3f(0.0);
    for (var k = 0; k < 8; k++) {
        let lambda = 410.0 + 40.0 * f32(k);
        let delta = 4.0 * PI * n * thicknessNm * cosT / lambda;
        let c = cos(delta);
        let R = (r1 * r1 + r2 * r2 + 2.0 * r1 * r2 * c) / (1.0 + r1 * r1 * r2 * r2 + 2.0 * r1 * r2 * c);
        let R0 = (r1 + r2) * (r1 + r2) / ((1.0 + r1 * r2) * (1.0 + r1 * r2));
        let w = cieXYZ(lambda);
        acc += w * (R / R0);
        norm += w;
    }
    return max(xyzToRgb(acc / norm * vec3f(1.0)) / max(xyzToRgb(vec3f(1.0)), vec3f(1e-3)), vec3f(0.0));
}

// Incandescence of forge heat (0..1 -> dull red .. orange .. gold), a blackbody-ish ramp.
fn heatColor(h: f32) -> vec3f {
    let x = clamp(h, 0.0, 1.5);
    return vec3f(1.0, 0.32 + 0.45 * smoothstep(0.2, 1.2, x), 0.06 + 0.25 * smoothstep(0.7, 1.5, x)) * (x * x) * 3.0;
}

// v2: a band's colour pulled toward the god's palette (band 0 key, 1 zone A, 2 rim, 3 eye), luminance preserved
fn palTint(k: i32) -> vec3f {
    var c = F.pal0.rgb;
    if (k == 1) { c = F.pal3.rgb; }
    if (k == 2) { c = F.pal1.rgb; }
    if (k == 3) { c = F.pal2.rgb; }
    return mix(vec3f(1.0), c, F.pal0.w);
}
// v2: the raking key (low, from the side the camera does not face) and the rim (from behind), in reflection space
fn keyRim(dir: vec3f) -> vec3f {
    if (F.pal3.w <= 0.0 && F.pal1.w <= 0.0) { return vec3f(0.0); }
    let fwd = normalize(F.camFwd.xyz);
    let right = normalize(cross(fwd, vec3f(0.0, 1.0, 0.0)) + vec3f(1e-4, 0.0, 0.0));
    let up = cross(right, fwd);
    let keyD = normalize(right * 0.9 - up * 0.25 - fwd * 0.35);
    let rimD = normalize(fwd * 0.9 + up * 0.35);
    var k = exp((dot(dir, keyD) - 1.0) * 5.0) * 1.6 * F.pal3.w;
    // v2 legibility: a broad frontal-high key at a formed peak (bright brow, cheekbones and nose bridge; sockets and the
    // mouth turned away from it stay dark) -- the T-configuration a viewer and a face detector read
    let keyF = normalize(-fwd * 0.75 + up * 0.65);
    k += exp((dot(dir, keyF) - 1.0) * 1.8) * 1.1 * F.lg0.x;
    let r = exp((dot(dir, rimD) - 1.0) * 3.5) * 1.8 * F.pal1.w;
    return k * mix(vec3f(1.0), F.pal0.rgb, F.pal0.w) + r * mix(vec3f(1.0), F.pal1.rgb, F.pal0.w);
}
