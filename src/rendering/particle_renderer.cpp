#include "rendering/particle_renderer.hpp"

#include "rendering/scene_targets.hpp" // the five colour targets of the scene pass (ADR-035)

#include "rendering/field_uniforms.hpp"
#include "rendering/spline_buffers.hpp"

#include "core/log.hpp"
#include "core/pre_roll.hpp" // ADR-397: the bounded pre-roll this warm-up is one consumer of
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace avgen::rendering {

namespace {
constexpr std::uint32_t kParticleStride = 48;
constexpr std::uint32_t kWorkgroup = 64;   // cs_emit / cs_simulate
constexpr std::uint32_t kScanBlock = 1024; // slots per compaction workgroup (256 threads x 4)
// 0 uniforms, 1 particles, 2 dead list, 3 counters + indirect draw args, 4 alive list,
// 5 trail history, 6 glow scratch (ADR-040), 7 compaction scratch (flags then block sums),
// 8 field block, 9 spline tables, 15 the simulated-grid table (declared by fields.wgsl; ADR-032).
// That is nine storage buffers in the compute stage, the same as before ADR-040: this adapter
// allows ten, which is why history, glow scratch, the counters and the compaction flags share
// buffers with their neighbours.
constexpr std::uint32_t kComputeBindings = 11;
constexpr std::uint32_t kComputeBindingSlots[kComputeBindings] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15};
// counters (16 bytes) then the billboard and ribbon DrawArgs.
constexpr std::uint32_t kCountersBytes = 48;
constexpr std::uint32_t kBillboardIndirectOffset = 16;
constexpr std::uint32_t kRibbonIndirectOffset = 32;
constexpr std::uint64_t kGlowBlockBytes = 32; // two vec4 partial sums per scan block
} // namespace

ParticleRenderer::ParticleRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders) {}

ParticleRenderer::~ParticleRenderer() = default;

void ParticleRenderer::setTimeline(gpu::FrameTimeline* timeline) { timeline_ = timeline; }

void ParticleRenderer::collectTimings() {
    if (timeline_ != nullptr) {
        const double ms = timeline_->msFor("particles");
        if (ms >= 0.0) {
            lastSimulateMs_ = ms;
        }
    }
    stats_.simulateMs = passThisFrame_ ? lastSimulateMs_ : -1.0;
}

Result<void> ParticleRenderer::init(wgpu::Buffer fieldBlock, wgpu::Buffer splineTable, wgpu::Buffer gridTable) {
    const auto& device = context_.device();
    fieldBlock_ = std::move(fieldBlock);
    splineTable_ = std::move(splineTable);
    gridTable_ = std::move(gridTable);
    if (!gridTable_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-grid-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kGridBufferSize;
        gridTable_ = device.CreateBuffer(&desc);
    }
    if (!splineTable_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-spline-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = SplineBuffers::kBufferSize;
        splineTable_ = device.CreateBuffer(&desc);
        const SplineInfoGpu zero{};
        context_.queue().WriteBuffer(splineTable_, 0, &zero, sizeof(zero));
    }
    if (!fieldBlock_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        fieldBlock_ = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        context_.queue().WriteBuffer(fieldBlock_, 0, &zero, sizeof(zero));
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-glow";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        desc.size = kGlowBufferSize;
        glowBuffer_ = device.CreateBuffer(&desc);
        const std::array<float, kMaxGlowSystems * 8> zero{};
        context_.queue().WriteBuffer(glowBuffer_, 0, zero.data(), kGlowBufferSize);
    }
    {
        // "Nothing in front": the same value the linear-depth pass clears to, so a frame with no
        // depth prepass behaves as if the fog marched all the way to its maximum distance.
        wgpu::TextureDescriptor desc{};
        desc.label = "particles-linear-depth-placeholder";
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::R32Float;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        depthPlaceholder_ = device.CreateTexture(&desc);
        depthPlaceholderView_ = depthPlaceholder_.CreateView();
        const float far = 1.0e7f;
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = depthPlaceholder_;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D extent{1, 1, 1};
        context_.queue().WriteTexture(&dst, &far, sizeof(far), &layout, &extent);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, kComputeBindings> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        for (std::uint32_t i = 1; i < 8; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
            entries[i].buffer.type = wgpu::BufferBindingType::Storage;
        }
        entries[8].binding = 8;
        entries[8].visibility = wgpu::ShaderStage::Compute;
        entries[8].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[8].buffer.minBindingSize = FieldUniforms::kBufferSize;
        entries[9].binding = 9;
        entries[9].visibility = wgpu::ShaderStage::Compute;
        entries[9].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[9].buffer.minBindingSize = SplineBuffers::kBufferSize;
        entries[10].binding = 15;
        entries[10].visibility = wgpu::ShaderStage::Compute;
        entries[10].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-compute-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        computeLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Vertex;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].binding = 4;
        entries[2].visibility = wgpu::ShaderStage::Vertex;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].binding = 5; // trail history, read by vs_ribbon (ADR-040)
        entries[3].visibility = wgpu::ShaderStage::Vertex;
        entries[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[4].binding = 13; // linear depth, read by the fog coupling in fs_particle
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[4].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-render-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        renderLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &computeLayout_;
        computePipelineLayout_ = device.CreatePipelineLayout(&desc);
        desc.bindGroupLayouts = &renderLayout_;
        renderPipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    auto module = shaders_.load("particles.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = createPipelines(*module); !r) {
        return r;
    }
    initialised_ = true;
    return {};
}

Result<void> ParticleRenderer::reload() {
    auto module = shaders_.load("particles.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return createPipelines(*module);
}

Result<void> ParticleRenderer::createPipelines(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    auto makeCompute = [&](const char* entry) -> Result<wgpu::ComputePipeline> {
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = entry;
        desc.layout = computePipelineLayout_;
        desc.compute.module = module;
        desc.compute.entryPoint = entry;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("particle compute pipeline '{}' failed: {}", entry, error);
        }
        return pipeline;
    };
    auto makeRender = [&](bool additive, const char* vertexEntry) -> Result<wgpu::RenderPipeline> {
        wgpu::BlendState blend{};
        blend.color.operation = wgpu::BlendOperation::Add;
        blend.color.srcFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::SrcAlpha;
        blend.color.dstFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.alpha.operation = wgpu::BlendOperation::Add;
        blend.alpha.srcFactor = wgpu::BlendFactor::One;
        blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        // The scene pass has five colour targets (ADR-035). Particles blend into the colour and
        // write the velocity target; the surface targets stay with the opaque geometry behind them.
        std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
        fillSceneTargets(colorTargets, kHdrFormat, &blend, wgpu::ColorWriteMask::None);
        colorTargets[2].writeMask = wgpu::ColorWriteMask::All;
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_particle";
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = colorTargets.data();
        wgpu::DepthStencilState depth{};
        depth.format = kDepthFormat;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor desc{};
        desc.label = additive ? "particles-additive" : "particles-alpha";
        desc.layout = renderPipelineLayout_;
        desc.vertex.module = module;
        desc.vertex.entryPoint = vertexEntry;
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("particle render pipeline failed: {}", error);
        }
        return pipeline;
    };
    auto emit = makeCompute("cs_emit");
    if (!emit) return std::unexpected(emit.error());
    auto simulate = makeCompute("cs_simulate");
    if (!simulate) return std::unexpected(simulate.error());
    auto reduce = makeCompute("cs_scan_reduce");
    if (!reduce) return std::unexpected(reduce.error());
    auto top = makeCompute("cs_scan_top");
    if (!top) return std::unexpected(top.error());
    auto scatter = makeCompute("cs_scan_scatter");
    if (!scatter) return std::unexpected(scatter.error());
    auto glowReduce = makeCompute("cs_glow_reduce");
    if (!glowReduce) return std::unexpected(glowReduce.error());
    auto glowTop = makeCompute("cs_glow_top");
    if (!glowTop) return std::unexpected(glowTop.error());
    auto additive = makeRender(true, "vs_particle");
    if (!additive) return std::unexpected(additive.error());
    auto alpha = makeRender(false, "vs_particle");
    if (!alpha) return std::unexpected(alpha.error());
    auto ribbonAdditive = makeRender(true, "vs_ribbon");
    if (!ribbonAdditive) return std::unexpected(ribbonAdditive.error());
    auto ribbonAlpha = makeRender(false, "vs_ribbon");
    if (!ribbonAlpha) return std::unexpected(ribbonAlpha.error());
    emitPipeline_ = *emit;
    simulatePipeline_ = *simulate;
    scanReducePipeline_ = *reduce;
    scanTopPipeline_ = *top;
    scanScatterPipeline_ = *scatter;
    glowReducePipeline_ = *glowReduce;
    glowTopPipeline_ = *glowTop;
    additivePipeline_ = *additive;
    alphaPipeline_ = *alpha;
    ribbonAdditivePipeline_ = *ribbonAdditive;
    ribbonAlphaPipeline_ = *ribbonAlpha;
    return {};
}

void ParticleRenderer::ensurePool(std::size_t index, std::uint32_t capacity, std::uint32_t historyPoints) {
    if (pools_.size() <= index) {
        pools_.resize(index + 1);
    }
    Pool& pool = pools_[index];
    capacity = std::clamp<std::uint32_t>(capacity, 64, 4u << 20);
    if (pool.capacity == capacity && pool.historyPoints == historyPoints && pool.particles) {
        return;
    }
    const auto& device = context_.device();
    pool = Pool{};
    pool.capacity = capacity;
    pool.historyPoints = historyPoints;
    pool.blocks = (capacity + kScanBlock - 1) / kScanBlock;
    auto buffer = [&](const char* label, std::uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor desc{};
        desc.label = label;
        desc.size = size;
        desc.usage = usage;
        return device.CreateBuffer(&desc);
    };
    const std::uint64_t listBytes = static_cast<std::uint64_t>(capacity) * 4;
    // flags for every slot, then one sum per scan block (shaders/particles.wgsl `blockSumIndex`).
    const std::uint64_t scratchBytes = listBytes + static_cast<std::uint64_t>(pool.blocks) * 4;
    constexpr auto kStorage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    pool.uniforms = buffer("particles-uniforms", sizeof(ParticleUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    pool.particles = buffer("particles-pool", static_cast<std::uint64_t>(capacity) * kParticleStride, kStorage);
    pool.deadList = buffer("particles-dead", listBytes, kStorage);
    pool.counters = buffer("particles-counters", kCountersBytes,
                           kStorage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::Indirect);
    pool.aliveList = buffer("particles-alive", listBytes, kStorage);
    pool.scratch = buffer("particles-scratch", scratchBytes, kStorage);
    // The history ring is the trail's whole cost: capacity * points * 16 bytes. A one-entry stub
    // keeps the bind group valid when the system has no trails, so trails really are free when off.
    const std::uint64_t historyBytes =
        historyPoints > 0 ? static_cast<std::uint64_t>(capacity) * historyPoints * scene::kTrailBytesPerPoint : 16;
    pool.history = buffer("particles-trail-history", historyBytes, kStorage | wgpu::BufferUsage::CopySrc);
    // Two vec4 per block, then the two-vec4 aggregate cs_glow_top writes past them.
    const std::uint64_t glowScratchBytes = (static_cast<std::uint64_t>(pool.blocks) + 1) * kGlowBlockBytes;
    pool.glowScratch = buffer("particles-glow-scratch", glowScratchBytes, kStorage | wgpu::BufferUsage::CopySrc);

    std::array<wgpu::BindGroupEntry, kComputeBindings> entries{};
    const wgpu::Buffer* buffers[kComputeBindings] = {&pool.uniforms,  &pool.particles,  &pool.deadList,
                                                     &pool.counters,  &pool.aliveList,  &pool.history,
                                                     &pool.glowScratch, &pool.scratch,  &fieldBlock_,
                                                     &splineTable_,   &gridTable_};
    const std::uint64_t sizes[kComputeBindings] = {sizeof(ParticleUniforms),
                                                   static_cast<std::uint64_t>(capacity) * kParticleStride,
                                                   listBytes, kCountersBytes, listBytes, historyBytes,
                                                   glowScratchBytes, scratchBytes,
                                                   FieldUniforms::kBufferSize, SplineBuffers::kBufferSize,
                                                   FieldUniforms::kGridBufferSize};
    for (std::uint32_t i = 0; i < kComputeBindings; ++i) {
        entries[i].binding = kComputeBindingSlots[i];
        entries[i].buffer = *buffers[i];
        entries[i].size = sizes[i];
    }
    wgpu::BindGroupDescriptor cdesc{};
    cdesc.label = "particles-compute-group";
    cdesc.layout = computeLayout_;
    cdesc.entryCount = entries.size();
    cdesc.entries = entries.data();
    pool.computeGroup = device.CreateBindGroup(&cdesc);
    pool.renderGroup = nullptr;
    pool.needsReset = true;
}

void ParticleRenderer::ensureRenderGroup(Pool& pool) {
    const wgpu::TextureView& depthView = frame_.linearDepth ? frame_.linearDepth : depthPlaceholderView_;
    if (pool.renderGroup && pool.renderDepthView.Get() == depthView.Get()) {
        return;
    }
    const std::uint64_t listBytes = static_cast<std::uint64_t>(pool.capacity) * 4;
    const std::uint64_t historyBytes =
        pool.historyPoints > 0 ? static_cast<std::uint64_t>(pool.capacity) * pool.historyPoints * scene::kTrailBytesPerPoint
                               : 16;
    std::array<wgpu::BindGroupEntry, 5> entries{};
    entries[0].binding = 0;
    entries[0].buffer = pool.uniforms;
    entries[0].size = sizeof(ParticleUniforms);
    entries[1].binding = 1;
    entries[1].buffer = pool.particles;
    entries[1].size = static_cast<std::uint64_t>(pool.capacity) * kParticleStride;
    entries[2].binding = 4;
    entries[2].buffer = pool.aliveList;
    entries[2].size = listBytes;
    entries[3].binding = 5;
    entries[3].buffer = pool.history;
    entries[3].size = historyBytes;
    entries[4].binding = 13;
    entries[4].textureView = depthView;
    wgpu::BindGroupDescriptor rdesc{};
    rdesc.label = "particles-render-group";
    rdesc.layout = renderLayout_;
    rdesc.entryCount = entries.size();
    rdesc.entries = entries.data();
    pool.renderGroup = context_.device().CreateBindGroup(&rdesc);
    pool.renderDepthView = depthView;
}

void ParticleRenderer::setFrameContext(const ParticleFrameContext& context) { frame_ = context; }

void ParticleRenderer::resetPool(Pool& pool) {
    // The same state the compaction pass would produce for an empty pool: every slot dead, the
    // dead list in slot order, no alive instances.
    std::vector<std::uint32_t> dead(pool.capacity);
    for (std::uint32_t i = 0; i < pool.capacity; ++i) {
        dead[i] = i;
    }
    context_.queue().WriteBuffer(pool.deadList, 0, dead.data(), dead.size() * 4);
    // deadCount, aliveCount, pad, pad, then the two indirect draws with no instances.
    const std::uint32_t counters[12] = {pool.capacity, 0, 0, 0, 6, 0, 0, 0, pool.historyPoints * 6, 0, 0, 0};
    context_.queue().WriteBuffer(pool.counters, 0, counters, sizeof(counters));
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(pool.capacity) * kParticleStride, 0);
    context_.queue().WriteBuffer(pool.particles, 0, zeros.data(), zeros.size());
    context_.queue().WriteBuffer(pool.scratch, 0, zeros.data(),
                                 (static_cast<std::size_t>(pool.capacity) + pool.blocks) * 4);
    if (pool.historyPoints > 0) {
        // A ring full of the origin would draw ribbons from (0,0,0) on the first frame; zeroing
        // it is not enough on its own (trailWrites gates reads) but keeps readbacks meaningful.
        std::vector<std::uint8_t> history(static_cast<std::size_t>(pool.capacity) * pool.historyPoints *
                                              scene::kTrailBytesPerPoint,
                                          0);
        context_.queue().WriteBuffer(pool.history, 0, history.data(), history.size());
    }
    pool.needsReset = false;
}

void ParticleRenderer::resetAll() {
    for (auto& pool : pools_) {
        pool.needsReset = true;
    }
    // ADR-360. A reset is the one moment the pools do not hold what a render that had played up to
    // here would hold, which is the whole of the "partial renders bloom in from nothing" half of
    // that ADR. Whether anything is done about it is the caller's, through warmUpFrames.
    warmUpPending_ = true;
}

void ParticleRenderer::runWarmUp(const scene::Scene& scene, const FrameTime& time, const glm::mat4& view,
                                 const glm::mat4& proj, const FieldUniforms* fields,
                                 const SplineBuffers* splines) {
    // ADR-397: the schedule is shared, the work is not. `planPreRoll` decides which timeline
    // seconds this roll consists of and what frame indices they carry; re-running the emit and
    // simulate passes over them is this renderer's own business. The temporal-media history
    // buffers take the same plan and re-run theirs.
    PreRoll roll;
    roll.frames = frame_.warmUpFrames;
    roll.cap = kMaxWarmUpFrames;
    const PreRollPlan plan = planPreRoll(roll, time);
    if (plan.frames.empty()) {
        return;
    }
    // `plan.arrivalFrameIndex` is deliberately ignored: nothing in the particle path keys history
    // continuity to the frame index (the compaction carries the pools across frames by itself), so
    // rewriting the arriving frame's index would change the trail stride's phase for no gain.
    const ParticleStats savedStats = stats_;
    const bool savedPass = passThisFrame_;
    warming_ = true;
    for (const FrameTime& warm : plan.frames) {
        wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
        update(encoder, scene, warm, view, proj, fields, splines);
        wgpu::CommandBuffer commands = encoder.Finish();
        context_.queue().Submit(1, &commands);
    }
    warming_ = false;
    stats_ = savedStats;
    passThisFrame_ = savedPass;
}

void ParticleRenderer::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                              const glm::mat4& view, const glm::mat4& proj, const FieldUniforms* fields,
                              const SplineBuffers* splines) {
    if (!warming_) {
        stats_ = ParticleStats{};
        passThisFrame_ = false;
    }
    if (!initialised_) {
        return;
    }
    if (scene_ != &scene) {
        resetAll();
        scene_ = &scene;
    }
    if (!warming_) {
        collectTimings(); // the previous frame's measurement (its command buffer is submitted by now)
        if (warmUpPending_) {
            // Cleared before the call, not after: runWarmUp re-enters update() and a flag still set
            // would warm the warm-up.
            warmUpPending_ = false;
            runWarmUp(scene, time, view, proj, fields, splines);
        }
    }
    std::size_t encoded = 0;
    const glm::mat4 invView = glm::inverse(view);
    const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(invView[1]));
    // Slots past the ones written below must read as "no light" in volume.wgsl, and a system that
    // stops glowing must not leave its last aggregate behind, so the table is cleared every frame.
    {
        const std::array<float, kMaxGlowSystems * 8> zero{};
        context_.queue().WriteBuffer(glowBuffer_, 0, zero.data(), kGlowBufferSize);
    }
    std::uint32_t glowSlot = 0;
    for (std::size_t i = 0; i < scene.particles.size(); ++i) {
        const auto& sys = scene.particles[i];
        // Trails are refused rather than silently truncated when they blow the memory budget
        // (ADR-040): the scene still renders, with stretched billboards instead of ribbons.
        std::uint32_t historyPoints = scene::trailHistoryPoints(sys);
        std::optional<std::string> trailRefusal;
        if (historyPoints > 0) {
            if (auto valid = scene::validateParticleSystem(sys); !valid) {
                trailRefusal = valid.error().message;
                historyPoints = 0;
            }
        }
        ensurePool(i, sys.capacity, historyPoints);
        Pool& pool = pools_[i];
        if (trailRefusal && !pool.trailWarned) {
            log::warn("particles: {}", *trailRefusal);
            pool.trailWarned = true;
        }
        if (pool.needsReset) {
            resetPool(pool);
        }
        if (!sys.enabled) {
            // Emptied on the frame it goes off, not on the frame it comes back. A pool that is
            // merely skipped keeps its particles at the age and the position they had when the
            // system was disabled, and the next enabled frame resumes them wherever the emitter
            // has since travelled to. Draining by waiting is not available: ageing only happens
            // on the stepped path below, which a disabled system never reaches.
            if (pool.wasEnabled) {
                // Emptied here rather than by setting `needsReset`: the reset check above has
                // already run for this frame, so deferring it would leave the pool full for one
                // more frame than this comment claims. Invisible today, because a disabled system
                // is not drawn either -- but a claim the code does not keep is the kind that gets
                // relied on later, and the GPU test asserts the frame this says it does.
                resetPool(pool);
                pool.wasEnabled = false;
            }
            continue;
        }
        pool.wasEnabled = true;
        const double dt = std::clamp(time.deltaTime, 0.0, 0.1);
        // ADR-360. How many spawns this frame owes, as a function of WHERE ON THE TIMELINE it is
        // rather than of a carry accumulated since the render started. The total emitted by time t
        // is floor(rate * t), so a frame owes the difference across its own interval.
        //
        // At a constant rate from t = 0 this is arithmetically identical to the carry it replaces
        // -- the carry's running total IS floor(rate * t) -- so no render that starts at zero
        // changes by a particle. What it fixes is the case the carry could not express: a render,
        // or a warm-up, that starts at t > 0 and has no carry to inherit. It is also partition
        // independent, so a stalled frame and two short ones emit the same total.
        const double rate = static_cast<double>(sys.spawnRate) * static_cast<double>(frame_.spawnScale);
        const double owed = std::floor(rate * time.renderTime) - std::floor(rate * (time.renderTime - dt));
        std::uint32_t emitCount =
            owed > 0.0 ? static_cast<std::uint32_t>(std::min(owed, static_cast<double>(pool.capacity))) : 0u;
        emitCount += static_cast<std::uint32_t>(std::max(0.0f, sys.burst));
        emitCount = std::min(emitCount, pool.capacity);

        // Spline emitters (ADR-026) need an uploaded spline; otherwise the shape falls back to Point.
        int splineSlot = -1;
        if (sys.shape == scene::EmitterShape::Spline && splines != nullptr) {
            splineSlot = splines->slotOf(sys.spline);
        }
        const scene::EmitterShape shape =
            sys.shape == scene::EmitterShape::Spline && splineSlot < 0 ? scene::EmitterShape::Point : sys.shape;

        const bool glowing = sys.volumeGlow > 0.0f && glowSlot < kMaxGlowSystems;
        const std::uint32_t mySlot = glowing ? glowSlot++ : 0;

        ParticleUniforms u{};
        u.viewProj = proj * view;
        u.prevViewProj = frame_.prevViewProj;
        u.cameraRight = glm::vec4(right, 0.0f);
        u.cameraUp = glm::vec4(up, 0.0f);
        u.cameraPos = glm::vec4(frame_.cameraPosition, std::max(0.0f, frame_.shutterSeconds));
        u.emitterPos = glm::vec4(sys.position, static_cast<float>(shape));
        u.extent = glm::vec4(sys.extent, sys.spread);
        u.direction = glm::vec4(sys.direction, sys.drag);
        u.speedLife = glm::vec4(sys.speedMin, sys.speedMax, sys.lifetimeMin, sys.lifetimeMax);
        u.gravity = glm::vec4(sys.gravity, sys.turbulence);
        u.turb = glm::vec4(sys.turbulenceScale, sys.turbulenceSpeed, sys.softness, sys.emissive);
    // ADR-370: all zero for a Round system, which is every system that has not asked otherwise, so
    // the shader's `params.leaf.x > 0.5` branch is never taken and the draw is what it always was.
    u.windDir = frame_.wind.dir;
    u.windRegion = frame_.wind.region;
    u.windGust = frame_.wind.gust;
    u.windTurb = frame_.wind.turbulence;
    u.windMix = glm::vec4(std::max(sys.windInfluence, 0.0f), 0.0f, 0.0f, 0.0f);
    u.leaf = sys.shape2d == scene::ParticleShape::Leaf
                 ? glm::vec4(1.0f, sys.tumbleRate, sys.leafAspect, glm::clamp(sys.twoSided, 0.0f, 1.0f))
                 : glm::vec4(0.0f);
        u.attractor = glm::vec4(sys.attractorPosition, sys.attractorStrength);
        u.attractor2 = glm::vec4(sys.attractorRadius, sys.orbit, sys.sizeStart, sys.sizeEnd);
        u.colorStart = sys.colorStart;
        u.colorEnd = sys.colorEnd;
        u.sim = glm::vec4(static_cast<float>(dt), static_cast<float>(time.renderTime),
                          static_cast<float>(time.frameIndex), static_cast<float>(sys.seed));
        // ADR-360: the spawn key. `sim.z` stays the frame index because the trail stride counts
        // frames; nothing in particles.wgsl seeds randomness from it any more.
        u.nonce = glm::uvec4(time.frameNonce(), 0u, 0u, 0u);
        u.stretch = glm::vec4(std::max(0.0f, sys.velocityStretch), std::max(0.0f, sys.stretchMax),
                              std::max(0.0f, sys.stretchMin), 0.0f);
        u.trail = glm::vec4(static_cast<float>(pool.historyPoints),
                            static_cast<float>(std::clamp<std::uint32_t>(sys.trailStride, 1, 64)),
                            std::max(0.0f, sys.trailWidth), std::clamp(sys.trailTaper, 0.0f, 1.0f));
        u.trail2 = glm::vec4(std::clamp(sys.trailFade, 0.0f, 1.0f), sys.trailTint);
        u.fog = glm::vec4(frame_.fogDensity, frame_.fogHeight, frame_.fogHeightFalloff, frame_.fogAbsorption);
        u.fog2 = glm::vec4(frame_.fogMaxDistance, std::clamp(sys.fogCoupling, 0.0f, 1.0f),
                           std::max(0.0f, sys.volumeGlow), frame_.linearDepth ? 1.0f : 0.0f);
        // Lifetime curves (ADR-040): at most kMaxCurveKeys keys each; fewer than two disables the
        // curve in the shader and the linear ramp above is used instead.
        const auto keyCount = [](std::size_t n) {
            return static_cast<std::uint32_t>(std::min(n, static_cast<std::size_t>(scene::kMaxCurveKeys)));
        };
        const std::uint32_t sizeKeys = keyCount(sys.sizeCurve.keys.size());
        const std::uint32_t colorKeys = keyCount(sys.colorCurve.keys.size());
        const std::uint32_t opacityKeys = keyCount(sys.opacityCurve.keys.size());
        for (std::uint32_t k = 0; k < sizeKeys; ++k) {
            u.sizeKeys[k] = glm::vec4(sys.sizeCurve.keys[k].t, sys.sizeCurve.keys[k].value, 0.0f, 0.0f);
        }
        for (std::uint32_t k = 0; k < opacityKeys; ++k) {
            u.opacityKeys[k] = glm::vec4(sys.opacityCurve.keys[k].t, sys.opacityCurve.keys[k].value, 0.0f, 0.0f);
        }
        for (std::uint32_t k = 0; k < colorKeys; ++k) {
            u.colorKeys[k] = glm::vec4(sys.colorCurve.keys[k].t, sys.colorCurve.keys[k].color);
        }
        u.curves = glm::uvec4(sizeKeys, colorKeys, opacityKeys, mySlot);
        u.counts = glm::uvec4(emitCount, pool.capacity, sys.blend == scene::ParticleBlend::Additive ? 0u : 1u,
                              pool.blocks);
        // Field forces (ADR-025): enabled entries bound to an uploaded field, in order.
        std::uint32_t forceCount = 0;
        if (fields != nullptr) {
            for (const auto& f : sys.fieldForces) {
                if (forceCount >= static_cast<std::uint32_t>(scene::kMaxFieldForces)) {
                    break;
                }
                if (!f.enabled) {
                    continue;
                }
                const int slot = fields->slotOf(f.field);
                if (slot < 0) {
                    continue;
                }
                u.fieldForces[forceCount * 2] = glm::vec4(static_cast<float>(f.mode), static_cast<float>(slot), f.strength,
                                                          std::clamp(f.mix, 0.0f, 1.0f));
                u.fieldForces[forceCount * 2 + 1] = glm::vec4(f.axis, 0.0f);
                ++forceCount;
            }
        }
        // ADR-520: the emission mask. A name the scene does not define resolves to slot -1 and the
        // mask is then *off* rather than zero -- a mask that resolves to nothing must not silently
        // delete the system, because "I typed the field name wrong" and "it is raining nowhere"
        // would then look identical. The unresolved name is reported by the field bus's
        // `unresolved()` list, which is where a dead subscription is already a stated problem.
        int maskSlot = -1;
        if (fields != nullptr && !sys.emitMaskField.empty()) {
            maskSlot = fields->slotOf(sys.emitMaskField);
        }
        u.fieldInfo = glm::uvec4(forceCount, static_cast<std::uint32_t>(splineSlot + 1),
                                 static_cast<std::uint32_t>(maskSlot + 1), 0u);
        // ---- ADR-520 ----
        u.volume = glm::vec4(glm::clamp(sys.volumeFollow, glm::vec3(0.0f), glm::vec3(1.0f)),
                             sys.volumeWrap ? 1.0f : 0.0f);
        u.collide = glm::vec4(static_cast<float>(sys.collision), sys.collisionHeight,
                              std::clamp(sys.collisionRestitution, 0.0f, 1.0f), std::max(0.0f, sys.splashSize));
        u.collide2 = glm::vec4(std::max(1e-3f, sys.splashLifetime), std::clamp(sys.ringThickness, 0.01f, 1.0f),
                               std::clamp(sys.dragSizeBias, 0.0f, 1.0f), 0.0f);
        u.pulse = glm::vec4(std::max(0.0f, sys.pulseRate), std::clamp(sys.pulseDepth, 0.0f, 1.0f),
                            std::clamp(sys.pulseSync, 0.0f, 1.0f), std::max(0.05f, sys.pulseSharpness));
        u.cluster = glm::vec4(static_cast<float>(sys.clusterCount), std::max(0.0f, sys.clusterRadius),
                              std::max(0.0f, sys.pauseRate), std::clamp(sys.pauseFraction, 0.0f, 1.0f));
        u.scatter = glm::vec4(std::max(0.0f, sys.scatterStrength), std::clamp(sys.scatterAnisotropy, -0.95f, 0.95f),
                              std::clamp(sys.sizeVariance, 0.0f, 1.0f), std::max(0.05f, sys.sizeSkew));
        // The key light. An all-zero direction -- a scene that never set one -- switches the phase
        // term off in the shader rather than normalising a zero vector.
        const float sunLen = glm::length(frame_.sunDirection);
        u.sun = sunLen > 1e-4f ? glm::vec4(frame_.sunDirection / sunLen, 1.0f) : glm::vec4(0.0f);
        u.sunColor = glm::vec4(frame_.sunColor, 0.0f);
        context_.queue().WriteBuffer(pool.uniforms, 0, &u, sizeof(u));

        // Pass order (see particles.wgsl): emit -> simulate -> reduce -> top scan -> scatter.
        // Dispatches in one compute pass are ordered, so each reads the previous one's writes;
        // emit consumes last frame's dead list before scatter rewrites it.
        wgpu::ComputePassDescriptor cdesc{};
        cdesc.label = "particles-compute";
        // One mark per system's pass; the timeline sums them under the one label, so a scene with
        // several systems reports what all of them cost rather than what the last one did.
        cdesc.timestampWrites = (timeline_ != nullptr && !warming_) ? timeline_->mark("particles") : nullptr;
        ++encoded;
        ++stats_.dispatches;
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cdesc);
        cp.SetBindGroup(0, pool.computeGroup);
        if (emitCount > 0) {
            cp.SetPipeline(emitPipeline_);
            cp.DispatchWorkgroups((emitCount + kWorkgroup - 1) / kWorkgroup);
        }
        cp.SetPipeline(simulatePipeline_);
        cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
        cp.SetPipeline(scanReducePipeline_);
        cp.DispatchWorkgroups(pool.blocks);
        cp.SetPipeline(scanTopPipeline_);
        cp.DispatchWorkgroups(1);
        cp.SetPipeline(scanScatterPipeline_);
        cp.DispatchWorkgroups(pool.blocks);
        if (glowing) {
            // One aggregate emissive sphere for the volume march (ADR-040). Both dispatches run
            // in this pass, after the simulation, so they see this frame's positions.
            cp.SetPipeline(glowReducePipeline_);
            cp.DispatchWorkgroups(pool.blocks);
            cp.SetPipeline(glowTopPipeline_);
            cp.DispatchWorkgroups(1);
        }
        cp.End();
        if (glowing) {
            encoder.CopyBufferToBuffer(pool.glowScratch, static_cast<std::uint64_t>(pool.blocks) * kGlowBlockBytes,
                                       glowBuffer_, static_cast<std::uint64_t>(mySlot) * kGlowBlockBytes,
                                       kGlowBlockBytes);
        }

        ++stats_.systems;
        stats_.capacity += pool.capacity;
        stats_.emittedThisFrame += emitCount;
        stats_.ribbonSystems += pool.historyPoints > 0 ? 1 : 0;
        stats_.glowSystems += glowing ? 1 : 0;
        stats_.trailBytes += static_cast<std::uint64_t>(pool.capacity) * pool.historyPoints * scene::kTrailBytesPerPoint;
    }
    if (encoded > 0) {
        passThisFrame_ = true;
        stats_.simulateMs = lastSimulateMs_;
    }
}

void ParticleRenderer::draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene) {
    if (!initialised_) {
        return;
    }
    for (std::size_t i = 0; i < scene.particles.size() && i < pools_.size(); ++i) {
        const auto& sys = scene.particles[i];
        if (!sys.enabled || !pools_[i].particles) {
            continue;
        }
        Pool& pool = pools_[i];
        ensureRenderGroup(pool);
        const bool additive = sys.blend == scene::ParticleBlend::Additive;
        pass.SetBindGroup(0, pool.renderGroup);
        // Ribbons first so an alpha-blended head sits on top of its own trail; additive systems
        // do not care about the order. The ribbon draw's instance count is 0 when trails are off.
        if (pool.historyPoints > 0) {
            pass.SetPipeline(additive ? ribbonAdditivePipeline_ : ribbonAlphaPipeline_);
            pass.DrawIndirect(pool.counters, kRibbonIndirectOffset);
        }
        pass.SetPipeline(additive ? additivePipeline_ : alphaPipeline_);
        pass.DrawIndirect(pool.counters, kBillboardIndirectOffset);
    }
}

Result<ParticleCounts> ParticleRenderer::readCounts(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].counters) {
        return fail("particle system {} has no pool", systemIndex);
    }
    auto bytes = gpu::readBuffer(context_, pools_[systemIndex].counters, 0, 16);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::uint32_t words[4];
    std::memcpy(words, bytes->data(), sizeof(words));
    return ParticleCounts{words[1], words[0]};
}

Result<std::array<std::uint32_t, 8>> ParticleRenderer::readDrawArgs(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].counters) {
        return fail("particle system {} has no pool", systemIndex);
    }
    auto bytes = gpu::readBuffer(context_, pools_[systemIndex].counters, kBillboardIndirectOffset, 32);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::array<std::uint32_t, 8> args{};
    std::memcpy(args.data(), bytes->data(), sizeof(args));
    return args;
}

Result<std::vector<float>> ParticleRenderer::readTrailHistory(std::size_t systemIndex, std::uint32_t points) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].history) {
        return fail("particle system {} has no pool", systemIndex);
    }
    const Pool& pool = pools_[systemIndex];
    if (pool.historyPoints == 0) {
        return fail("particle system {} has no trail history", systemIndex);
    }
    const std::uint64_t wanted = std::min<std::uint64_t>(points, static_cast<std::uint64_t>(pool.capacity) * pool.historyPoints);
    auto bytes = gpu::readBuffer(context_, pool.history, 0, wanted * scene::kTrailBytesPerPoint);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<float> out(wanted * 4);
    std::memcpy(out.data(), bytes->data(), out.size() * sizeof(float));
    return out;
}

Result<std::vector<float>> ParticleRenderer::readGlow() {
    if (!glowBuffer_) {
        return fail("particle renderer is not initialised");
    }
    auto bytes = gpu::readBuffer(context_, glowBuffer_, 0, kGlowBufferSize);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<float> out(kGlowBufferSize / sizeof(float));
    std::memcpy(out.data(), bytes->data(), kGlowBufferSize);
    return out;
}

} // namespace avgen::rendering
