#include "rendering/simulation.hpp"

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

std::uint64_t layoutHashOf(const std::vector<spatial::GridField>& grids) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint64_t v) {
        h = (h ^ v) * 0x100000001b3ull;
    };
    mix(grids.size());
    for (const spatial::GridField& g : grids) {
        mix(g.structuralHash());
    }
    return h;
}

} // namespace

struct Simulation::Impl {
    struct GridState {
        std::uint64_t stepsTaken = 0;
        bool currentIsA = true;
    };

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {
        uniformStaging.resize(static_cast<std::size_t>(kMaxGrids) * kUniformStride);
    }

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    bool ensureBuffers(std::uint64_t floats);
    void rebuildGroups();
    void uploadInitialState(const std::vector<spatial::GridField>& grids);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::Buffer fieldBlock;
    wgpu::Buffer gridTable;
    wgpu::Buffer bufferA;
    wgpu::Buffer bufferB;
    wgpu::Buffer bufferInitial;
    std::uint64_t bufferBytes = 0;
    wgpu::Buffer uniforms;
    wgpu::BindGroupLayout layout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::ComputePipeline injectPipeline;
    wgpu::ComputePipeline advectPipeline;
    wgpu::ComputePipeline diffusePipeline;
    wgpu::ComputePipeline dissipatePipeline;
    wgpu::ComputePipeline reactionPipeline;
    wgpu::ComputePipeline copyPipeline;
    wgpu::BindGroup groupToA;      // dst = A, src = B, initial = C
    wgpu::BindGroup groupToB;      // dst = B, src = A, initial = C
    wgpu::BindGroup groupToCFromA; // dst = C, src = A (the diffusion snapshot)
    wgpu::BindGroup groupToCFromB; // dst = C, src = B
    gpu::FrameTimeline* timeline = nullptr;
    std::vector<std::uint8_t> uniformStaging;
    std::vector<GridState> states;
    std::uint64_t layoutHash = 0;
    double lastRenderTime = -1.0;
    double lastMs = -1.0;
    bool passThisFrame = false;
    bool needsReset = true;
    bool initialised = false;
    bool warnedLimit = false;
};

Simulation::Simulation(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

Simulation::~Simulation() = default;

Result<void> Simulation::init(wgpu::Buffer fieldBlock, wgpu::Buffer gridTable) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.fieldBlock = std::move(fieldBlock);
    im.gridTable = std::move(gridTable);
    if (!im.fieldBlock) {
        wgpu::BufferDescriptor desc{};
        desc.label = "simulation-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        im.fieldBlock = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        im.context.queue().WriteBuffer(im.fieldBlock, 0, &zero, sizeof(zero));
    }
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
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
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
        entries[4].buffer.minBindingSize = FieldUniforms::kBufferSize;
        entries[5].binding = 15;
        entries[5].visibility = wgpu::ShaderStage::Compute;
        entries[5].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);
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
    auto inject = make("cs_inject");
    auto advect = make("cs_advect");
    auto diffuse = make("cs_diffuse");
    auto dissipate = make("cs_dissipate");
    auto reaction = make("cs_reaction");
    auto copy = make("cs_copy");
    if (!copy) {
        return std::unexpected(copy.error());
    }
    if (!inject) {
        return std::unexpected(inject.error());
    }
    if (!advect) {
        return std::unexpected(advect.error());
    }
    if (!diffuse) {
        return std::unexpected(diffuse.error());
    }
    if (!dissipate) {
        return std::unexpected(dissipate.error());
    }
    if (!reaction) {
        return std::unexpected(reaction.error());
    }
    injectPipeline = *inject;
    advectPipeline = *advect;
    diffusePipeline = *diffuse;
    dissipatePipeline = *dissipate;
    reactionPipeline = *reaction;
    copyPipeline = *copy;
    return {};
}

bool Simulation::Impl::ensureBuffers(std::uint64_t floats) {
    const std::uint64_t bytes = std::max<std::uint64_t>(floats * sizeof(float), 16);
    if (bufferA && bufferBytes >= bytes) {
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
    bufferBytes = desc.size;
    rebuildGroups();
    return true;
}

void Simulation::Impl::rebuildGroups() {
    const auto& device = context.device();
    auto make = [&](const wgpu::Buffer& dst, const wgpu::Buffer& src, const wgpu::Buffer& init,
                    const char* label) {
        std::array<wgpu::BindGroupEntry, 6> entries{};
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
        entries[4].buffer = fieldBlock;
        entries[4].size = FieldUniforms::kBufferSize;
        entries[5].binding = 15;
        entries[5].buffer = gridTable;
        entries[5].size = FieldUniforms::kGridBufferSize;
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

void Simulation::Impl::uploadInitialState(const std::vector<spatial::GridField>& grids) {
    const auto& queue = context.queue();
    for (std::size_t i = 0; i < grids.size() && i < kMaxGrids; ++i) {
        spatial::GridField grid = grids[i];
        grid.reset(); // zero (Rd: A = 1) plus the seeded fbm3 noise; deterministic in `seed`
        if (grid.data.empty()) {
            continue;
        }
        const std::uint64_t offset = spatial::gridTableOffset(grids, i) * sizeof(float);
        const std::uint64_t bytes = grid.data.size() * sizeof(float);
        queue.WriteBuffer(bufferA, offset, grid.data.data(), bytes);
        queue.WriteBuffer(bufferB, offset, grid.data.data(), bytes);
        queue.WriteBuffer(bufferInitial, offset, grid.data.data(), bytes);
        queue.WriteBuffer(gridTable, offset, grid.data.data(), bytes);
    }
    states.assign(grids.size(), GridState{});
}

void Simulation::reset() {
    impl_->needsReset = true;
    impl_->lastRenderTime = -1.0;
}

void Simulation::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time) {
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
    if (floats * sizeof(float) > FieldUniforms::kGridBufferSize) {
        if (!im.warnedLimit) {
            log::warn("simulated grids need {} floats; the shared table holds {}", floats,
                      spatial::kMaxGridTableFloats);
            im.warnedLimit = true;
        }
        return;
    }
    const std::uint64_t hash = layoutHashOf(grids);
    bool reallocated = im.ensureBuffers(floats);
    if (reallocated || hash != im.layoutHash || im.needsReset || time.renderTime < im.lastRenderTime) {
        im.uploadInitialState(grids);
        im.layoutHash = hash;
        im.needsReset = false;
        im.lastRenderTime = -1.0; // the next block treats this as the first frame (full catch-up)
    }
    const bool firstFrame = im.lastRenderTime < 0.0;
    im.lastRenderTime = time.renderTime;
    if (im.states.size() != grids.size()) {
        im.states.assign(grids.size(), Impl::GridState{});
    }

    // ---- per-grid uniforms and step counts ----
    struct Work {
        std::size_t index = 0;
        std::uint32_t offset = 0; // dynamic uniform offset
        std::uint32_t steps = 0;
    };
    std::vector<Work> work;
    std::uint32_t slot = 0;
    for (std::size_t i = 0; i < grids.size() && i < kMaxGrids; ++i) {
        const spatial::GridField& grid = grids[i];
        if (!grid.enabled || grid.floatCount() == 0 || !grid.validate()) {
            continue;
        }
        Impl::GridState& state = im.states[i];
        const auto target = static_cast<std::uint64_t>(
            std::max(0.0, std::floor(time.renderTime * static_cast<double>(grid.simRate))));
        if (target < state.stepsTaken) {
            state.stepsTaken = target; // a seek backwards; the state re-uploads above
        }
        std::uint64_t backlog = target - state.stepsTaken;
        const std::uint64_t budget = firstFrame ? kCatchUpSteps : static_cast<std::uint64_t>(grid.maxSubSteps);
        if (firstFrame && backlog > budget) {
            log::warn("grid '{}' starts {} sub-steps behind; skipping ahead", grid.name, backlog);
            state.stepsTaken = target - budget;
            backlog = budget;
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
        u.bounds1 = glm::vec4(grid.boundsMax, static_cast<float>(time.renderTime));
        u.params0 = glm::vec4(grid.injectRate, grid.advect, grid.diffusion, grid.dissipation);
        u.params1 = glm::vec4(grid.feed, grid.kill, grid.diffusionA, grid.diffusionB);
        const int injectSlot = grid.injectField.empty() ? -1 : scene.fields.indexOf(grid.injectField);
        const int velocitySlot = grid.velocityField.empty() ? -1 : scene.fields.indexOf(grid.velocityField);
        u.slots = glm::ivec4(injectSlot, velocitySlot, static_cast<int>(grid.mode), 0);
        const std::uint32_t offset = slot * kUniformStride;
        std::memcpy(im.uniformStaging.data() + offset, &u, sizeof(u));
        work.push_back(Work{i, offset, steps});
        ++slot;
        ++stats_.grids;
    }
    if (slot == 0) {
        return;
    }
    im.context.queue().WriteBuffer(im.uniforms, 0, im.uniformStaging.data(),
                                   static_cast<std::size_t>(slot) * kUniformStride);

    // ---- encode the sub-steps ----
    // One compute pass for the whole frame: in a compute pass the usage scope is a single
    // dispatch, so the ping-pong buffers may swap between readable and writable between
    // dispatches, and dispatches in one pass are ordered (each reads the previous one's writes).
    std::uint32_t encoded = 0;
    wgpu::ComputePassDescriptor passDesc{};
    passDesc.label = "simulate";
    passDesc.timestampWrites = im.timeline != nullptr ? im.timeline->mark("sim") : nullptr;
    wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&passDesc);
    for (const Work& w : work) {
        const spatial::GridField& grid = grids[w.index];
        Impl::GridState& state = im.states[w.index];
        const auto cells = static_cast<std::uint32_t>(grid.cellCount());
        const std::uint32_t groups = (cells + kWorkgroup - 1) / kWorkgroup;
        const bool reaction = grid.mode == spatial::GridMode::ReactionDiffusion;

        // Writes the other ping-pong buffer and makes it current.
        auto dispatch = [&](const wgpu::ComputePipeline& pipeline) {
            cp.SetPipeline(pipeline);
            cp.SetBindGroup(0, state.currentIsA ? im.groupToB : im.groupToA, 1, &w.offset);
            cp.DispatchWorkgroups(groups);
            state.currentIsA = !state.currentIsA;
            ++encoded;
        };
        // Writes the snapshot buffer from the current state; the current buffer does not change.
        auto snapshot = [&]() {
            cp.SetPipeline(im.copyPipeline);
            cp.SetBindGroup(0, state.currentIsA ? im.groupToCFromA : im.groupToCFromB, 1, &w.offset);
            cp.DispatchWorkgroups(groups);
            ++encoded;
        };

        for (std::uint32_t step = 0; step < w.steps; ++step) {
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
                continue;
            }
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
        state.stepsTaken += w.steps;
        stats_.steps += w.steps;
    }
    cp.End();
    // Publish the finished states where fields.wgsl reads them (copies cannot be inside a pass).
    for (const Work& w : work) {
        const spatial::GridField& grid = grids[w.index];
        if (w.steps == 0 && !firstFrame) {
            continue;
        }
        const Impl::GridState& state = im.states[w.index];
        const std::uint64_t byteOffset = spatial::gridTableOffset(grids, w.index) * sizeof(float);
        const std::uint64_t byteCount = grid.floatCount() * sizeof(float);
        const wgpu::Buffer& current = state.currentIsA ? im.bufferA : im.bufferB;
        encoder.CopyBufferToBuffer(current, byteOffset, im.gridTable, byteOffset, byteCount);
    }
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

} // namespace avgen::rendering
