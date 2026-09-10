// GPU frustum/distance culling and LOD selection for procedural instances (ADR-029).
//
// One dispatch chain per object per frame, encoded after the effector pass (so it sees the live
// records when the object has effectors) and before the lit pass:
//
//   1. cs_cull_classify  one thread per record: world bounding sphere (centre = objectToWorld *
//                        record position, radius = source radius x max |scale| x object scale)
//                        against the six frustum planes, then maxDistance and minScreenRadius;
//                        survivors get a LOD level 0..lodCount-1, rejects get kCulled.
//                        Writes lodIndex[i].
//   2. cs_cull_reduce    (blocks, lodCount) workgroups: blockSums[level * blocks + b] = how many
//                        of block b's records are at `level`.
//   3. cs_cull_top       (1, lodCount) workgroups: exclusive scan of that level's blockSums in
//                        place; thread 0 writes the level's drawIndexedIndirect args and the
//                        object's stats slot.
//   4. cs_cull_scatter   (blocks, lodCount) workgroups: local exclusive scan + blockSums gives
//                        every record at `level` its rank r; visibleIndices[level * stride + r] = i.
//
// Determinism: exactly the structure of the particle compaction (particles.wgsl) - prefix sums,
// no atomics anywhere - so `visibleIndices` is always in ascending record order and the whole
// pass is a pure function of (records, camera, parameters). Dispatches inside one compute pass
// are ordered and their storage writes are visible to later dispatches, which is what lets step 2
// feed step 3 and lets every level reuse the same blockSums region layout.
//
// The draw reads instances[visibleIndices[instance_index]] (procedural.wgsl, group 1 binding 5)
// with one drawIndexedIndirect per level; levels 2 and 3 are billboards drawn through the shader's
// camera-facing Point path.
//
// Bindings (group 0): 0 CullParams, 1 records (read), 2 lodIndex (read_write), 3 blockSums
// (read_write), 4 visibleIndices (read_write), 5 indirect args (read_write), 6 stats
// (read_write). Mirrors rendering/procedural_renderer.hpp CullPassUniforms.

struct InstanceRecord {
    position: vec4<f32>,  // xyz, w = density
    rotation: vec4<f32>,  // unit quaternion (x, y, z, w)
    scale: vec4<f32>,     // xyz, w = normalised index
    random: vec4<f32>,
    color: vec4<f32>,     // rgb, a = instance id
    emissive: vec4<f32>,  // rgb, a = extra lane
};

struct CullParams {
    objectToWorld: mat4x4<f32>,
    planes: array<vec4<f32>, 6>, // left, right, bottom, top, near, far; xyz = unit normal, w = d
    cameraPos: vec4<f32>,        // xyz = camera position, w = projScale (pixels per unit at 1 unit)
    limits: vec4<f32>,           // x = maxDistance, y = minScreenRadius, z = source radius, w = object scale
    thresholds: vec4<f32>,       // xyz = lodDistances (LOD0->1, 1->2, 2->3), w = 0
    counts: vec4<u32>,           // x = record count, y = lod count, z = visible stride, w = scan blocks
    flags: vec4<u32>,            // x = cull enabled, y = thresholds are screen radii, z = stats slot, w = 0
    indexCounts: vec4<u32>,      // index count of each level's mesh (for the indirect args)
    // ADR-038 depth layers, as (start, end, density, detail); the count is flags.w.
    depthLayers: array<vec4<f32>, 6>,
};

@group(0) @binding(0) var<uniform> cullParams: CullParams;
@group(0) @binding(1) var<storage, read> records: array<InstanceRecord>;
@group(0) @binding(2) var<storage, read_write> lodIndex: array<u32>;
@group(0) @binding(3) var<storage, read_write> blockSums: array<u32>;
@group(0) @binding(4) var<storage, read_write> visibleIndices: array<u32>;
@group(0) @binding(5) var<storage, read_write> indirectArgs: array<u32>;
@group(0) @binding(6) var<storage, read_write> cullStats: array<u32>;

const kCulled: u32 = 0xFFFFFFFFu;
const kStatsStride: u32 = 8u; // per object: [0..3] per-level counts, [4] records, [5] used marker
const kMaxLodLevels: u32 = 4u; // scene::kMaxLodLevels: indirect-arg slots per object

// vec4 components by dynamic index without indexing a uniform vector.
fn thresholdAt(k: u32) -> f32 {
    let t = cullParams.thresholds;
    var v = t.x;
    if (k == 1u) { v = t.y; }
    if (k == 2u) { v = t.z; }
    return v;
}

fn indexCountAt(level: u32) -> u32 {
    let c = cullParams.indexCounts;
    var v = c.x;
    if (level == 1u) { v = c.y; }
    if (level == 2u) { v = c.z; }
    if (level == 3u) { v = c.w; }
    return v;
}

// ---- depth layers (ADR-038) ---------------------------------------------------------------------
// A layer is a distance band with its own instance density and detail. Unlike the per-pixel grade
// in post.wgsl, which interpolates so the image has no seam, an instance is either in a band or it
// is not: `density` thins the band and `detail` scales the LOD thresholds, and both are discrete
// decisions about whole objects. The band search returns (density, detail), or (1, 1) when the
// scene declares no layers.
fn depthBand(dist: f32) -> vec2<f32> {
    let count = cullParams.flags.w;
    if (count == 0u) {
        return vec2<f32>(1.0, 1.0);
    }
    var last = vec2<f32>(1.0, 1.0);
    for (var i = 0u; i < 6u; i = i + 1u) {
        if (i >= count) { break; }
        let layer = cullParams.depthLayers[i];
        last = layer.zw;
        if (dist < layer.y) {
            return last;
        }
    }
    return last; // past the last band, it clamps
}

// Deterministic per-instance value in [0, 1) from the record index. Keyed on the instance rather
// than the frame, so thinning a band is stable under motion instead of flickering.
fn instanceHash(i: u32) -> f32 {
    var h = i * 0x9E3779B1u;
    h = h ^ (h >> 15u);
    h = h * 0x2C1B3C6Du;
    h = h ^ (h >> 12u);
    return f32(h >> 8u) / 16777216.0;
}

// ---- classification -----------------------------------------------------------------------------

@compute @workgroup_size(64)
fn cs_cull_classify(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= cullParams.counts.x) { return; }
    let r = records[i];
    let center = (cullParams.objectToWorld * vec4<f32>(r.position.xyz, 1.0)).xyz;
    let s = abs(r.scale.xyz);
    let radius = cullParams.limits.z * max(max(s.x, s.y), s.z) * cullParams.limits.w;
    let dist = length(center - cullParams.cameraPos.xyz);
    // Projected radius in pixels: radius / distance * (height / (2 tan(fovY / 2))).
    let screenRadius = radius / max(dist, 1e-4) * cullParams.cameraPos.w;

    var culled = false;
    if (cullParams.flags.x != 0u) {
        for (var k = 0u; k < 6u; k = k + 1u) {
            let p = cullParams.planes[k];
            if (dot(p.xyz, center) + p.w < -radius) { culled = true; }
        }
        let maxDist = cullParams.limits.x;
        if (maxDist > 0.0 && dist - radius > maxDist) { culled = true; }
        let minRadius = cullParams.limits.y;
        if (minRadius > 0.0 && screenRadius < minRadius) { culled = true; }
    }

    // The composition's depth bands: `density` thins this band, `detail` moves the LOD ladder.
    let band = depthBand(dist);
    if (band.x < 1.0 && instanceHash(i) >= max(band.x, 0.0)) { culled = true; }
    let detail = max(band.y, 1e-3);

    // LOD ladder: a threshold of 0 ends it, so an unconfigured object stays at LOD0.
    var level = 0u;
    let lodCount = max(cullParams.counts.y, 1u);
    let byScreen = cullParams.flags.y != 0u;
    for (var k = 0u; k < 3u; k = k + 1u) {
        if (k + 1u >= lodCount) { break; }
        let t = thresholdAt(k);
        if (t <= 0.0) { break; }
        // Higher detail pushes the ladder further out (distance) or accepts a smaller sliver
        // before dropping a level (screen radius).
        var take = dist >= t * detail;
        if (byScreen) { take = screenRadius <= t / detail; }
        if (!take) { break; }
        level = k + 1u;
    }
    lodIndex[i] = select(level, kCulled, culled);
}

// ---- stable stream compaction (the structure of particles.wgsl) ---------------------------------

const kScanThreads: u32 = 256u;
const kScanElems: u32 = 4u;
const kScanBlock: u32 = 1024u; // kScanThreads * kScanElems

var<workgroup> scanShared: array<u32, 256>;

// Workgroup-wide exclusive prefix sum of one value per thread (Hillis-Steele, 8 rounds).
// Must be called in uniform control flow. Returns (exclusive prefix, workgroup total).
fn workgroupScan(tid: u32, value: u32) -> vec2<u32> {
    workgroupBarrier(); // previous call's readers are done with scanShared
    scanShared[tid] = value;
    workgroupBarrier();
    for (var offset = 1u; offset < kScanThreads; offset = offset << 1u) {
        var v = scanShared[tid];
        if (tid >= offset) { v += scanShared[tid - offset]; }
        workgroupBarrier();
        scanShared[tid] = v;
        workgroupBarrier();
    }
    let inclusive = scanShared[tid];
    let total = scanShared[kScanThreads - 1u];
    return vec2<u32>(inclusive - value, total);
}

// This thread's kScanElems membership flags for `level` (0 beyond the record count) and their sum.
fn loadFlags(base: u32, level: u32, out: ptr<function, array<u32, 4>>) -> u32 {
    var sum = 0u;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        var f = 0u;
        if (i < cullParams.counts.x && lodIndex[i] == level) { f = 1u; }
        (*out)[k] = f;
        sum += f;
    }
    return sum;
}

@compute @workgroup_size(256)
fn cs_cull_reduce(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    var f: array<u32, 4>;
    let local = loadFlags(wid.x * kScanBlock + tid * kScanElems, wid.y, &f);
    let r = workgroupScan(tid, local);
    if (tid == 0u) { blockSums[wid.y * cullParams.counts.w + wid.x] = r.y; }
}

@compute @workgroup_size(256)
fn cs_cull_top(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    let level = wid.y;
    let blocks = cullParams.counts.w;
    let levelBase = level * blocks;
    var carry = 0u;
    for (var chunk = 0u; chunk < blocks; chunk += kScanBlock) {
        let base = chunk + tid * kScanElems;
        var v: array<u32, 4>;
        var local = 0u;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            var sum = 0u;
            if (i < blocks) { sum = blockSums[levelBase + i]; }
            v[k] = sum;
            local += sum;
        }
        let r = workgroupScan(tid, local);
        var run = carry + r.x;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            if (i < blocks) { blockSums[levelBase + i] = run; }
            run += v[k];
        }
        carry += r.y;
    }
    if (tid == 0u) {
        let count = min(carry, cullParams.counts.x);
        // One indirect buffer holds every object's args, kMaxLodLevels slots of five u32 each,
        // indexed by the object's slot (flags.z, the same slot it writes its stats to).
        let a = (cullParams.flags.z * kMaxLodLevels + level) * 5u; // indexCount, instanceCount, firstIndex, baseVertex, firstInstance
        indirectArgs[a + 0u] = indexCountAt(level);
        indirectArgs[a + 1u] = count;
        indirectArgs[a + 2u] = 0u;
        indirectArgs[a + 3u] = 0u;
        indirectArgs[a + 4u] = 0u;
        let statsBase = cullParams.flags.z * kStatsStride;
        cullStats[statsBase + level] = count;
        if (level == 0u) {
            cullStats[statsBase + 4u] = cullParams.counts.x;
            cullStats[statsBase + 5u] = 1u;
        }
    }
}

@compute @workgroup_size(256)
fn cs_cull_scatter(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    let level = wid.y;
    let base = wid.x * kScanBlock + tid * kScanElems;
    var f: array<u32, 4>;
    let local = loadFlags(base, level, &f);
    let r = workgroupScan(tid, local);
    var rank = blockSums[level * cullParams.counts.w + wid.x] + r.x;
    let outBase = level * cullParams.counts.z;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        if (i < cullParams.counts.x) {
            if (f[k] != 0u) { visibleIndices[outBase + rank] = i; }
            rank += f[k];
        }
    }
}
