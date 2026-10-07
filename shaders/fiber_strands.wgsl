// ADR-1181: the strand pass. One thread per record of a Fiber object with Streamline deformers
// integrates the fiber's centre line ONCE per frame and writes its segments + 1 points into the
// object's strand buffer, which the vertex stage (procedural.wgsl, binding 7) reads for every vertex
// of every pass (camera, prepass, shadows). Without it each vertex re-integrated its own prefix of the
// line, O(segments^2) field samples per fiber per pass; with it the cost is O(segments) once.
//
// The rule is scene::fiberCentreLine's, operation for operation:
//   r_0 = model * position, d_0 = normalize(model * rotate(q, +Y)), ds = shape.x * scale.y
//   P(r, a) = sum_j tension_j(v_j(r) * amount_j * ramp_j(a)), ramp_j(a) = clamp(a / stiffness_j, 0, 1)
//   h = turn(d_k, P(r_k, k ds), ds / 2), m = r_k + h ds / 2, d_{k+1} = turn(d_k, P(m, (k + 1/2) ds), ds)
//   r_{k+1} = r_k + d_{k+1} ds, with turn(d, p, h) = normalize(d + p h) and tension_j(p) = p max(|p| - T_j, 0) / |p|. A zero-scale record (an
// empty generator cell, or one an effector thinned away) is never drawn and is skipped.
//
// Bindings (group 0): 0 StrandParams, 1 records (read), 2 strands (read_write), 3 FieldBlock,
// 15 the simulated-grid table (declared by fields.wgsl). Mirrors rendering/procedural_renderer.cpp
// FiberStrandUniforms.
#include "fields.wgsl"

struct InstanceRecord {
    position: vec4<f32>,
    rotation: vec4<f32>,
    scale: vec4<f32>,
    random: vec4<f32>,
    color: vec4<f32>,
    emissive: vec4<f32>,
};

struct StrandParams {
    model: mat4x4<f32>,
    info: vec4<u32>,            // x = record count, y = points per fiber (segments + 1), z = pulls
    shape: vec4<f32>,           // x = segment length at scale 1
    pulls: array<vec4<f32>, 8>, // x = field slot, y = steering, z = stiffness, w = tension
};

@group(0) @binding(0) var<uniform> strandParams: StrandParams;
@group(0) @binding(1) var<storage, read> records: array<InstanceRecord>;
@group(0) @binding(2) var<storage, read_write> strands: array<vec4<f32>>;
@group(0) @binding(3) var<uniform> fieldBlock: FieldBlock;

fn strandQuatRotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The summed pull at r for arc length a: every pull's field, steering, stiffness ramp and tension.
fn strandPull(r: vec3<f32>, a: f32) -> vec3<f32> {
    let pullCount = min(strandParams.info.z, 8u);
    var total = vec3<f32>(0.0);
    for (var j = 0u; j < 8u; j = j + 1u) {
        if (j >= pullCount) { break; }
        let p = strandParams.pulls[j];
        var ramp = 1.0;
        if (p.z > 0.0) {
            ramp = clamp(a / p.z, 0.0, 1.0);
        }
        var v = fieldVector(i32(floor(p.x + 0.5)), r) * (p.y * ramp);
        if (p.w > 0.0) {
            let m = length(v);
            v = select(vec3<f32>(0.0), v * (max(m - p.w, 0.0) / m), m > 1e-8);
        }
        total = total + v;
    }
    return total;
}

fn strandTurn(d: vec3<f32>, pull: vec3<f32>, h: f32) -> vec3<f32> {
    let t = d + pull * h;
    let len = length(t);
    if (len > 1e-6) {
        return t / len;
    }
    return d;
}

@compute @workgroup_size(64)
fn cs_fiber_strands(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= strandParams.info.x) {
        return;
    }
    let rec = records[i];
    let s = rec.scale.xyz;
    if (max(max(s.x, s.y), s.z) <= 0.0) {
        return;
    }
    fieldElement = rec.random.w; // ADR-1116: an Element-band audio field hears this fiber's own band
    let points = strandParams.info.y;
    let base = i * points;
    let ds = strandParams.shape.x * s.y;
    var r = (strandParams.model * vec4<f32>(rec.position.xyz, 1.0)).xyz;
    var d = normalize((strandParams.model * vec4<f32>(strandQuatRotate(rec.rotation, vec3<f32>(0.0, 1.0, 0.0)), 0.0)).xyz);
    strands[base] = vec4<f32>(r, 1.0);
    for (var k = 0u; k + 1u < points; k = k + 1u) {
        let arc = ds * f32(k);
        // Midpoint (RK2): the pull at r and at the half step (scene::fiberCentreLine).
        let half = strandTurn(d, strandPull(r, arc), 0.5 * ds);
        let mid = r + half * (0.5 * ds);
        d = strandTurn(d, strandPull(mid, arc + 0.5 * ds), ds);
        r = r + d * ds;
        strands[base + k + 1u] = vec4<f32>(r, 1.0);
    }
}
