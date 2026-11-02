/*{
  "NAME": "chronos-singularity",
  "DESCRIPTION": "CHRONOS, Temporal Singularity: the collapse of time and space. A drifting, breathing main mass and two counter-spinning eddies make one gravitational field; every structure here -- the dark core, a broken white-hot rim, three accretion bands turning at different speeds and in different directions on a tilting, inclined disk, inward-falling radial streaks, displaced fragments, the eddies' own spirals, and the full-screen nebula, filaments and stars -- is drawn in that field's lensed, swirled coordinates. Stateless: a pure function of the render time.",
  "CREDIT": "avgen -- CHRONOS",
  "CATEGORIES": ["Generator", "Background"],
  "INPUTS": [
    {"NAME": "intensity", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Overall intensity"},
    {"NAME": "coreRadius", "TYPE": "float", "DEFAULT": 0.085, "MIN": 0.02, "MAX": 0.3, "LABEL": "Core radius (frame heights)"},
    {"NAME": "scale", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.3, "MAX": 3.0, "LABEL": "Singularity scale"},
    {"NAME": "expansion", "TYPE": "float", "DEFAULT": 0.0, "MIN": -1.0, "MAX": 2.0, "LABEL": "Expansion (route a transient here)"},
    {"NAME": "rotation", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Rotation rate"},
    {"NAME": "turbulence", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 3.0, "LABEL": "Extra turbulence"},
    {"NAME": "fragmentation", "TYPE": "float", "DEFAULT": 0.0, "MIN": 0.0, "MAX": 1.0, "LABEL": "Extra fragmentation"},
    {"NAME": "lens", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 3.0, "LABEL": "Gravitational lensing"},
    {"NAME": "bandGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Accretion bands"},
    {"NAME": "streakGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Radial streaks"},
    {"NAME": "rimGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Rim"},
    {"NAME": "eddyGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Secondary spirals"},
    {"NAME": "backgroundGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Background"},
    {"NAME": "starGain", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Stars"},
    {"NAME": "timeOffset", "TYPE": "float", "DEFAULT": 0.0, "MIN": -1000.0, "MAX": 1000.0, "LABEL": "Time offset (s)"}
  ]
}*/

// Scene-linear HDR, drawn behind any geometry. The core is black; the rim reaches white-hot (about 6); the bands sit at
// 0.3-3; the background at 0.01-0.1, subordinate but never empty.

const TAU: f32 = 6.28318530718;

// ---- noise --------------------------------------------------------------------------------------------------------

fn pcg(v: u32) -> u32 {
    let s = v * 747796405u + 2891336453u;
    let w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

fn hash3(p: vec3<i32>) -> f32 {
    let h = pcg(bitcast<u32>(p.x) ^ pcg(bitcast<u32>(p.y) ^ pcg(bitcast<u32>(p.z))));
    return f32(h >> 8u) / 16777216.0;
}

fn hash2(p: vec2<i32>) -> f32 {
    return f32(pcg(bitcast<u32>(p.x) ^ pcg(bitcast<u32>(p.y) + 0x9e3779b9u)) >> 8u) / 16777216.0;
}

fn vnoise(p: vec3<f32>) -> f32 {
    let i = vec3<i32>(floor(p));
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = mix(hash3(i), hash3(i + vec3<i32>(1, 0, 0)), u.x);
    let b = mix(hash3(i + vec3<i32>(0, 1, 0)), hash3(i + vec3<i32>(1, 1, 0)), u.x);
    let c = mix(hash3(i + vec3<i32>(0, 0, 1)), hash3(i + vec3<i32>(1, 0, 1)), u.x);
    let d = mix(hash3(i + vec3<i32>(0, 1, 1)), hash3(i + vec3<i32>(1, 1, 1)), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}

fn fbm(p: vec3<f32>, octaves: i32) -> f32 {
    var s = 0.0;
    var a = 0.5;
    var q = p;
    var n = 0.0;
    for (var i = 0; i < octaves; i = i + 1) {
        s = s + a * vnoise(q);
        n = n + a;
        a = a * 0.5;
        q = q * 2.03 + vec3<f32>(17.1, -9.3, 5.7);
    }
    return s / n;
}

fn ridged(p: vec3<f32>, octaves: i32) -> f32 {
    var s = 0.0;
    var a = 0.5;
    var q = p;
    var n = 0.0;
    for (var i = 0; i < octaves; i = i + 1) {
        let r = 1.0 - abs(2.0 * vnoise(q) - 1.0);
        s = s + a * r * r;
        n = n + a;
        a = a * 0.5;
        q = q * 2.07 + vec3<f32>(-3.7, 11.9, 2.3);
    }
    return s / n;
}

fn rot(v: vec2<f32>, a: f32) -> vec2<f32> {
    let c = cos(a);
    let s = sin(a);
    return vec2<f32>(c * v.x - s * v.y, s * v.x + c * v.y);
}

// ---- the field: one source of truth for every structure --------------------------------------------------------------

// The instability: irregular moments when the singularity tears and reforms.
const TEAR_COUNT: i32 = 12;
fn tearTime(k: i32) -> f32 {
    var t = array<f32, 12>(13.0, 31.5, 52.0, 71.0, 92.5, 110.0, 131.0, 149.5, 170.0, 188.0, 206.0, 226.0);
    return t[k];
}

fn tear(t: f32) -> f32 {
    var d = 0.0;
    for (var k = 0; k < TEAR_COUNT; k = k + 1) {
        let x = (t - tearTime(k)) / 2.4;
        d = max(d, exp(-x * x));
    }
    return d;
}

// Its integral (in seconds), so a tear accelerates the rotation and the rotation keeps what it gained.
fn tearIntegral(t: f32) -> f32 {
    var s = 0.0;
    for (var k = 0; k < TEAR_COUNT; k = k + 1) {
        s = s + 2.13 * (1.0 + tanh(0.6 * (t - tearTime(k))));
    }
    return s;
}

fn mainCenter(t: f32) -> vec2<f32> {
    return vec2<f32>(0.10 + 0.075 * sin(0.071 * t + 1.0) + 0.035 * sin(0.173 * t + 2.3),
                     0.02 + 0.05 * sin(0.053 * t + 0.4) + 0.028 * sin(0.131 * t + 4.1));
}

fn breathing(t: f32) -> f32 {
    return 1.0 + 0.10 * sin(0.21 * t + 0.3) + 0.06 * sin(0.57 * t + 1.7) + 0.04 * sin(1.13 * t + 0.2);
}

struct Eddy {
    c: vec2<f32>,
    r: f32,       // its own core radius
    spin: f32,    // signed
    w: f32,       // presence 0..1
};

fn eddy(i: i32, t: f32, c0: vec2<f32>, R0: f32) -> Eddy {
    var e: Eddy;
    if (i == 0) {
        let ph = 0.9 + 0.17 * t;
        let a = 0.42 * (1.0 + 0.25 * cos(0.31 * t));
        e.c = c0 + a * vec2<f32>(cos(ph), 0.72 * sin(ph));
        e.r = R0 * 0.32;
        e.spin = -1.0;
        e.w = smoothstep(0.1, 0.8, 0.5 + 0.5 * sin(0.083 * t + 0.6));
    } else {
        let ph = 4.0 - 0.11 * t;
        let a = 0.66 * (1.0 + 0.2 * sin(0.23 * t));
        e.c = c0 + a * vec2<f32>(cos(ph), 0.62 * sin(ph));
        e.r = R0 * 0.24;
        e.spin = 1.0;
        e.w = smoothstep(0.2, 0.9, 0.5 + 0.5 * sin(0.061 * t + 2.9));
    }
    return e;
}

// ---- colour --------------------------------------------------------------------------------------------------------

// heat 0..1: deep purple, violet, magenta, crimson, hot orange, amber, gold, white-hot
fn palette(h: f32) -> vec3<f32> {
    var cols = array<vec3<f32>, 8>(vec3<f32>(0.10, 0.015, 0.16), vec3<f32>(0.32, 0.04, 0.62), vec3<f32>(0.78, 0.05, 0.48),
                                   vec3<f32>(0.82, 0.03, 0.07), vec3<f32>(1.0, 0.30, 0.03), vec3<f32>(1.0, 0.55, 0.09),
                                   vec3<f32>(1.0, 0.78, 0.30), vec3<f32>(1.0, 0.95, 0.86));
    let x = clamp(h, 0.0, 1.0) * 7.0;
    let i = i32(floor(min(x, 6.999)));
    return mix(cols[i], cols[i + 1], x - f32(i));
}

// ---- the image ---------------------------------------------------------------------------------------------------

fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let t = sys.time + inputs.timeOffset;
    let aspect = sys.renderSize.x / max(sys.renderSize.y, 1.0);
    var p = vec2<f32>((uv.x - 0.5) * aspect, 0.5 - uv.y);

    // the composition: a slow turn of the whole frame, and occasional slow pushes toward the singularity
    let push = 0.14 * exp(-pow((t - 40.0) / 9.0, 2.0)) + 0.18 * exp(-pow((t - 120.0) / 11.0, 2.0)) +
               0.12 * exp(-pow((t - 185.0) / 9.0, 2.0));
    let zoom = (1.0 + 0.05 * sin(0.043 * t) + push) * inputs.scale;
    p = rot(p, 0.011 * t + 0.07 * sin(0.031 * t)) / zoom;

    let D = clamp(tear(t) + inputs.turbulence * 0.35, 0.0, 2.0);       // instability
    let c0 = mainCenter(t);
    let R0 = inputs.coreRadius * breathing(t) * (1.0 + 0.22 * D + 0.35 * inputs.expansion);
    let spinT = inputs.rotation * (t + 2.5 * sin(0.083 * t) + 3.0 * tearIntegral(t)); // accelerate, decelerate

    // lensing: each mass pulls the image toward itself (a point lens), then drags it round (its spin)
    var q = p;
    let e0 = eddy(0, t, c0, R0);
    let e1 = eddy(1, t, c0, R0);
    let lensK = inputs.lens;
    {
        let d = p - c0;
        let r2 = dot(d, d);
        q = q - lensK * (1.5 * R0) * (1.5 * R0) * d / (r2 + 0.25 * R0 * R0);
        for (var i = 0; i < 2; i = i + 1) {
            var e = e0;
            if (i == 1) {
                e = e1;
            }
            let de = p - e.c;
            q = q - lensK * e.w * (2.0 * e.r) * (2.0 * e.r) * de / (dot(de, de) + e.r * e.r);
        }
    }
    // frame dragging: a twist that decays with distance; the eddies twist the other way
    var qs = q;
    {
        let de0 = qs - e0.c;
        qs = e0.c + rot(de0, e0.spin * e0.w * 1.6 * e0.r * e0.r / (dot(de0, de0) + e0.r * e0.r) * 2.5);
        let de1 = qs - e1.c;
        qs = e1.c + rot(de1, e1.spin * e1.w * 1.6 * e1.r * e1.r / (dot(de1, de1) + e1.r * e1.r) * 2.5);
        let d = qs - c0;
        let r = length(d);
        qs = c0 + rot(d, -(1.4 + 0.6 * D) * exp(-r / (6.0 * R0)) - 0.35 * exp(-r / 0.9));
    }

    // ---- the background: lensed, swirled nebula, filaments, stars, haze ----------------------------------------------
    var col = vec3<f32>(0.0);
    {
        let d = qs - c0;
        let r = length(d);
        let qb = c0 + rot(d, -0.018 * spinT * 0.6);           // the far field turns slowly, rigidly
        let n1 = fbm(vec3<f32>(qb * 2.1, 0.035 * t), 5);
        let n2 = fbm(vec3<f32>(qb * 4.3 + vec2<f32>(3.1, -1.7), 0.05 * t + 7.0), 4);
        let neb = smoothstep(0.45, 0.9, n1) * (0.4 + 0.9 * n2);
        let tint = mix(vec3<f32>(0.20, 0.025, 0.06), vec3<f32>(0.16, 0.03, 0.30), smoothstep(0.3, 0.7, n2));
        col = col + tint * neb * 0.055;
        // filaments: ridged lines following the swirl, magenta and violet
        let f = ridged(vec3<f32>(qb * 3.2, 0.04 * t + 2.0), 4);
        col = col + mix(vec3<f32>(0.55, 0.05, 0.40), vec3<f32>(0.30, 0.06, 0.65), n1) * pow(f, 9.0) * 0.12;
        // a broad glow round the singularity: the haze
        col = col + vec3<f32>(0.22, 0.03, 0.12) * exp(-r / (5.0 * R0)) * 0.18 + vec3<f32>(0.10, 0.02, 0.16) * exp(-r / 0.7) * 0.06;
        // the base: never a dead black, deep burgundy to purple
        col = col + mix(vec3<f32>(0.006, 0.001, 0.003), vec3<f32>(0.005, 0.0015, 0.011), uv.x);
        // stars: sampled along the local swirl so they smear into arcs, and lensed into arcs near the masses
        var stars = 0.0;
        for (var layer = 0; layer < 2; layer = layer + 1) {
            let sc = select(38.0, 75.0, layer == 1);
            for (var k = 0; k < 4; k = k + 1) {
                let sp = c0 + rot(qb - c0, f32(k) * 0.012 * (1.0 + 2.0 * D));
                let g = sp * sc;
                let cell = vec2<i32>(floor(g));
                let h = hash2(cell + vec2<i32>(layer * 977, 13));
                let jitter = vec2<f32>(hash2(cell + vec2<i32>(7, layer)), hash2(cell + vec2<i32>(layer, 29)));
                let dd = length(fract(g) - jitter);
                stars = stars + pow(h, 30.0) * smoothstep(0.09, 0.0, dd) * (1.0 - 0.2 * f32(k));
            }
        }
        col = col + vec3<f32>(1.0, 0.8, 0.9) * stars * 0.5 * inputs.starGain;
        col = col * inputs.backgroundGain;
    }

    // ---- the disk: tilted, inclined, turning in bands ------------------------------------------------------------
    let tilt = 0.35 + 0.25 * sin(0.037 * t) + 0.1 * sin(0.11 * t);
    let incl = 0.58 + 0.12 * sin(0.029 * t + 1.0);
    var v = rot(qs - c0, tilt);
    v.y = v.y / incl;
    let rd = length(v);
    let x = rd / max(R0, 1e-4);
    let a = atan2(v.y, v.x);
    let lx = log(max(x, 1e-3));

    // fragmentation: cells of the disk (in turning polar coordinates) displaced in angle and radius
    let fragAmount = clamp(0.18 + 0.7 * D + inputs.fragmentation, 0.0, 1.0);
    let cellUv = vec2<f32>((a + 0.4 * spinT * 0.25) * 2.6, lx * 3.2);
    let cell = vec2<i32>(floor(cellUv));
    let ch = hash2(cell + vec2<i32>(31, 7));
    var da = 0.0;
    var dl = 0.0;
    if (ch < fragAmount * 0.55 && x < 6.0) {
        // soft at the cell's edges, so a fragment is a displaced piece of the disk, not a rendering fault
        let fc = fract(cellUv);
        let soft = smoothstep(0.0, 0.18, fc.x) * smoothstep(1.0, 0.82, fc.x) * smoothstep(0.0, 0.18, fc.y) *
                   smoothstep(1.0, 0.82, fc.y);
        da = (hash2(cell + vec2<i32>(3, 91)) - 0.5) * (0.5 + 0.8 * D) * soft;
        dl = (hash2(cell + vec2<i32>(57, 5)) - 0.5) * 0.35 * soft;
    }

    // irregular band radii: each band's ring swells and dents with angle and time
    let dent = fbm(vec3<f32>(cos(a) * 1.3, sin(a) * 1.3, 0.09 * t), 3) - 0.5;
    let warpAmt = 0.35 + 0.9 * D;

    var disk = vec3<f32>(0.0);
    for (var b = 0; b < 3; b = b + 1) {
        var rc = 1.8;
        var w = 0.85;
        var dir = -1.0;
        var spd = 0.55;
        var pitch = 2.4;
        var freq = 3.2;
        var heatBase = 0.86;
        var gain = 1.1;
        var sharp = 3.0;
        if (b == 1) {
            rc = 3.6; w = 1.6; dir = 1.0; spd = 0.23; pitch = -1.7; freq = 2.6; heatBase = 0.55; gain = 0.6; sharp = 2.6;
        } else if (b == 2) {
            rc = 6.5; w = 2.8; dir = -1.0; spd = 0.09; pitch = 1.1; freq = 2.0; heatBase = 0.22; gain = 0.3; sharp = 2.2;
        }
        var xb = x * (1.0 + 0.35 * dent);
        if (b < 2) {
            xb = xb * exp(dl);
        }
        let env = exp(-pow((xb - rc) / w, 2.0));
        if (env < 0.003) {
            continue;
        }
        let ang = a + select(0.0, da, b < 2) + dir * spd * spinT + pitch * log(max(xb, 1e-3));
        var P = vec3<f32>(cos(ang) * freq, sin(ang) * freq, log(max(xb, 1e-3)) * 1.7 - 0.12 * t * (1.0 + f32(b)));
        // turbulence: domain warp
        P = P + warpAmt * (vec3<f32>(fbm(P * 0.8 + vec3<f32>(0.0, 0.0, 0.07 * t), 3),
                                     fbm(P * 0.8 + vec3<f32>(5.2, 1.3, 0.07 * t), 3), 0.0) - 0.5) * 2.0;
        let rib = ridged(P, 4);
        let broad = fbm(P * 0.5 + vec3<f32>(9.0), 3);
        let s = pow(rib, sharp) * (0.35 + 1.2 * broad);
        let heat = heatBase + 0.18 * (rib - 0.5) - 0.08 * (xb - rc) / w;
        disk = disk + palette(heat) * s * env * gain;
    }
    // beaming: the side turning toward us burns brighter, and the bright side wanders
    let beam = 1.0 + 0.65 * cos(a - (2.2 + 0.4 * sin(0.047 * t) + 0.25 * spinT * 0.1));
    disk = disk * inputs.bandGain * beam;

    // radial streaks: thin, falling inward faster near the core, in sectors that come and go
    {
        let fall = 0.9 * spinT * 0.5 + 0.35 * t;
        let n = vnoise(vec3<f32>(cos(a + da) * 64.0, sin(a + da) * 64.0, lx * 1.4 + fall));
        let sector = smoothstep(0.45, 0.75, vnoise(vec3<f32>(cos(a) * 1.8, sin(a) * 1.8, 0.06 * t + 3.0)) + 0.25 * D);
        let rad = smoothstep(1.15, 1.6, x) * exp(-(x - 1.4) / 1.6) * smoothstep(5.5, 3.5, x);
        disk = disk + palette(0.74 + 0.12 * n) * pow(n, 14.0) * sector * rad * 2.2 * inputs.streakGain;
    }

    // the rim: a thin white-hot ring just outside the core, broken into fragments
    {
        // never a clean circle: its radius wanders with angle and time, its width varies, and it is broken into arcs
        let ra = a + 0.9 * spinT;
        let wob = fbm(vec3<f32>(cos(a) * 1.7, sin(a) * 1.7, 0.21 * t), 3) - 0.5;
        let rx = x * (1.0 + 0.28 * wob + 0.05 * (vnoise(vec3<f32>(cos(a) * 6.0, sin(a) * 6.0, 0.5 * t)) - 0.5));
        let wid = 0.05 + 0.06 * vnoise(vec3<f32>(cos(ra) * 2.0, sin(ra) * 2.0, 0.2 * t)) + 0.05 * D;
        let ring = exp(-pow((rx - 1.6) / wid, 2.0));
        let breaks = smoothstep(0.42, 0.68, vnoise(vec3<f32>(cos(ra) * 4.0, sin(ra) * 4.0, 0.3 * t)));
        disk = disk + palette(0.93 + 0.06 * breaks) * ring * breaks * 3.6 * beam * inputs.rimGain;
        // a second, fainter arc inside it on one side only: the lensed underside
        let arc2 = exp(-pow((rx - 1.28) / (0.6 * wid), 2.0)) * smoothstep(0.1, 0.8, cos(a + 1.3 + 0.2 * sin(0.07 * t)));
        disk = disk + palette(0.8) * arc2 * 1.4 * inputs.rimGain;
        // a burgundy glow hugging the core
        disk = disk + vec3<f32>(0.20, 0.008, 0.035) * exp(-(x - 1.0) * 1.3) * step(1.0, x) * 0.7;
    }

    // ---- the eddies' own spirals ------------------------------------------------------------------------------------
    var ed = vec3<f32>(0.0);
    for (var i = 0; i < 2; i = i + 1) {
        var e = e0;
        if (i == 1) {
            e = e1;
        }
        let d = qs - e.c;
        let xe = length(d) / e.r;
        let ae = atan2(d.y, d.x);
        let env = exp(-pow((xe - 2.6) / 1.6, 2.0)) * e.w;
        if (env > 0.003) {
            let ang = ae + e.spin * 0.9 * spinT + 1.8 * e.spin * log(max(xe, 1e-3));
            let P = vec3<f32>(cos(ang) * 2.4, sin(ang) * 2.4, 0.15 * t + f32(i) * 11.0);
            let s = pow(ridged(P, 3), 2.6);
            ed = ed + palette(0.32 + 0.25 * s + 0.12 * f32(i)) * s * env * 1.1;
        }
        // and a small darkness at each eddy's heart
        col = col * mix(1.0, smoothstep(0.6, 1.6, xe), e.w);
    }
    ed = ed * inputs.eddyGain;

    // ---- compose: the core swallows everything inside it ---------------------------------------------------------------
    // the shadow is measured in the UNLENSED frame: inside it the lens would show a second, inner image of the disk,
    // and a singularity has no inside to show
    var vp = rot(p - c0, tilt);
    vp.y = vp.y / mix(1.0, incl, 0.5);
    let xp = length(vp) / max(R0, 1e-4);
    let ap = atan2(vp.y, vp.x);
    let edge = xp + 0.22 * (vnoise(vec3<f32>(cos(ap) * 2.5, sin(ap) * 2.5, 0.25 * t)) - 0.5) * (1.0 + D);
    let core = smoothstep(1.05, 1.4, edge);
    // the innermost disk runs to crimson and burgundy rather than grey as it falls toward the shadow
    let hot = smoothstep(2.0, 1.25, x);
    disk = mix(disk, disk * vec3<f32>(1.0, 0.32, 0.38), hot * 0.65);
    let inner = smoothstep(1.3, 2.2, x);
    var outc = (col * inner + disk + ed) * core;
    outc = outc * inputs.intensity;
    return vec4<f32>(max(outc, vec3<f32>(0.0)), 1.0);
}
