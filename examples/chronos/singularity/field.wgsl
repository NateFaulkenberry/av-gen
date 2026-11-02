// ---- THE FIELD: one source of truth (field.wgsl, spliced into every layer by build_singularity.py; field.py mirrors
// it line for line for the figures' trajectories). Do not edit a generated copy. -----------------------------------

const TAU: f32 = 6.28318530718;

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

// the tears: irregular moments when the singularity tears and reforms
fn tearTime(k: i32) -> f32 {
    var t = array<f32, 12>(13.0, 31.5, 52.0, 71.0, 92.5, 110.0, 131.0, 149.5, 170.0, 188.0, 206.0, 226.0);
    return t[k];
}

fn tear(t: f32) -> f32 {
    var d = 0.0;
    for (var k = 0; k < 12; k = k + 1) {
        let x = (t - tearTime(k)) / 2.4;
        d = max(d, exp(-x * x));
    }
    return d;
}

fn tearIntegral(t: f32) -> f32 {
    var s = 0.0;
    for (var k = 0; k < 12; k = k + 1) {
        // clamped: Metal computes tanh through exp(2x), which overflows a float past x = 44.4 and returns inf/inf =
        // NaN -- 87 s after the first tear, the whole field went NaN and the frame black. tanh(10) is 1 to float precision.
        s = s + 2.13 * (1.0 + tanh(clamp(0.6 * (t - tearTime(k)), -10.0, 10.0)));
    }
    return s;
}

// the outward bursts: quieter events between the tears
fn burstTime(k: i32) -> f32 {
    var t = array<f32, 16>(5.5, 21.0, 24.5, 41.5, 46.0, 61.0, 82.0, 86.5, 101.0, 121.5, 139.0, 158.5, 162.0, 178.5, 197.0, 216.5);
    return t[k];
}

// the main mass wanders the frame: the camera never frames it dead centre for long
fn mainCenter(t: f32) -> vec2<f32> {
    return vec2<f32>(0.04 + 0.27 * sin(0.047 * t + 1.0) + 0.06 * sin(0.131 * t + 2.3),
                     0.01 + 0.12 * sin(0.061 * t + 0.4) + 0.04 * sin(0.157 * t + 4.1));
}

fn coreRadiusAt(t: f32, base: f32, expansion: f32) -> f32 {
    let breathe = 1.0 + 0.10 * sin(0.21 * t + 0.3) + 0.06 * sin(0.57 * t + 1.7) + 0.04 * sin(1.13 * t + 0.2);
    let slow = 1.0 + 0.28 * sin(0.037 * t + 2.0) + 0.1 * sin(0.089 * t + 0.5);
    return base * breathe * slow * (1.0 + 0.22 * tear(t) + 0.35 * expansion);
}

// the core's shape: never a circle -- an ellipse and a three-lobed dent that turn and change on their own clocks
fn coreShape(a: f32, t: f32) -> f32 {
    return 1.0 + 0.16 * sin(2.0 * a + 0.13 * t + 1.0) + 0.09 * sin(3.0 * a - 0.21 * t + 2.0) + 0.05 * sin(5.0 * a + 0.4 * t);
}

struct Eddy {
    c: vec2<f32>,
    r: f32,
    spin: f32,
    w: f32,
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
    } else if (i == 1) {
        let ph = 4.0 - 0.11 * t;
        let a = 0.66 * (1.0 + 0.2 * sin(0.23 * t));
        e.c = c0 + a * vec2<f32>(cos(ph), 0.62 * sin(ph));
        e.r = R0 * 0.24;
        e.spin = 1.0;
        e.w = smoothstep(0.2, 0.9, 0.5 + 0.5 * sin(0.061 * t + 2.9));
    } else {
        // the third forms out in the far field, drifts, and dissolves: a secondary spiral that is born and dies
        let ph = 2.2 + 0.05 * t;
        let a = 0.95 + 0.2 * sin(0.07 * t);
        e.c = c0 + a * vec2<f32>(cos(ph), 0.5 * sin(ph));
        e.r = R0 * 0.38;
        e.spin = -1.0;
        e.w = smoothstep(0.55, 0.95, 0.5 + 0.5 * sin(0.044 * t + 4.4));
    }
    return e;
}

// the frame: a slow turn and occasional slow pushes toward the singularity. field = rot(screen, turn) / zoom
fn frameZoom(t: f32, scale: f32) -> f32 {
    let push = 0.14 * exp(-pow((t - 40.0) / 9.0, 2.0)) + 0.18 * exp(-pow((t - 120.0) / 11.0, 2.0)) +
               0.12 * exp(-pow((t - 185.0) / 9.0, 2.0));
    return (1.0 + 0.05 * sin(0.043 * t) + push) * scale;
}

fn frameTurn(t: f32) -> f32 {
    return 0.011 * t + 0.07 * sin(0.031 * t);
}

// the swirl's angular rate: accelerating, decelerating, lurching forward at each tear
fn spinTime(t: f32, rotation: f32) -> f32 {
    return rotation * (t + 2.5 * sin(0.083 * t) + 3.0 * tearIntegral(t));
}

// ---- end of THE FIELD ---------------------------------------------------------------------------------------------
