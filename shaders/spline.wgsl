// Spline tables on the GPU (ADR-026; rendering/spline_buffers.hpp). Every scene spline is a
// table of SPLINE_SAMPLES entries equally spaced by arc length (spatial::packSplineTable):
// position.w = distance, tangent.w = scale, normal.w = t, binormal.w = 1. The frame convention
// is the CPU's: x = binormal, y = normal, z = tangent. Sampling by distance interpolates the two
// neighbouring entries linearly and re-orthonormalises the frame exactly like
// spatial::Spline::sampleByDistance (the CPU samples its own denser table, so the two agree
// to the table's interpolation error, not bit for bit).
//
// The includer declares the storage binding, e.g.
//   @group(1) @binding(4) var<storage, read> splineTable: SplineTable;

struct SplineSampleGpu {
    position: vec4<f32>, // xyz, w = distance
    tangent: vec4<f32>,  // xyz unit, w = scale
    normal: vec4<f32>,   // xyz unit, w = t
    binormal: vec4<f32>, // xyz unit, w = 1
};

struct SplineTable {
    info: array<vec4<f32>, 16>,       // per slot: x = length, y = closed, z = sample count, w = valid
    samples: array<SplineSampleGpu>,  // slot * SPLINE_SAMPLES + k
};

struct SplineSampleOut {
    position: vec3<f32>,
    tangent: vec3<f32>,
    normal: vec3<f32>,
    binormal: vec3<f32>,
    distance: f32,
    t: f32,
    scale: f32,
};

const SPLINE_MAX: i32 = 16;
const SPLINE_SAMPLES: u32 = 512u;

fn splineValid(slot: i32) -> bool {
    if (slot < 0 || slot >= SPLINE_MAX) {
        return false;
    }
    return splineTable.info[slot].w > 0.5;
}

fn splineLength(slot: i32) -> f32 {
    if (slot < 0 || slot >= SPLINE_MAX) {
        return 0.0;
    }
    return splineTable.info[slot].x;
}

fn splineEntry(e: SplineSampleGpu) -> SplineSampleOut {
    var s: SplineSampleOut;
    s.position = e.position.xyz;
    s.tangent = e.tangent.xyz;
    s.normal = e.normal.xyz;
    s.binormal = e.binormal.xyz;
    s.distance = e.position.w;
    s.t = e.normal.w;
    s.scale = e.tangent.w;
    return s;
}

// Linear interpolation of two entries with the frame renormalised and re-orthogonalised
// (spatial lerpSamples): tangent = normalize(mix) (fallback a), normal = the mixed normal
// projected perpendicular to the tangent (fallback a.normal), binormal = cross(normal, tangent).
fn splineLerp(a: SplineSampleGpu, b: SplineSampleGpu, f: f32, distance: f32) -> SplineSampleOut {
    var s: SplineSampleOut;
    s.position = mix(a.position.xyz, b.position.xyz, f);
    var t = mix(a.tangent.xyz, b.tangent.xyz, f);
    if (dot(t, t) > 1e-16) {
        t = normalize(t);
    } else {
        t = a.tangent.xyz;
    }
    var n = mix(a.normal.xyz, b.normal.xyz, f);
    if (length(n) <= 1e-5) {
        n = a.normal.xyz;
    }
    var proj = n - t * dot(n, t);
    if (length(proj) > 1e-5) {
        proj = normalize(proj);
    } else {
        // a stable perpendicular of t (spatial anyPerpendicular)
        var helper = vec3<f32>(1.0, 0.0, 0.0);
        if (abs(t.x) >= 0.9) {
            helper = vec3<f32>(0.0, 1.0, 0.0);
        }
        proj = normalize(cross(t, helper));
    }
    s.tangent = t;
    s.normal = proj;
    s.binormal = cross(proj, t);
    s.distance = distance;
    s.t = mix(a.normal.w, b.normal.w, f);
    s.scale = mix(a.tangent.w, b.tangent.w, f);
    return s;
}

// The sample at arc length `distanceIn` (clamped to [0, length] for open splines, wrapped for
// closed ones). Binary search over the distance column; a closed spline interpolates its last
// entry towards the first (at distance = length) beyond the last entry.
fn splineSample(slot: i32, distanceIn: f32) -> SplineSampleOut {
    var s: SplineSampleOut;
    if (!splineValid(slot)) {
        s.tangent = vec3<f32>(0.0, 0.0, 1.0);
        s.normal = vec3<f32>(0.0, 1.0, 0.0);
        s.binormal = vec3<f32>(1.0, 0.0, 0.0);
        s.scale = 1.0;
        return s;
    }
    let info = splineTable.info[slot];
    let len = info.x;
    let closed = info.y > 0.5;
    let count = u32(info.z + 0.5);
    let base = u32(slot) * SPLINE_SAMPLES;
    if (count < 2u || len <= 1e-8) {
        return splineEntry(splineTable.samples[base]);
    }
    var d = distanceIn;
    if (closed) {
        d = d - floor(d / len) * len;
        if (d >= len) {
            d = 0.0;
        }
    } else {
        d = clamp(d, 0.0, len);
    }
    let last = splineTable.samples[base + count - 1u];
    if (d >= last.position.w) {
        if (closed) {
            // wrap: last entry -> first entry at distance `len`
            var first = splineTable.samples[base];
            first.position.w = len;
            let span = len - last.position.w;
            var f = 0.0;
            if (span > 1e-8) {
                f = clamp((d - last.position.w) / span, 0.0, 1.0);
            }
            return splineLerp(last, first, f, d);
        }
        return splineEntry(last);
    }
    // First entry whose distance is > d, in [1, count - 1]; the segment is [hi - 1, hi].
    var lo = 1u;
    var hi = count - 1u;
    for (var iter = 0u; iter < 16u; iter = iter + 1u) {
        if (lo >= hi) { break; }
        let mid = (lo + hi) / 2u;
        if (splineTable.samples[base + mid].position.w > d) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    let a = splineTable.samples[base + lo - 1u];
    let b = splineTable.samples[base + lo];
    let span = b.position.w - a.position.w;
    var f = 0.0;
    if (span > 1e-8) {
        f = clamp((d - a.position.w) / span, 0.0, 1.0);
    }
    return splineLerp(a, b, f, d);
}
