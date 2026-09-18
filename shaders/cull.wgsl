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
//   2. cs_cull_reduce    (blocks, levels) workgroups -- `levels` is lodCount, or 2 * lodCount when
//                        the shadow caster list is being built too: blockSums[level * blocks + b] = how many
//                        of block b's records are at `level`.
//   3. cs_cull_top       (1, levels) workgroups: exclusive scan of that level's blockSums in
//                        place; thread 0 writes the level's drawIndexedIndirect args and the
//                        object's stats slot.
//   4. cs_cull_scatter   (blocks, levels) workgroups: local exclusive scan + blockSums gives
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
// (read_write), 7 ShadowVolumes (uniform, frame-global). Mirrors
// rendering/procedural_renderer.hpp CullPassUniforms.
//
// ---- the second list: shadow casters (ADR-265 found it, ADR-287 built it) --------------------------------------------------
//
// A shadow caster list is a second cull, and this pass used to build one list that three passes
// read: the camera's, the depth prepass's and every cascade's. So an instance the camera could not
// see cast nothing, however plainly its shadow fell into shot -- 62,284 procedural instances on
// Glowmere multicam, 496 surviving the camera cull, and 496 drawn into the shadow maps.
//
// The two lists are built by ONE classify dispatch and ONE compaction, by doubling the level axis.
// Levels [0, lodCount) are the camera's list and levels [lodCount, 2 * lodCount) are the shadow
// caster list; `lodIndex` holds the camera classification at [0, count) and the shadow one at
// [count, 2 * count). The rung is computed once and shared, so an instance in both lists is on the
// same rung in both, and the two classifications differ in exactly one term: the camera frustum
// test becomes "some shadow view's frustum contains this sphere".
//
// Widening the single cull's frustum instead was considered and is wrong: it makes the *camera*
// pass draw everything the light can see, and the camera pass is the triangle-bound one.

struct InstanceRecord {
    position: vec4<f32>,  // xyz, w = density
    rotation: vec4<f32>,  // unit quaternion (x, y, z, w)
    scale: vec4<f32>,     // xyz, w = normalised index
    random: vec4<f32>,
    color: vec4<f32>,     // rgb, a = instance id
    emissive: vec4<f32>,  // rgb, a = extra lane
};

// ADR-108: one material part of a multi-material asset, served by another object's cull. The
// parts of an asset are one spatial instance set drawn several times, so the classification and
// the compaction happen once on the lead and each part's indirect args are written from the
// lead's per-level counts with the part's own index count.
struct CullFanout {
    indexCounts: vec4<u32>, // index count of each LOD level of THIS part's mesh
    slot: vec4<u32>,        // x = the part's indirect-args / stats slot
};

struct CullParams {
    objectToWorld: mat4x4<f32>,
    planes: array<vec4<f32>, 6>, // left, right, bottom, top, near, far; xyz = unit normal, w = d
    cameraPos: vec4<f32>,        // xyz = camera position, w = projScale (pixels per unit at 1 unit)
    limits: vec4<f32>,           // x = maxDistance, y = minScreenRadius, z = source radius, w = object scale
    // xyz = lodDistances (LOD0->1, 1->2, 2->3).
    // w = the radius the *ladder* measures size with, in source units, or 0 to use limits.z.
    //     See the note above cs_cull_classify: the rejection tests and the ladder are asking two
    //     different questions and need two different spheres.
    thresholds: vec4<f32>,
    counts: vec4<u32>,           // x = record count, y = lod count, z = visible stride, w = scan blocks
    flags: vec4<u32>,            // x = cull enabled, y = thresholds are screen radii, z = stats slot, w = 0
    indexCounts: vec4<u32>,      // index count of each level's mesh (for the indirect args)
    stability: vec4<f32>,        // x = per-instance spread, y = hysteresis dead zone (ADR-082)
    // ADR-038 depth layers, as (start, end, density, detail); the count is flags.w.
    depthLayers: array<vec4<f32>, 6>,
    // ADR-108. x = how many entries of `fanout` are live (0 = this cull serves only itself).
    fanoutInfo: vec4<u32>,
    fanout: array<CullFanout, 7>,
    // x = 1 when this dispatch also builds the shadow caster list (0 = camera list only, which is
    // every object in a frame with no shadow views and every object that does not cast).
    // y = the indirect-args / stats slot the shadow list's levels are written to.
    shadowInfo: vec4<u32>,
};

// The frame's shadow views as plane sets, world space. Frame-global rather than per object --
// every object's cull tests the same volumes -- so it is one buffer written once per frame instead
// of 768 bytes appended to every object's uniform.
const kMaxShadowCullViews: u32 = 8u; // rendering::kMaxShadowViews

struct ShadowVolumes {
    info: vec4<u32>,                    // x = live view count (0 = no shadow maps this frame)
    planes: array<vec4<f32>, 48>,       // kMaxShadowCullViews * 6, same convention as `planes`
};

@group(0) @binding(0) var<uniform> cullParams: CullParams;
@group(0) @binding(1) var<storage, read> records: array<InstanceRecord>;
@group(0) @binding(2) var<storage, read_write> lodIndex: array<u32>;
@group(0) @binding(3) var<storage, read_write> blockSums: array<u32>;
@group(0) @binding(4) var<storage, read_write> visibleIndices: array<u32>;
@group(0) @binding(5) var<storage, read_write> indirectArgs: array<u32>;
@group(0) @binding(6) var<storage, read_write> cullStats: array<u32>;
@group(0) @binding(7) var<uniform> shadowVolumes: ShadowVolumes;

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

fn componentAt(c: vec4<u32>, level: u32) -> u32 {
    var v = c.x;
    if (level == 1u) { v = c.y; }
    if (level == 2u) { v = c.z; }
    if (level == 3u) { v = c.w; }
    return v;
}

fn indexCountAt(level: u32) -> u32 {
    return componentAt(cullParams.indexCounts, level);
}

const kMaxCullFanout: u32 = 7u; // rendering/procedural_renderer.hpp kMaxCullFanout

// Writes one object slot's args and stats for `level`. The lead writes its own, then one per
// material part sharing its decision -- same instance count, each part's own index count.
fn writeLevel(slot: u32, level: u32, indexCount: u32, count: u32, records: u32) {
    let a = (slot * kMaxLodLevels + level) * 5u; // indexCount, instanceCount, firstIndex, baseVertex, firstInstance
    indirectArgs[a + 0u] = indexCount;
    indirectArgs[a + 1u] = count;
    indirectArgs[a + 2u] = 0u;
    indirectArgs[a + 3u] = 0u;
    indirectArgs[a + 4u] = 0u;
    let statsBase = slot * kStatsStride;
    cullStats[statsBase + level] = count;
    if (level == 0u) {
        cullStats[statsBase + 4u] = records;
        cullStats[statsBase + 5u] = 1u;
    }
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

// A second stable per-instance number, decorrelated from the one density thinning uses.
//
// It has to be decorrelated: if the same hash decided both which instances survive thinning and
// which ones change LOD early, the two would agree, and the instances that swap first would be
// exactly the ones thinning already removed -- so the spread would do nothing where it is needed.
fn ladderHash(i: u32) -> f32 {
    return instanceHash(i * 0x9E3779B9u + 0x85EBCA6Bu);
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

    // Two spheres, because there are two questions.
    //
    // `radius` above is the conservative one: it is centred on the record position, which is where
    // this pass puts it, so it has to reach the furthest corner of the source *from the source's
    // own origin*. That is the right bound for a rejection -- an instance must never be discarded
    // while some of its geometry is on screen -- and the Visibility Lab corrected it to exactly
    // that (rendering::sourceCullRadius).
    //
    // It is the wrong number for the **ladder**, which is not asking whether anything is on screen
    // but how large the thing looks, and the difference between the two rules is how far the artist
    // put the geometry from its origin. Measured over Glowmere's thirteen scatter layers, the same
    // authored threshold of 28 px fires when an object's drawn radius is 11.1..18.7 px under the
    // conservative rule and 14.8..22.6 px under the tight one -- so a tree and a fern, side by side
    // at the same apparent size, change mesh at sizes that differ by a factor which says nothing
    // about either of them (tests/unit/test_lod_ladder.cpp, "What 28 px means, per production
    // layer"). `lodRadius` is the tight sphere about the source's own box, which is what the
    // thresholds were authored against and is a property of the shape rather than of the origin.
    //
    // 0 means the renderer did not supply one, and the ladder then measures with the cull's sphere,
    // which is what this pass did before the two were separated.
    let lodSource = select(cullParams.thresholds.w, cullParams.limits.z, cullParams.thresholds.w <= 0.0);
    let lodRadius = lodSource * max(max(s.x, s.y), s.z) * cullParams.limits.w;
    let lodScreenRadius = lodRadius / max(dist, 1e-4) * cullParams.cameraPos.w;

    // Hysteresis and per-instance spread (ADR-082).
    //
    // Every threshold below used to be a hard binary comparison, evaluated fresh from the current
    // camera with no memory of what this instance decided last frame. Two artefacts follow, and
    // together they are what the brief calls popping:
    //
    //   * An instance whose projected radius sits near a threshold flips state every time the
    //     camera breathes, because the comparison has no dead zone.
    //   * Every instance at a given radius crosses its threshold on the *same frame*, so a whole
    //     band of the world changes mesh at once.
    //
    // The first wants memory; the second wants the instances to disagree with each other. Both are
    // available without allocating anything. `lodIndex` is grow-only and is written by this pass
    // and no other, so last frame's level is still sitting in it when this frame starts -- and
    // `ladderHash` gives each instance a stable offset of its own.
    //
    // Zero hysteresis reproduces the old behaviour exactly, which is what the tests rely on.
    // The two are deliberately separate settings, because they differ in kind.
    //
    // `spread` gives every instance its own slightly offset threshold. It is a pure function of
    // the instance index, so it is exactly as deterministic as the old hard comparison, and it is
    // what stops a whole band of the world changing mesh on the same frame: no instance is ever
    // half-way between two meshes, but the *population* is. This is the crossfade, and it is on by
    // default.
    //
    // `hysteresis` is the dead zone, and it is the one that reads `previous`. That makes the
    // image depend on the camera's history rather than only on where the camera is now, so it is
    // off by default and opt-in -- the divergence is bounded to instances within the dead zone of
    // a threshold, which are by definition at a size where the two levels are near
    // indistinguishable, but it is a divergence and this engine promises frame-independence.
    let previous = lodIndex[i];
    let spreadAmount = clamp(cullParams.stability.x, 0.0, 0.5);
    let hysteresis = clamp(cullParams.stability.y, 0.0, 0.5);
    let spread = 1.0 + (ladderHash(i) - 0.5) * spreadAmount;

    // Three rejections, kept apart because two lists are being built from them. `limitsCulled` is
    // the distance / screen-size / density half, which is the same question for the camera and for
    // a shadow view; `cameraOutside` and `shadowOutside` are the two volume tests, and they are the
    // only thing the lists differ by.
    var limitsCulled = false;
    var cameraOutside = false;
    var shadowOutside = false;
    if (cullParams.flags.x != 0u) {
        for (var k = 0u; k < 6u; k = k + 1u) {
            let p = cullParams.planes[k];
            if (dot(p.xyz, center) + p.w < -radius) { cameraOutside = true; }
        }
        let maxDist = cullParams.limits.x;
        if (maxDist > 0.0 && dist - radius > maxDist) { limitsCulled = true; }
        let minRadius = cullParams.limits.y;
        if (minRadius > 0.0) {
            // It takes a larger radius to come back than it does to disappear, so an instance
            // sitting on the bar stays where it is instead of strobing.
            let bar = minRadius * spread;
            let wasCulled = previous == kCulled;
            let limit = select(bar * (1.0 - hysteresis), bar * (1.0 + hysteresis), wasCulled);
            if (screenRadius < limit) { limitsCulled = true; }
        }
        // ADR-287's second list. A caster is kept when SOME shadow view's frustum contains it: the
        // shadow passes draw one list into every view and each view rejects what it cannot see on
        // its own, so the union is the right question here. No views is no maps, and the loop then
        // leaves `seen` false and rejects everything, which is what a frame with shadows switched
        // off wants.
        if (cullParams.shadowInfo.x != 0u) {
            let views = min(shadowVolumes.info.x, kMaxShadowCullViews);
            var seen = false;
            for (var v = 0u; v < kMaxShadowCullViews; v = v + 1u) {
                if (v >= views) { break; }
                var inside = true;
                for (var k = 0u; k < 6u; k = k + 1u) {
                    let p = shadowVolumes.planes[v * 6u + k];
                    if (dot(p.xyz, center) + p.w < -radius) { inside = false; }
                }
                seen = seen || inside;
            }
            shadowOutside = !seen;
        }
    }

    // The composition's depth bands: `density` thins this band, `detail` moves the LOD ladder.
    let band = depthBand(dist);
    if (band.x < 1.0 && instanceHash(i) >= max(band.x, 0.0)) { limitsCulled = true; }
    let detail = max(band.y, 1e-3);
    let culled = limitsCulled || cameraOutside;

    // LOD ladder: a threshold of 0 ends it, so an unconfigured object stays at LOD0.
    var level = 0u;
    let lodCount = max(cullParams.counts.y, 1u);
    let byScreen = cullParams.flags.y != 0u;
    // A culled instance has no level to remember, so it re-enters at whatever this frame says.
    let hadLevel = select(previous, 0u, previous == kCulled);
    for (var k = 0u; k < 3u; k = k + 1u) {
        if (k + 1u >= lodCount) { break; }
        let t0 = thresholdAt(k);
        if (t0 <= 0.0) { break; }
        // Higher detail pushes the ladder further out (distance) or accepts a smaller sliver
        // before dropping a level (screen radius).
        let alreadyTaken = hadLevel > k;
        var take: bool;
        if (byScreen) {
            let t = t0 / detail * spread;
            // Already down a level: keep it until the radius grows back past the upper edge.
            take = lodScreenRadius <= select(t * (1.0 - hysteresis), t * (1.0 + hysteresis), alreadyTaken);
        } else {
            let t = t0 * detail * spread;
            take = dist >= select(t * (1.0 + hysteresis), t * (1.0 - hysteresis), alreadyTaken);
        }
        if (!take) { break; }
        level = k + 1u;
    }
    lodIndex[i] = select(level, kCulled, culled);
    // The shadow half, at [count, 2 * count). The level is the one computed above -- the ladder is
    // the camera's for both lists, so a caster is rasterised at the mesh the frame draws, and the
    // hysteresis memory the ladder reads stays the single history at [0, count).
    if (cullParams.shadowInfo.x != 0u) {
        lodIndex[cullParams.counts.x + i] = select(level, kCulled, limitsCulled || shadowOutside);
    }
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

// Which half of `lodIndex` a compaction level reads, and which rung it is looking for there.
// Levels [0, lodCount) are the camera's list at [0, count); levels [lodCount, 2 * lodCount) are the
// shadow caster list at [count, 2 * count).
struct ListSlice {
    base: u32,  // offset into lodIndex
    rung: u32,  // the value a member carries
};

fn sliceOf(level: u32) -> ListSlice {
    let lodCount = max(cullParams.counts.y, 1u);
    var out: ListSlice;
    out.base = 0u;
    out.rung = level;
    if (level >= lodCount) {
        out.base = cullParams.counts.x;
        out.rung = level - lodCount;
    }
    return out;
}

// This thread's kScanElems membership flags for `level` (0 beyond the record count) and their sum.
fn loadFlags(base: u32, level: u32, out: ptr<function, array<u32, 4>>) -> u32 {
    let slice = sliceOf(level);
    var sum = 0u;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        var f = 0u;
        if (i < cullParams.counts.x && lodIndex[slice.base + i] == slice.rung) { f = 1u; }
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
        // indexed by the object's slot (flags.z, the same slot it writes its stats to). The shadow
        // caster list's levels go to a second slot (shadowInfo.y) in the same buffer rather than to
        // a buffer of their own: ADR-051 measured the cost per distinct indirect *buffer* a pass
        // reads, not per draw, and this frame already reads one in five passes.
        let slice = sliceOf(level);
        let shadowHalf = slice.rung != level;
        let slot = select(cullParams.flags.z, cullParams.shadowInfo.y, shadowHalf);
        writeLevel(slot, slice.rung, indexCountAt(slice.rung), count, cullParams.counts.x);
        // ADR-108: the same decision, emitted for every material part of this asset. One spatial
        // instance, culled once; the parts differ only in which mesh the draw reads. A part's
        // shadow slot is `slot.y`, filled in beside its camera slot.
        let fanoutCount = cullParams.fanoutInfo.x;
        for (var f = 0u; f < kMaxCullFanout; f = f + 1u) {
            if (f >= fanoutCount) { break; }
            let part = cullParams.fanout[f];
            let partSlot = select(part.slot.x, part.slot.y, shadowHalf);
            writeLevel(partSlot, slice.rung, componentAt(part.indexCounts, slice.rung), count,
                       cullParams.counts.x);
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
