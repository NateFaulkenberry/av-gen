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
    return length(p - (a.xyz + ab * t)) - mix(a.w, b.w, t) * (1.0 + au.formation.y);
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
        return outside + 1.0; // the surface lies inside the box (with a margin): stepping to the box is safe
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
    var q = p - base;
    let qxz = rot2(q.xz, -angle);
    var d = 1e5;
    var c = vec2<f32>(0.0);
    var y0 = -40.0;
    var r = scale;
    var h = height + 40.0;
    var a = 0.0;
    for (var k = 0; k <= depth; k = k + 1) {
        let u = rot2(qxz - c, -a) / r;
        let flare = 1.0 + 0.5 * smoothstep(y0 + 0.4 * h, y0, q.y);
        let e = towerOutline(u / flare) - 1.0;
        let d2 = e * r * 0.5;
        d = min(d, max(d2, q.y - (y0 + h)));
        let j = vec2<f32>(hash01(k, 11, seed), hash01(k, 12, seed)) - 0.5;
        c = c + rot2(j, a) * r * 0.5;
        y0 = y0 + h;
        if (k == 0) {
            h = height;
        }
        r = r * 0.55;
        h = h * 0.8;
        a = a + 1.1 + hash01(k, 13, seed) * 1.4;
    }
    return d;
}

// ---- the arch fin ----

// One fin with one arch, in its own frame: ground at y = 0, the fin along x, its arch opening through z.
fn sdFinUnit(q: vec3<f32>, L: f32, H: f32) -> f32 {
    let T = 0.17 * L;
    let xn = q.x / L;
    let top = H * (0.80 + 0.20 * (1.0 - xn * xn)) + 0.06 * H * fbm(vec2<f32>(q.x, q.z) / (0.35 * L), 2, 31u);
    let side = 0.12 * T * fbm(vec2<f32>(q.x, q.y) / (0.18 * L), 2, 33u);
    let body = max(max(abs(q.x) - L, abs(q.z) - T - side), q.y - top);
    let a = 0.44 * L;
    let b = 0.66 * H;
    let arch = (length(vec2<f32>(q.x / a, q.y / b)) - 1.0) * min(a, b);
    return max(body, -arch);
}

fn sdFin(p: vec3<f32>, base: vec3<f32>, scale: f32, height: f32, angle: f32, depth: i32, seed: u32) -> f32 {
    var q = p - base;
    let r = rot2(q.xz, -angle);
    q = vec3<f32>(r.x, q.y + 30.0, r.y); // sunk 30 m, so the fin rises out of the ground rather than sitting on it
    var d = 1e5;
    var s = 1.0;
    var off = vec3<f32>(0.0);
    var yaw = 0.0;
    for (var k = 0; k <= depth; k = k + 1) {
        let lq = q - off;
        let lr = rot2(lq.xz, -yaw);
        let local = vec3<f32>(lr.x, lq.y, lr.y);
        d = min(d, sdFinUnit(local / s, scale, height + 30.0) * s);
        // the next fin stands inside this one's arch, on the same ground
        off = off + vec3<f32>(rot2(vec2<f32>((hash01(k, 21, seed) - 0.5) * 0.25 * scale * s, 0.0), yaw).x, 0.0,
                              rot2(vec2<f32>((hash01(k, 21, seed) - 0.5) * 0.25 * scale * s, 0.0), yaw).y);
        yaw = yaw + (hash01(k, 22, seed) - 0.5) * 0.5;
        s = s * 0.38;
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
    return soft * 2.2 * smoothstep(0.0, 0.12, bf) * smoothstep(1.0, 0.85, bf);
}

// x = distance (m), y = 1 for the formations (the caller compares it with the terrain)
fn sdFormations(p: vec3<f32>) -> f32 {
    var d = sdTube(p);
    let n = i32(au.formation.x);
    for (var i = 0; i < n; i = i + 1) {
        let b = au.landmarks[3 * i + 2];
        let toBound = length(p - b.xyz) - b.w;
        if (toBound > 0.0) {
            d = min(d, toBound + 1.0);
            continue;
        }
        let a = au.landmarks[3 * i];
        let k = au.landmarks[3 * i + 1];
        let seed = u32(i) * 7919u + 13u;
        if (a.w < 0.5) {
            d = min(d, sdTower(p, a.xyz, k.x, k.y, k.z, i32(k.w), seed));
        } else {
            d = min(d, sdFin(p, a.xyz, k.x, k.y, k.z, i32(k.w), seed));
        }
    }
    if (d < 6.0) {
        d = d + strataRecess(p);
    }
    return d;
}
