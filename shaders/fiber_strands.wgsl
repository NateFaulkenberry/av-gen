// ADR-1181: the strand pass. One thread per record of a Fiber object with Streamline deformers
// integrates the fiber's centre line ONCE per frame and writes its segments + 1 points into the
// object's strand buffer, which the vertex stage (procedural.wgsl, binding 7) reads for every vertex
// of every pass (camera, prepass, shadows). Without it each vertex re-integrated its own prefix of the
// line, O(segments^2) field samples per fiber per pass; with it the cost is O(segments) once.
//
// The rule is scene::fiberCentreLine's, operation for operation:
//   r_0 = model * position, d_0 = normalize(model * rotate(q, +Y)), ds = shape.x * scale.y
//   d_{k+1} = normalize(d_k + sum_j v_j(r_k) * amount_j * ramp_j(k ds) * ds), r_{k+1} = r_k + d_{k+1} ds
// with ramp_j(a) = clamp(a / stiffness_j, 0, 1) (1 when stiffness is 0). A zero-scale record (an
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
    pulls: array<vec4<f32>, 8>, // x = field slot, y = steering, z = stiffness
};

@group(0) @binding(0) var<uniform> strandParams: StrandParams;
@group(0) @binding(1) var<storage, read> records: array<InstanceRecord>;
@group(0) @binding(2) var<storage, read_write> strands: array<vec4<f32>>;
@group(0) @binding(3) var<uniform> fieldBlock: FieldBlock;

fn strandQuatRotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
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
    let pullCount = min(strandParams.info.z, 8u);
    for (var k = 0u; k + 1u < points; k = k + 1u) {
        let arc = ds * f32(k);
        var pull = vec3<f32>(0.0);
        for (var j = 0u; j < 8u; j = j + 1u) {
            if (j >= pullCount) { break; }
            let p = strandParams.pulls[j];
            var ramp = 1.0;
            if (p.z > 0.0) {
                ramp = clamp(arc / p.z, 0.0, 1.0);
            }
            pull = pull + fieldVector(i32(floor(p.x + 0.5)), r) * (p.y * ramp);
        }
        let turned = d + pull * ds;
        let len = length(turned);
        if (len > 1e-6) {
            d = turned / len;
        }
        r = r + d * ds;
        strands[base + k + 1u] = vec4<f32>(r, 1.0);
    }
}
