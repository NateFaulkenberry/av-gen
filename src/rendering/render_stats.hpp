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

} // namespace avgen::rendering
