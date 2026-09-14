#pragma once

// The numbers a frame is allowed to state about itself (ADR-077).
//
// Two rules govern everything here, and both were learnt the hard way.
//
// The first: a statistic must say which question it answers. The renderer used to report a single
// `tris=15154902` for Glowmere, which is `sourceTriangles * instances` -- the geometry the world
// *contains*, counted before LOD selection and before the GPU cull rejects anything. Nothing
// measured the geometry the frame actually submitted, so no LOD or culling change could be shown
// to have done anything. The two live here side by side under names that cannot be read as each
// other: `logical*` is the content, `submitted*` is the submission.
//
// The second: a number that cannot be gathered honestly is not gathered. The instance counts of
// indirect draws are written by the cull pass on the GPU and the CPU never sees this frame's;
// reading them back would stall the frame that is being measured. So a draw whose instance count
// came from the last *completed* cull readback is counted in `estimatedDraws` as well, and the
// caller can see exactly how much of the total is one to three frames stale. It is not presented
// as if the CPU knew.

#include "gpu/timeline_math.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::rendering {

// Triangles one recorded draw submits. Separate from the call site because index counts come from
// meshes and instance counts from the cull pass, and multiplying two 32-bit numbers into a 32-bit
// one is how a 15-million-triangle scene reports a plausible-looking small number.
[[nodiscard]] std::uint64_t drawTriangles(std::uint32_t indexCount, std::uint32_t instanceCount);

// What one recorder handed to one class of pass. Accumulated while draws are recorded, so it
// counts what the command buffer contains rather than what the scene holds.
struct SubmittedGeometry {
    std::uint64_t triangles = 0;
    std::uint64_t instances = 0;
    std::uint32_t draws = 0;
    // Of `draws`, those whose instance count is the last completed cull readback's rather than
    // this frame's. Their triangle and instance contributions lag by one to three frames; in a
    // still scene that is exact and under a fast camera cut it is not. A caller that needs to
    // know how much of the total it can trust reads this against `draws`.
    std::uint32_t estimatedDraws = 0;
    // Of `draws`, those whose instance count the CPU does not have at all -- an indirect draw
    // before the first cull readback has landed. They contribute nothing to `triangles` or
    // `instances`, so a non-zero value here means the two are a floor. The alternative, guessing
    // the count from the object's record total, would put a number in the frame that no pass
    // ever drew.
    std::uint32_t unmeasuredDraws = 0;

    SubmittedGeometry& operator+=(const SubmittedGeometry& other);
    // Records one draw. `estimated` marks an instance count that came from the cull readback.
    void record(std::uint32_t indexCount, std::uint32_t instanceCount, bool estimated);
    // Records a draw whose instance count is not knowable on the CPU this frame.
    void recordUnmeasured();
};

// A frame's geometry, split by the question each number answers.
struct GeometryCounters {
    // The world's content: source triangles times instance records, before LOD and before culling.
    // This is the number the renderer has always reported as `tris`. It is not wrong; it answers
    // "how big is this world", and it does not move when culling improves.
    std::uint64_t logicalTriangles = 0;
    std::uint64_t logicalInstances = 0;
    // The frame's submission, per class of pass. A camera-side triangle and a shadow-side triangle
    // cost different amounts and are counted apart so a shadow change cannot look like a scene one.
    SubmittedGeometry camera; // the lit scene pass
    SubmittedGeometry depth;  // the depth prepass (no fragment work, but the vertices are paid for)
    SubmittedGeometry shadow; // every shadow view, summed
    [[nodiscard]] SubmittedGeometry total() const;
};

// Redundant state is not free, and until it is counted nobody can say whether a submission change
// removed any. SCOPE: these count what `SceneRenderer` and `ProceduralRenderer` record -- entity
// draws, procedural draws, the depth prepass, the shadow views and the frame's own fullscreen
// passes. Particles, SDFs, the post chain, AO and the volumetrics record their own binds and are
// not instrumented, so every field here is a floor for the frame, not the frame's total. The pass
// counts on gpu::FrameTimeline are the frame's total; `RenderStats::gpuPasses` carries it.
struct StateChangeCounters {
    std::uint32_t pipelineBinds = 0;   // SetPipeline calls recorded
    std::uint32_t bindGroupBinds = 0;  // SetBindGroup calls recorded
    std::uint32_t vertexBufferBinds = 0;
    std::uint32_t indexBufferBinds = 0;
    // Binds the recorder skipped because the state was already the one it wanted. Only the
    // procedural renderer tracks bound state, so this is its dedup alone; a zero does not mean
    // there was no redundancy elsewhere.
    std::uint32_t redundantBindsAvoided = 0;
    // Passes of the whole frame, by kind, as marked on gpu::FrameTimeline -- not restricted to the
    // scope above. Every render pass is a render-target switch, and on a tile-based GPU a tile load
    // and store as well; a compute pass switches no target. A pass whose caller did not say which
    // it was is in neither, and is counted as unclassified alongside these.
    std::uint32_t renderPasses = 0;
    std::uint32_t computePasses = 0;

    StateChangeCounters& operator+=(const StateChangeCounters& other);
};

// CPU time inside SceneRenderer::render(), by stage, so the GPU pass list has a CPU counterpart.
//
// The audit that opened this work had two sampled per-update counters -- 0.14 ms of procedural
// rebuild and 0.77 ms of scene update against a 25.7 ms frame -- and concluded from them that the
// CPU was not the bottleneck. It probably is not, but two samples of a thirteen-stage frame is not
// how anyone should find that out. These stages partition the whole of render() in submission
// order, so they sum to it: a stage that is added without a mark shows up in `unattributedMs()`
// rather than quietly vanishing into a neighbour.
//
// These are *encode* times on the submitting thread. They do not include the scene rebuild that
// happens before render() is called -- the application measures that separately -- and only the
// offline path fills `finishMs`/`submitMs`/`queueWaitMs`, because the live path owns its own
// command encoder and submits it itself. On that path `totalMs` covers those three as well as
// render(); on the live path it is render() alone.
struct CpuFrameBreakdown {
    double uploadsMs = 0.0;     // mesh / texture / environment uploads, shader-layer sync
    double lightsMs = 0.0;      // frame uniforms, light packing, cascade fitting, the froxel encode
    double objectsMs = 0.0;     // entity traversal, object uniforms, shadow-caster selection
    double fieldsMs = 0.0;      // fields, material programs, spline tables
    double simulationMs = 0.0;  // grid-field simulation encode
    double particlesMs = 0.0;   // particle update encode
    double proceduralMs = 0.0;  // procedural instance rebuild, uploads, effector and cull encode
    double sdfMs = 0.0;         // SDF node packing and mesh uploads
    double shadowEncodeMs = 0.0;     // the cascade / spot depth passes
    double backgroundEncodeMs = 0.0; // the HDR clear and background shader layers
    double depthEncodeMs = 0.0;      // depth prepass, linear depth, AO encode
    double sceneEncodeMs = 0.0;      // the lit scene pass
    double volumeEncodeMs = 0.0;     // volumetric march, composite and debug geometry
    double postEncodeMs = 0.0;       // user shader layers and the built-in post chain
    double tonemapEncodeMs = 0.0;    // aux debug view, tone mapping, the timeline resolve
    double finishMs = 0.0;       // CommandEncoder::Finish() -- offline path only
    double submitMs = 0.0;       // Queue::Submit() -- offline path only
    double queueWaitMs = 0.0;    // blocking on the queue -- offline path only, never the live one
    double totalMs = 0.0;        // see above: render(), plus submission on the offline path

    [[nodiscard]] double stagesMs() const;
    [[nodiscard]] double unattributedMs() const;
};

// ---- reading a timeline ----------------------------------------------------------------------

// One frame's passes summed per label. Several passes share a label by design (two cascades, a
// bloom pyramid, a scene pass split around the SDF raymarch), and it is the phase's total that an
// A/B moves. Order follows first appearance, which is submission order.
[[nodiscard]] std::vector<gpu::TimelineInterval> sumByLabel(const std::vector<gpu::TimelineInterval>& passes);

// Per-label medians over many frames. A single frame's sample of a counter says very little: the
// same Glowmere pass reads 19 ms and 31 ms in consecutive frames when anything else on the machine
// touches the GPU. A label missing from a frame contributes no sample to it rather than a zero,
// because "this pass did not run" and "this pass cost nothing" are different claims.
[[nodiscard]] std::vector<gpu::TimelineInterval>
medianByLabel(const std::vector<std::vector<gpu::TimelineInterval>>& frames);

// Where a removal A/B's saving actually landed.
//
// The passes partition the frame exactly -- each is the interval between two consecutive pass ends
// on one timeline -- so a phase that is switched off cannot take cost out of the frame without
// that cost being visible somewhere in the per-pass numbers. When `--disable volume` saves 1.44 ms
// and the volume pass reported 0.85, the remaining 0.59 ms is not missing: it is in the other
// labels, and this finds it. `elsewhereMs` is the part of the saving the removed phase was never
// charged for, and `byLabel` says which passes gave it up, largest first.
struct RemovalAttribution {
    double frameDeltaMs = 0.0;   // baseline frame - arm frame; negative when the arm was slower
    double removedPassMs = 0.0;  // what the removed label reported in the baseline (0 if absent)
    double elsewhereMs = 0.0;    // frameDeltaMs - removedPassMs
    std::vector<gpu::TimelineInterval> byLabel; // per-label delta (baseline - arm), largest first
};

[[nodiscard]] RemovalAttribution attributeRemoval(const std::vector<gpu::TimelineInterval>& baseline,
                                                  const std::vector<gpu::TimelineInterval>& arm,
                                                  std::string_view removedLabel);

// ---- distributions (ADR-113) -------------------------------------------------------------------

// The rank `q` names in a sorted sample, by the *nearest-rank* definition: the smallest value at
// or below which at least a fraction `q` of the sample lies, i.e. the element at
// `ceil(q * n) - 1`, clamped. `q = 0` is the minimum and `q = 1` the maximum. No interpolation
// between neighbours, because a frame time that no frame took is not a frame time -- and with the
// 108 steady frames the harness measures, interpolation moves p99 by more than the thing it is
// usually asked to detect.
//
// `samples` is taken by value and sorted in place; callers that already hold a sorted copy should
// prefer `describe`, which sorts once for every statistic it computes.
[[nodiscard]] double percentileOf(std::vector<double> samples, double q);

// A frame-time distribution. Everything is milliseconds, larger being worse, and every statistic
// is over the *measured* window -- the caller drops its own warm-up before handing samples here.
//
// **`p99` and `low1Percent` are different numbers and neither is a synonym for the other.** This
// has to be written down because the games press uses "1% low" for both and the two disagree by
// whatever the tail's shape is:
//
//   p99          the frame time 99% of frames came in under -- ONE value out of the sample, the
//                second-slowest of a hundred. It says where the tail begins.
//   low1Percent  the *mean of the slowest 1% of frames* -- so with 108 frames, the average of the
//                two worst. It says how bad the tail is once you are in it, and it is always
//                >= p99 for the same sample.
//
// A stutter that doubles one frame in two hundred moves `low1Percent` and barely moves `p99`.
// Both are reported; a comparison that quotes one must not call it the other.
//
// The slowest-1% means take `ceil(n / 100)` frames (and `ceil(n / 1000)` for the 0.1% low), never
// zero. **With the harness's 108 steady frames the 0.1% low is therefore exactly one frame and is
// identical to `max`.** It is reported anyway so a longer run can use it, but at 108 frames it
// carries a single frame's luck and must not be A/B'd.
struct Distribution {
    std::size_t count = 0;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double p10 = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double low1Percent = 0.0;  // mean of the slowest ceil(n/100) frames -- NOT p99
    double low01Percent = 0.0; // mean of the slowest ceil(n/1000) frames
    double variance = 0.0;     // population variance, ms^2 (divided by n, not n-1: this is the
                               // whole measured window, not a sample drawn from a larger one)
    double stddev = 0.0;

    [[nodiscard]] bool valid() const { return count > 0; }
};

// Every statistic above, from one pass over the sorted sample. An empty input returns a
// `Distribution` with `count == 0` and zeroes -- not a zero-millisecond frame, which is why
// `valid()` exists and why nothing here reports "0.00 ms" for a measurement that never happened.
[[nodiscard]] Distribution describe(std::vector<double> samples);

// ---- A/B comparison (ADR-113) ------------------------------------------------------------------

// The measured within-session spread of this engine on the reference machine
// (docs/renderer-upgrade/01-audit-and-baseline.md §3.3: five consecutive Glowmere runs, GPU 1.0%,
// wall 3.0%). A difference smaller than this is not distinguishable from the machine, so the
// floors are set at twice the measured spread and a change below them is reported as "no result"
// rather than as a small win.
//
// These are floors, not the whole rule: `compareArms` raises them to the spread it actually
// observes between this session's own baseline blocks, so a noisy session cannot certify a
// difference the constant was calibrated to reject on a quiet one.
constexpr double kGpuNoiseFloorPercent = 2.0;
constexpr double kWallNoiseFloorPercent = 4.0;

// One clock's paired result. `deltaMs` is baseline minus arm, so positive means the arm was
// *faster* -- the direction an optimisation is hoping for.
struct PairedDelta {
    double baselineMs = 0.0;
    double armMs = 0.0;
    double deltaMs = 0.0;
    double deltaPercent = 0.0;       // of the baseline
    // The floor actually applied: the largest of the four components below. A difference smaller
    // than it is reported as no result.
    double noiseFloorPercent = 0.0;
    // The components, reported separately so a reader can see which one bound rather than being
    // handed one number and asked to trust it (ADR-148).
    //
    // **All three session components matter, and taking only the first was a defect.** The floor
    // was originally derived from the baseline blocks alone. In a null A/B the two arms are the same
    // code and are equally noisy, so a baseline that happened to land tight against an arm that
    // happened to wobble certified the wobble: a Glowmere null was blessed at -2.39% against the
    // 2.00% constant, and the agent that hit it withdrew two of its own rows rather than keep
    // numbers the harness had approved.
    double calibratedFloorPercent = 0.0; // the constant, from the reference machine
    double baselineSpreadPercent = 0.0;  // (max-min)/median of the baseline blocks' medians
    double armSpreadPercent = 0.0;       // the same, of the arm's
    // The peak-to-peak of the **per-pair deltas**, as a percentage of the baseline median. This is
    // the variability of the quantity actually being certified, and it is the only component that
    // can see pairs which disagree with each other while each arm is individually steady. ADR-113
    // printed the per-pair deltas for exactly that case and then certified their median anyway.
    double deltaSpreadPercent = 0.0;
    [[nodiscard]] bool isResult() const;
};

// One interleaved block: the same arm, measured once, on both clocks.
struct AbBlock {
    Distribution wallMs;
    Distribution gpuMs;
};

// The paired comparison of an interleaved A/B/A/B run.
//
// Paired, and interleaved, because the thing being defended against is drift: a machine that warms
// up, a shader cache that fills, another process that starts halfway through. Running all of A and
// then all of B charges that drift to the change. Running A, B, A, B and differencing each pair
// charges it to both arms equally, and the per-block deltas below make a drift that survives
// pairing visible instead of averaging it away.
//
// Cross-session comparison is not offered at all, deliberately. The audit found a 28% gap between
// two recorded Glowmere figures that run-to-run variance cannot explain, so a number from a
// document is not a baseline. Both arms live in one process, or there is no comparison.
// Whether the machine held still for long enough that the run means anything (ADR-181).
//
// Counterbalancing the arm order removes the *bias* from drift -- an arm that always runs second
// always pays for whatever the machine did during the run -- but it averages drift rather than
// detecting it, so a counterbalanced run can still return an impossible sign and say nothing about
// why. This is the detector: the baseline arm is measured throughout the run, so its own first half
// against its own second half is a direct reading of how far the machine moved while the experiment
// was happening. It costs nothing extra, because those measurements were taken anyway.
//
// A run whose drift is larger than the effect it claims is void. Not "noisy" -- void: the thing it
// measured changed underneath it, and the delta is a difference between two machines.
struct DriftCheck {
    double firstHalfMs = 0.0;  // the baseline arm's median over the first half of the run
    double secondHalfMs = 0.0; // ...and over the second half
    double driftMs = 0.0;      // second - first; signed, because which way it moved is diagnostic
    double driftPercent = 0.0; // of the first half
    int samples = 0;           // baseline blocks the check had to work with; < 2 means no check
    [[nodiscard]] bool measurable() const { return samples >= 2; }
    // The claimed effect has to stand clear of how far the machine moved. Equality voids: an effect
    // exactly the size of the drift is indistinguishable from the drift.
    [[nodiscard]] bool voids(double effectMs) const {
        return measurable() && std::abs(driftMs) >= std::abs(effectMs);
    }
};

[[nodiscard]] DriftCheck driftOf(const std::vector<AbBlock>& baselineBlocks, bool gpuClock);

struct AbSummary {
    std::string arm;
    int blocks = 0; // A/B pairs that produced a delta
    PairedDelta gpu;
    PairedDelta wall;
    DriftCheck gpuDrift;
    DriftCheck wallDrift;
    std::vector<double> gpuBlockDeltaMs;  // per pair, in the order they ran
    std::vector<double> wallBlockDeltaMs;
    // What the baseline arm's own median did across the session, as a percentage of its median:
    // (max - min) / median. This is the noise floor *observed* rather than assumed, and it is what
    // raises `PairedDelta::noiseFloorPercent` when the session is noisier than the constants.
    double gpuSpreadPercent = 0.0;
    double wallSpreadPercent = 0.0;
};

// Pairs block i of `baseline` with block i of `arm`. Extra blocks on either side are ignored (they
// have no partner, and an unpaired block is the drift this exists to remove). Fewer than one pair
// gives `blocks == 0` and no claim.
[[nodiscard]] AbSummary compareArms(std::string_view arm, const std::vector<AbBlock>& baseline,
                                    const std::vector<AbBlock>& armBlocks);

// ---- cluster occupancy (ADR-114) ---------------------------------------------------------------

// How hard the clustered-forward froxel grid is being worked. The grid is 16x8x24 = 3072 clusters
// and each holds at most `kMaxLightsPerCluster` light indices; what nobody could say before this
// existed is whether that cap is ever reached, or whether the grid is mostly empty and the cost is
// somewhere else entirely.
//
// **Every count here is the *uncapped demand*: how many lights actually reach the cluster, not how
// many the list had room for.** That is the whole point -- a statistic computed after the cap
// saturates at the cap and can never show that the cap is the problem. `dropped` is the difference
// the cap makes, and it is the number that says whether lights are being lost.
//
// The grid is written by a compute pass (`shaders/clusters.wgsl`) and reading the buffer back
// would stall the frame being measured, so these are computed on the CPU from the same inputs the
// pass is given, by the same reference implementation (`assignClusters`) that
// `tests/rendering/test_shadows_gpu.cpp` already checks the GPU's own output against index for
// index. It is a replica of the pass, not an approximation of it -- but it is CPU work inside a
// measured frame, so it is off unless asked for.
struct ClusterOccupancy {
    std::uint32_t clusters = 0;   // froxels examined (grid.count())
    std::uint32_t lights = 0;     // local lights offered to the grid (directionals never enter it)
    std::uint32_t cap = 0;        // kMaxLightsPerCluster at the time of measurement
    std::uint32_t empty = 0;      // clusters no light reaches -- shading there pays nothing per-light
    std::uint32_t overflowed = 0; // clusters whose demand exceeded `cap`, so a light was dropped
    std::uint32_t min = 0;
    std::uint32_t max = 0;        // the busiest cluster's demand. > cap means the cap bites.
    std::uint32_t p50 = 0;
    std::uint32_t p90 = 0;
    std::uint32_t p99 = 0;
    double mean = 0.0;
    std::uint64_t demand = 0;     // sum of every cluster's uncapped count
    std::uint64_t dropped = 0;    // demand - what the capped lists actually hold
};

// ---- the machine-readable record (ADR-113) -----------------------------------------------------

// A number without its conditions is not comparable with any other number, and the audit's two
// unexplained Glowmere figures are what that costs. Every record carries these, and the JSON is
// unreadable without them by design.
struct BenchmarkConditions {
    std::string scene;        // the --composition or --project path, as given
    std::string sceneKind;    // "composition" or "project"
    std::string arm;          // "baseline", or the phase this block switched off
    // The camera, by name *and* by pose. The name alone does not identify the workload -- a
    // directed camera moves, and "the Glowmere camera" covered three different views on three
    // days of this investigation. The pose is what decides how much of the valley is on screen.
    std::string camera;
    double cameraPosition[3] = {0.0, 0.0, 0.0};
    double cameraTarget[3] = {0.0, 0.0, 0.0};
    double fovYDegrees = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string qualityTier;  // the render quality tier this run used
    std::string gitRevision;  // the engine revision, short; "unknown" when the build could not see one
    bool gitDirty = false;    // uncommitted changes were in the tree when the binary was built
    std::string buildType;    // "Release", "Debug", ...
    std::string backend;      // the WebGPU backend, e.g. "Metal"
    std::string platform;     // the adapter: two GPUs are two machines, whatever else matches
    // One id per *process*. Two records sharing it were measured in one machine session and may be
    // compared; two that do not, may not. This is the field that makes the rule checkable rather
    // than a convention somebody remembers.
    std::string sessionId;
    std::string startedAt;    // ISO-8601, local time, for the human reading the file
    int framesRendered = 0;   // frames this block rendered, warm-up included
    int warmupFrames = 0;     // leading frames discarded before any statistic was taken
    int measuredFrames = 0;   // framesRendered - warmupFrames
    double offlineFps = 0.0;  // the fixed-step clock's rate: which frame range the block covered
    // True when cluster-occupancy instrumentation ran. It is CPU work inside the measured frames,
    // so the wall-clock numbers of such a record are *perturbed* and must not be A/B'd against a
    // record without it. The GPU numbers are unaffected.
    bool clusterStats = false;
};

// The workload the timings were taken over. Each is the median over the measured window, because a
// counter can move frame to frame (an indirect draw's instance count lands a frame or three late,
// a particle system emits in bursts) and one frame's value would not describe the window the
// timings came from. `varied` says whether any of them moved at all: when it is false these are
// exact for every measured frame, and when it is true a reader knows the medians are summarising.
struct BenchmarkCounters {
    double draws = 0.0;
    double shadowDraws = 0.0;
    double triangles = 0.0;         // submitted, camera-side (RenderStats::triangles)
    double logicalTriangles = 0.0;  // what the world contains, before LOD and culling
    double visibleInstances = 0.0;
    double culledInstances = 0.0;
    double lod[4] = {0.0, 0.0, 0.0, 0.0};
    double shadowCasters = 0.0;
    // Every enabled light the frame shaded with. Not `RenderStats::lights`, which counts slots in
    // the 8-long uniform fallback array and reports 8 for a 230-light scene.
    double lights = 0.0;
    double directionalLights = 0.0; // evaluated per fragment, never through the grid
    double clusteredLights = 0.0;   // the local half, routed through the froxel grid
    double uniformPathLights = 0.0; // the fallback array's occupancy, for what it is worth
    double particleSystems = 0.0;
    double particleCapacity = 0.0;   // sum of the pools; not how many are alive
    double particlesEmitted = 0.0;   // spawns requested this frame (the GPU clamps to free slots)
    double transientTextures = 0.0;
    double entities = 0.0;
    double computeDispatches = 0.0;
    double gpuPasses = 0.0;
    bool varied = false;
};

// One block of one arm: its conditions, its two clocks, where the GPU time went, and the workload
// it went on.
struct BenchmarkRecord {
    BenchmarkConditions conditions;
    Distribution wallMs;
    Distribution gpuMs;
    // The CPU frame: `CpuFrameBreakdown::totalMs` per frame, distributed. Distinct from `wallMs`,
    // which is the whole headless iteration -- engine update, scene rebuild, encode, submit and
    // whatever the loop does around them. A change that moves one and not the other says which.
    //
    // **On the offline path this includes `queueWaitMs`, which is the CPU blocking on the GPU.**
    // So a Glowmere record showing a 21 ms CPU frame against an 18.5 ms GPU frame is not a
    // CPU-bound frame: it is 0.7 ms of encode and the rest spent waiting. `cpuStageMedianMs`
    // splits it, and `queueWait` is the line that says how much of it was waiting.
    Distribution cpuMs;
    std::vector<gpu::TimelineInterval> passMedianMs; // per label, largest first
    BenchmarkCounters counters;
    CpuFrameBreakdown cpuMedian; // the CPU stage split, each stage's median over the window
    // Only filled when `conditions.clusterStats` -- see ClusterOccupancy. Taken on the last
    // measured frame rather than averaged, because occupancy is a property of one camera position
    // and a mean over a moving camera describes no camera at all.
    ClusterOccupancy clusters;
    bool haveClusters = false;
};

// The whole run as JSON: one record per block, plus the A/B summary when there was one. Pretty
// printed, because these files are read by people at least as often as by scripts.
[[nodiscard]] std::string benchmarkJson(const std::vector<BenchmarkRecord>& records,
                                        const AbSummary* ab);


// ---- scalability sweeps (ADR-144) --------------------------------------------------------------
//
// A curve is a set of arms measured at different values of one independent variable, and the whole
// difficulty of this project has been that a curve looks like a finding whether or not it is one.
// ADR-131 records what that costs: a single-run sweep put a minimum at 8,000 px/triangle against a
// competitor 52% slower, and three repeats dissolved every interval into every other. The instrument
// was fine. The subject did not vary, and one run per arm could not tell the difference.
//
// So this type refuses to represent a curve that does not carry its own noise floor. A `SweepPoint`
// holds *every repeat*, not a summary of them, and `summariseSweep` derives the floor from the arms'
// own spread rather than from a constant somebody calibrated on another scene. Every comparison the
// summary offers is against that floor. There is deliberately no way to ask this type "which arm was
// fastest" without also being told whether the question is answerable.
struct SweepPoint {
    std::string arm;              // the label this arm is reported under
    double x = 0.0;               // the independent variable: objects, instances, visible fraction
    std::vector<double> repeats;  // one per repeat, in the order taken -- never pre-averaged
    // Filled by `summariseSweep`.
    Distribution stats;           // over the repeats, not over frames: n is the repeat count
    double spreadPercent = 0.0;   // (max - min) / median, in percent: this arm's own reproducibility
    // Counters the arm was taken at, carried so a curve records the workload it measured rather
    // than only the number it produced. Zero means "not recorded", which is why they are doubles
    // and not optionals: a curve that forgot to record them says so by reporting zero everywhere.
    double draws = 0.0;
    double triangles = 0.0;
    double visibleInstances = 0.0;
    double culledInstances = 0.0;
    double entities = 0.0;
};

// The curve, with the verdict attached. `noiseFloorPercent` is the largest spread any single arm
// showed, floored at `kGpuNoiseFloorPercent` -- so a quiet session cannot certify below the
// engine-wide floor, and a noisy one raises the bar on itself. This is ADR-113 §4's rule applied to
// a sweep instead of to an A/B pair.
struct SweepSummary {
    std::string subject;   // what was varied and what was held
    std::string xLabel;
    std::string yLabel;
    std::vector<SweepPoint> points;  // in the order given, which is the order of x, not of time

    double noiseFloorPercent = 0.0;
    // The two ends, which is the question a scalability curve is asked: over the whole swept range,
    // how much did cost move? `endpointChangePercent` is (last - first) / first.
    double endpointChangePercent = 0.0;
    // True when the ends differ by more than the floor. **When this is false the curve has measured
    // that the cost does not depend on x at this instrument's resolution** -- which for an
    // existence sweep is the result being hoped for, and for a visibility sweep is a broken fixture.
    bool endpointsSeparated = false;
    // Cost per unit x between the ends, in the y unit. Meaningless when `endpointsSeparated` is
    // false, and reported anyway so a reader can see how small "not separated" was.
    double slopePerUnitX = 0.0;
    // The same endpoint comparison taken on each arm's **fastest repeat** instead of its median.
    //
    // This is the reading `docs/performance.md` reached and ADR-113 kept without adopting: under
    // contention the medians of identical runs differed by 3x while the minima agreed to a tenth of
    // a millisecond, because contention is never negative. On a machine shared with other agents --
    // which is the machine this project actually has -- the median curve can be swamped while the
    // minimum curve is clean.
    //
    // It is reported *alongside* the median reading and never instead of it, because it is blind to
    // exactly what the 1% low was added to see: a change that leaves the fast frames alone and
    // doubles the slow ones is invisible here. Read it as "what the hardware does when nothing else
    // is in the way", not as "what the frame costs".
    double endpointChangeMinPercent = 0.0;
    // The floor for the minimum reading: the largest gap any arm showed between its own fastest
    // repeat and its second-fastest, as a percentage. If the fastest repeats themselves scatter,
    // the minimum is not the clean statistic it is being used as, and this says so.
    double minFloorPercent = 0.0;
    bool endpointsSeparatedByMin = false;
    // The largest step between *adjacent* arms that clears the floor, and where it is. A curve can
    // be flat end to end and still have a knee in the middle; ADR-131's expensive half is exactly
    // that shape, and an endpoint-only verdict would have missed it.
    std::size_t largestStepIndex = 0;   // index of the arm the step lands on; 0 when there is none
    double largestStepPercent = 0.0;
    bool haveStep = false;

    [[nodiscard]] bool valid() const { return points.size() >= 2; }
};

// Derives the statistics above. Arms with no repeats are kept and reported as invalid rather than
// dropped: an arm that failed to measure is information, and silently shortening a curve is how a
// sweep comes to describe a different experiment than the one that ran.
[[nodiscard]] SweepSummary summariseSweep(std::string subject, std::string xLabel, std::string yLabel,
                                          std::vector<SweepPoint> points);

// The curve as a fixed-width table, every repeat's spread in its own column and the verdict below
// it. Printed by the perf tests; also the thing pasted into an ADR, which is why it is text and not
// a struct somebody has to format again.
[[nodiscard]] std::string sweepTable(const SweepSummary& summary);

} // namespace avgen::rendering
