#pragma once

// GPU stepping of simulated grid fields (ADR-032). Owns the ping-pong buffers, drives the
// kernels in `shaders/simulate.wgsl` and copies the finished state into the shared grid table
// (`FieldUniforms::gridBuffer()`), which every module that includes `fields.wgsl` reads at
// group 0 binding 15 - so a `FieldKind::Grid` field is sampled by effectors, deformers,
// particles, materials and the volume pass with no change on their side.
//
// Determinism. Time is the only input: a grid has taken `floor(renderTime * simRate)` sub-steps
// of `1 / simRate` seconds, independent of the frame rate, because every frame runs the backlog
// (`clamp(target - taken, 0, maxSubSteps)` in continuous play; the WHOLE backlog, up to
// `kMaxCatchUpSteps`, on the first frame after a reset and on the frame after a seek, so an offline
// render that starts mid-timeline and a scrub both reach the state a played timeline did --
// ADR-1114; until then the catch-up was capped at 240 steps and a seek past 4 s drew a younger
// grid, ADR-581). All kernels gather, the iteration counts are fixed, the seed is explicit and the initial
// state comes from `spatial::GridField::reset()` on the CPU, so two runs are bit-identical.
// Fields sampled by the kernels are evaluated at the frame's `renderTime` for every sub-step of
// that frame.
//
// A reset (initial state re-uploaded, step counter cleared) happens when the set of grids
// changes structurally or when the render time moves backwards; a seek forwards keeps the state and
// runs the gap. What the replay does NOT reproduce: fields sampled by the kernels are evaluated at
// the arriving frame's time for every replayed step, as they are for every sub-step of one played
// frame, so a grid fed by a field that moves in time is exact only up to that (ADR-1114 sec. 4).

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/scene.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;

struct SimulationStats {
    std::uint32_t grids = 0;       // simulated grids this frame
    std::uint32_t steps = 0;       // sub-steps encoded this frame (summed over grids)
    std::uint64_t catchUpSteps = 0; // of which owed by a reset or a seek (ADR-1114)
    std::uint32_t dispatches = 0;  // compute dispatches encoded this frame
    std::uint64_t tableFloats = 0; // floats the scene's grids occupy in the shared table
    double simulateMs = -1.0;      // GPU time of the compute passes (-1 = none / unavailable)
};

// Group 0 binding 0 of every kernel (112 bytes in a 256-byte dynamic-offset slot). Mirrors
// `SimUniforms` in shaders/simulate.wgsl.
struct SimUniforms {
    glm::uvec4 res;      // resolution.xyz, components per cell
    glm::uvec4 layout0;  // offset in floats, cell count, wrap (0 clamp, 1 wrap), 0
    glm::vec4 bounds0;   // boundsMin.xyz, dt
    glm::vec4 bounds1;   // boundsMax.xyz, time
    glm::vec4 params0;   // injectRate, advect, diffusion, dissipation
    glm::vec4 params1;   // feed, kill, diffusionA, diffusionB
    glm::ivec4 slots;    // injection field slot, velocity field slot, mode, 0
};
static_assert(sizeof(SimUniforms) == 112);

class Simulation {
public:
    Simulation(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // `fieldBlock` and `gridTable` are FieldUniforms' buffers (zeroed private ones when null).
    [[nodiscard]] Result<void> init(wgpu::Buffer fieldBlock = nullptr, wgpu::Buffer gridTable = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of simulate.wgsl (keeps the state)

    // Encodes this frame's sub-steps for every enabled grid. Call once per frame, after
    // FieldUniforms::update() and before anything that samples the grids.
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time);
    // Drops the simulation state; the next update() re-uploads the initial state.
    void reset();
    // The timeline jumped (a seek): the next update() runs the whole backlog instead of
    // `maxSubSteps` of it. A jump backwards also resets, which update() sees for itself.
    void markDiscontinuity();
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed.
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    [[nodiscard]] const SimulationStats& stats() const { return stats_; }
    // Blocking readback of one grid's cell values from the shared table (tests and tools).
    [[nodiscard]] Result<std::vector<float>> readGrid(const spatial::FieldSet& fields, std::size_t index);

    static constexpr std::uint32_t kMaxGrids = 8;
    static constexpr std::uint32_t kUniformStride = 256;
    static constexpr std::uint32_t kWorkgroup = 64;
    // Sub-steps one frame may run after a reset or a seek. A ceiling against a runaway, not a
    // budget: 30 minutes at the default 60 Hz sub-step, past any song. Beyond it the grid skips
    // ahead with a warning and the frame is knowingly not the played one.
    static constexpr std::uint64_t kMaxCatchUpSteps = 60ull * 60ull * 30ull;
    // Catch-up sub-steps per command buffer, so a long replay is many bounded submissions.
    static constexpr std::uint32_t kStepsPerSubmit = 256;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SimulationStats stats_;
};

} // namespace avgen::rendering
