#pragma once

// GPU stepping of simulated grid fields (ADR-032). Owns the ping-pong buffers, drives the
// kernels in `shaders/simulate.wgsl` and copies the finished state into the shared grid table
// (`FieldUniforms::gridBuffer()`), which every module that includes `fields.wgsl` reads at
// group 0 binding 15 - so a `FieldKind::Grid` field is sampled by effectors, deformers,
// particles, materials and the volume pass with no change on their side.
//
// Determinism. A grid has taken `floor(renderTime * simRate)` sub-steps of `1 / simRate` seconds,
// independent of the frame rate: every frame runs the backlog (`clamp(target - taken, 0, maxSubSteps)`
// in continuous play; the WHOLE backlog, up to `kMaxCatchUpSteps`, on the first frame after a reset and
// on the frame after a seek -- ADR-1114). All kernels gather, except the agents' deposits, which are
// u32 fixed-point atomic adds and so order-free (ADR-1120). The iteration counts are fixed, the seed is
// explicit and the initial state comes from `spatial::GridField::reset()` on the CPU (agents: a hash of
// their index), so two runs are bit-identical.
//
// ADR-1119, per-step inputs: sub-step k is computed with its OWN field block, packed at its own second
// ((k + 1) / simRate) -- field animation, wave fronts, onset ages and the newest audio row of that
// second, and the step index (FieldBlock::pad[0]) -- bound by dynamic offset. So a step replayed after
// a seek sees exactly what it saw when it was played, and a grid fed by moving or audio-driven fields
// replays exactly. (Before, every sub-step of a frame read the frame's block.) What is still sampled per
// FRAME: a node transform the engine animates (a field's frame), and a triggered field's age (ADR-906).
//
// ADR-1119, GPU checkpoints: while a grid steps -- in play or in a replay -- it copies its state (cells,
// and agents) GPU-to-GPU every `checkpointInterval` seconds of steps, under a memory budget
// (`setCheckpointBudget`, 512 MB by default; over it, a grid's spacing doubles and the odd ones go). A
// seek restores the newest checkpoint at or before the target whose key matches -- the grid's settings,
// the scene's input key (every parameter base, the audio revision: FieldSet::inputKey) -- and replays at
// most the spacing. No checkpoint: replay from the initial state. Slower, never inexact. A live input's
// audio has no past, so a grid fed by it never restores.

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
    // ADR-1119/1120
    std::uint64_t agents = 0;           // agents stepped
    std::uint64_t stateBytes = 0;       // ping-pong + initial + agents + accumulators
    std::uint32_t checkpoints = 0;      // held
    std::uint64_t checkpointBytes = 0;  // held, over every grid
    std::uint32_t checkpointsTaken = 0; // this frame
    std::uint32_t restores = 0;         // this frame: a seek restored a checkpoint
    std::uint64_t restoredFromStep = 0; // the step the last restore landed on
};

// Group 0 binding 0 of every kernel (224 bytes in a 256-byte dynamic-offset slot). Mirrors
// `SimUniforms` in shaders/simulate.wgsl.
struct SimUniforms {
    glm::uvec4 res;      // resolution.xyz, components per cell
    glm::uvec4 layout0;  // offset in floats, cell count, wrap (0 clamp, 1 wrap), 0
    glm::vec4 bounds0;   // boundsMin.xyz, dt
    glm::vec4 bounds1;   // boundsMax.xyz, 0 (time is the step's field block's, ADR-1119)
    glm::vec4 params0;   // injectRate, advect, diffusion, dissipation
    glm::vec4 params1;   // feed, kill, diffusionA, diffusionB
    glm::ivec4 slots;    // injection field slot, velocity field slot, mode, deposit field slot (agents)
    glm::uvec4 agents;   // ADR-1120: count, species, offset (in agents), seed
    glm::vec4 agentSense; // sensorAngle, sensorDistance (cells), turnAngle, stepSize (cells)
    glm::vec4 agentDeposit; // depositAmount, repel, 0, 0
    // ADR-1201, excitable grids (zero for every other mode):
    glm::vec4 excite0;     // threshold, coupling, waveSpeed, riseRate
    glm::vec4 excite1;     // excitationDecay, refractoryTime, refractoryStrength, energyTime
    glm::vec4 excite2;     // wakeTime, noise, ceiling (0 = none), 0
    glm::uvec4 exciteSlots; // conductivity field slot (as i32 bits; -1 = none), noise epoch (steps), seed, 0
};
static_assert(sizeof(SimUniforms) == 224);

class Simulation {
public:
    Simulation(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // `gridTable` is FieldUniforms' table (a zeroed private one when null). The field blocks the kernels
    // read are this object's own, one per sub-step (ADR-1119).
    [[nodiscard]] Result<void> init(wgpu::Buffer gridTable = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of simulate.wgsl (keeps the state)

    // Encodes this frame's sub-steps for every enabled grid. Call once per frame, after
    // FieldUniforms::update() and before anything that samples the grids. `fields` is that
    // FieldUniforms: its audio ring is repositioned for a long replay and put back (null: no audio).
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                FieldUniforms* fields = nullptr);
    // Drops the simulation state; the next update() re-uploads the initial state. Checkpoints stay
    // (they are keyed; a stale one is never restored).
    void reset();
    // The timeline jumped (a seek): the next update() runs the backlog -- from the best checkpoint --
    // instead of `maxSubSteps` of it. A jump backwards restores or resets, which update() sees itself.
    void markDiscontinuity();
    // ADR-1119: the memory all checkpoints of all grids may hold (bytes). 0 disables checkpoints.
    void setCheckpointBudget(std::uint64_t bytes);
    void dropCheckpoints();
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed.
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    [[nodiscard]] const SimulationStats& stats() const { return stats_; }
    // Blocking readback of one grid's cell values from the shared table (tests and tools).
    [[nodiscard]] Result<std::vector<float>> readGrid(const spatial::FieldSet& fields, std::size_t index);
    // Blocking readback of one agents grid's agents (x cell, z cell, heading, species), tests and tools.
    [[nodiscard]] Result<std::vector<glm::vec4>> readAgents(const spatial::FieldSet& fields, std::size_t index);

    static constexpr std::uint32_t kMaxGrids = 8;
    static constexpr std::uint32_t kUniformStride = 256;
    static constexpr std::uint32_t kWorkgroup = 64;
    // Sub-steps one frame may run after a reset or a seek. A ceiling against a runaway, not a
    // budget: 30 minutes at the default 60 Hz sub-step, past any song. Beyond it the grid skips
    // ahead with a warning and the frame is knowingly not the played one.
    static constexpr std::uint64_t kMaxCatchUpSteps = 60ull * 60ull * 30ull;
    // Catch-up sub-steps per command buffer, so a long replay is many bounded submissions. Also at
    // most 4 s of steps, so a replay chunk's audio rows all fit the field table's ring (ADR-1119).
    static constexpr std::uint32_t kStepsPerSubmit = 256;
    // Field blocks one submission may use (the frame's own pass: up to kMaxGrids x maxSubSteps).
    static constexpr std::uint32_t kStepBlockSlots = 512;
    static constexpr std::uint64_t kDefaultCheckpointBudget = 512ull * 1024ull * 1024ull;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SimulationStats stats_;
};

} // namespace avgen::rendering
