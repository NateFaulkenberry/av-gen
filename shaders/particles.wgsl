// GPU particle system (ADR-015, revision 2026-09-08: deterministic compaction).
//
// Fixed pool of `capacity` slots. Every list this shader produces is a pure function of
// (slot index, frame index, parameters): there are no atomics anywhere, so which slot a spawn
// takes, its random seeds, and the draw order are bit-identical from run to run.
//
// Pass order per system per frame (one compute pass; dispatches in a pass are ordered and their
// storage writes are visible to later dispatches):
//   1. cs_emit          thread i < min(emitCount, counters.deadCount) initialises slot
//                       deadList[i]. deadList/deadCount are the previous frame's compaction
//                       output (or the CPU reset: deadList = 0..capacity-1, deadCount = capacity).
//   2. cs_simulate      every slot: age, kill, integrate. Writes scratch[slot] = 1 if alive else 0.
//   3. cs_scan_reduce   one workgroup per block of kScanBlock slots: block sum b = alive count.
//   4. cs_scan_top      one workgroup: exclusive scan of the block sums in place (looping over chunks
//                       of kScanBlock); thread 0 writes counters.aliveCount, counters.deadCount
//                       = capacity - aliveCount, and the indirect draw args.
//   5. cs_scan_scatter  one workgroup per block: local exclusive scan + block sum b gives every
//                       alive slot its rank r; aliveList[r] = slot, and every dead slot its rank
//                       slot - r; deadList[slot - r] = slot. Both lists are therefore in slot order.
//   Render: vs_particle reads aliveList[instance_index], so instances are drawn in slot order.
// Randomness is a hash of (slot, frameNonce, seed, salt): keyed to the timeline second (ADR-360),
// not to the frame index, so the same second seeds the same spawns wherever the render began.
//
// Field forces (ADR-025): params.fieldForces holds up to 4 (mode, slot, strength, mix) + (axis, 0)
// pairs sampled from the FieldBlock (binding 8, fields.wgsl) at the particle position after the
// built-in forces: Force / Turbulence add fieldVector * strength * dt to the velocity, Velocity
// blends the velocity towards fieldVector * strength by `mix`, Kill removes the particle where
// fieldScalar >= 0.5. Scalar fields act along the force's axis (fieldScalar * axis).
//
// Velocity stretching, trails and curves (ADR-040):
//   * vs_particle stretches the billboard along the *screen projection* of the simulated velocity
//     by stretchLength() = clamp(projectedSpeed * shutterSeconds * stretch, 0, stretchMax), with
//     an added length below stretchMin dropped so slow particles stay perfectly round. The width
//     stays the particle size, so the quad becomes a streak and the radial falloff an ellipse.
//   * vs_ribbon draws an opt-in per-particle history ring (binding 10, params.trail.x points,
//     recorded every params.trail.y-th step by cs_simulate *before* integration) as a
//     camera-facing ribbon: point 0 is the live position, point j the j-th newest history entry.
//     Width and colour taper along the length. History is simulation state, so it is deterministic.
//   * size, colour and opacity read small keyframed curves (params.sizeKeys / colorKeys /
//     opacityKeys, up to 8 keys each) when their key count is >= 2, and fall back to the linear
//     start-to-end ramp otherwise, so pre-curve scenes are bit-identical.
//   * fs_particle multiplies by the volumetric transmittance to the particle's own depth divided
//     by the transmittance to the depth the volume march will use for that pixel, so that after
//     the volume composite (which multiplies everything by the latter) the particle is fogged to
//     where it actually is instead of to the surface behind it.
//   * cs_glow_reduce / cs_glow_top reduce the alive emissive particles to one aggregate sphere
//     (centroid, spread radius, mean colour, total power) that volume.wgsl adds as an emission
//     term, so sparks light the dust around them. The reduction is a fixed-order tree plus a
//     serial loop, so it is deterministic.
//
// Spline emitter (ADR-026, spline.wgsl): shape 4 spawns at S(u * length).position of the spline
// slot params.fieldInfo.y - 1 (u = a per-spawn hash, so spawns cover the whole curve evenly by
// arc length), jittered inside a sphere of radius extent.x; `direction` is read in the spline
// frame (x = binormal, y = normal, z = tangent), so the default (0, 1, 0) rises along the
// sample normal and (0, 0, 1) follows the tangent. The CPU falls back to the Point shape when
// the spline is missing.
// ADR-370: the wind field itself, with no bindings in it. `wind.wgsl` is the frame-bound
// wrapper and this shader has no frame group, so it takes the arithmetic and passes its own
// copy of the sixteen floats. There used to be a hand-maintained transliteration here
// instead, and it had already dropped `WindSample::phase`.
#include "height_fog.wgsl"
#include "wind_field.wgsl"
#include "fields.wgsl"
#include "spline.wgsl"

struct Particle {
    position: vec3<f32>,
    age: f32,
    velocity: vec3<f32>,
    life: f32,       // 0 = dead
    seed: f32,
    size: f32,
    trailWrites: f32, // history samples written since birth (ADR-040); 0 at emit
    // ADR-520: which life the particle is living. 0 = the primary one, 1 = the splash ring it
    // became when it hit the ground. This was `pad`, so the struct's size and layout are
    // unchanged and every existing pool is bit-identical: a system with no collision response
    // writes 0 here exactly where it used to write 0 there.
    stage: f32,
    // Scatter-anchored clusters (scene::ScatterAnchor): the crown centre this particle was born
    // round, w = 1. It is kept per particle rather than looked up in the table each step because
    // the table follows the camera: a tree that leaves it must not drag its swarm across the valley
    // to whichever tree took its slot. All zero for every other system, and then the attractor
    // below is `params.attractor`, exactly as it always was.
    home: vec4<f32>,
};

struct Params {
    viewProj: mat4x4<f32>,
    prevViewProj: mat4x4<f32>, // ADR-035: last frame's, for the velocity target
    cameraRight: vec4<f32>,
    cameraUp: vec4<f32>,
    cameraPos: vec4<f32>,   // xyz = eye position, w = shutter open time in seconds (ADR-037)
    emitterPos: vec4<f32>,  // xyz, w = shape (0 point, 1 sphere, 2 disc, 3 box, 4 spline)
    extent: vec4<f32>,      // xyz, w = spread
    direction: vec4<f32>,   // xyz, w = drag
    speedLife: vec4<f32>,   // speedMin, speedMax, lifeMin, lifeMax
    gravity: vec4<f32>,     // xyz, w = turbulence strength
    turb: vec4<f32>,        // scale, speed, softness, emissive
    attractor: vec4<f32>,   // xyz, w = strength
    attractor2: vec4<f32>,  // radius, orbit, sizeStart, sizeEnd
    colorStart: vec4<f32>,
    colorEnd: vec4<f32>,
    sim: vec4<f32>,         // dt, time, frameIndex, seed
    // ADR-360: the spawn key. x = FrameTime::frameNonce(), which is derived from the position on
    // the TIMELINE (round(renderTime * 240), wrapped at 2^24) and not from how many frames have
    // been drawn since this render started. `sim.z` is still the frame index and is still what the
    // trail stride counts, because "every Nth frame" is a rate and not a seed; nothing else in this
    // shader may key randomness to it. yzw = 0.
    nonce: vec4<u32>,
    stretch: vec4<f32>,     // velocityStretch, stretchMax, stretchMin, 0
    trail: vec4<f32>,       // history points (0 = trails off), stride, width, taper
    trail2: vec4<f32>,      // tail alpha fraction, tail tint rgb
    fog: vec4<f32>,         // volume density, fog height, height falloff, absorption
    fog2: vec4<f32>,        // volume max distance, fog coupling 0..1, glow strength, linear depth 1/0
    fog3: vec4<f32>,        // ADR-568: x = fogUpperDensity, y = fogHeightCurve; ADR-715: z = fogGroundFollow, w = 0
    terrain0: vec4<f32>,    // ADR-715: the terrain height's placement, as FrameUniforms::terrainMap0
    terrain1: vec4<f32>,    // ADR-715: ... and terrainMap1 (w = 1 when there is a terrain)
    // ADR-370: leaf cards. x = shape (0 round, 1 leaf), y = tumble rate (rad/s), z = leaf aspect
    // (length over width), w = two-sided shading depth. All zero is the round dot this always was.
    leaf: vec4<f32>,
    // ADR-370: the ADR-055 wind field, copied into the particle uniforms rather than reached
    // through the frame group. `particles.wgsl` binds no frame uniform at all -- it never has --
    // and adding one would change the bind-group layout of every particle pipeline for four
    // vectors. Same bytes and the same meaning as `FrameUniforms`', and -- since the copy of the
    // ARITHMETIC that used to sit below them was deleted -- literally the same function:
    // `wind_field.wgsl`'s `windSampleFrom`, which is what `wind.wgsl` calls too.
    windDir: vec4<f32>,
    windRegion: vec4<f32>,
    windGust: vec4<f32>,
    windTurb: vec4<f32>,
    windMix: vec4<f32>, // x = how much of the flow a particle catches, yzw = 0
    // ---- ADR-520 ----
    volume: vec4<f32>,   // volumeFollow.xyz (per-axis 0..1), w = 1 when the box wraps
    collide: vec4<f32>,  // response (0 none, 1 kill, 2 bounce, 3 splash), height, restitution, splashSize
    collide2: vec4<f32>, // splashLifetime, ringThickness, dragSizeBias, 0
    pulse: vec4<f32>,    // rate (Hz, 0 = off), depth, sync, sharpness
    cluster: vec4<f32>,  // cluster count (0 = off), cluster radius, pause rate, pause fraction
    scatter: vec4<f32>,  // scatter strength, HG anisotropy, size variance, size skew
    sun: vec4<f32>,      // xyz = unit direction TOWARDS the key light, w = 1 when it is usable
    sunColor: vec4<f32>, // rgb = the key light's colour times its intensity
    curves: vec4<u32>,      // size key count, colour key count, opacity key count, glow slot
    counts: vec4<u32>,      // emitCount, capacity, blend (0 additive, 1 alpha), scan blocks
    fieldInfo: vec4<u32>,   // x = field force count, y = spline slot + 1 (0 = none)
    fieldForces: array<vec4<f32>, 8>, // per force: (mode, slot, strength, mix), (axis.xyz, 0)
    sizeKeys: array<vec4<f32>, 8>,    // (t, value, 0, 0)
    opacityKeys: array<vec4<f32>, 8>, // (t, value, 0, 0)
    colorKeys: array<vec4<f32>, 8>,   // (t, r, g, b)
    // Scatter-anchored clusters: x = anchors in the table, y = 1 when the system is anchored.
    anchorInfo: vec4<f32>,
    anchors: array<vec4<f32>, 64>,    // crown centres chosen on the CPU for this camera, w = 1
};

struct DrawArgs {
    vertexCount: u32,
    instanceCount: u32,
    firstVertex: u32,
    firstInstance: u32,
};

// Plain values written by one thread of cs_scan_top: the counters cs_emit reads next frame, then
// the two indirect draws (the stretched billboards at byte 16, the ribbons at byte 32). Counters
// and draw arguments share one buffer because the Metal adapter allows only ten storage buffers
// per stage and the compute pass needs the trail history and the glow scratch as well.
struct Counters {
    deadCount: u32,
    aliveCount: u32,
    pad0: u32,
    pad1: u32,
    billboard: DrawArgs,
    ribbon: DrawArgs,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read_write> deadList: array<u32>;
@group(0) @binding(3) var<storage, read_write> counters: Counters;
@group(0) @binding(4) var<storage, read_write> aliveList: array<u32>;
// Trail history ring (ADR-040): params.trail.x entries per slot, xyz = position, w = 1 written.
@group(0) @binding(5) var<storage, read_write> history: array<vec4<f32>>;
// Emissive aggregate scratch (ADR-040): two vec4 per scan block, then the system's own two-vec4
// result at index 2 * blocks, which the CPU copies into the shared table volume.wgsl reads.
@group(0) @binding(6) var<storage, read_write> glowScratch: array<vec4<f32>>;
// Compaction scratch: flags (1 = alive after simulate) at [0, capacity), then one sum per scan
// block at [capacity, capacity + blocks). One buffer, again for the ten-storage-buffer limit.
@group(0) @binding(7) var<storage, read_write> scratch: array<u32>;
@group(0) @binding(8) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(9) var<storage, read> splineTable: SplineTable;
// Read-only views for the render stage (same bindings, used only by the vertex stages).
@group(0) @binding(1) var<storage, read> particlesRead: array<Particle>;
@group(0) @binding(4) var<storage, read> aliveRead: array<u32>;
@group(0) @binding(5) var<storage, read> historyRead: array<vec4<f32>>;
// The R32F view distance of the opaque scene (ADR-035); 1e7 where nothing was drawn.
@group(0) @binding(13) var linearDepthTex: texture_2d<f32>;
// ADR-715: the terrain's baked height, for a fog layer that follows the ground. A placeholder that
// is never read when `params.terrain1.w` is 0.
@group(0) @binding(12) var terrainHeightTex: texture_2d<f32>;

// ---- hashing / noise ----------------------------------------------------------------------

// pcg3d comes from noise.wgsl (through fields.wgsl); the built-in turbulence keeps its own
// hash3-based value noise (turbValueNoise / turbCurl) so existing scenes stay bit-identical.
fn rand3(slot: u32, frame: u32, salt: u32) -> vec3<f32> {
    let h = pcg3d(vec3<u32>(slot, frame + u32(params.sim.w) * 7919u, salt));
    return vec3<f32>(h) * (1.0 / 4294967296.0);
}

// ADR-520: a hash that does NOT mix the frame. A cluster centre has to be the same place at every
// time the shot can be scrubbed to, so a firefly swarm is a swarm over a second rather than a
// uniform field resampled sixty times. rand3 above deliberately mixes `frame`, which is exactly
// wrong here, and using it was the first version of this and the clusters strobed.
fn clusterRand(cluster: u32, salt: u32) -> vec3<f32> {
    let h = pcg3d(vec3<u32>(cluster * 2654435761u, u32(params.sim.w) * 7919u, salt));
    return vec3<f32>(h) * (1.0 / 4294967296.0);
}

// ADR-520: where the emitter box actually is this frame. `volumeFollow` is per axis so rain can
// track the camera across the valley (x, z) while staying pinned to the sky (y) if that is what
// the author wants -- and all-zero, the default, is the world-anchored emitter this always had.
fn emitterCentre() -> vec3<f32> {
    return params.emitterPos.xyz + params.cameraPos.xyz * params.volume.xyz;
}

fn hash3(p: vec3<f32>) -> f32 {
    let q = fract(p * vec3<f32>(0.1031, 0.1030, 0.0973));
    let d = dot(q, vec3<f32>(q.y + 33.33, q.z + 33.33, q.x + 33.33));
    let r = q + vec3<f32>(d);
    return fract((r.x + r.y) * r.z);
}

fn turbValueNoise(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let n000 = hash3(i);
    let n100 = hash3(i + vec3<f32>(1.0, 0.0, 0.0));
    let n010 = hash3(i + vec3<f32>(0.0, 1.0, 0.0));
    let n110 = hash3(i + vec3<f32>(1.0, 1.0, 0.0));
    let n001 = hash3(i + vec3<f32>(0.0, 0.0, 1.0));
    let n101 = hash3(i + vec3<f32>(1.0, 0.0, 1.0));
    let n011 = hash3(i + vec3<f32>(0.0, 1.0, 1.0));
    let n111 = hash3(i + vec3<f32>(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// Three decorrelated potentials; curl of the potential field is divergence-free (Bridson 2007).
fn potential(p: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(turbValueNoise(p), turbValueNoise(p + vec3<f32>(31.4, 47.1, 12.9)), turbValueNoise(p + vec3<f32>(-17.2, 5.3, 29.8))) - vec3<f32>(0.5);
}

// ADR-370: the frame's wind, as this shader's uniforms carry it. `particles.wgsl` binds no frame
// uniform at all -- it never has -- so the field is copied into these five vectors by
// `ParticleRenderer::update` straight out of `ParticleFrameContext::wind`, which is the same
// `wind::WindUniforms` `FrameUniforms` gets. Gathering them is all this function does; the
// sampling is `wind_field.wgsl`'s, which is also what `wind.wgsl` calls.
fn particleWindField() -> WindField {
    var w: WindField;
    w.dir = params.windDir;
    w.region = params.windRegion;
    w.gust = params.windGust;
    w.turbulence = params.windTurb;
    return w;
}

fn turbCurl(p: vec3<f32>) -> vec3<f32> {
    let e = 0.05;
    let dx = potential(p + vec3<f32>(e, 0.0, 0.0)) - potential(p - vec3<f32>(e, 0.0, 0.0));
    let dy = potential(p + vec3<f32>(0.0, e, 0.0)) - potential(p - vec3<f32>(0.0, e, 0.0));
    let dz = potential(p + vec3<f32>(0.0, 0.0, e)) - potential(p - vec3<f32>(0.0, 0.0, e));
    let inv = 1.0 / (2.0 * e);
    return vec3<f32>(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) * inv;
}

fn sphereDir(r: vec2<f32>) -> vec3<f32> {
    let z = 1.0 - 2.0 * r.x;
    let s = sqrt(max(0.0, 1.0 - z * z));
    let phi = 6.28318530 * r.y;
    return vec3<f32>(s * cos(phi), s * sin(phi), z);
}

// ---- emit / simulate --------------------------------------------------------------------------

@compute @workgroup_size(64)
fn cs_emit(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    // Spawn i takes the i-th free slot (lowest slot first). Clamped to last frame's dead count.
    if (i >= params.counts.x || i >= counters.deadCount) { return; }
    let slot = deadList[i];
    // ADR-360. This used to be `u32(params.sim.z)` -- the frame index -- so the same second of the
    // same scene seeded different spawns in a full render, in a re-render of a section, and in the
    // live application, whose RealtimeClock never resets the counter at all. Measured before this
    // changed: two fresh renderers handed the same renderTime and frame indices 0 and 97 differed
    // over 7656 of 147456 bytes, max delta 235.
    let frame = params.nonce.x;
    let r1 = rand3(slot, frame, 1u);
    let r2 = rand3(slot, frame, 2u);
    let r3 = rand3(slot, frame, 3u);

    var p: Particle;
    let shape = params.emitterPos.w;
    let centre = emitterCentre();
    var offset = vec3<f32>(0.0);
    var baseDir = normalize(params.direction.xyz + vec3<f32>(1e-5, 0.0, 0.0));
    if (shape > 3.5) {
        // spline: S(u * length) + a sphere jitter of extent.x; direction in the spline frame
        let splineSlot = i32(params.fieldInfo.y) - 1;
        let u = rand3(slot, frame, 4u).x;
        let s = splineSample(splineSlot, u * splineLength(splineSlot));
        offset = s.position - params.emitterPos.xyz + sphereDir(r1.xy) * pow(r1.z, 1.0 / 3.0) * params.extent.x;
        let dir = params.direction.xyz;
        baseDir = normalize(s.binormal * dir.x + s.normal * dir.y + s.tangent * dir.z + vec3<f32>(1e-5, 0.0, 0.0));
    } else if (shape > 2.5) {
        offset = (r1 * 2.0 - 1.0) * params.extent.xyz;
    } else if (shape > 1.5) {
        // A disc in the plane the emitter faces, with extent.x and extent.z as its two radii. It
        // used to be nailed to XZ and to read only extent.x, which made a disc emitter unable to
        // point anywhere and unable to be an ellipse -- and the shipped scenes had already been
        // authored as though it could be: `[11, 1, 11]` and `[46, 2, 46]` are somebody writing two
        // radii and a thickness into a field that was using one of them.
        //
        // The basis is built so that the default +Y direction gives exactly the old X/Z plane, in
        // that order, which is why the helper axis is +Z rather than the usual +Y: every scene that
        // emitted a disc before this renders the same particles afterwards.
        let n = normalize(params.direction.xyz + vec3<f32>(1e-5, 0.0, 0.0));
        let helper = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(n.y) > 0.99);
        let u = normalize(cross(n, helper));
        let v = cross(u, n);
        let ang = 6.28318530 * r1.x;
        let rad = sqrt(r1.y);
        offset = u * (cos(ang) * rad * params.extent.x) + v * (sin(ang) * rad * params.extent.z);
    } else if (shape > 0.5) {
        offset = sphereDir(r1.xy) * pow(r1.z, 1.0 / 3.0) * params.extent.x;
    }
    // ADR-520: clustering. The offset the emitter shape just produced is re-used as the CLUSTER's
    // place rather than the particle's -- so the clusters are distributed exactly the way the
    // author shaped the emitter, and the particles are distributed about them. Drawing the cluster
    // centre from its own uniform box instead would have quietly ignored the emitter shape, which
    // is how a disc emitter ends up spawning a cube of fireflies.
    let clusterCount = u32(max(params.cluster.x, 0.0));
    var home = vec4<f32>(0.0);
    if (params.anchorInfo.y > 0.5) {
        // Scatter-anchored: the cluster centres are real trees (the CPU's table for this camera),
        // not `clusterRand` points. Which tree a spawn joins is spawn randomness -- keyed to the
        // frame like every other spawn roll -- but the tree itself is a place in the world and the
        // same place at every time the shot can be scrubbed to. The CPU emits nothing when the
        // table is empty, so `n` is never zero here; the max() only keeps the modulo defined.
        let n = max(u32(params.anchorInfo.x), 1u);
        let pick = min(u32(rand3(slot, frame, 5u).x * f32(n)), n - 1u);
        let a = params.anchors[pick].xyz;
        let jitter = sphereDir(r1.xy) * pow(r1.z, 1.0 / 3.0) * params.cluster.y;
        offset = a + jitter - centre;
        home = vec4<f32>(a, 1.0);
    } else if (clusterCount > 0u) {
        let c = slot % clusterCount;
        let cr = clusterRand(c, 17u);
        var cOffset = (cr * 2.0 - 1.0) * params.extent.xyz;
        if (shape > 0.5 && shape < 1.5) {
            cOffset = sphereDir(cr.xy) * pow(cr.z, 1.0 / 3.0) * params.extent.x;
        }
        let jitter = sphereDir(r1.xy) * pow(r1.z, 1.0 / 3.0) * params.cluster.y;
        offset = cOffset + jitter;
    }
    p.position = centre + offset;
    p.home = home;
    let randomDir = sphereDir(r2.xy);
    let dir = normalize(mix(baseDir, randomDir, params.extent.w) + vec3<f32>(1e-5));
    let speed = mix(params.speedLife.x, params.speedLife.y, r2.z);
    p.velocity = dir * speed;
    p.age = 0.0;
    // lifeMin + (lifeMax - lifeMin) * r rather than mix(): exact when lifeMin == lifeMax, so a
    // fixed lifetime dies on a predictable frame.
    p.life = params.speedLife.z + (params.speedLife.w - params.speedLife.z) * r3.x;
    p.seed = r3.y;
    // ADR-520: layered scale. variance 0.3 with skew 1 is exactly the mix(0.7, 1.3, r) this was,
    // bit for bit, so nothing that does not ask changes; skew above 1 pushes the distribution down
    // and gives a population of many small and a few large, which is what drops, flakes, motes and
    // embers actually look like and what uniform randomness never does.
    let sizeVariance = clamp(params.scatter.z, 0.0, 1.0);
    let sizeSkew = max(params.scatter.w, 0.05);
    let sizeRoll = select(pow(r3.z, sizeSkew), r3.z, abs(sizeSkew - 1.0) < 1e-6);
    p.size = mix(1.0 - sizeVariance, 1.0 + sizeVariance, sizeRoll);
    p.trailWrites = 0.0; // a fresh particle has no history, so its ribbon grows from nothing
    p.stage = 0.0;      // ADR-520: born into its primary life
    // ADR-520: the emission mask. The spawn survives with probability equal to the field's scalar
    // at the spawn point, so a front's leading edge is a gradient of density rather than a wall.
    // A slot that loses the roll is born dead and is returned to the free list by this frame's
    // compaction, so the pool is not held hostage by a mask that is currently zero everywhere.
    let maskSlot = i32(params.fieldInfo.z) - 1;
    if (maskSlot >= 0) {
        if (rand3(slot, frame, 9u).x >= clamp(fieldScalar(maskSlot, p.position), 0.0, 1.0)) {
            p.life = 0.0;
        }
    }
    particles[slot] = p;
}

@compute @workgroup_size(64)
fn cs_simulate(@builtin(global_invocation_id) gid: vec3<u32>) {
    let slot = gid.x;
    if (slot >= params.counts.y) { return; }
    var p = particles[slot];
    if (p.life <= 0.0) {
        scratch[slot] = 0u;
        return;
    }
    let dt = params.sim.x;
    p.age += dt;
    if (p.age >= p.life) {
        p.life = 0.0;
        particles[slot] = p;
        scratch[slot] = 0u;
        return;
    }
    // ADR-520: a splash ring is not a particle in flight. It sits on the ground, it does not fall,
    // and above all it cannot collide a second time -- gravity pulling it back through the plane
    // it is lying on would re-trigger the response every frame and it would never die.
    if (p.stage > 0.5) {
        particles[slot] = p;
        scratch[slot] = 1u;
        return;
    }
    // Trail history (ADR-040): record where the particle *was* at the start of this step, so the
    // ribbon's live head (its current position) is never duplicated by the newest history entry.
    // The ring index is a pure function of the write count, which is simulation state.
    let historyPoints = u32(params.trail.x);
    if (historyPoints > 0u) {
        let stride = max(u32(params.trail.y), 1u);
        if (u32(params.sim.z) % stride == 0u) {
            let writes = u32(p.trailWrites);
            history[slot * historyPoints + (writes % historyPoints)] = vec4<f32>(p.position, 1.0);
            p.trailWrites = f32(writes + 1u);
        }
    }
    // forces
    var force = params.gravity.xyz;
    let turbStrength = params.gravity.w;
    if (turbStrength > 0.0) {
        let np = p.position * params.turb.x + vec3<f32>(0.0, 0.0, params.sim.y * params.turb.y);
        force += turbCurl(np) * turbStrength;
    }
    // ADR-370: the wind. A leaf that ignores the scene's wind while the tree it fell from bends in
    // it is the same defect as the tree ignoring it, one object along -- and the brief's acceptance
    // list asks for leaves that respond to both the speed and the DIRECTION. The field is spatial,
    // so two leaves ten metres apart catch different air and a gust front arrives at one before the
    // other, which is what stops a shower drifting as a single sheet.
    if (params.windMix.x > 0.0 && params.windDir.w > 0.5) {
        let w = windSampleFrom(particleWindField(), p.position, params.sim.y);
        let flow = vec3<f32>(w.direction.x, 0.0, w.direction.y) * (w.strength * (1.0 + w.gust));
        // A force toward matching the air, not a shove: a leaf accelerates until it is travelling
        // with the wind and then stops accelerating, which is what drag against moving air does.
        force += (flow * params.windMix.x - vec3<f32>(p.velocity.x, 0.0, p.velocity.z)) * params.windMix.x;
    }
    // A scatter-anchored particle circles the tree it was born at; everything else circles the
    // system's attractor. `select` picks the same float it always read when `home.w` is 0, so no
    // other system's arithmetic changes.
    let attractAt = select(params.attractor.xyz, p.home.xyz, p.home.w > 0.5);
    let toA = attractAt - p.position;
    let dist = length(toA) + 1e-4;
    let falloff = clamp(1.0 - dist / max(params.attractor2.x, 1e-3), 0.0, 1.0);
    let dirA = toA / dist;
    force += dirA * params.attractor.w * falloff;
    let tangent = cross(vec3<f32>(0.0, 1.0, 0.0), dirA);
    force += tangent * params.attractor2.y * falloff;
    p.velocity += force * dt;
    // field forces (in order; a Kill force ends the particle here)
    let fieldCount = min(params.fieldInfo.x, 4u);
    for (var k = 0u; k < 4u; k = k + 1u) {
        if (k >= fieldCount) { break; }
        let a = params.fieldForces[k * 2u];
        let axis = params.fieldForces[k * 2u + 1u].xyz;
        let mode = u32(a.x + 0.5);
        let fslot = i32(floor(a.y + 0.5));
        if (fslot < 0) { continue; }
        if (mode == 3u) { // kill
            if (fieldScalar(fslot, p.position) >= 0.5) {
                p.life = 0.0;
                particles[slot] = p;
                scratch[slot] = 0u;
                return;
            }
            continue;
        }
        var fv: vec3<f32>;
        if (fieldTypeOf(fslot) == FIELD_TYPE_VECTOR) {
            fv = fieldVector(fslot, p.position);
        } else {
            fv = fieldScalar(fslot, p.position) * axis;
        }
        if (mode == 1u) { // velocity
            p.velocity = mix(p.velocity, fv * a.z, a.w);
        } else {          // force / turbulence
            p.velocity += fv * (a.z * dt);
        }
    }
    // ADR-520: drag that knows how big the particle is. bias 0 -- the default -- is exactly
    // `params.direction.w`, so this multiply changes nothing for a system that has not asked.
    let dragBias = clamp(params.collide2.z, 0.0, 1.0);
    let effectiveDrag = params.direction.w * mix(1.0, 1.0 / max(p.size, 0.05), dragBias);
    p.velocity *= max(0.0, 1.0 - effectiveDrag * dt);
    // ADR-520: dart and hover. Real flying insects do not cruise; they move, stop, hang, and move
    // again, and a field of particles all moving at once is the "identical particle motion" the
    // quality bar names. The phase is per particle, so at any instant some of the swarm is still
    // and some is not -- which is the readable difference between a swarm and a snowfall.
    if (params.cluster.z > 0.0) {
        let phase = fract(params.sim.y * params.cluster.z + p.seed * 7.13 + f32(slot % 101u) * 0.0099);
        let share = clamp(params.cluster.w, 0.0, 1.0);
        let paused = 1.0 - smoothstep(max(share - 0.06, 0.0), share + 0.06, phase);
        p.velocity *= max(0.0, 1.0 - paused * 9.0 * dt);
    }
    p.position += p.velocity * dt;
    // ADR-520: the ground. Tested after integration, so the contact is found on the step that
    // crossed the plane rather than one step late.
    let collideMode = u32(params.collide.x + 0.5);
    if (collideMode > 0u && p.position.y < params.collide.y) {
        if (collideMode == 1u) { // kill
            p.life = 0.0;
            particles[slot] = p;
            scratch[slot] = 0u;
            return;
        } else if (collideMode == 2u) { // bounce
            let e = clamp(params.collide.z, 0.0, 1.0);
            p.position.y = params.collide.y + (params.collide.y - p.position.y) * e;
            p.velocity.y = abs(p.velocity.y) * e;
            // Tangential friction scaled by the same number: a dead bounce also stops sliding.
            p.velocity.x *= mix(0.35, 1.0, e);
            p.velocity.z *= mix(0.35, 1.0, e);
        } else { // splash: the second life
            p.position.y = params.collide.y;
            p.velocity = vec3<f32>(0.0);
            p.age = 0.0;
            p.life = max(params.collide2.x, 1e-3);
            p.stage = 1.0;
            particles[slot] = p;
            scratch[slot] = 1u;
            return;
        }
    }
    // ADR-520: the wrapping volume. A particle that leaves the box re-enters on the opposite face,
    // so the field is steady instead of blooming in and fading out once per lifetime. Pure function
    // of position: no accumulation, nothing to diverge between two renders of the same second.
    if (params.volume.w > 0.5) {
        let c = emitterCentre();
        let half = max(params.extent.xyz, vec3<f32>(1e-3));
        let rel = p.position - c + half;
        p.position = c - half + (rel - floor(rel / (2.0 * half)) * (2.0 * half));
    }
    particles[slot] = p;
    scratch[slot] = 1u;
}

// ---- stable stream compaction -------------------------------------------------------------------
// Workgroups of kScanThreads threads, kScanElems consecutive slots per thread: kScanBlock slots
// per block. Integer sums are associative, so the result does not depend on scheduling.

const kScanThreads: u32 = 256u;
const kScanElems: u32 = 4u;
const kScanBlock: u32 = 1024u; // kScanThreads * kScanElems

// The two halves of the `scratch` buffer: flags at [0, capacity), block sums after them.
fn blockSumIndex(b: u32) -> u32 { return params.counts.y + b; }

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

// Loads this thread's kScanElems flags (0 beyond capacity) and returns their sum.
fn loadFlags(base: u32, out: ptr<function, array<u32, 4>>) -> u32 {
    var sum = 0u;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        var f = 0u;
        if (i < params.counts.y) { f = scratch[i]; }
        (*out)[k] = f;
        sum += f;
    }
    return sum;
}

@compute @workgroup_size(256)
fn cs_scan_reduce(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    var f: array<u32, 4>;
    let local = loadFlags(wid.x * kScanBlock + tid * kScanElems, &f);
    let r = workgroupScan(tid, local);
    if (tid == 0u) { scratch[blockSumIndex(wid.x)] = r.y; }
}

@compute @workgroup_size(256)
fn cs_scan_top(@builtin(local_invocation_id) lid: vec3<u32>) {
    let tid = lid.x;
    let blocks = params.counts.w;
    var carry = 0u;
    for (var chunk = 0u; chunk < blocks; chunk += kScanBlock) {
        let base = chunk + tid * kScanElems;
        var v: array<u32, 4>;
        var local = 0u;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            var s = 0u;
            if (i < blocks) { s = scratch[blockSumIndex(i)]; }
            v[k] = s;
            local += s;
        }
        let r = workgroupScan(tid, local);
        var run = carry + r.x;
        for (var k = 0u; k < kScanElems; k++) {
            let i = base + k;
            if (i < blocks) { scratch[blockSumIndex(i)] = run; }
            run += v[k];
        }
        carry += r.y;
    }
    if (tid == 0u) {
        let alive = min(carry, params.counts.y);
        counters.aliveCount = alive;
        counters.deadCount = params.counts.y - alive;
        counters.billboard = DrawArgs(6u, alive, 0u, 0u);
        // One quad per ribbon segment; zero instances when the system has no trails, so the
        // second draw costs nothing.
        let historyPoints = u32(params.trail.x);
        var ribbonInstances = alive;
        if (historyPoints == 0u) { ribbonInstances = 0u; }
        counters.ribbon = DrawArgs(historyPoints * 6u, ribbonInstances, 0u, 0u);
    }
}

@compute @workgroup_size(256)
fn cs_scan_scatter(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    let base = wid.x * kScanBlock + tid * kScanElems;
    var f: array<u32, 4>;
    let local = loadFlags(base, &f);
    let r = workgroupScan(tid, local);
    var rank = scratch[blockSumIndex(wid.x)] + r.x; // alive slots before slot `base`
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        if (i < params.counts.y) {
            if (f[k] != 0u) {
                aliveList[rank] = i;
            } else {
                deadList[i - rank] = i;
            }
            rank += f[k];
        }
    }
}

// ---- lifetime curves (ADR-040) ------------------------------------------------------------
// Clamped piecewise-linear evaluation of up to 8 keys, ascending in t. Fewer than two keys means
// the curve is not authored and the caller's linear ramp is used, so pre-curve scenes are
// bit-identical. scene::ParticleCurve::evaluate() is the CPU reference for exactly this rule.

fn sizeCurveAt(t: f32, fallback: f32) -> f32 {
    let count = params.curves.x;
    if (count < 2u) { return fallback; }
    if (t <= params.sizeKeys[0].x) { return params.sizeKeys[0].y; }
    for (var i = 1u; i < count; i = i + 1u) {
        if (t <= params.sizeKeys[i].x) {
            let span = params.sizeKeys[i].x - params.sizeKeys[i - 1u].x;
            var u = 0.0;
            if (span > 1e-6) { u = (t - params.sizeKeys[i - 1u].x) / span; }
            return mix(params.sizeKeys[i - 1u].y, params.sizeKeys[i].y, u);
        }
    }
    return params.sizeKeys[count - 1u].y;
}

fn opacityCurveAt(t: f32, fallback: f32) -> f32 {
    let count = params.curves.z;
    if (count < 2u) { return fallback; }
    if (t <= params.opacityKeys[0].x) { return params.opacityKeys[0].y; }
    for (var i = 1u; i < count; i = i + 1u) {
        if (t <= params.opacityKeys[i].x) {
            let span = params.opacityKeys[i].x - params.opacityKeys[i - 1u].x;
            var u = 0.0;
            if (span > 1e-6) { u = (t - params.opacityKeys[i - 1u].x) / span; }
            return mix(params.opacityKeys[i - 1u].y, params.opacityKeys[i].y, u);
        }
    }
    return params.opacityKeys[count - 1u].y;
}

fn colorCurveAt(t: f32, fallback: vec3<f32>) -> vec3<f32> {
    let count = params.curves.y;
    if (count < 2u) { return fallback; }
    if (t <= params.colorKeys[0].x) { return params.colorKeys[0].yzw; }
    for (var i = 1u; i < count; i = i + 1u) {
        if (t <= params.colorKeys[i].x) {
            let span = params.colorKeys[i].x - params.colorKeys[i - 1u].x;
            var u = 0.0;
            if (span > 1e-6) { u = (t - params.colorKeys[i - 1u].x) / span; }
            return mix(params.colorKeys[i - 1u].yzw, params.colorKeys[i].yzw, u);
        }
    }
    return params.colorKeys[count - 1u].yzw;
}

// Size, colour and opacity at normalised age t: the curve when authored, else the linear ramp.
fn particleSize(t: f32) -> f32 {
    return sizeCurveAt(t, mix(params.attractor2.z, params.attractor2.w, t));
}
fn particleAlpha(t: f32) -> f32 {
    return opacityCurveAt(t, mix(params.colorStart.a, params.colorEnd.a, t));
}
// Per-particle tint variation from the seed keeps clouds from looking flat.
fn particleTint(t: f32, seed: f32) -> vec3<f32> {
    let base = colorCurveAt(t, mix(params.colorStart.rgb, params.colorEnd.rgb, t));
    return base * (0.85 + 0.3 * seed);
}

// ---- velocity stretching (ADR-040) --------------------------------------------------------
// `speed` is the length of the velocity *projected onto the camera plane*, so a particle flying
// straight at the lens stays round. Mirrors scene::particleStretchLength() exactly.
fn stretchLength(speed: f32) -> f32 {
    let amount = params.stretch.x;
    let shutter = params.cameraPos.w;
    if (amount <= 0.0 || shutter <= 0.0) { return 0.0; }
    let length = max(speed, 0.0) * shutter * amount;
    if (length < params.stretch.z) { return 0.0; }
    return min(length, max(params.stretch.y, 0.0));
}

// ---- emissive aggregate for the volume (ADR-040) ------------------------------------------
// Reduces the alive particles to one sphere: the emission-weighted centroid, the standard
// deviation of the positions about it (the cloud's spread), the mean colour and the total power.
// Two passes, both fixed-order: a workgroup tree per scan block, then a serial sum over blocks.
// No atomics, so the floating-point sum order is identical from run to run.

var<workgroup> glowSharedA: array<vec4<f32>, 256>;
var<workgroup> glowSharedB: array<vec4<f32>, 256>;

@compute @workgroup_size(256)
fn cs_glow_reduce(@builtin(local_invocation_id) lid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>) {
    let tid = lid.x;
    var a = vec4<f32>(0.0);
    var b = vec4<f32>(0.0);
    let base = wid.x * kScanBlock + tid * kScanElems;
    for (var k = 0u; k < kScanElems; k++) {
        let i = base + k;
        if (i >= params.counts.y) { continue; }
        let p = particles[i];
        if (p.life <= 0.0) { continue; }
        let t = clamp(p.age / max(p.life, 1e-4), 0.0, 1.0);
        let w = particleAlpha(t) * max(params.turb.w, 0.0);
        if (w <= 0.0) { continue; }
        a += vec4<f32>(p.position * w, w);
        b += vec4<f32>(particleTint(t, p.seed) * w, dot(p.position, p.position) * w);
    }
    glowSharedA[tid] = a;
    glowSharedB[tid] = b;
    workgroupBarrier();
    for (var s = 128u; s > 0u; s = s >> 1u) {
        if (tid < s) {
            glowSharedA[tid] += glowSharedA[tid + s];
            glowSharedB[tid] += glowSharedB[tid + s];
        }
        workgroupBarrier();
    }
    if (tid == 0u) {
        glowScratch[wid.x * 2u] = glowSharedA[0];
        glowScratch[wid.x * 2u + 1u] = glowSharedB[0];
    }
}

@compute @workgroup_size(1)
fn cs_glow_top() {
    var a = vec4<f32>(0.0);
    var b = vec4<f32>(0.0);
    for (var i = 0u; i < params.counts.w; i++) {
        a += glowScratch[i * 2u];
        b += glowScratch[i * 2u + 1u];
    }
    var center = vec3<f32>(0.0);
    var color = vec3<f32>(0.0);
    var radius = 0.0;
    var power = 0.0;
    if (a.w > 1e-6) {
        center = a.xyz / a.w;
        color = b.xyz / a.w;
        radius = sqrt(max(0.0, b.w / a.w - dot(center, center)));
        power = a.w * max(params.fog2.z, 0.0);
    }
    // The result lands past the block sums; the CPU copies it into the shared glow table.
    let out = params.counts.w * 2u;
    glowScratch[out] = vec4<f32>(center, radius);
    glowScratch[out + 1u] = vec4<f32>(color, power);
}

// ---- rendering --------------------------------------------------------------------------------

struct VsOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) prevClip: vec4<f32>, // the simulated previous position, for the velocity target
    @location(3) world: vec3<f32>,    // for the volumetric transmittance (ADR-040)
    @location(4) nowClip: vec4<f32>,  // this frame's clip position; @builtin(position) is in
                                      // framebuffer pixels with w = 1 / clip.w, so dividing that
                                      // by its own w would not give NDC (see common.wgsl).
    // ADR-520: which silhouette the fragment stage should cut. 0 = the shape `params.leaf.x`
    // selects, 1 = the splash ring, whose stage is a property of the PARTICLE and not of the
    // system -- which is why it cannot be read from the uniforms the way `leaf` is.
    @location(5) ringMask: f32,
};

// ---- ADR-520: the flash and the light ------------------------------------------------------
// One oscillator. `sync` is the whole reason this is one feature and not two: at 0 the phase is
// the particle's own and the field twinkles at random, at 1 every particle shares one phase and
// the field flashes as a chorus. `sharpness` is the exponent that turns the sine into a blink.
fn pulseGain(seed: f32, slot: u32) -> f32 {
    let rate = params.pulse.x;
    if (rate <= 0.0) { return 1.0; }
    let own = seed * 6.28318530718 + f32(slot % 257u) * 0.3917;
    let phase = params.sim.y * rate * 6.28318530718 + own * (1.0 - clamp(params.pulse.z, 0.0, 1.0));
    let wave = pow(max(0.5 + 0.5 * sin(phase), 0.0), max(params.pulse.w, 0.05));
    return mix(1.0, wave, clamp(params.pulse.y, 0.0, 1.0));
}

// Henyey-Greenstein. `g` -> 1 concentrates the scattered light into the forward direction, which
// is why a mote is nearly invisible across the sun and blazes when you look into it. `cosTheta` is
// between the direction the light TRAVELS and the direction the view ray travels.
fn scatterGain(world: vec3<f32>) -> vec3<f32> {
    let strength = params.scatter.x;
    if (strength <= 0.0 || params.sun.w < 0.5) { return vec3<f32>(1.0); }
    let toEye = normalize(params.cameraPos.xyz - world);
    // params.sun.xyz points TOWARDS the light, so the light travels along -sun; the view ray
    // travels along -toEye. cos between them is dot(-sun, -toEye) = dot(sun, toEye).
    let cosTheta = clamp(dot(params.sun.xyz, toEye), -1.0, 1.0);
    let g = clamp(params.scatter.y, -0.95, 0.95);
    let gg = g * g;
    let denom = max(1.0 + gg - 2.0 * g * cosTheta, 1e-4);
    let hg = (1.0 - gg) / (4.0 * 3.14159265 * pow(denom, 1.5));
    // The term is ADDED to the particle's own brightness, never subtracted from it.
    //
    // The first version of this was `1 + strength * (4*pi*hg - 1)`, which normalises the phase
    // function so that an isotropic phase at strength 1 is exactly no change. That is the correct
    // normalisation and it is the wrong control: a phase function redistributes a fixed amount of
    // light, so anything outside the forward lobe comes out DARKER, and at the strengths this
    // effect wants -- 5 and up, because the whole point is a mote that blazes -- the bracket goes
    // below -1/strength and every mote outside a narrow cone renders at exactly zero. Measured:
    // the dust-motes scene rendered 74 pixels above 200 with scatterStrength 0 and 0 pixels with
    // scatterStrength 1; the motes had not got dimmer, they had been multiplied by a negative
    // number and clamped away.
    //
    // A real mote is lit by the whole sky as well as by the sun. So: it keeps what it had, and the
    // key light ADDS. `strength` is then monotone -- more is always more -- and 0 is still exactly
    // off, which is the property that keeps every existing system bit-identical.
    let lit = 1.0 + strength * (4.0 * 3.14159265 * hg);
    // The light's HUE, not its radiance: `sunColor` is already multiplied by an intensity that is
    // routinely 8, and multiplying a particle by that would make "tint it slightly like the sun"
    // into "make it eight times brighter", which is the unit bug this repository keeps paying for.
    let sc = params.sunColor.rgb;
    let peak = max(max(sc.r, sc.g), max(sc.b, 1e-4));
    return mix(vec3<f32>(1.0), sc / peak, 0.35) * max(lit, 0.0);
}

// The scene pass writes five colour targets (ADR-035); particles fill the colour and the velocity
// and leave the surface targets to the geometry behind them (their write masks are off).
struct ParticleOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec2<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

@vertex
fn vs_particle(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VsOut {
    let slot = aliveRead[ii];
    let p = particlesRead[slot];
    let t = clamp(p.age / max(p.life, 1e-4), 0.0, 1.0);
    let isRing = p.stage > 0.5; // ADR-520
    let size = particleSize(t) * p.size;
    var corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(-1.0, 1.0),
                                      vec2<f32>(-1.0, 1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0));
    let c = corners[vi];
    var out_size_scale = 1.0; // ADR-370: the leaf's short-axis squash; 1 for a round particle
    // Velocity-aligned stretching: rotate the billboard basis so +x runs along the screen-space
    // velocity and grow only that axis. The width is untouched, so the fragment falloff turns the
    // disc into an ellipse and a slow particle is exactly the round quad it always was.
    var axisX = params.cameraRight.xyz;
    var axisY = params.cameraUp.xyz;
    var halfLength = size;
    // ADR-370: a leaf is a CARD, not a dot -- it has a long axis, it spins about that axis, and it
    // turns edge-on twice a revolution. Two rotations do all of it: one in the view plane, which is
    // the leaf yawing as it falls, and one that squashes the short axis, which is the same card
    // seen at an angle. Cheaper than orienting a real quad in 3D and indistinguishable at the size
    // a leaf occupies. Velocity stretch is deliberately skipped for leaves: a smeared leaf reads as
    // a spark, which is the exact failure the brief names.
    var faceLit = 1.0;
    if (params.leaf.x > 0.5) {
        let spin = p.seed * 6.28318530718 + p.age * params.leaf.y * (0.55 + p.seed);
        let cs = cos(spin);
        let sn = sin(spin);
        let r = params.cameraRight.xyz * cs + params.cameraUp.xyz * sn;
        let u = params.cameraRight.xyz * -sn + params.cameraUp.xyz * cs;
        // The flip. `edge` goes to zero twice a turn, which is the leaf presenting its edge.
        let edge = cos(spin * 0.7 + p.seed * 11.0);
        axisX = r;
        axisY = u;
        halfLength = size * max(params.leaf.z, 0.2);
        // Squash the short axis rather than the long one: a leaf turning edge-on gets narrower,
        // not shorter.
        out_size_scale = abs(edge);
        // ...and a face turned away from the light catches less of it. Two-sided shading, for one
        // multiply, with no normal and no light to look up.
        faceLit = mix(1.0 - params.leaf.w, 1.0, abs(edge));
    }
    // How much of the shutter's travel the stretched quad already covers. The billboard *is* a
    // smear, so writing the full per-frame motion into the velocity target as well would blur it
    // twice; the velocity is scaled by what the stretch has not already drawn.
    var motionLeft = 1.0;
    let screenVel = vec2<f32>(dot(p.velocity, params.cameraRight.xyz), dot(p.velocity, params.cameraUp.xyz));
    let screenSpeed = length(screenVel);
    let added = stretchLength(screenSpeed);
    if (added > 0.0 && screenSpeed > 1e-6) {
        let d = screenVel / screenSpeed;
        axisX = params.cameraRight.xyz * d.x + params.cameraUp.xyz * d.y;
        axisY = params.cameraRight.xyz * -d.y + params.cameraUp.xyz * d.x;
        halfLength = size + 0.5 * added;
        let shutterTravel = screenSpeed * params.cameraPos.w;
        motionLeft = clamp(1.0 - added / max(shutterTravel, 1e-6), 0.0, 1.0);
    }
    // ADR-520: the splash ring. It lies FLAT on the ground rather than facing the camera, because
    // a ripple is a thing on a surface and a camera-facing ring at a grazing angle reads as a
    // floating hoop. It expands as sqrt(t) -- fast, then slowing -- which is what a disturbance
    // spreading on a surface does, and is the difference between a ripple and a growing circle.
    var ringFade = 1.0;
    if (isRing) {
        axisX = vec3<f32>(1.0, 0.0, 0.0);
        axisY = vec3<f32>(0.0, 0.0, 1.0);
        let radius = params.attractor2.z * p.size * max(params.collide.w, 0.0) * sqrt(t);
        halfLength = radius;
        out_size_scale = 1.0;
        // Guaranteed to die invisible. Without this a ring whose opacity curve ends above zero
        // would vanish on a frame boundary, which is exactly the pop a ripple must not have.
        ringFade = 1.0 - t;
    }
    let world = p.position + axisX * (c.x * halfLength) + axisY * (c.y * select(size * out_size_scale, halfLength, isRing));
    var out: VsOut;
    out.clip = params.viewProj * vec4<f32>(world, 1.0);
    out.nowClip = out.clip;
    let prevWorld = world - p.velocity * (params.sim.x * motionLeft);
    out.prevClip = params.prevViewProj * vec4<f32>(prevWorld, 1.0);
    out.uv = c;
    out.world = world;
    // ADR-520: the flash and the light. Both are multipliers on the tint, so a system that asks
    // for neither (rate 0, strength 0) gets exactly 1 from both and is bit-identical.
    let lit = faceLit * pulseGain(p.seed, slot);
    out.color = vec4<f32>(particleTint(t, p.seed) * lit * scatterGain(world), particleAlpha(t) * ringFade);
    out.ringMask = select(0.0, 1.0, isRing);
    if (p.life <= 0.0) { out.clip = vec4<f32>(0.0, 0.0, 2.0, 1.0); } // cull dead (never drawn)
    return out;
}

// ---- ribbons (ADR-040) --------------------------------------------------------------------
// Point 0 is the live position; point j (1..n) is the j-th newest history entry, so the ribbon
// is n quads of 6 vertices. Points past the particle's own write count collapse onto the last
// valid one, which makes the ribbon *grow* out of a newborn particle instead of springing from
// whatever the slot's previous occupant left behind.

fn trailPoint(slot: u32, live: vec3<f32>, writes: u32, points: u32, j: u32) -> vec3<f32> {
    if (j == 0u) { return live; }
    let valid = min(writes, points);
    if (valid == 0u) { return live; }
    let jj = min(j, valid);
    return historyRead[slot * points + ((writes - jj) % points)].xyz;
}

@vertex
fn vs_ribbon(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VsOut {
    let slot = aliveRead[ii];
    let p = particlesRead[slot];
    let points = u32(params.trail.x);
    let segment = vi / 6u;
    let corner = vi % 6u;
    // Two triangles: (0,0) (0,1) (1,0) / (1,0) (0,1) (1,1) in (along, across).
    var alongIdx = array<u32, 6>(0u, 0u, 1u, 1u, 0u, 1u);
    var sideIdx = array<f32, 6>(-1.0, 1.0, -1.0, -1.0, 1.0, 1.0);
    let j = segment + alongIdx[corner];
    let side = sideIdx[corner];

    let writes = u32(p.trailWrites);
    let here = trailPoint(slot, p.position, writes, points, j);
    var prev = here;
    var next = here;
    if (j > 0u) { prev = trailPoint(slot, p.position, writes, points, j - 1u); }
    if (j + 1u <= points) { next = trailPoint(slot, p.position, writes, points, j + 1u); }
    var tangent = next - prev;
    if (dot(tangent, tangent) < 1e-12) { tangent = params.cameraRight.xyz; }
    tangent = normalize(tangent);
    // Camera-facing: the ribbon's width runs perpendicular to both the path and the eye ray.
    var toEye = params.cameraPos.xyz - here;
    if (dot(toEye, toEye) < 1e-12) { toEye = vec3<f32>(0.0, 0.0, 1.0); }
    var across = cross(tangent, normalize(toEye));
    if (dot(across, across) < 1e-12) { across = params.cameraRight.xyz; }
    across = normalize(across);

    let t = clamp(p.age / max(p.life, 1e-4), 0.0, 1.0);
    let headSize = particleSize(t) * p.size;
    let u = f32(j) / f32(max(points, 1u));                 // 0 at the head, 1 at the tail
    let width = headSize * max(params.trail.z, 0.0) * mix(1.0, clamp(params.trail.w, 0.0, 1.0), u);
    let world = here + across * (side * width);

    var out: VsOut;
    out.clip = params.viewProj * vec4<f32>(world, 1.0);
    out.nowClip = out.clip;
    out.prevClip = params.prevViewProj * vec4<f32>(world - p.velocity * params.sim.x, 1.0);
    out.uv = vec2<f32>(side, 0.0); // the fragment falloff softens the ribbon across its width
    out.world = world;
    let tint = mix(vec3<f32>(1.0), params.trail2.yzw, u);
    let alpha = particleAlpha(t) * mix(1.0, clamp(params.trail2.x, 0.0, 1.0), u);
    out.color = vec4<f32>(particleTint(t, p.seed) * tint, alpha);
    out.ringMask = 0.0; // ADR-520: a ribbon is never a ring
    if (p.life <= 0.0) { out.clip = vec4<f32>(0.0, 0.0, 2.0, 1.0); }
    return out;
}

// ---- atmosphere coupling (ADR-040) --------------------------------------------------------
// The volume composite multiplies the whole HDR buffer by the transmittance it marched to the
// *opaque surface*, which over-fogs a particle floating in front of distant geometry. We divide
// that out and put back the transmittance to the particle's own depth, so a particle ends up
// fogged by exactly the fog in front of it. The estimate uses the same exponential height model
// the march does (without the noise and field terms, which cancel to first order in the ratio),
// integrated with four midpoint samples - cheap, deterministic, and exact about the y > fogHeight
// clamp that a closed form would have to case-split on.

fn fogTransmittance(origin: vec3<f32>, dir: vec3<f32>, dist: f32) -> f32 {
    let density = params.fog.x;
    if (density <= 0.0 || dist <= 0.0) { return 1.0; }
    var sum = 0.0;
    for (var i = 0; i < 4; i = i + 1) {
        let t = (f32(i) + 0.5) * 0.25 * dist;
        let y = origin.y + dir.y * t;
        // ADR-567: the SAME profile the march evaluates and the surface pass integrates. This was
        // a third statement of the model, written out here -- the shape ADR-562 §9 names: every
        // reader of a shared model is a call site to audit, and a reader that restates it is one
        // edit away from being a different atmosphere in the same frame.
        if (params.fog3.z > 0.0 && params.terrain1.w > 0.5) {
            // ADR-715: the same ground-following term the march evaluates, per sample.
            let p = origin + dir * t;
            sum += fogGroundProfileAt(terrainHeightTex, params.terrain0, params.terrain1, p, params.fog.y,
                                      params.fog3.z, params.fog.z, params.fog3.x, params.fog3.y);
        } else {
            sum += fogHeightProfile(y - params.fog.y, params.fog.z, params.fog3.x, params.fog3.y);
        }
    }
    return exp(-density * params.fog.w * (sum * 0.25) * dist);
}

fn fogCorrection(world: vec3<f32>, pixel: vec2<i32>) -> f32 {
    let coupling = clamp(params.fog2.y, 0.0, 1.0);
    // No fog, no coupling asked for, or no linear-depth target to say what the volume composite
    // will have marched to: leave the particle exactly as it was before ADR-040.
    if (params.fog.x <= 0.0 || coupling <= 0.0 || params.fog2.w < 0.5) { return 1.0; }
    let toParticle = world - params.cameraPos.xyz;
    let dist = length(toParticle);
    if (dist < 1e-5) { return 1.0; }
    let dir = toParticle / dist;
    // ADR-372: `linearDepthTex` holds VIEW-SPACE Z, `dist` above is EUCLIDEAN ray distance, and
    // `fog2.x` (volumeMaxDistance) is the march's euclidean limit. This line compared all three as
    // though they were one quantity. They agree only on the optical axis: off it, euclidean exceeds
    // view-Z by 1/cos(theta) -- measured at the default 42 degree vertical FOV on 16:9, **+7.1% at
    // the top edge, +21.1% at the side, +27.0% in the corner**.
    //
    // The consequence was not a small error but a silent switch-off. `max(sceneDist, dist)` below
    // picks the particle's own distance whenever the surface reads nearer, so understating the
    // surface by up to 27% made the two transmittances equal and the correction returned 1.0 --
    // strongest exactly at the frame corners, where a particle in front of distant geometry stayed
    // over-fogged, which is the defect ADR-040 added this function to remove.
    //
    // `softParticleFade` below already solves the same problem the other way (it converts the
    // particle to view-Z) and its comment names the trap; this function did not get the memo. The
    // conversion goes to euclidean here rather than to view-Z because `fogTransmittance` marches
    // along `dir` for a distance, so euclidean is what it wants.
    let axis = normalize(cross(params.cameraRight.xyz, params.cameraUp.xyz));
    let cosTheta = max(abs(dot(dir, axis)), 1e-4);
    let sceneZ = textureLoad(linearDepthTex, pixel, 0).x;
    let sceneDist = min(sceneZ / cosTheta, params.fog2.x);
    let toParticleT = fogTransmittance(params.cameraPos.xyz, dir, dist);
    let toSurfaceT = fogTransmittance(params.cameraPos.xyz, dir, max(sceneDist, dist));
    return mix(1.0, clamp(toParticleT / max(toSurfaceT, 1e-4), 0.0, 64.0), coupling);
}

// ADR-367: the depth-aware soft-particle fade `ParticleSystem::softness` has been promising since
// ADR-015. The value was authored, serialised, uploaded into `turb.z` -- and read by nothing, so
// fifteen scene files carry deliberately tuned values from 0.3 to 3.0 that have never rendered.
//
// A billboard is a flat card in a world with depth, so where it crosses a surface it shows its own
// silhouette as a hard line -- the tell that the volume is a sprite. Fading the card out as it
// approaches the surface behind it hides the intersection, which is the whole trick.
//
// `linearDepthTex` holds VIEW-SPACE Z (`dot(p - cameraPos, cameraForward)` in linear_depth.wgsl),
// not ray distance, so the particle has to be measured the same way or the fade would tighten
// toward the corners of the frame where the two diverge. There is no camera forward in `Params`,
// but `cameraRight` and `cameraUp` are the orthonormal billboard basis, so their cross product is
// the view axis up to sign; every drawn particle is in front of the eye, so `abs` picks the sign.
//
// Returns exactly 1.0 when softness is 0 or no linear-depth target is bound, so "off" is off and a
// pre-ADR-367 frame is reproduced bit for bit.
fn softParticleFade(world: vec3<f32>, pixel: vec2<i32>) -> f32 {
    let softness = params.turb.z;
    if (softness <= 0.0 || params.fog2.w < 0.5) { return 1.0; }
    let axis = normalize(cross(params.cameraRight.xyz, params.cameraUp.xyz));
    let particleZ = abs(dot(world - params.cameraPos.xyz, axis));
    // Where nothing was drawn, linear_depth.wgsl writes 1e7, so the fade is 1 and a particle
    // against the sky is untouched.
    let sceneZ = textureLoad(linearDepthTex, pixel, 0).x;
    return clamp((sceneZ - particleZ) / softness, 0.0, 1.0);
}

// ADR-370: the leaf silhouette. A pointed ellipse -- half-width tapering to zero at both ends by a
// cosine raised to a power, which is the cheapest shape that reads as a leaf rather than as a
// lozenge -- plus a faint midrib, because the rib is what the eye uses to tell a leaf from a petal.
// Returns coverage in 0..1 with a soft edge, so the card antialiases instead of stair-stepping.
fn leafCoverage(uv: vec2<f32>) -> f32 {
    let along = clamp(uv.y, -1.0, 1.0);
    let halfWidth = 0.66 * pow(max(cos(along * 1.5707963), 0.0), 0.62);
    if (halfWidth <= 1.0e-4) { return 0.0; }
    let a = abs(uv.x);
    // One pixel of feather in uv terms is not knowable here, so the feather is a fraction of the
    // width: narrow enough to stay crisp, wide enough that the tips do not crawl.
    let cover = 1.0 - smoothstep(halfWidth * 0.72, halfWidth, a);
    let rib = 1.0 + 0.35 * (1.0 - smoothstep(0.0, halfWidth * 0.22, a)) * (1.0 - abs(along));
    return cover * rib;
}

// ADR-520: the ring a splash leaves. An annulus with a soft inner and outer wall, brightest at
// its crest. `ringThickness` 1 collapses it to a filled disc, which is the other useful shape a
// landing makes (a spreading bloom rather than a ripple) and costs no second code path.
fn ringCoverage(uv: vec2<f32>, thickness: f32) -> f32 {
    let r = length(uv);
    if (r > 1.0) { return 0.0; }
    let w = clamp(thickness, 0.01, 1.0);
    if (w >= 0.999) { return 1.0 - r * r; }
    // Distance from the crest, which sits just inside the rim so the ring's leading edge is the
    // outer one -- that is the direction a ripple travels and the eye reads the bright edge as
    // the front.
    let crest = 1.0 - w * 0.5;
    let d = abs(r - crest) / max(w * 0.5, 1e-3);
    return clamp(1.0 - d * d, 0.0, 1.0);
}

@fragment
fn fs_particle(in: VsOut) -> ParticleOut {
    var falloff: f32;
    if (in.ringMask > 0.5) {
        falloff = ringCoverage(in.uv, params.collide2.y);
        if (falloff <= 0.0) { discard; }
    } else if (params.leaf.x > 0.5) {
        falloff = leafCoverage(in.uv);
        if (falloff <= 0.0) { discard; }
    } else {
        let r2 = dot(in.uv, in.uv);
        if (r2 > 1.0) { discard; }
        falloff = (1.0 - r2) * (1.0 - r2);
    }
    let pixel = vec2<i32>(floor(in.clip.xy));
    let alpha = in.color.a * falloff * softParticleFade(in.world, pixel);
    let emissive = params.turb.w * fogCorrection(in.world, pixel);
    var out: ParticleOut;
    if (params.counts.z == 0u) {
        out.color = vec4<f32>(in.color.rgb * alpha * emissive, alpha); // additive: premultiplied
    } else {
        out.color = vec4<f32>(in.color.rgb * emissive, alpha);
    }
    out.normalRoughness = vec4<f32>(0.0, 0.0, 1.0, 2.0);
    let now = in.nowClip.xy / max(abs(in.nowClip.w), 1e-6) * sign(max(in.nowClip.w, 1e-6));
    let before = in.prevClip.xy / max(abs(in.prevClip.w), 1e-6) * sign(max(in.prevClip.w, 1e-6));
    out.velocity = vec2<f32>((now.x - before.x) * 0.5, (before.y - now.y) * 0.5);
    out.emission = vec4<f32>(out.color.rgb, alpha);
    out.ids = 0u;
    return out;
}
