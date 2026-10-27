// ADR-1852: THE STRANGE ATTRACTOR's formations, as signed distances (included by attractor/world.wgsl).
//   * the attractor's stone ribbon: a baked distance volume (scene::bakeTubeVolume), refined exactly against the
//     nearest segments near the surface so the tube is smooth however close the camera comes;
//   * the butte tower: tiers that each repeat one outline (the same noise, in the tier's own normalised frame) at
//     0.55 of the scale below, rotated and offset, with a talus flare at each foot;
//   * the arch fin: a fin pierced by an arch, standing inside it a smaller fin pierced by an arch, and so on.
// All of them carry the land's strata (soft bands recessed), and the world pass fillets them into the terrain.

@group(0) @binding(5) var tubeVol: texture_3d<f32>;
@group(0) @binding(6) var<storage, read> tubePts: array<vec4<f32>>;

fn smin(a: f32, b: f32, k: f32) -> f32 {
    let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

fn rot2(v: vec2<f32>, a: f32) -> vec2<f32> {
    let c = cos(a);
    let s = sin(a);
    return vec2<f32>(c * v.x - s * v.y, s * v.x + c * v.y);
}

// ---- the ribbon ----

fn tubeSegment(p: vec3<f32>, i: i32) -> f32 {
    let a = tubePts[i];
    let b = tubePts[i + 1];
    let ab = b.xyz - a.xyz;
    let t = clamp(dot(p - a.xyz, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    let r = mix(a.w, b.w, t) * (1.0 + au.formation.y);
    // a weathered surface: pocked and grooved, a fraction of the radius
    let weather = (vnoise(p.xz / 9.0 + vec2<f32>(p.y / 11.0, 0.0)) - 0.5) * 0.22 * r;
    return length(p - (a.xyz + ab * t)) - r + weather;
}

fn sdTube(p: vec3<f32>) -> f32 {
    if (au.tubeMin.w < 1.5) {
        return 1e5;
    }
    let lo = au.tubeMin.xyz;
    let hi = au.tubeMax.xyz;
    let q = clamp(p, lo, hi);
    let outside = length(p - q);
    if (outside > 0.0) {
        // the surface lies at least the box's margin (2.5 voxels) inside it, so `outside + 10 m` is still a lower bound
        // -- and, unlike `outside + 1`, larger than the march's hit epsilon at any range (which drew the box as dots)
        return outside + 10.0;
    }
    let res = i32(au.tubeMin.w);
    let g = (p - lo) / (hi - lo) * f32(res - 1);
    let i = clamp(vec3<i32>(floor(g)), vec3<i32>(0), vec3<i32>(res - 2));
    let f = g - vec3<f32>(i);
    let c000 = textureLoad(tubeVol, i, 0).x;
    let c100 = textureLoad(tubeVol, i + vec3<i32>(1, 0, 0), 0).x;
    let c010 = textureLoad(tubeVol, i + vec3<i32>(0, 1, 0), 0).x;
    let c110 = textureLoad(tubeVol, i + vec3<i32>(1, 1, 0), 0).x;
    let c001 = textureLoad(tubeVol, i + vec3<i32>(0, 0, 1), 0).x;
    let c101 = textureLoad(tubeVol, i + vec3<i32>(1, 0, 1), 0).x;
    let c011 = textureLoad(tubeVol, i + vec3<i32>(0, 1, 1), 0).x;
    let c111 = textureLoad(tubeVol, i + vec3<i32>(1, 1, 1), 0).x;
    let d = mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y), mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
    let cell = (hi.x - lo.x) / f32(res - 1);
    if (d > 3.0 * cell + au.formation.y * 20.0) {
        return d - au.formation.y * 20.0;
    }
    // near the surface: exact, over the segments around the voxel's nearest one
    let seg = i32(textureLoad(tubeVol, clamp(vec3<i32>(round(g)), vec3<i32>(0), vec3<i32>(res - 1)), 0).y);
    let count = i32(au.tubeMax.w);
    var best = 1e5;
    for (var k = -6; k <= 6; k = k + 1) {
        let s = clamp(seg + k, 0, count - 1);
        best = min(best, tubeSegment(p, s));
    }
    return best;
}

// ---- the butte tower ----

// One outline for every tier: the same noise in the tier's normalised frame, so each tier is the one below at 0.55.
fn towerOutline(u: vec2<f32>) -> f32 {
    return length(u) * (1.0 + 0.32 * fbm(u * 1.6 + vec2<f32>(3.1, 7.7), 3, 77u));
}

fn sdTower(p: vec3<f32>, base: vec3<f32>, scale: f32, height: f32, angle: f32, depth: i32, seed: u32) -> f32 {
    let q = p - base;
    let qxz = rot2(q.xz, -angle);
    var d = 1e5;
    var c = vec2<f32>(0.0);
    var y0 = -40.0;
    var r = scale;
    var h = height + 40.0;
    var a = 0.0;
    for (var k = 0; k <= depth; k = k + 1) {
        // each tier: the plateau's own profile in miniature -- a talus foot, a cliff, a caprock that overhangs a little
        let yl = (q.y - y0) / h;
        let flare = 1.0 + 0.55 * smoothstep(0.42, 0.0, yl);
        let cap = 1.0 + 0.12 * smoothstep(0.86, 0.95, yl);
        let u = rot2(qxz - c, -a) / (r * flare * cap);
        let e = towerOutline(u) - 1.0;
        let d2 = e * r * 0.45;
        let topY = y0 + h + 0.06 * h * fbm(u * 2.3, 2, 79u);
        d = min(d, max(d2, q.y - topY));
        // the next tier leans out over its edge: the stack is balanced as no stack of rock could be
        let j = vec2<f32>(hash01(k, 11, seed), hash01(k, 12, seed)) - 0.5;
        c = c + rot2(normalize(j + vec2<f32>(0.3, 0.0)), a) * r * 0.42;
        y0 = y0 + h;
        if (k == 0) {
            h = height;
        }
        r = r * 0.58;
        h = h * (0.72 + 0.2 * hash01(k, 14, seed));
        a = a + 0.9 + hash01(k, 13, seed) * 1.8;
    }
    return d;
}

// ---- the nested arches ----

// One free-standing natural arch in its own frame: ground at y = 0, the span across x, its thickness along z. Legs
// thick at the foot, a thin lintel, a weathered surface; the legs run 30 m into the ground.
fn sdArchUnit(q: vec3<f32>, A: f32, B: f32) -> f32 {
    let yn = clamp(q.y / B, 0.0, 1.0);
    let e = length(vec2<f32>(q.x / A, q.y / B));
    let n1 = fbm(vec2<f32>(q.x + q.z, q.y) / (0.22 * A), 3, 41u);
    let w = A * mix(0.27, 0.10, yn) * (1.0 + 0.25 * n1);
    let ring = abs(e - 1.0) * min(A, B) - w;
    let T = A * mix(0.21, 0.08, yn) * (1.0 + 0.2 * fbm(vec2<f32>(q.x, q.y) / (0.3 * A), 2, 43u));
    let d2 = vec2<f32>(ring, abs(q.z) - T);
    var d = min(max(d2.x, d2.y), 0.0) + length(max(d2, vec2<f32>(0.0)));
    d = max(d, -q.y - 30.0);
    return d * 0.8;
}

// An arch, and inside its opening a smaller arch turned a little, and inside that one another, to `depth` levels:
// the same shape repeated within itself.
fn sdArches(p: vec3<f32>, base: vec3<f32>, scale: f32, height: f32, angle: f32, depth: i32, seed: u32) -> f32 {
    let q0 = p - base;
    var d = 1e5;
    var s = 1.0;
    var yaw = angle;
    var shift = vec2<f32>(0.0);
    for (var k = 0; k <= depth; k = k + 1) {
        let r = rot2(q0.xz - shift, -yaw);
        d = min(d, sdArchUnit(vec3<f32>(r.x, q0.y, r.y), scale * s, height * s));
        shift = shift + rot2(vec2<f32>(0.0, (hash01(k, 21, seed) - 0.5) * 0.3 * scale * s), yaw);
        yaw = yaw + (hash01(k, 22, seed) - 0.5) * 0.9;
        s = s * 0.56;
    }
    return d;
}

// ---- all of them ----

// Strata on the formations: soft bands recessed a little, so the layers read as weathered rock, not paint.
fn strataRecess(p: vec3<f32>) -> f32 {
    let s = strataCoord(p);
    let band = au.strata.z;
    let t = s / band;
    let bi = floor(t);
    let bf = t - bi;
    let soft = 1.0 - smoothstep(0.35, 0.75, bandHardness(bi));
    return soft * 3.5 * smoothstep(0.0, 0.12, bf) * smoothstep(1.0, 0.85, bf);
}

// x = distance (m), y = 1 for the formations (the caller compares it with the terrain)
fn sdFormations(p: vec3<f32>) -> f32 {
    var d = sdTube(p);
    let n = i32(au.formation.x);
    for (var i = 0; i < n; i = i + 1) {
        let b = au.landmarks[3 * i + 2];
        let toBound = length(p - b.xyz) - b.w;
        if (toBound > 0.0) {
            d = min(d, toBound + 20.0); // the sphere is inflated by 20 m on the CPU, so this stays a lower bound
            continue;
        }
        let a = au.landmarks[3 * i];
        let k = au.landmarks[3 * i + 1];
        let seed = u32(i) * 7919u + 13u;
        if (a.w < 0.5) {
            d = min(d, sdTower(p, a.xyz, k.x, k.y, k.z, i32(k.w), seed));
        } else {
            d = min(d, sdArches(p, a.xyz, k.x, k.y, k.z, i32(k.w), seed));
        }
    }
    if (d < 6.0) {
        d = d + strataRecess(p);
    }
    return d;
}
