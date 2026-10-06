#include "rendering/simulation.hpp"
#include "gpu/resource_stats.hpp"

#include "rendering/field_uniforms.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "spatial/grid_field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

namespace {

constexpr std::uint64_t kBufferAlignment = 4;
// One field block per sub-step, 256-aligned for the dynamic offset (ADR-1119).
constexpr std::uint64_t kStepBlockStride = (sizeof(FieldBlock) + 255) / 256 * 256;
// The agents' deposits: u32 fixed point, 1/4096 of a unit (ADR-1120).
constexpr float kDepositScale = 4096.0f;

std::uint64_t layoutHashOf(const std::vector<spatial::GridField>& grids) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint64_t v) {
        h = (h ^ v) * 0x100000001b3ull;
    };
    mix(grids.size());
    for (const spatial::GridField& g : grids) {
        mix(g.layoutHash()); // ADR-1122: behaviour changes continue the state; only the layout re-seeds it
    }
    return h;
}

std::uint64_t mixKey(std::uint64_t h, std::uint64_t v) {
    return (h ^ v) * 0x100000001b3ull;
}

// Agent offsets (in agents) of each grid, back to back over the agents grids, in list order.
std::vector<std::uint64_t> agentOffsets(const std::vector<spatial::GridField>& grids, std::uint64_t& total) {
    std::vector<std::uint64_t> out(grids.size(), 0);
    total = 0;
    for (std::size_t i = 0; i < grids.size(); ++i) {
        out[i] = total;
        if (grids[i].agents() && grids[i].enabled) {
            total += static_cast<std::uint64_t>(std::max(grids[i].agentCount, 0));
        }
    }
    return out;
}

} // namespace

struct Simulation::Impl {
    struct Checkpoint {
        std::uint64_t step = 0;
        wgpu::Buffer buffer;   // the grid's cells, then (agents grids) its agents
        std::uint64_t bytes = 0;
    };
    struct GridState {
        std::uint64_t stepsTaken = 0;
        bool currentIsA = true;
        std::uint64_t key = 0;               // ADR-1119: what a checkpoint must match to be restored
        std::uint64_t intervalSteps = 0;     // the spacing now (doubled when the budget overflows)
        std::vector<Checkpoint> checkpoints; // ascending step
    };

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {
        uniformStaging.resize(static_cast<std::size_t>(kMaxGrids) * kUniformStride);
    }

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    bool ensureBuffers(std::uint64_t floats, std::uint64_t agents);
    void rebuildGroups();
    // The grid's initial state into A, B, initial and the table; its accumulators zeroed. An agents grid
    // also needs its agents seeded, which is a dispatch (`initAgents`).
    void uploadInitialState(const std::vector<spatial::GridField>& grids, std::size_t index);
    std::uint64_t checkpointBytes() const {
        std::uint64_t b = 0;
        for (const GridState& s : states) {
            for (const Checkpoint& c : s.checkpoints) {
                b += c.bytes;
            }
        }
        return b;
    }

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::Buffer gridTable;
    wgpu::Buffer bufferA;
    wgpu::Buffer bufferB;
    wgpu::Buffer bufferInitial;
    wgpu::Buffer accum;       // ADR-1120: u32 fixed-point deposits, one per float of the table
    wgpu::Buffer agentBuffer; // ADR-1120: vec4 per agent
    std::uint64_t bufferBytes = 0;
    std::uint64_t agentBytes = 0;
    wgpu::Buffer uniforms;
    wgpu::Buffer stepBlocks;  // ADR-1119: kStepBlockSlots field blocks
    std::vector<std::uint8_t> blockStaging;
    wgpu::BindGroupLayout layout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::ComputePipeline injectPipeline;
    wgpu::ComputePipeline advectPipeline;
    wgpu::ComputePipeline diffusePipeline;
    wgpu::ComputePipeline dissipatePipeline;
    wgpu::ComputePipeline reactionPipeline;
    wgpu::ComputePipeline copyPipeline;
    wgpu::ComputePipeline agentsInitPipeline;
    wgpu::ComputePipeline agentsMovePipeline;
    wgpu::ComputePipeline agentsResolvePipeline;
    wgpu::BindGroup groupToA;      // dst = A, src = B, initial = C
    wgpu::BindGroup groupToB;      // dst = B, src = A, initial = C
    wgpu::BindGroup groupToCFromA; // dst = C, src = A (the diffusion snapshot)
    wgpu::BindGroup groupToCFromB; // dst = C, src = B
    gpu::FrameTimeline* timeline = nullptr;
    std::vector<std::uint8_t> uniformStaging;
    std::vector<GridState> states;
    std::vector<bool> agentsPending; // grids whose agents must be seeded before they step
    std::uint64_t layoutHash = 0;
    std::uint64_t checkpointBudget = kDefaultCheckpointBudget;
    double lastRenderTime = -1.0;
    bool discontinuity = false; // set by markDiscontinuity(); the next update runs the full backlog
    double lastMs = -1.0;
    bool passThisFrame = false;
    bool needsReset = true;
    bool initialised = false;
    bool warnedLimit = false;
};

Simulation::Simulation(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

Simulation::~Simulation() = default;

Result<void> Simulation::init(wgpu::Buffer gridTable) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.gridTable = std::move(gridTable);
    if (!im.gridTable) {
        wgpu::BufferDescriptor desc{};
        desc.label = "simulation-private-grid-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        desc.size = FieldUniforms::kGridBufferSize;
        im.gridTable = device.CreateBuffer(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "simulation-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(kMaxGrids) * kUniformStride;
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "simulation-step-field-blocks";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(kStepBlockSlots) * kStepBlockStride;
        im.stepBlocks = device.CreateBuffer(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(SimUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::Storage;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Compute;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Compute;
        entries[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[4].binding = 4;
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[4].buffer.hasDynamicOffset = true; // ADR-1119: the step's own field block
        entries[4].buffer.minBindingSize = FieldUniforms::kBufferSize;
        entries[5].binding = 5;
        entries[5].visibility = wgpu::ShaderStage::Compute;
        entries[5].buffer.type = wgpu::BufferBindingType::Storage;
        entries[6].binding = 6;
        entries[6].visibility = wgpu::ShaderStage::Compute;
        entries[6].buffer.type = wgpu::BufferBindingType::Storage;
        entries[7].binding = 15;
        entries[7].visibility = wgpu::ShaderStage::Compute;
        entries[7].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "simulation-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.layout = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.label = "simulation-pipeline-layout";
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &im.layout;
        im.pipelineLayout = device.CreatePipelineLayout(&pdesc);
    }
    auto module = im.shaders.load("simulate.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipelines(*module); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> Simulation::reload() {
    auto module = impl_->shaders.load("simulate.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return impl_->createPipelines(*module);
}

Result<void> Simulation::Impl::createPipelines(const wgpu::ShaderModule& module) {
    const auto& device = context.device();
    auto make = [&](const char* entry) -> Result<wgpu::ComputePipeline> {
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = entry;
        desc.layout = pipelineLayout;
        desc.compute.module = module;
        desc.compute.entryPoint = entry;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::ComputePipeline pipeline = gpu::createComputePipeline(device, &desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("simulation compute pipeline '{}' failed: {}", entry, error);
        }
        return pipeline;
    };
    const std::array<std::pair<const char*, wgpu::ComputePipeline*>, 9> all{{
        {"cs_inject", &injectPipeline},
        {"cs_advect", &advectPipeline},
        {"cs_diffuse", &diffusePipeline},
        {"cs_dissipate", &dissipatePipeline},
        {"cs_reaction", &reactionPipeline},
        {"cs_copy", &copyPipeline},
        {"cs_agents_init", &agentsInitPipeline},
        {"cs_agents_move", &agentsMovePipeline},
        {"cs_agents_resolve", &agentsResolvePipeline},
    }};
    std::array<wgpu::ComputePipeline, 9> built{};
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto p = make(all[i].first);
        if (!p) {
            return std::unexpected(p.error()); // all or nothing: a failed hot reload keeps the old set
        }
        built[i] = *p;
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        *all[i].second = built[i];
    }
    return {};
}

bool Simulation::Impl::ensureBuffers(std::uint64_t floats, std::uint64_t agents) {
    const std::uint64_t bytes = std::max<std::uint64_t>(floats * sizeof(float), 16);
    const std::uint64_t aBytes = std::max<std::uint64_t>(agents * 16, 16);
    if (bufferA && bufferBytes >= bytes && agentBytes >= aBytes) {
        return false;
    }
    const auto& device = context.device();
    wgpu::BufferDescriptor desc{};
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    desc.size = (bytes + kBufferAlignment - 1) / kBufferAlignment * kBufferAlignment;
    desc.label = "simulation-a";
    bufferA = device.CreateBuffer(&desc);
    desc.label = "simulation-b";
    bufferB = device.CreateBuffer(&desc);
    desc.label = "simulation-initial";
    bufferInitial = device.CreateBuffer(&desc);
    desc.label = "simulation-deposits";
    accum = device.CreateBuffer(&desc);
    bufferBytes = desc.size;
    desc.label = "simulation-agents";
    desc.size = aBytes;
    agentBuffer = device.CreateBuffer(&desc);
    agentBytes = aBytes;
    rebuildGroups();
    return true;
}

void Simulation::Impl::rebuildGroups() {
    const auto& device = context.device();
    auto make = [&](const wgpu::Buffer& dst, const wgpu::Buffer& src, const wgpu::Buffer& init,
                    const char* label) {
        std::array<wgpu::BindGroupEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].buffer = uniforms;
        entries[0].size = sizeof(SimUniforms);
        entries[1].binding = 1;
        entries[1].buffer = dst;
        entries[1].size = bufferBytes;
        entries[2].binding = 2;
        entries[2].buffer = src;
        entries[2].size = bufferBytes;
        entries[3].binding = 3;
        entries[3].buffer = init;
        entries[3].size = bufferBytes;
        entries[4].binding = 4;
        entries[4].buffer = stepBlocks;
        entries[4].size = FieldUniforms::kBufferSize;
        entries[5].binding = 5;
        entries[5].buffer = agentBuffer;
        entries[5].size = agentBytes;
        entries[6].binding = 6;
        entries[6].buffer = accum;
        entries[6].size = bufferBytes;
        entries[7].binding = 15;
        entries[7].buffer = gridTable;
        entries[7].size = FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = layout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    groupToA = make(bufferA, bufferB, bufferInitial, "simulation-group-to-a");
    groupToB = make(bufferB, bufferA, bufferInitial, "simulation-group-to-b");
    // The snapshot writes the initial buffer, so it cannot also read it: bind the spare
    // ping-pong buffer at binding 3 instead (cs_copy never reads it).
    groupToCFromA = make(bufferInitial, bufferA, bufferB, "simulation-group-to-c-from-a");
    groupToCFromB = make(bufferInitial, bufferB, bufferA, "simulation-group-to-c-from-b");
}

void Simulation::Impl::uploadInitialState(const std::vector<spatial::GridField>& grids, std::size_t index) {
    const auto& queue = context.queue();
    spatial::GridField grid = grids[index];
    grid.reset(); // zero (Rd: A = 1) plus the seeded fbm3 noise; deterministic in `seed`
    if (index < states.size()) {
        states[index].stepsTaken = 0;
        states[index].currentIsA = true;
    }
    if (index < agentsPending.size()) {
        agentsPending[index] = grid.agents() && grid.agentCount > 0;
    }
    if (grid.data.empty()) {
        return;
    }
    const std::uint64_t offset = spatial::gridTableOffset(grids, index) * sizeof(float);
    const std::uint64_t bytes = grid.data.size() * sizeof(float);
    queue.WriteBuffer(bufferA, offset, grid.data.data(), bytes);
    queue.WriteBuffer(bufferB, offset, grid.data.data(), bytes);
    queue.WriteBuffer(bufferInitial, offset, grid.data.data(), bytes);
    queue.WriteBuffer(gridTable, offset, grid.data.data(), bytes);
    const std::vector<std::uint32_t> zeros(grid.data.size(), 0u);
    queue.WriteBuffer(accum, offset, zeros.data(), bytes);
}

void Simulation::reset() {
    impl_->needsReset = true;
    impl_->lastRenderTime = -1.0;
}

void Simulation::markDiscontinuity() { impl_->discontinuity = true; }

void Simulation::setCheckpointBudget(std::uint64_t bytes) {
    impl_->checkpointBudget = bytes;
    if (bytes == 0) {
        dropCheckpoints();
    }
}

void Simulation::dropCheckpoints() {
    for (Impl::GridState& s : impl_->states) {
        s.checkpoints.clear();
    }
}

void Simulation::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                        FieldUniforms* fields) {
    Impl& im = *impl_;
    collectTimings();
    im.passThisFrame = false;
    stats_ = SimulationStats{};
    if (!im.initialised) {
        return;
    }
    const std::vector<spatial::GridField>& grids = scene.fields.grids;
    if (grids.empty()) {
        im.needsReset = true;
        return;
    }
    if (grids.size() > kMaxGrids && !im.warnedLimit) {
        log::warn("scene has {} simulated grids; only the first {} are stepped", grids.size(), kMaxGrids);
        im.warnedLimit = true;
    }
    const std::uint64_t floats = spatial::gridTableFloats(grids);
    stats_.tableFloats = floats;
    if (floats > spatial::kMaxGridTableFloats) {
        if (!im.warnedLimit) {
            log::warn("simulated grids need {} floats; the shared table holds {}", floats,
                      spatial::kMaxGridTableFloats);
            im.warnedLimit = true;
        }
        return;
    }
    std::uint64_t totalAgents = 0;
    const std::vector<std::uint64_t> agentOffset = agentOffsets(grids, totalAgents);
    const std::uint64_t hash = layoutHashOf(grids);
    const bool reallocated = im.ensureBuffers(floats, totalAgents);
    stats_.stateBytes = im.bufferBytes * 4 + im.agentBytes;
    const bool layoutChanged = reallocated || hash != im.layoutHash;
    if (layoutChanged) {
        im.states.assign(grids.size(), Impl::GridState{}); // checkpoints of another layout mean nothing
        im.agentsPending.assign(grids.size(), false);
    }
    if (layoutChanged || im.needsReset) {
        for (std::size_t i = 0; i < grids.size() && i < kMaxGrids; ++i) {
            im.uploadInitialState(grids, i);
        }
        im.layoutHash = hash;
        im.needsReset = false;
        im.lastRenderTime = -1.0; // the next block treats this as the first frame (full catch-up)
    }
    const bool firstFrame = im.lastRenderTime < 0.0;
    const bool backward = !firstFrame && time.renderTime < im.lastRenderTime;
    // A frame that follows a reset or a seek owes the whole backlog, not `maxSubSteps` of it: the
    // state it draws must be the state a play would have reached (ADR-1114) -- from the best
    // checkpoint (ADR-1119). `maxSubSteps` stays the stall guard for continuous playback.
    const bool catchUp = firstFrame || im.discontinuity || backward;
    im.discontinuity = false;
    im.lastRenderTime = time.renderTime;
    const spatial::AudioHistory* audio = scene.fields.audio.get();
    // ADR-1119: a live input's audio has no past; nothing fed by it is ever restored or kept.
    const bool checkpointsAllowed = im.checkpointBudget > 0 && !(audio != nullptr && audio->live());

    // ---- per-grid keys, restores, uniforms and step counts ----
    struct Work {
        std::size_t index = 0;
        std::uint32_t offset = 0; // dynamic uniform offset
        std::uint32_t steps = 0;
    };
    std::vector<Work> work;
    std::vector<std::pair<std::size_t, const Impl::Checkpoint*>> restores;
    std::uint32_t slot = 0;
    for (std::size_t i = 0; i < grids.size() && i < kMaxGrids; ++i) {
        const spatial::GridField& grid = grids[i];
        if (!grid.enabled || grid.floatCount() == 0 || !grid.validate()) {
            continue;
        }
        Impl::GridState& state = im.states[i];
        // The key a checkpoint must match: this grid's settings, the scene's input key (every parameter
        // base and the audio revision, FieldSet::inputKey) and the layout.
        std::uint64_t key = mixKey(0xcbf29ce484222325ull, grid.structuralHash());
        key = mixKey(key, scene.fields.inputKey);
        key = mixKey(key, hash);
        if (key != state.key) {
            state.checkpoints.clear();
            state.key = key;
        }
        state.intervalSteps = std::max<std::uint64_t>(state.intervalSteps,
            static_cast<std::uint64_t>(std::llround(static_cast<double>(grid.checkpointInterval) * grid.simRate)));
        const auto target = static_cast<std::uint64_t>(
            std::max(0.0, std::floor(time.renderTime * static_cast<double>(grid.simRate))));
        if (catchUp) {
            // The newest usable checkpoint at or before the target, if it is better than what the grid
            // already holds (ahead of it, or the grid is past the target).
            const Impl::Checkpoint* best = nullptr;
            if (checkpointsAllowed) {
                for (const Impl::Checkpoint& c : state.checkpoints) {
                    if (c.step <= target && (best == nullptr || c.step > best->step)) {
                        best = &c;
                    }
                }
            }
            const bool ahead = target < state.stepsTaken;
            if (best != nullptr && (best->step > state.stepsTaken || ahead)) {
                restores.emplace_back(i, best);
                state.stepsTaken = best->step;
                state.currentIsA = true;
                im.agentsPending[i] = false;
                ++stats_.restores;
                stats_.restoredFromStep = best->step;
            } else if (ahead) {
                im.uploadInitialState(grids, i); // a seek backwards with nothing to restore: replay from 0
            }
        }
        std::uint64_t backlog = target > state.stepsTaken ? target - state.stepsTaken : 0;
        const std::uint64_t budget = catchUp ? kMaxCatchUpSteps : static_cast<std::uint64_t>(grid.maxSubSteps);
        if (catchUp && backlog > budget) {
            // Beyond the ceiling the frame cannot be the played one, and says so.
            log::warn("grid '{}' is {} sub-steps behind, beyond the {}-step catch-up ceiling; skipping ahead",
                      grid.name, backlog, kMaxCatchUpSteps);
            state.stepsTaken = target - budget;
            backlog = budget;
        }
        if (catchUp && backlog > 0) {
            stats_.catchUpSteps += backlog;
        }
        const auto steps = static_cast<std::uint32_t>(std::min<std::uint64_t>(backlog, budget));
        SimUniforms u{};
        u.res = glm::uvec4(static_cast<std::uint32_t>(grid.resolution.x),
                           static_cast<std::uint32_t>(grid.resolution.y),
                           static_cast<std::uint32_t>(grid.resolution.z),
                           static_cast<std::uint32_t>(grid.components()));
        u.layout0 = glm::uvec4(static_cast<std::uint32_t>(spatial::gridTableOffset(grids, i)),
                               static_cast<std::uint32_t>(grid.cellCount()),
                               grid.wrap == spatial::GridWrap::Wrap ? 1u : 0u, 0u);
        u.bounds0 = glm::vec4(grid.boundsMin, 1.0f / grid.simRate);
        u.bounds1 = glm::vec4(grid.boundsMax, 0.0f);
        u.params0 = glm::vec4(grid.injectRate, grid.advect, grid.diffusion, grid.dissipation);
        u.params1 = glm::vec4(grid.feed, grid.kill, grid.diffusionA, grid.diffusionB);
        const int injectSlot = grid.injectField.empty() ? -1 : scene.fields.indexOf(grid.injectField);
        const int velocitySlot = grid.velocityField.empty() ? -1 : scene.fields.indexOf(grid.velocityField);
        const int depositSlot = grid.depositField.empty() ? -1 : scene.fields.indexOf(grid.depositField);
        u.slots = glm::ivec4(injectSlot, velocitySlot, static_cast<int>(grid.mode), depositSlot);
        // ADR-1163: a scalar grid's ceiling rides in agentDeposit.w, the lane agents leave unused.
        u.agentDeposit = glm::vec4(0.0f, 0.0f, 0.0f, grid.mode == spatial::GridMode::Scalar ? grid.ceiling : 0.0f);
        if (grid.agents()) {
            u.agents = glm::uvec4(static_cast<std::uint32_t>(std::max(grid.agentCount, 0)),
                                  static_cast<std::uint32_t>(grid.species),
                                  static_cast<std::uint32_t>(agentOffset[i]), grid.seed);
            u.agentSense = glm::vec4(grid.sensorAngle, grid.sensorDistance, grid.turnAngle, grid.stepSize);
            u.agentDeposit = glm::vec4(grid.depositAmount, grid.repel, kDepositScale, 0.0f);
            stats_.agents += static_cast<std::uint64_t>(std::max(grid.agentCount, 0));
        }
        const std::uint32_t offset = slot * kUniformStride;
        std::memcpy(im.uniformStaging.data() + offset, &u, sizeof(u));
        work.push_back(Work{i, offset, steps});
        ++slot;
        ++stats_.grids;
    }
    if (slot == 0) {
        return;
    }
    const auto& queue = im.context.queue();
    queue.WriteBuffer(im.uniforms, 0, im.uniformStaging.data(), static_cast<std::size_t>(slot) * kUniformStride);

    // ---- seeds and restores, ahead of every step ----
    {
        wgpu::CommandEncoder pre = im.context.device().CreateCommandEncoder();
        bool any = false;
        for (const Work& w : work) {
            if (!im.agentsPending[w.index]) {
                continue;
            }
            const spatial::GridField& grid = grids[w.index];
            wgpu::ComputePassDescriptor d{};
            d.label = "simulate-agents-init";
            wgpu::ComputePassEncoder cp = pre.BeginComputePass(&d);
            cp.SetPipeline(im.agentsInitPipeline);
            const std::array<std::uint32_t, 2> offsets{w.offset, 0u};
            cp.SetBindGroup(0, im.groupToB, offsets.size(), offsets.data());
            cp.DispatchWorkgroups((static_cast<std::uint32_t>(grid.agentCount) + kWorkgroup - 1) / kWorkgroup);
            cp.End();
            im.agentsPending[w.index] = false;
            any = true;
        }
        for (const auto& [index, checkpoint] : restores) {
            const spatial::GridField& grid = grids[index];
            const std::uint64_t cellBytes = grid.floatCount() * sizeof(float);
            const std::uint64_t offset = spatial::gridTableOffset(grids, index) * sizeof(float);
            pre.CopyBufferToBuffer(checkpoint->buffer, 0, im.bufferA, offset, cellBytes);
            if (grid.agents() && grid.agentCount > 0) {
                pre.CopyBufferToBuffer(checkpoint->buffer, cellBytes, im.agentBuffer, agentOffset[index] * 16,
                                       static_cast<std::uint64_t>(grid.agentCount) * 16);
            }
            any = true;
        }
        if (any) {
            wgpu::CommandBuffer commands = pre.Finish();
            queue.Submit(1, &commands);
        }
    }

    // ---- encode the sub-steps ----
    std::uint32_t encoded = 0;
    std::uint32_t blockSlot = 0; // the next free field-block slot of the submission being built
    // Packs the field block of grid `w`'s next `count` steps into consecutive slots from `blockSlot`.
    auto packBlocks = [&](const Work& w, std::uint32_t count) {
        const spatial::GridField& grid = grids[w.index];
        const Impl::GridState& state = im.states[w.index];
        const std::size_t first = blockSlot;
        im.blockStaging.resize(static_cast<std::size_t>(first + count) * kStepBlockStride);
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::uint64_t step = state.stepsTaken + k + 1;
            FieldBlock block = FieldUniforms::pack(scene.fields, static_cast<double>(step) / grid.simRate);
            block.pad[0] = static_cast<std::uint32_t>(step); // the step index the agents hash with
            std::memcpy(im.blockStaging.data() + (first + k) * kStepBlockStride, &block, sizeof(block));
        }
        blockSlot += count;
        return static_cast<std::uint32_t>(first);
    };
    // Encodes `count` sub-steps of one grid, whose field blocks start at slot `firstBlock`, into
    // `enc`; checkpoints are taken between passes when a step lands on the spacing.
    auto encodeSteps = [&](wgpu::CommandEncoder& enc, const Work& w, std::uint32_t count, std::uint32_t firstBlock,
                           const char* label, const wgpu::PassTimestampWrites* timestamps) {
        const spatial::GridField& grid = grids[w.index];
        Impl::GridState& state = im.states[w.index];
        const auto cells = static_cast<std::uint32_t>(grid.cellCount());
        const std::uint32_t groups = (cells + kWorkgroup - 1) / kWorkgroup;
        const bool reaction = grid.mode == spatial::GridMode::ReactionDiffusion;
        const bool agents = grid.agents();
        const std::uint64_t cellBytes = grid.floatCount() * sizeof(float);
        const std::uint64_t tableOffset = spatial::gridTableOffset(grids, w.index) * sizeof(float);
        const std::uint64_t agentBytes = agents ? static_cast<std::uint64_t>(std::max(grid.agentCount, 0)) * 16 : 0;
        wgpu::ComputePassDescriptor passDesc{};
        passDesc.label = label;
        passDesc.timestampWrites = timestamps; // the first pass only
        wgpu::ComputePassEncoder cp = enc.BeginComputePass(&passDesc);
        passDesc.timestampWrites = nullptr;
        std::array<std::uint32_t, 2> offsets{w.offset, 0u};
        // Writes the other ping-pong buffer and makes it current.
        auto dispatch = [&](const wgpu::ComputePipeline& pipeline) {
            cp.SetPipeline(pipeline);
            cp.SetBindGroup(0, state.currentIsA ? im.groupToB : im.groupToA, offsets.size(), offsets.data());
            cp.DispatchWorkgroups(groups);
            state.currentIsA = !state.currentIsA;
            ++encoded;
        };
        // Writes the snapshot buffer from the current state; the current buffer does not change.
        auto snapshot = [&]() {
            cp.SetPipeline(im.copyPipeline);
            cp.SetBindGroup(0, state.currentIsA ? im.groupToCFromA : im.groupToCFromB, offsets.size(),
                            offsets.data());
            cp.DispatchWorkgroups(groups);
            ++encoded;
        };
        for (std::uint32_t k = 0; k < count; ++k) {
            offsets[1] = static_cast<std::uint32_t>((firstBlock + k) * kStepBlockStride);
            if (agents) {
                // Move and deposit (reads the current trail, writes only the deposits), then resolve.
                if (grid.agentCount > 0) {
                    cp.SetPipeline(im.agentsMovePipeline);
                    cp.SetBindGroup(0, state.currentIsA ? im.groupToB : im.groupToA, offsets.size(), offsets.data());
                    cp.DispatchWorkgroups((static_cast<std::uint32_t>(grid.agentCount) + kWorkgroup - 1) / kWorkgroup);
                    ++encoded;
                }
                dispatch(im.agentsResolvePipeline);
            } else {
                if (!grid.injectField.empty() && grid.injectRate != 0.0f &&
                    scene.fields.indexOf(grid.injectField) >= 0) {
                    dispatch(im.injectPipeline);
                }
                if (!grid.velocityField.empty() && grid.advect != 0.0f &&
                    scene.fields.indexOf(grid.velocityField) >= 0) {
                    dispatch(im.advectPipeline);
                }
                if (reaction) {
                    dispatch(im.reactionPipeline);
                } else {
                    if (grid.diffusion > 0.0f && grid.diffuseIterations > 0) {
                        snapshot(); // the Jacobi sweeps all relax towards the state they started from
                        for (int it = 0; it < grid.diffuseIterations; ++it) {
                            dispatch(im.diffusePipeline);
                        }
                    }
                    if (grid.dissipation != 0.0f) {
                        dispatch(im.dissipatePipeline);
                    }
                }
            }
            ++state.stepsTaken;
            // ADR-1119: a checkpoint between passes, GPU to GPU, when this step lands on the spacing.
            if (checkpointsAllowed && state.intervalSteps > 0 && state.stepsTaken % state.intervalSteps == 0) {
                const bool held = std::any_of(state.checkpoints.begin(), state.checkpoints.end(),
                                              [&](const Impl::Checkpoint& c) { return c.step == state.stepsTaken; });
                const std::uint64_t bytes = cellBytes + agentBytes;
                // Over budget: this grid's spacing doubles and the checkpoints off the new spacing go.
                while (!held && im.checkpointBytes() + bytes > im.checkpointBudget && state.intervalSteps < (1ull << 40)) {
                    state.intervalSteps *= 2;
                    std::erase_if(state.checkpoints, [&](const Impl::Checkpoint& c) {
                        return c.step % state.intervalSteps != 0;
                    });
                    if (state.stepsTaken % state.intervalSteps != 0) {
                        break;
                    }
                }
                if (!held && state.stepsTaken % state.intervalSteps == 0 &&
                    im.checkpointBytes() + bytes <= im.checkpointBudget) {
                    cp.End();
                    Impl::Checkpoint c;
                    c.step = state.stepsTaken;
                    c.bytes = bytes;
                    wgpu::BufferDescriptor d{};
                    d.label = "simulation-checkpoint";
                    d.size = (bytes + 3) / 4 * 4;
                    d.usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
                    c.buffer = im.context.device().CreateBuffer(&d);
                    enc.CopyBufferToBuffer(state.currentIsA ? im.bufferA : im.bufferB, tableOffset, c.buffer, 0,
                                           cellBytes);
                    if (agentBytes > 0) {
                        // Agents are updated in place; the deposits are zero between steps.
                        enc.CopyBufferToBuffer(im.agentBuffer, agentOffset[w.index] * 16, c.buffer, cellBytes, agentBytes);
                    }
                    state.checkpoints.push_back(std::move(c));
                    std::sort(state.checkpoints.begin(), state.checkpoints.end(),
                              [](const Impl::Checkpoint& a, const Impl::Checkpoint& b) { return a.step < b.step; });
                    ++stats_.checkpointsTaken;
                    cp = enc.BeginComputePass(&passDesc);
                }
            }
        }
        cp.End();
        stats_.steps += count;
    };

    // A catch-up backlog runs ahead of the frame in command buffers of its own, a bounded number of
    // sub-steps each (at most 4 s of steps, so the audio ring holds every row they hear), so a seek to
    // minute five is many bounded submissions rather than one unbounded one. Each writes its own
    // field blocks first; the queue orders those writes before the submission that reads them.
    const auto frameNewest = audio != nullptr ? audio->newestRow(audio->now(time.renderTime)) : -1;
    bool ringMoved = false;
    for (Work& w : work) {
        const spatial::GridField& grid = grids[w.index];
        const auto chunk = std::max<std::uint32_t>(
            1u, std::min<std::uint32_t>(kStepsPerSubmit, static_cast<std::uint32_t>(std::floor(4.0f * grid.simRate))));
        std::uint32_t remaining = w.steps;
        while (remaining > chunk) {
            const Impl::GridState& state = im.states[w.index];
            if (fields != nullptr && audio != nullptr) {
                const double last = static_cast<double>(state.stepsTaken + chunk) / grid.simRate;
                fields->holdAudioRows(*audio, audio->newestRow(audio->now(last)));
                ringMoved = true;
            }
            blockSlot = 0;
            const std::uint32_t first = packBlocks(w, chunk);
            queue.WriteBuffer(im.stepBlocks, 0, im.blockStaging.data(), static_cast<std::size_t>(chunk) * kStepBlockStride);
            wgpu::CommandEncoder ahead = im.context.device().CreateCommandEncoder();
            encodeSteps(ahead, w, chunk, first, "simulate-catch-up", nullptr);
            wgpu::CommandBuffer commands = ahead.Finish();
            queue.Submit(1, &commands);
            // Wait for each chunk before queueing the next. Measured: a fresh `--render` of the Mycelium
            // regression scene at 32 s queued ~6 s of catch-up GPU work in eight chunks with no sync, and
            // the frame came back EMPTY -- every pixel 0, "GPU errors: 0", device not lost -- where 28 s
            // (~5.2 s of work) rendered and the test harness, which happens to wait, rendered 40 s.
            // Root cause in Dawn/Metal not found; this bounds what is queued to one chunk (<= 4 s of
            // steps). It runs only while a seek or a fresh render replays a backlog, never in playback.
            im.context.waitForQueue();
            remaining -= chunk;
        }
        w.steps = remaining; // what is left goes into the frame's own pass
    }
    if (ringMoved && fields != nullptr && audio != nullptr) {
        fields->holdAudioRows(*audio, frameNewest); // the frame's other passes read the frame's rows
    }

    // The frame's own steps, every grid's, with their field blocks in consecutive slots.
    blockSlot = 0;
    std::vector<std::uint32_t> firstBlock(work.size(), 0);
    for (std::size_t k = 0; k < work.size(); ++k) {
        const std::uint32_t n = std::min<std::uint32_t>(work[k].steps, kStepBlockSlots - blockSlot);
        work[k].steps = n;
        firstBlock[k] = packBlocks(work[k], n);
    }
    if (blockSlot > 0) {
        queue.WriteBuffer(im.stepBlocks, 0, im.blockStaging.data(), static_cast<std::size_t>(blockSlot) * kStepBlockStride);
    }
    bool marked = false;
    for (std::size_t k = 0; k < work.size(); ++k) {
        if (work[k].steps > 0) {
            // The frame's simulate time is the first pass's (a checkpoint splits a grid's passes).
            const wgpu::PassTimestampWrites* ts =
                !marked && im.timeline != nullptr ? im.timeline->mark("sim", gpu::FrameTimeline::PassKind::Compute)
                                                  : nullptr;
            marked = true;
            encodeSteps(encoder, work[k], work[k].steps, firstBlock[k], "simulate", ts);
        }
    }
    // Publish the finished states where fields.wgsl reads them (copies cannot be inside a pass).
    for (const Work& w : work) {
        const spatial::GridField& grid = grids[w.index];
        if (w.steps == 0 && !catchUp) {
            continue;
        }
        const Impl::GridState& state = im.states[w.index];
        const std::uint64_t byteOffset = spatial::gridTableOffset(grids, w.index) * sizeof(float);
        const std::uint64_t byteCount = grid.floatCount() * sizeof(float);
        const wgpu::Buffer& current = state.currentIsA ? im.bufferA : im.bufferB;
        encoder.CopyBufferToBuffer(current, byteOffset, im.gridTable, byteOffset, byteCount);
    }
    for (const Impl::GridState& s : im.states) {
        stats_.checkpoints += static_cast<std::uint32_t>(s.checkpoints.size());
    }
    stats_.checkpointBytes = im.checkpointBytes();
    stats_.dispatches = encoded;
    im.passThisFrame = true;
    stats_.simulateMs = im.lastMs;
}

void Simulation::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void Simulation::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double ms = im.timeline->msFor("sim");
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
    }
    stats_.simulateMs = im.passThisFrame ? im.lastMs : -1.0;
}

Result<std::vector<float>> Simulation::readGrid(const spatial::FieldSet& fields, std::size_t index) {
    Impl& im = *impl_;
    if (index >= fields.grids.size()) {
        return fail("grid index {} out of range ({} grids)", index, fields.grids.size());
    }
    const spatial::GridField& grid = fields.grids[index];
    const std::uint64_t offset = spatial::gridTableOffset(fields.grids, index) * sizeof(float);
    const std::uint64_t bytes = grid.floatCount() * sizeof(float);
    if (bytes == 0) {
        return std::vector<float>{};
    }
    auto raw = gpu::readBuffer(im.context, im.gridTable, 0, offset + bytes);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    std::vector<float> out(grid.floatCount());
    std::memcpy(out.data(), raw->data() + offset, bytes);
    return out;
}

Result<std::vector<glm::vec4>> Simulation::readAgents(const spatial::FieldSet& fields, std::size_t index) {
    Impl& im = *impl_;
    if (index >= fields.grids.size() || !fields.grids[index].agents()) {
        return fail("grid {} is not an agents grid", index);
    }
    std::uint64_t total = 0;
    const std::vector<std::uint64_t> offsets = agentOffsets(fields.grids, total);
    const auto count = static_cast<std::uint64_t>(std::max(fields.grids[index].agentCount, 0));
    if (count == 0 || !im.agentBuffer) {
        return std::vector<glm::vec4>{};
    }
    auto raw = gpu::readBuffer(im.context, im.agentBuffer, offsets[index] * 16, count * 16);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    std::vector<glm::vec4> out(count);
    std::memcpy(out.data(), raw->data(), count * 16);
    return out;
}

} // namespace avgen::rendering
