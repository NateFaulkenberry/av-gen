#include "rendering/particle_renderer.hpp"
#include "gpu/resource_stats.hpp"

#include "rendering/scene_targets.hpp" // the five colour targets of the scene pass (ADR-035)

#include "rendering/field_uniforms.hpp"
#include "rendering/spline_buffers.hpp"

#include "core/log.hpp"
#include "core/pre_roll.hpp" // ADR-397: the bounded pre-roll this warm-up is one consumer of
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/texture.hpp"
#include "scene/particle_latent.hpp"

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace avgen::rendering {

namespace {
constexpr std::uint32_t kParticleStride = 64; // particles.wgsl `Particle`, home included
constexpr std::uint32_t kWorkgroup = 64;   // cs_emit / cs_simulate
constexpr std::uint32_t kScanBlock = 1024; // slots per compaction workgroup (256 threads x 4)
// 0 uniforms, 1 particles, 2 dead list, 3 counters + indirect draw args, 4 alive list,
// 5 trail history, 6 glow scratch (ADR-040), 7 compaction scratch (flags then block sums),
// 8 field block, 9 spline tables, 15 the simulated-grid table (declared by fields.wgsl; ADR-032),
// 10 the latent SDF's packed program (ADR-1140).
// That is TEN storage buffers in the compute stage: this adapter allows ten, which is why history,
// glow scratch, the counters and the compaction flags share buffers with their neighbours, and why
// ADR-1140's program spent the last slot. ADR-1141's density passes have a layout of their own.
constexpr std::uint32_t kComputeBindings = 12;
constexpr std::uint32_t kComputeBindingSlots[kComputeBindings] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 10};
constexpr std::uint64_t kSdfNodeBytes = sizeof(spatial::SdfNodeGpu); // 112
// ADR-1141: particle_density.wgsl's DensityParams.
struct DensityParamsGpu {
    glm::vec4 boundsMin; // xyz, 0
    glm::vec4 boundsMax; // xyz, weight
    glm::uvec4 counts;   // capacity, resolution, 0, 0
};
static_assert(sizeof(DensityParamsGpu) == 48);
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
        const double densityMs = timeline_->msFor("particle-density");
        if (densityMs >= 0.0) {
            lastDensityMs_ = densityMs;
        }
    }
    stats_.simulateMs = passThisFrame_ ? lastSimulateMs_ : -1.0;
    stats_.densityMs = densityThisFrame_ ? lastDensityMs_ : -1.0;
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
        entries[11].binding = 10; // ADR-1140: the latent SDF program
        entries[11].visibility = wgpu::ShaderStage::Compute;
        entries[11].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[11].buffer.minBindingSize = kSdfNodeBytes;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-compute-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        computeLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
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
        entries[4].binding = 13; // linear depth, read by the fog coupling in fs_particle (and ADR-1147's shard test)
        entries[4].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[4].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[4].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[5] = entries[4];
        entries[5].visibility = wgpu::ShaderStage::Fragment;
        entries[5].binding = 12; // ADR-715: the terrain's baked height, read by the same fog coupling
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-render-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        renderLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        // ADR-1141: 0 DensityParams, 1 the pool (read), 2 the u32 grid, 3 the 3-D texture (write).
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(DensityParamsGpu);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Compute;
        entries[2].buffer.type = wgpu::BufferBindingType::Storage;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Compute;
        entries[3].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
        entries[3].storageTexture.format = wgpu::TextureFormat::RGBA16Float;
        entries[3].storageTexture.viewDimension = wgpu::TextureViewDimension::e3D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-density-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        densityLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        // ADR-1150: the coarse occupancy dispatch reads the resolved volume, so it cannot share the
        // resolve's group (which binds that texture as a storage target): 0 DensityParams, 4 the volume
        // (textureLoad), 5 the coarse grid (write).
        std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(DensityParamsGpu);
        entries[1].binding = 4;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[1].texture.viewDimension = wgpu::TextureViewDimension::e3D;
        entries[2].binding = 5;
        entries[2].visibility = wgpu::ShaderStage::Compute;
        entries[2].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
        entries[2].storageTexture.format = wgpu::TextureFormat::RGBA16Float;
        entries[2].storageTexture.viewDimension = wgpu::TextureViewDimension::e3D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-density-coarse-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        densityCoarseLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        // ADR-1141: what a consumer binds when the system it names has no volume (yet): one zero
        // texel, which reads as "no matter anywhere".
        wgpu::TextureDescriptor desc{};
        desc.label = "particles-density-placeholder";
        desc.dimension = wgpu::TextureDimension::e3D;
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        densityPlaceholder_ = device.CreateTexture(&desc);
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e3D;
        densityPlaceholderView_ = densityPlaceholder_.CreateView(&view);
        const std::array<std::uint16_t, 4> zero{};
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = densityPlaceholder_;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D extent{1, 1, 1};
        context_.queue().WriteTexture(&dst, zero.data(), sizeof(zero), &layout, &extent);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &computeLayout_;
        computePipelineLayout_ = device.CreatePipelineLayout(&desc);
        desc.bindGroupLayouts = &renderLayout_;
        renderPipelineLayout_ = device.CreatePipelineLayout(&desc);
        desc.bindGroupLayouts = &densityLayout_;
        densityPipelineLayout_ = device.CreatePipelineLayout(&desc);
        desc.bindGroupLayouts = &densityCoarseLayout_;
        densityCoarsePipelineLayout_ = device.CreatePipelineLayout(&desc);
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
    compiledLatent_.clear(); // ADR-1145: a reload changes the source every variant was spliced into
    auto module = shaders_.load("particles.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return createPipelines(*module);
}

// ADR-1145: cs_latent with its four packed-interpreter calls replaced by the tree compiled to WGSL
// (ADR-1003's sdfField over the per-node table), so an anatomy the interpreter cannot run -- the ADR-1144
// kinds, or more than its stacks -- binds matter, at the cost of one module per tree structure. The
// particles.wgsl text is untouched: the variant is a copy with the calls renamed and the field appended,
// so no other system's kernel changes.
const ParticleRenderer::CompiledLatent& ParticleRenderer::compiledLatentPipeline(const spatial::SdfTree& tree,
                                                                                const spatial::FieldSet* fields) {
    const std::uint64_t key = spatial::sdfCompileKey(tree);
    if (auto it = compiledLatent_.find(key); it != compiledLatent_.end()) {
        return it->second;
    }
    CompiledLatent& slot = compiledLatent_[key];
    const auto start = std::chrono::steady_clock::now();
    auto source = shaders_.loadSource("particles.wgsl");
    if (!source) {
        log::warn("particles: the compiled latent variant could not load particles.wgsl: {}", source.error().message);
        return slot;
    }
    std::string text = *source;
    constexpr std::string_view kCall = "= sdfEvaluate(0u, count, pl + k";
    int replaced = 0;
    for (auto at = text.find(kCall); at != std::string::npos; at = text.find(kCall, at)) {
        text.replace(at, kCall.size(), "= sdfField(0u, count, pl + k");
        ++replaced;
    }
    if (replaced != 8) { // cs_latent's four taps and cs_latent_project's four (ADR-1155)
        log::warn("particles: cs_latent and cs_latent_project no longer have their eight sdfEvaluate taps ({}); the "
                  "compiled latent is off",
                  replaced);
        return slot;
    }
    std::vector<spatial::SdfNodeGpu> table;
    text += "\n" + spatial::sdfCompileWgsl(tree, table, fields);
    auto module = shaders_.compile(text, "particles-latent-compiled");
    if (!module) {
        log::warn("particles: compiling the latent tree failed: {}", module.error().message);
        return slot;
    }
    const auto& device = context_.device();
    const auto build = [&](const char* entry) -> wgpu::ComputePipeline {
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = entry;
        desc.layout = computePipelineLayout_;
        desc.compute.module = *module;
        desc.compute.entryPoint = entry;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::ComputePipeline pipeline = gpu::createComputePipeline(device, &desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            log::warn("particles: the compiled latent pipeline '{}' failed: {}", entry, error);
            return {};
        }
        return pipeline;
    };
    wgpu::ComputePipeline plain = build("cs_latent");
    wgpu::ComputePipeline project = build("cs_latent_project");
    if (!plain || !project) {
        return slot;
    }
    slot.plain = plain;
    slot.project = project;
    log::info("particles: compiled latent variant {:016x} ({} node records) in {:.1f} ms", key, table.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return slot;
}

Result<void> ParticleRenderer::createPipelines(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    auto makeCompute = [&](const char* entry, const wgpu::ShaderModule* other = nullptr,
                           const wgpu::PipelineLayout* layout = nullptr) -> Result<wgpu::ComputePipeline> {
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = entry;
        desc.layout = layout != nullptr ? *layout : computePipelineLayout_;
        desc.compute.module = other != nullptr ? *other : module;
        desc.compute.entryPoint = entry;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::ComputePipeline pipeline = gpu::createComputePipeline(device, &desc);
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
        wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &desc);
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
    auto latent = makeCompute("cs_latent"); // ADR-1140
    if (!latent) return std::unexpected(latent.error());
    auto latentProject = makeCompute("cs_latent_project"); // ADR-1155
    if (!latentProject) return std::unexpected(latentProject.error());
    auto latentSpring = makeCompute("cs_latent_spring");
    if (!latentSpring) return std::unexpected(latentSpring.error());
    auto densityModule = shaders_.load("particle_density.wgsl"); // ADR-1141
    if (!densityModule) return std::unexpected(densityModule.error());
    auto densitySplat = makeCompute("cs_density_splat", &*densityModule, &densityPipelineLayout_);
    if (!densitySplat) return std::unexpected(densitySplat.error());
    auto densityResolve = makeCompute("cs_density_resolve", &*densityModule, &densityPipelineLayout_);
    if (!densityResolve) return std::unexpected(densityResolve.error());
    auto densityCoarse = makeCompute("cs_density_coarse", &*densityModule, &densityCoarsePipelineLayout_); // ADR-1150
    if (!densityCoarse) return std::unexpected(densityCoarse.error());
    auto additive = makeRender(true, "vs_particle");
    if (!additive) return std::unexpected(additive.error());
    auto alpha = makeRender(false, "vs_particle");
    if (!alpha) return std::unexpected(alpha.error());
    auto ribbonAdditive = makeRender(true, "vs_ribbon");
    if (!ribbonAdditive) return std::unexpected(ribbonAdditive.error());
    auto ribbonAlpha = makeRender(false, "vs_ribbon");
    if (!ribbonAlpha) return std::unexpected(ribbonAlpha.error());
    auto flakeAdditive = makeRender(true, "vs_flake"); // ADR-1153
    if (!flakeAdditive) return std::unexpected(flakeAdditive.error());
    auto flakeAlpha = makeRender(false, "vs_flake");
    if (!flakeAlpha) return std::unexpected(flakeAlpha.error());
    auto tendon = makeCompute("cs_tendon"); // ADR-1146
    if (!tendon) return std::unexpected(tendon.error());
    auto heat = makeCompute("cs_heat"); // ADR-1148
    if (!heat) return std::unexpected(heat.error());
    // ADR-1147: shards are opaque plates: no blend, depth written, the colour and velocity targets.
    Result<wgpu::RenderPipeline> shard = [&]() -> Result<wgpu::RenderPipeline> {
        std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
        fillSceneTargets(colorTargets, kHdrFormat, nullptr, wgpu::ColorWriteMask::None);
        colorTargets[2].writeMask = wgpu::ColorWriteMask::All;
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_shard";
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = colorTargets.data();
        wgpu::DepthStencilState depth{};
        depth.format = kDepthFormat;
        depth.depthWriteEnabled = wgpu::OptionalBool::True;
        depth.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor desc{};
        desc.label = "particles-shards";
        desc.layout = renderPipelineLayout_;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_shard";
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("particle shard pipeline failed: {}", error);
        }
        return pipeline;
    }();
    if (!shard) return std::unexpected(shard.error());
    emitPipeline_ = *emit;
    simulatePipeline_ = *simulate;
    scanReducePipeline_ = *reduce;
    scanTopPipeline_ = *top;
    scanScatterPipeline_ = *scatter;
    glowReducePipeline_ = *glowReduce;
    glowTopPipeline_ = *glowTop;
    latentPipeline_ = *latent;
    latentProjectPipeline_ = *latentProject;
    latentSpringPipeline_ = *latentSpring;
    densitySplatPipeline_ = *densitySplat;
    densityResolvePipeline_ = *densityResolve;
    densityCoarsePipeline_ = *densityCoarse;
    additivePipeline_ = *additive;
    alphaPipeline_ = *alpha;
    ribbonAdditivePipeline_ = *ribbonAdditive;
    ribbonAlphaPipeline_ = *ribbonAlpha;
    flakeAdditivePipeline_ = *flakeAdditive;
    flakeAlphaPipeline_ = *flakeAlpha;
    tendonPipeline_ = *tendon;
    heatPipeline_ = *heat;
    shardPipeline_ = *shard;
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
    // CopySrc: readParticles (tests and tools) copies the pool out; no frame path reads it that way.
    pool.particles = buffer("particles-pool", static_cast<std::uint64_t>(capacity) * kParticleStride,
                            kStorage | wgpu::BufferUsage::CopySrc);
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
    // ADR-1140: one record until a latent asks for more (ensureLatentBuffer re-creates the group).
    pool.latentBytes = kSdfNodeBytes;
    pool.latentNodes = buffer("particles-latent-sdf", pool.latentBytes, kStorage);
    buildComputeGroup(pool);
    pool.renderGroup = nullptr;
    pool.needsReset = true;
}

void ParticleRenderer::buildComputeGroup(Pool& pool) {
    const std::uint64_t listBytes = static_cast<std::uint64_t>(pool.capacity) * 4;
    const std::uint64_t scratchBytes = listBytes + static_cast<std::uint64_t>(pool.blocks) * 4;
    const std::uint64_t historyBytes =
        pool.historyPoints > 0 ? static_cast<std::uint64_t>(pool.capacity) * pool.historyPoints * scene::kTrailBytesPerPoint
                               : 16;
    const std::uint64_t glowScratchBytes = (static_cast<std::uint64_t>(pool.blocks) + 1) * kGlowBlockBytes;
    std::array<wgpu::BindGroupEntry, kComputeBindings> entries{};
    const wgpu::Buffer* buffers[kComputeBindings] = {&pool.uniforms,  &pool.particles,  &pool.deadList,
                                                     &pool.counters,  &pool.aliveList,  &pool.history,
                                                     &pool.glowScratch, &pool.scratch,  &fieldBlock_,
                                                     &splineTable_,   &gridTable_,     &pool.latentNodes};
    const std::uint64_t sizes[kComputeBindings] = {sizeof(ParticleUniforms),
                                                   static_cast<std::uint64_t>(pool.capacity) * kParticleStride,
                                                   listBytes, kCountersBytes, listBytes, historyBytes,
                                                   glowScratchBytes, scratchBytes,
                                                   FieldUniforms::kBufferSize, SplineBuffers::kBufferSize,
                                                   FieldUniforms::kGridBufferSize, pool.latentBytes};
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
    pool.computeGroup = context_.device().CreateBindGroup(&cdesc);
}

void ParticleRenderer::ensureLatentBuffer(Pool& pool, std::uint64_t bytes) {
    if (pool.latentNodes && pool.latentBytes >= bytes) {
        return;
    }
    wgpu::BufferDescriptor desc{};
    desc.label = "particles-latent-sdf";
    desc.size = std::max<std::uint64_t>(bytes, kSdfNodeBytes);
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    pool.latentNodes = context_.device().CreateBuffer(&desc);
    pool.latentBytes = desc.size;
    buildComputeGroup(pool);
}

void ParticleRenderer::ensureDensity(Pool& pool, const scene::ParticleDensity& density) {
    Pool::Density& d = pool.density;
    const int res = std::clamp(density.resolution, scene::kMinDensityResolution, scene::kMaxDensityResolution);
    d.boundsMin = density.boundsMin;
    d.boundsMax = density.boundsMax;
    if (d.texture && d.resolution == res && d.group) {
        return;
    }
    const auto& device = context_.device();
    const auto r = static_cast<std::uint64_t>(res);
    d = Pool::Density{};
    d.resolution = res;
    d.boundsMin = density.boundsMin;
    d.boundsMax = density.boundsMax;
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-density-params";
        desc.size = sizeof(DensityParamsGpu);
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        d.params = device.CreateBuffer(&desc);
        desc.label = "particles-density-grid";
        desc.size = r * r * r * 4;
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        d.grid = device.CreateBuffer(&desc);
    }
    {
        // rgba16float: filterable (a linear sampler can read it, which r32float cannot be in core
        // WebGPU) and usable as a write-only storage texture, so one compute pass writes what the
        // raymarch samples. Only r carries the density; the format has no one-channel half that is
        // both.
        wgpu::TextureDescriptor desc{};
        desc.label = "particles-density-volume";
        desc.dimension = wgpu::TextureDimension::e3D;
        desc.size = {static_cast<std::uint32_t>(res), static_cast<std::uint32_t>(res), static_cast<std::uint32_t>(res)};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        desc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
        d.texture = device.CreateTexture(&desc);
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e3D;
        d.view = d.texture.CreateView(&view);
    }
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = d.params;
    entries[0].size = sizeof(DensityParamsGpu);
    entries[1].binding = 1;
    entries[1].buffer = pool.particles;
    entries[1].size = static_cast<std::uint64_t>(pool.capacity) * kParticleStride;
    entries[2].binding = 2;
    entries[2].buffer = d.grid;
    entries[2].size = r * r * r * 4;
    entries[3].binding = 3;
    entries[3].textureView = d.view;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "particles-density-group";
    desc.layout = densityLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    d.group = device.CreateBindGroup(&desc);
    {
        // ADR-1150: the coarse max-occupancy grid, one texel per 8^3 block. rgba16float for the same reason
        // as the volume (storage-writable in core); 1/512 of the volume's texels.
        const auto cr = static_cast<std::uint32_t>(scene::densityCoarseResolution(res));
        wgpu::TextureDescriptor tdesc{};
        tdesc.label = "particles-density-coarse";
        tdesc.dimension = wgpu::TextureDimension::e3D;
        tdesc.size = {cr, cr, cr};
        tdesc.format = wgpu::TextureFormat::RGBA16Float;
        tdesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
        d.coarse = device.CreateTexture(&tdesc);
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e3D;
        d.coarseView = d.coarse.CreateView(&view);
        std::array<wgpu::BindGroupEntry, 3> ce{};
        ce[0].binding = 0;
        ce[0].buffer = d.params;
        ce[0].size = sizeof(DensityParamsGpu);
        ce[1].binding = 4;
        ce[1].textureView = d.view;
        ce[2].binding = 5;
        ce[2].textureView = d.coarseView;
        wgpu::BindGroupDescriptor cdesc{};
        cdesc.label = "particles-density-coarse-group";
        cdesc.layout = densityCoarseLayout_;
        cdesc.entryCount = ce.size();
        cdesc.entries = ce.data();
        d.coarseGroup = device.CreateBindGroup(&cdesc);
    }
}

void ParticleRenderer::encodeDensity(wgpu::CommandEncoder& encoder, Pool& pool, const scene::ParticleDensity& density,
                                     bool splat) {
    ensureDensity(pool, density);
    Pool::Density& d = pool.density;
    const auto res = static_cast<std::uint32_t>(d.resolution);
    DensityParamsGpu u{};
    u.boundsMin = glm::vec4(d.boundsMin, 0.0f);
    u.boundsMax = glm::vec4(d.boundsMax, std::max(density.weight, 0.0f));
    u.counts = glm::uvec4(pool.capacity, res, 0u, 0u);
    context_.queue().WriteBuffer(d.params, 0, &u, sizeof(u));
    encoder.ClearBuffer(d.grid, 0, static_cast<std::uint64_t>(res) * res * res * 4);
    wgpu::ComputePassDescriptor cdesc{};
    cdesc.label = "particles-density";
    cdesc.timestampWrites = (timeline_ != nullptr && !warming_) ? timeline_->mark("particle-density") : nullptr;
    wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cdesc);
    cp.SetBindGroup(0, d.group);
    if (splat) {
        cp.SetPipeline(densitySplatPipeline_);
        cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
    }
    cp.SetPipeline(densityResolvePipeline_);
    const std::uint32_t groups = (res + 3) / 4;
    cp.DispatchWorkgroups(groups, groups, groups);
    // ADR-1150: the block maxima, from the volume just resolved (a separate dispatch, so the texture the
    // resolve wrote is complete and readable).
    cp.SetBindGroup(0, d.coarseGroup);
    cp.SetPipeline(densityCoarsePipeline_);
    const std::uint32_t coarseGroups = (static_cast<std::uint32_t>(scene::densityCoarseResolution(d.resolution)) + 3) / 4;
    cp.DispatchWorkgroups(coarseGroups, coarseGroups, coarseGroups);
    cp.End();
    d.resolved = true;
    densityThisFrame_ = true;
    ++stats_.densityVolumes;
}

void ParticleRenderer::ensureRenderGroup(Pool& pool) {
    const wgpu::TextureView& depthView = frame_.linearDepth ? frame_.linearDepth : depthPlaceholderView_;
    // ADR-715: the placeholder is never read -- `terrain1.w` is 0 whenever this is bound.
    const wgpu::TextureView& terrainView = frame_.terrainHeight ? frame_.terrainHeight : depthPlaceholderView_;
    if (pool.renderGroup && pool.renderDepthView.Get() == depthView.Get() &&
        pool.renderTerrainView.Get() == terrainView.Get()) {
        return;
    }
    const std::uint64_t listBytes = static_cast<std::uint64_t>(pool.capacity) * 4;
    const std::uint64_t historyBytes =
        pool.historyPoints > 0 ? static_cast<std::uint64_t>(pool.capacity) * pool.historyPoints * scene::kTrailBytesPerPoint
                               : 16;
    std::array<wgpu::BindGroupEntry, 6> entries{};
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
    entries[5].binding = 12;
    entries[5].textureView = terrainView;
    wgpu::BindGroupDescriptor rdesc{};
    rdesc.label = "particles-render-group";
    rdesc.layout = renderLayout_;
    rdesc.entryCount = entries.size();
    rdesc.entries = entries.data();
    pool.renderGroup = context_.device().CreateBindGroup(&rdesc);
    pool.renderDepthView = depthView;
    pool.renderTerrainView = terrainView;
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
    pool.latentPrevValid = false; // ADR-1140: a reset is not a coherence drop
    pool.heatFrontStart = -1.0;   // ADR-1148
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
        densityThisFrame_ = false;
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
        // ADR-1098: the live distance cull, treated exactly as a disabled system (the reasons are the comment below).
        bool culled = false;
        if (frame_.cullDistance > 0.0f && sys.shape != scene::EmitterShape::Spline &&
            !sys.scatterAnchor.active()) {
            const float weight = scene::importanceLeverWeight(sys.importance);
            const float reach = std::max({sys.extent.x, sys.extent.y, sys.extent.z, 0.0f});
            if (weight > 0.0f && glm::length(sys.position - frame_.cameraPosition) - reach > frame_.cullDistance / weight) {
                culled = true;
                ++stats_.culledByDistance;
            }
        }
        if (!sys.enabled || culled) {
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
            // ADR-1141: a system that is off has no matter, so a consumer of its volume must see none
            // -- not the last frame it was on. Resolved empty (no splat), and only outside a warm-up.
            if (sys.density.enabled && !warming_) {
                encodeDensity(encoder, pool, sys.density, false);
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
        // Scatter-anchored clusters (scene::ScatterAnchor). The candidates -- every gated instance of
        // the named layers -- are rebuilt only when those objects' structure moves; the table is
        // then chosen for this camera, which makes it a function of where the camera is and never
        // of which frames came before (ADR-360).
        std::vector<glm::vec3> anchorTable;
        const bool anchored = sys.scatterAnchor.active() && sys.clusterCount > 0;
        const std::uint32_t anchorSlots = std::min(sys.clusterCount, scene::kMaxScatterAnchors);
        if (anchored) {
            const std::uint64_t version = scene::scatterAnchorVersion(scene.procedurals, sys.scatterAnchor);
            if (!pool.anchorBuilt || version != pool.anchorVersion) {
                scene::ScatterAnchorSet set = scene::scatterAnchorPoints(scene.procedurals, sys.scatterAnchor);
                // Said out loud, because the failure this guards against is a swarm system that
                // loads, finds no trees and draws nothing -- indistinguishable, in a frame, from
                // a camera that simply has none nearby.
                for (const std::string& layer : set.missing) {
                    log::warn("particles '{}': scatterAnchor layer '{}' names no scatter object with instances "
                              "(looked for '{}')",
                              sys.name, layer, scene::scatterObjectName(sys.scatterAnchor.terrain, layer));
                }
                log::info("particles '{}': {} of {} scatter instances carry a swarm ({:.1f}%; {} of them lit)",
                          sys.name, set.points.size(), set.considered,
                          set.considered > 0 ? 100.0 * static_cast<double>(set.points.size()) /
                                                   static_cast<double>(set.considered)
                                             : 0.0,
                          set.lit);
                pool.anchorPoints = std::move(set.points);
                pool.anchorVersion = set.version;
                pool.anchorBuilt = true;
            }
            anchorTable = scene::nearestScatterAnchors(pool.anchorPoints, frame_.cameraPosition,
                                                       sys.scatterAnchor.viewDistance, anchorSlots);
            if (!warming_) {
                stats_.anchors += static_cast<std::uint32_t>(anchorTable.size());
            }
        }
        // ADR-1109: a hero's emitter keeps its authored rate under every live spawn scale (the brief's 5.3:
        // "particles: preserve"); only the levers' weight-0 rule, the same as the distance cull above.
        const float spawnScale = scene::importanceLeverWeight(sys.importance) <= 0.0f ? 1.0f : frame_.spawnScale;
        double rate = static_cast<double>(sys.spawnRate) * static_cast<double>(spawnScale);
        if (anchored) {
            // `spawnRate` is the rate with the table full, so a tree's swarm is as dense when three
            // trees are in reach as when sixty are.
            rate *= static_cast<double>(anchorTable.size()) / static_cast<double>(std::max(anchorSlots, 1u));
        }
        const double owed = std::floor(rate * time.renderTime) - std::floor(rate * (time.renderTime - dt));
        std::uint32_t emitCount =
            owed > 0.0 ? static_cast<std::uint32_t>(std::min(owed, static_cast<double>(pool.capacity))) : 0u;
        emitCount += static_cast<std::uint32_t>(std::max(0.0f, sys.burst));
        emitCount = std::min(emitCount, pool.capacity);
        if (anchored && anchorTable.empty()) {
            emitCount = 0; // nowhere to be born
        }

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
    if (sys.shape2d == scene::ParticleShape::Flake) { // ADR-1153; all zero otherwise
        const scene::ParticleFlake& f = sys.flake;
        u.flake0 = glm::vec4(glm::clamp(f.metal, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
        u.flake1 = glm::vec4(std::max(f.temper, 0.0f), std::max(f.filmIor, 1.0f), std::max(f.glint, 1e-3f),
                             std::max(f.tumble, 0.0f));
        u.flake2 = glm::vec4(std::max(f.free, 0.0f), std::max(f.bound, 0.0f), glm::clamp(f.latentNormal, 0.0f, 1.0f),
                             glm::clamp(f.sparkle, 0.0f, 1.0f));
        u.flake3 = glm::vec4(std::max(f.sparkleGain, 0.0f), glm::clamp(f.fuse, 0.0f, 1.0f), 0.0f, 0.0f);
        u.bandsInfo = frame_.bands.info;
        u.bandsSoft = frame_.bands.soft;
        u.bandsSoft2 = frame_.bands.soft2;
        u.bandsRate = frame_.bands.rate;
        for (std::size_t k = 0; k < frame_.bands.bands.size(); ++k) {
            u.bands[k] = frame_.bands.bands[k];
        }
    }
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
        const bool ground = frame_.terrainHeight && frame_.terrainMap1.w > 0.5f;
        u.fog3 = glm::vec4(std::clamp(frame_.fogUpperDensity, 0.0f, 1.0f),
                           std::clamp(frame_.fogHeightCurve, 0.0f, 1.0f),
                           ground ? std::clamp(frame_.fogGroundFollow, 0.0f, 1.0f) : 0.0f,
                           std::clamp(frame_.horizonDensity, 0.0f, scene::kHorizonDensityMax));
        u.terrain0 = ground ? frame_.terrainMap0 : glm::vec4(0.0f);
        u.terrain1 = ground ? frame_.terrainMap1 : glm::vec4(0.0f);
        u.fogPool = glm::vec4(ground ? std::clamp(frame_.fogPooling, 0.0f, 1.0f) : 0.0f, 0.0f, 0.0f, 0.0f);
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
        u.anchorInfo = glm::vec4(static_cast<float>(anchorTable.size()), anchored ? 1.0f : 0.0f, 0.0f, 0.0f);
        for (std::size_t k = 0; k < anchorTable.size(); ++k) {
            u.anchors[k] = glm::vec4(anchorTable[k], 1.0f);
        }
        // ADR-1140: the latent SDF force. The object is looked up by its flattened name every frame
        // (its tree's parameters are live) and packed into this pool's own buffer -- never
        // SdfRenderer's, which holds only the visible raymarch objects and reallocates. A latent is
        // typically invisible, which is exactly the object SdfRenderer skips.
        bool latentOn = false;
        wgpu::ComputePipeline latentCompiled; // ADR-1145: null = the interpreter's cs_latent
        // ADR-1147: shards (a flake system's near bound plates), when the frame knows its pixel angle.
        if (sys.shards.enabled && sys.shape2d == scene::ParticleShape::Flake && frame_.pixelAngle > 0.0f) {
            u.shard0 = glm::vec4(std::max(sys.shards.pixels, 0.1f), std::clamp(sys.shards.fraction, 0.0f, 1.0f),
                                 std::max(sys.shards.size, 1e-4f), 1.0f);
            u.shard1 = glm::vec4(std::max(sys.shards.grooves, 0.0f), std::clamp(sys.shards.bevel, 0.0f, 0.9f),
                                 frame_.pixelAngle, 0.0f);
        }
        if (sys.latent.active()) {
            const scene::SdfObject* object = nullptr;
            for (const scene::SdfObject& candidate : scene.sdfs) {
                if (candidate.name == sys.latent.sdf) {
                    object = &candidate;
                    break;
                }
            }
            std::optional<std::string> why;
            int count = 0;
            latentCompiled = wgpu::ComputePipeline{};
            const bool tendons = sys.latent.tendons.active(); // ADR-1146: the curves, not the tree
            if (object == nullptr) {
                why = "names no sdf object in the scene";
            } else if (tendons) {
                latentScratch_.clear();
                for (const auto& curve : sys.latent.tendons.curves) {
                    // resampled by arc length to kTendonSamples points (p0.xyz = the point, latent-local)
                    std::vector<float> acc(curve.size(), 0.0f);
                    for (std::size_t k = 1; k < curve.size(); ++k) {
                        acc[k] = acc[k - 1] + glm::length(curve[k] - curve[k - 1]);
                    }
                    const float total = std::max(acc.back(), 1e-6f);
                    std::size_t seg = 0;
                    for (int j = 0; j < scene::kTendonSamples; ++j) {
                        const float at = total * static_cast<float>(j) / static_cast<float>(scene::kTendonSamples - 1);
                        while (seg + 2 < curve.size() && acc[seg + 1] < at) {
                            ++seg;
                        }
                        const float len = std::max(acc[seg + 1] - acc[seg], 1e-9f);
                        const float f = std::clamp((at - acc[seg]) / len, 0.0f, 1.0f);
                        spatial::SdfNodeGpu rec{};
                        rec.fieldSlot = -1;
                        rec.p0 = glm::vec4(curve[seg] + (curve[seg + 1] - curve[seg]) * f, at);
                        latentScratch_.push_back(rec);
                    }
                }
                count = static_cast<int>(latentScratch_.size());
            } else if (object->compile) {
                // ADR-1145: a compiled latent object binds through a compiled variant of cs_latent.
                if (auto fits = object->tree.validate(spatial::SdfEvaluator::Compiled); !fits) {
                    why = fits.error().message;
                } else if (const CompiledLatent& built = compiledLatentPipeline(object->tree, &scene.fields); !built.plain) {
                    why = "its compiled force did not build";
                } else {
                    latentCompiled = sys.latent.stagger > 1 ? built.project : built.plain;
                    spatial::sdfCompileTable(object->tree, latentScratch_, &scene.fields);
                    count = static_cast<int>(latentScratch_.size());
                    if (count <= 0) {
                        why = "its tree compiles to no node";
                    }
                }
            } else if (auto fits = object->tree.validate(spatial::SdfEvaluator::Interpreter); !fits) {
                why = fits.error().message;
            } else {
                count = spatial::packSdfTree(object->tree, latentScratch_, &scene.fields);
                if (count <= 0) {
                    why = "its tree packs to no enabled node";
                }
            }
            if (why) {
                // Composition refuses these at load; a scene edited live can still get here.
                if (!pool.latentWarned) {
                    log::warn("particles '{}': latent sdf '{}' {}; the latent force is off", sys.name, sys.latent.sdf, *why);
                    pool.latentWarned = true;
                }
            } else {
                // DisplaceField references: FieldSet indices -> GPU slots, exactly as SdfRenderer maps them.
                for (auto& g : latentScratch_) {
                    if (g.kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::DisplaceField) && g.fieldSlot >= 0) {
                        int gpuSlot = -1;
                        if (fields != nullptr && static_cast<std::size_t>(g.fieldSlot) < scene.fields.fields.size()) {
                            gpuSlot = fields->slotOf(scene.fields.fields[static_cast<std::size_t>(g.fieldSlot)].name);
                        }
                        g.fieldSlot = gpuSlot;
                    }
                }
                const std::uint64_t bytes = latentScratch_.size() * kSdfNodeBytes;
                ensureLatentBuffer(pool, bytes);
                context_.queue().WriteBuffer(pool.latentNodes, 0, latentScratch_.data(), bytes);
                const glm::mat4 model = object->transform.matrix();
                const float coherence = std::clamp(sys.latent.coherence, 0.0f, 1.0f);
                const float previous = pool.latentPrevValid ? pool.latentPrevCoherence : coherence;
                u.latentModel = model;
                u.latentInverse = glm::inverse(model);
                u.latentNormal = glm::transpose(u.latentInverse);
                u.latent0 = glm::vec4(coherence, previous, std::clamp(sys.latent.width, 0.005f, 0.5f),
                                      std::max(sys.latent.strength, 0.0f));
                u.latent1 = glm::vec4(std::max(sys.latent.flow, 0.0f), std::max(sys.latent.release, 0.0f),
                                      scene::latentGradientEpsilon(object->boundsMin, object->boundsMax), 0.0f);
                // ADR-1153: a flake system's bound plates take the latent normal, which cs_latent then stores in
                // the record's `home` lane (w stays 0, so the attractor reads it as "no home"); never for an
                // anchored system, whose `home` is its crown centre.
                const bool storeNormal = sys.shape2d == scene::ParticleShape::Flake && !anchored;
                // ADR-1155: z = the stagger stride (0 = unstaggered, as before; never with tendons or anchors)
                const std::uint32_t stagger =
                    sys.latent.stagger > 1 && !tendons && !anchored ? static_cast<std::uint32_t>(sys.latent.stagger) : 0u;
                u.latentInfo = glm::uvec4(static_cast<std::uint32_t>(count), storeNormal ? 1u : 0u, stagger, 0u);
                if (tendons) { // ADR-1146
                    const scene::ParticleTendons& t = sys.latent.tendons;
                    float mean = 0.0f;
                    for (const auto& curve : t.curves) {
                        for (std::size_t k = 1; k < curve.size(); ++k) {
                            mean += glm::length(curve[k] - curve[k - 1]);
                        }
                    }
                    mean /= static_cast<float>(t.curves.size());
                    u.tendon0 = glm::vec4(std::max(t.speed, 0.0f) / std::max(mean, 1e-4f), std::max(t.stiffness, 0.0f),
                                          std::max(t.spray, 0.0f), std::clamp(t.ramp, 0.0f, 0.9f));
                    u.tendonInfo = glm::uvec4(0u, static_cast<std::uint32_t>(t.curves.size()),
                                              static_cast<std::uint32_t>(scene::kTendonSamples), 0u);
                }
                if (const scene::ParticleHeat& h = sys.latent.heat; h.enabled) { // ADR-1148
                    // the front sets out on the step the coherence starts to fall, and is gone once it rises again
                    const double now = time.renderTime;
                    if (coherence < previous - 1e-6f && pool.heatFrontStart < 0.0) {
                        pool.heatFrontStart = now;
                    } else if (coherence > previous + 1e-6f) {
                        pool.heatFrontStart = -1.0;
                    }
                    const float scale = std::cbrt(std::max(std::abs(glm::determinant(glm::mat3(model))), 1e-12f));
                    const glm::vec3 origin = glm::vec3(model * glm::vec4(h.origin, 1.0f));
                    u.heat0 = glm::vec4(origin, pool.heatFrontStart < 0.0 ? -1.0f
                                                                          : static_cast<float>(now - pool.heatFrontStart));
                    u.heat1 = glm::vec4(std::max(h.speed, 0.0f) * scale, std::max(h.width, 1e-4f) * scale,
                                        std::max(h.inject, 0.0f), std::max(h.decay, 0.0f));
                    u.heat2 = glm::vec4(std::clamp(h.fraction, 0.0f, 1.0f), std::max(h.gain, 0.0f), 1.0f, 0.0f);
                }
                pool.latentPrevCoherence = coherence;
                pool.latentPrevValid = true;
                pool.latentWarned = false;
                // Say once (and again on a change) which force this system runs: the evidence a cost reads.
                const std::uint32_t mode = 1u + (tendons ? 3u : (latentCompiled ? 1u : 0u)) + (stagger > 1u ? 4u : 0u);
                if (pool.latentModeLogged != mode) {
                    log::info("particles '{}': latent force {}{} ({} records)", sys.name,
                              tendons ? "cs_tendon" : (latentCompiled ? "compiled" : "interpreted"),
                              stagger > 1u ? fmt::format(", staggered {}", stagger) : std::string(), count);
                    pool.latentModeLogged = mode;
                }
                latentOn = true;
                if (!warming_) {
                    ++stats_.latentSystems;
                }
            }
        }
        context_.queue().WriteBuffer(pool.uniforms, 0, &u, sizeof(u));

        // Pass order (see particles.wgsl): emit -> [latent] -> simulate -> reduce -> top scan -> scatter.
        // Dispatches in one compute pass are ordered, so each reads the previous one's writes;
        // emit consumes last frame's dead list before scatter rewrites it.
        wgpu::ComputePassDescriptor cdesc{};
        cdesc.label = "particles-compute";
        // One mark per system's pass; the timeline sums them under the one label, so a scene with
        // several systems reports what all of them cost rather than what the last one did.
        cdesc.timestampWrites = (timeline_ != nullptr && !warming_) ? timeline_->mark("particles") : nullptr;
        ++encoded;
        ++stats_.dispatches;
        ++stats_.simulationSteps; // ADR-1091: one emit-and-simulate step per enabled system per frame
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cdesc);
        cp.SetBindGroup(0, pool.computeGroup);
        if (emitCount > 0) {
            cp.SetPipeline(emitPipeline_);
            cp.DispatchWorkgroups((emitCount + kWorkgroup - 1) / kWorkgroup);
        }
        if (latentOn) {
            // ADR-1140: adds the spring toward the latent's projection to the velocity, which the
            // simulate dispatch then integrates. Not dispatched at all for a system without one.
            // ADR-1146: a latent with tendons binds to its curves (cs_tendon) instead of the zero set.
            if (sys.latent.tendons.active()) {
                cp.SetPipeline(tendonPipeline_);
                cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
            } else if (u.latentInfo.z > 1u) {
                // ADR-1155: project this step's third (its blocks only), then spring everything to what is stored.
                const std::uint32_t blocks = (pool.capacity + 63u) / 64u;
                const std::uint32_t turn = (blocks + u.latentInfo.z - 1u) / u.latentInfo.z;
                cp.SetPipeline(latentCompiled ? latentCompiled : latentProjectPipeline_);
                cp.DispatchWorkgroups(std::max(turn * 64u / kWorkgroup, 1u));
                cp.SetPipeline(latentSpringPipeline_);
                cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
            } else {
                cp.SetPipeline(latentCompiled ? latentCompiled : latentPipeline_);
                cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
            }
            if (sys.latent.heat.enabled) { // ADR-1148: the release front's heat, after the release it reads
                cp.SetPipeline(heatPipeline_);
                cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
            }
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
        // ADR-1141: the volume is a picture of THIS frame's particles, so it is skipped inside a
        // warm-up (only the arriving frame's is ever read) and rebuilt from scratch every frame.
        if (sys.density.enabled && !warming_) {
            encodeDensity(encoder, pool, sys.density, true);
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
    if (densityThisFrame_) {
        stats_.densityMs = lastDensityMs_;
    }
    if (!warming_) {
        for (const Pool& pool : pools_) {
            if (pool.density.texture) {
                stats_.densityBytes += scene::densityMemoryBytes(pool.density.resolution);
            }
        }
    }
}

ParticleDensityVolume ParticleRenderer::densityVolume(std::size_t systemIndex) const {
    ParticleDensityVolume v;
    v.view = densityPlaceholderView_;
    v.coarseView = densityPlaceholderView_;
    if (systemIndex < pools_.size()) {
        const Pool::Density& d = pools_[systemIndex].density;
        if (d.view && d.resolved) {
            v.view = d.view;
            v.coarseView = d.coarseView;
            v.boundsMin = d.boundsMin;
            v.boundsMax = d.boundsMax;
            v.resolution = d.resolution;
            v.valid = true;
        }
    }
    return v;
}

ParticleDensityVolume ParticleRenderer::densityVolume(const scene::Scene& scene, const std::string& name) const {
    for (std::size_t i = 0; i < scene.particles.size(); ++i) {
        if (scene.particles[i].name == name) {
            return densityVolume(i);
        }
    }
    return densityVolume(pools_.size()); // the placeholder
}

Result<std::vector<ParticleSnapshot>> ParticleRenderer::readParticles(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].particles) {
        return fail("particle system {} has no pool", systemIndex);
    }
    const Pool& pool = pools_[systemIndex];
    auto bytes = gpu::readBuffer(context_, pool.particles, 0, static_cast<std::uint64_t>(pool.capacity) * kParticleStride);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<ParticleSnapshot> out(pool.capacity);
    for (std::uint32_t i = 0; i < pool.capacity; ++i) {
        float rec[16];
        std::memcpy(rec, bytes->data() + static_cast<std::size_t>(i) * kParticleStride, sizeof(rec));
        out[i].position = glm::vec3(rec[0], rec[1], rec[2]);
        out[i].age = rec[3];
        out[i].velocity = glm::vec3(rec[4], rec[5], rec[6]);
        out[i].life = rec[7];
        out[i].seed = rec[8];
        out[i].trail = rec[10]; // ADR-1148: a heated system's heat
        out[i].home = glm::vec4(rec[12], rec[13], rec[14], rec[15]);
    }
    return out;
}

Result<std::vector<float>> ParticleRenderer::readDensity(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].density.texture) {
        return fail("particle system {} has no density volume", systemIndex);
    }
    const Pool::Density& d = pools_[systemIndex].density;
    return readVolumeR(d.texture, static_cast<std::uint32_t>(d.resolution));
}

Result<std::vector<float>> ParticleRenderer::readDensityCoarse(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].density.coarse) {
        return fail("particle system {} has no density volume", systemIndex);
    }
    const Pool::Density& d = pools_[systemIndex].density;
    return readVolumeR(d.coarse, static_cast<std::uint32_t>(scene::densityCoarseResolution(d.resolution)));
}

Result<std::vector<float>> ParticleRenderer::readVolumeR(const wgpu::Texture& texture, std::uint32_t res) {
    const std::uint32_t rowBytes = ((res * 8 + 255) / 256) * 256; // bytesPerRow must be a multiple of 256
    const std::uint64_t total = static_cast<std::uint64_t>(rowBytes) * res * res;
    wgpu::BufferDescriptor bdesc{};
    bdesc.label = "particles-density-readback";
    bdesc.size = total;
    bdesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer staging = context_.device().CreateBuffer(&bdesc);
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo src{};
    src.texture = texture;
    wgpu::TexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.bytesPerRow = rowBytes;
    dst.layout.rowsPerImage = res;
    const wgpu::Extent3D extent{res, res, res};
    encoder.CopyTextureToBuffer(&src, &dst, &extent);
    wgpu::CommandBuffer commands = encoder.Finish();
    context_.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(context_, staging, 0, total);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<float> out(static_cast<std::size_t>(res) * res * res);
    for (std::uint32_t z = 0; z < res; ++z) {
        for (std::uint32_t y = 0; y < res; ++y) {
            const std::uint8_t* row = bytes->data() + (static_cast<std::size_t>(z) * res + y) * rowBytes;
            for (std::uint32_t x = 0; x < res; ++x) {
                std::uint16_t half = 0;
                std::memcpy(&half, row + static_cast<std::size_t>(x) * 8, sizeof(half));
                out[x + static_cast<std::size_t>(res) * (y + static_cast<std::size_t>(res) * z)] = gpu::halfToFloat(half);
            }
        }
    }
    return out;
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
        if (sys.shape2d == scene::ParticleShape::Flake && sys.shards.enabled && frame_.pixelAngle > 0.0f) {
            // ADR-1147: the near plates as opaque shards first (every 4th slot is a candidate), then the flakes.
            pass.SetPipeline(shardPipeline_);
            pass.Draw(12, (pool.capacity + 3) / 4);
            pass.SetBindGroup(0, pool.renderGroup);
        }
        if (sys.shape2d == scene::ParticleShape::Flake) { // ADR-1153: its own entry point, so no other system's
            pass.SetPipeline(additive ? flakeAdditivePipeline_ : flakeAlphaPipeline_); // vertex code changes
        } else {
            pass.SetPipeline(additive ? additivePipeline_ : alphaPipeline_);
        }
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
