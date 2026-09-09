#include "rendering/ao_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/render_target.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::rendering {

namespace {

std::uint32_t scaled(std::uint32_t v, float scale) {
    return std::max<std::uint32_t>(static_cast<std::uint32_t>(std::lround(static_cast<float>(v) * scale)), 1u);
}

} // namespace

struct AoRenderer::Impl {
    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> finish(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    Result<void> ensureTargets(std::uint32_t width, std::uint32_t height);
    void rebuildGroups(const wgpu::TextureView& linearDepth);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout aoLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline occlusionPipeline;
    wgpu::RenderPipeline temporalPipeline;
    wgpu::Buffer uniforms;
    gpu::RenderTarget raw;         // this frame's occlusion, before the temporal filter
    gpu::RenderTarget history[2];  // ping-pong accumulation
    wgpu::Texture white;           // 1x1 stand-in when AO is off or a pass writes the real one
    wgpu::TextureView whiteView;
    wgpu::BindGroup occlusionGroup;
    wgpu::BindGroup temporalGroup;
    wgpu::TextureView boundDepth;
    std::unique_ptr<gpu::GpuTimer> timer;
    double lastMs = -1.0;
    int historyIndex = 0;
    std::uint64_t lastFrameIndex = 0;
    bool haveLastFrame = false;
    bool hasHistory = false;
    bool activeThisFrame = false;
    bool passThisFrame = false;
    bool initialised = false;
};

AoRenderer::AoRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

AoRenderer::~AoRenderer() = default;

Result<wgpu::RenderPipeline> AoRenderer::Impl::finish(const wgpu::RenderPipelineDescriptor& desc,
                                                      const char* label) {
    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !pipeline) {
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    return pipeline;
}

Result<void> AoRenderer::Impl::createPipelines(const wgpu::ShaderModule& module) {
    wgpu::ColorTargetState target{};
    target.format = AoRenderer::kFormat;
    target.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_gtao";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "gtao-occlusion";
    desc.layout = pipelineLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    auto occlusion = finish(desc, "gtao-occlusion");
    if (!occlusion) {
        return std::unexpected(occlusion.error());
    }
    fragment.entryPoint = "fs_temporal";
    desc.label = "gtao-temporal";
    auto temporal = finish(desc, "gtao-temporal");
    if (!temporal) {
        return std::unexpected(temporal.error());
    }
    occlusionPipeline = *occlusion;
    temporalPipeline = *temporal;
    return {};
}

Result<void> AoRenderer::Impl::ensureTargets(std::uint32_t width, std::uint32_t height) {
    if (raw.valid() && raw.width() == width && raw.height() == height) {
        return {};
    }
    gpu::RenderTargetDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.colorFormat = AoRenderer::kFormat;
    desc.depthFormat = wgpu::TextureFormat::Undefined;
    desc.extraColorUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
    desc.label = "gtao-raw";
    auto made = gpu::RenderTarget::create(context, desc);
    if (!made) {
        return std::unexpected(made.error());
    }
    raw = std::move(*made);
    for (int i = 0; i < 2; ++i) {
        desc.label = "gtao-history";
        auto made2 = gpu::RenderTarget::create(context, desc);
        if (!made2) {
            return std::unexpected(made2.error());
        }
        history[i] = std::move(*made2);
    }
    occlusionGroup = nullptr;
    temporalGroup = nullptr;
    hasHistory = false;
    historyIndex = 0;
    return {};
}

void AoRenderer::Impl::rebuildGroups(const wgpu::TextureView& linearDepth) {
    const auto& device = context.device();
    const wgpu::TextureView previous = history[1 - historyIndex].colorView();
    auto make = [&](const wgpu::TextureView& historyView, const wgpu::TextureView& rawView, const char* label) {
        std::array<wgpu::BindGroupEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].buffer = uniforms;
        entries[0].size = sizeof(AoUniforms);
        entries[1].binding = 1;
        entries[1].textureView = linearDepth;
        entries[2].binding = 2;
        entries[2].textureView = historyView;
        entries[3].binding = 3;
        entries[3].textureView = rawView;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = aoLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    // The occlusion pass writes `raw`, so it binds the placeholder in its place.
    occlusionGroup = make(whiteView, whiteView, "gtao-occlusion-group");
    temporalGroup = make(previous, raw.colorView(), "gtao-temporal-group");
    boundDepth = linearDepth;
}

Result<void> AoRenderer::init(const wgpu::BindGroupLayout& frameLayout) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.frameLayout = frameLayout;
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "gtao-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(AoUniforms);
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(AoUniforms);
        for (std::uint32_t i = 1; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "gtao-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.aoLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.aoLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "gtao-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "gtao-white";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = kFormat;
        im.white = device.CreateTexture(&desc);
        im.whiteView = im.white.CreateView();
        const std::array<std::uint16_t, 4> texel = {0x0000, 0x3C00, 0x0000, 0x3C00}; // (0, 1, 0, 1) half
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = im.white;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D size = {1, 1, 1};
        im.context.queue().WriteTexture(&destination, texel.data(), texel.size() * sizeof(std::uint16_t), &layout,
                                        &size);
    }
    im.timer = std::make_unique<gpu::GpuTimer>(im.context);
    auto module = im.shaders.load("gtao.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipelines(*module); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> AoRenderer::reload() {
    auto module = impl_->shaders.load("gtao.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return impl_->createPipelines(*module);
}

void AoRenderer::update(std::uint32_t width, std::uint32_t height, const wgpu::TextureView& linearDepth,
                        const QualitySettings& quality, std::uint64_t frameIndex, float fovYRadians,
                        float aspect, float nearPlane, float farPlane, float worldRadius, float strength) {
    Impl& im = *impl_;
    collectTimings();
    im.activeThisFrame = false;
    im.passThisFrame = false;
    stats_ = AoStats{};
    if (!im.initialised || !quality.ambientOcclusion || width == 0 || height == 0 || linearDepth == nullptr) {
        im.hasHistory = false;
        return;
    }
    const std::uint32_t aw = scaled(width, quality.aoResolutionScale);
    const std::uint32_t ah = scaled(height, quality.aoResolutionScale);
    if (auto r = im.ensureTargets(aw, ah); !r) {
        log::warn("ambient occlusion disabled this frame: {}", r.error().message);
        return;
    }
    // Determinism (ADR-035): the temporal history is only valid for a strictly advancing frame
    // index. Re-rendering a frame, or seeking, drops it, so two renders of the same frame agree.
    if (im.haveLastFrame && frameIndex != im.lastFrameIndex + 1) {
        im.hasHistory = false;
    }
    im.lastFrameIndex = frameIndex;
    im.haveLastFrame = true;
    im.historyIndex = 1 - im.historyIndex;
    im.rebuildGroups(linearDepth);

    const float tanHalf = std::tan(std::clamp(fovYRadians, 0.05f, 3.0f) * 0.5f);
    AoUniforms u{};
    u.sizes = glm::vec4(static_cast<float>(aw), static_cast<float>(ah), 1.0f / static_cast<float>(aw),
                        1.0f / static_cast<float>(ah));
    u.fullSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                           1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
    u.params = glm::vec4(std::max(worldRadius, 1e-3f), std::clamp(strength, 0.0f, 4.0f),
                         static_cast<float>(std::max(quality.aoSlices, 1u)),
                         static_cast<float>(std::max(quality.aoStepsPerSlice, 1u)));
    const float blend = 1.0f / static_cast<float>(std::max(quality.aoHistoryFrames, 1u));
    u.temporal = glm::vec4(static_cast<float>(frameIndex % 64u), blend, im.hasHistory ? 1.0f : 0.0f,
                           std::max(worldRadius * 0.5f, 0.05f));
    u.projection = glm::vec4(tanHalf * aspect, tanHalf, nearPlane, farPlane);
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    im.activeThisFrame = true;
    stats_.width = aw;
    stats_.height = ah;
    stats_.slices = std::max(quality.aoSlices, 1u);
    stats_.steps = std::max(quality.aoStepsPerSlice, 1u);
    stats_.aoMs = im.lastMs;
}

void AoRenderer::encode(wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frameBindGroup) {
    Impl& im = *impl_;
    if (!im.activeThisFrame) {
        return;
    }
    auto pass = [&](const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                    const wgpu::BindGroup& group, const char* label, bool timed) {
        wgpu::RenderPassColorAttachment attachment{};
        attachment.view = target;
        attachment.loadOp = wgpu::LoadOp::Clear;
        attachment.storeOp = wgpu::StoreOp::Store;
        attachment.clearValue = {0.0, 1.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor desc{};
        desc.label = label;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &attachment;
        desc.timestampWrites = timed ? im.timer->passWrites() : nullptr;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&desc);
        rp.SetPipeline(pipeline);
        rp.SetBindGroup(0, frameBindGroup);
        rp.SetBindGroup(1, group);
        rp.Draw(3);
        rp.End();
    };
    pass(im.occlusionPipeline, im.raw.colorView(), im.occlusionGroup, "gtao-pass", true);
    pass(im.temporalPipeline, im.history[im.historyIndex].colorView(), im.temporalGroup, "gtao-temporal-pass",
         false);
    im.timer->resolve(encoder);
    im.hasHistory = true;
    im.passThisFrame = true;
    stats_.aoMs = im.lastMs;
}

void AoRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timer) {
        const double ms = im.timer->collect();
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
    }
    stats_.aoMs = im.passThisFrame ? im.lastMs : -1.0;
}

void AoRenderer::resetHistory() {
    impl_->hasHistory = false;
}

const wgpu::TextureView& AoRenderer::output() const {
    Impl& im = *impl_;
    return im.activeThisFrame && im.history[im.historyIndex].valid() ? im.history[im.historyIndex].colorView()
                                                                     : im.whiteView;
}

const wgpu::TextureView& AoRenderer::placeholder() const {
    return impl_->whiteView;
}

bool AoRenderer::active() const {
    return impl_->activeThisFrame;
}

} // namespace avgen::rendering
