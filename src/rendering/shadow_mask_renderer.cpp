#include "rendering/shadow_mask_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
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

struct ShadowMaskRenderer::Impl {
    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    Result<void> createPipeline(const wgpu::ShaderModule& module);
    Result<void> ensureTarget(std::uint32_t width, std::uint32_t height);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout maskLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer uniforms;
    wgpu::BindGroup maskGroup;
    gpu::RenderTarget target;
    wgpu::Texture white; // 1x1 stand-in: fully lit, at a depth no surface agrees with
    wgpu::TextureView whiteView;
    gpu::FrameTimeline* timeline = nullptr;
    double lastMs = -1.0;
    bool activeThisFrame = false;
    bool passThisFrame = false;
    bool initialised = false;
};

ShadowMaskRenderer::ShadowMaskRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

ShadowMaskRenderer::~ShadowMaskRenderer() = default;

Result<void> ShadowMaskRenderer::Impl::createPipeline(const wgpu::ShaderModule& module) {
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = ShadowMaskRenderer::kFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_shadow_mask";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "shadow-mask";
    desc.layout = pipelineLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;

    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline made = device.CreateRenderPipeline(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !made) {
        return fail("pipeline 'shadow-mask' creation failed: {}", error);
    }
    pipeline = made;
    return {};
}

Result<void> ShadowMaskRenderer::Impl::ensureTarget(std::uint32_t width, std::uint32_t height) {
    if (target.valid() && target.width() == width && target.height() == height) {
        return {};
    }
    gpu::RenderTargetDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.colorFormat = ShadowMaskRenderer::kFormat;
    desc.depthFormat = wgpu::TextureFormat::Undefined;
    desc.extraColorUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
    desc.label = "shadow-mask-target";
    auto made = gpu::RenderTarget::create(context, desc);
    if (!made) {
        return std::unexpected(made.error());
    }
    target = std::move(*made);
    return {};
}

Result<void> ShadowMaskRenderer::init(const wgpu::BindGroupLayout& frameLayout) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.frameLayout = frameLayout;
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "shadow-mask-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(ShadowMaskUniforms);
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.minBindingSize = sizeof(ShadowMaskUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "shadow-mask-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        im.maskLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.maskLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "shadow-mask-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = im.uniforms;
        entry.size = sizeof(ShadowMaskUniforms);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "shadow-mask-group";
        desc.layout = im.maskLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        im.maskGroup = device.CreateBindGroup(&desc);
    }
    {
        // The stand-in bound when the mask is off, and into the mask's own pass. Fully lit, with a
        // view depth of zero: `shadowMaskLookup` rejects a texel whose depth disagrees with the
        // fragment's, and zero disagrees with everything, so a shader that reads the placeholder by
        // accident falls through to the full-resolution path rather than shading with white.
        wgpu::TextureDescriptor desc{};
        desc.label = "shadow-mask-white";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = kFormat;
        im.white = device.CreateTexture(&desc);
        im.whiteView = im.white.CreateView();
        const std::array<std::uint16_t, 4> texel = {0x3C00, 0x3C00, 0x3C00, 0x0000}; // (1, 1, 1, 0) half
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = im.white;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D size = {1, 1, 1};
        im.context.queue().WriteTexture(&destination, texel.data(), texel.size() * sizeof(std::uint16_t), &layout,
                                        &size);
    }
    auto module = im.shaders.load("shadow_mask.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipeline(*module); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> ShadowMaskRenderer::reload() {
    auto module = impl_->shaders.load("shadow_mask.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return impl_->createPipeline(*module);
}

void ShadowMaskRenderer::update(std::uint32_t width, std::uint32_t height, const QualitySettings& quality,
                                bool enabled, std::uint32_t directionalLights, float fovYRadians, float aspect,
                                float nearPlane, float farPlane) {
    Impl& im = *impl_;
    collectTimings();
    im.activeThisFrame = false;
    im.passThisFrame = false;
    stats_ = ShadowMaskStats{};
    // `shadowMaskScale >= 1` is how a tier says "do not build a mask": a full-resolution mask is a
    // second full-resolution pass over the same term and would cost more than it saves, so the
    // knob's top of range is off rather than a redundant pass. Offline and high sit there, which is
    // what keeps an offline render identical to the frame this code shipped without.
    const std::uint32_t lights = std::min(directionalLights, kMaxMaskedDirectionalLights);
    if (!im.initialised || !enabled || quality.shadowMaskScale >= 1.0f || lights == 0 || width == 0 ||
        height == 0) {
        return;
    }
    const std::uint32_t mw = scaled(width, quality.shadowMaskScale);
    const std::uint32_t mh = scaled(height, quality.shadowMaskScale);
    if (auto r = im.ensureTarget(mw, mh); !r) {
        log::warn("shadow mask disabled this frame: {}", r.error().message);
        return;
    }
    const float tanHalf = std::tan(std::clamp(fovYRadians, 0.05f, 3.0f) * 0.5f);
    ShadowMaskUniforms u{};
    u.sizes = glm::vec4(static_cast<float>(mw), static_cast<float>(mh), 1.0f / static_cast<float>(mw),
                        1.0f / static_cast<float>(mh));
    u.fullSize = glm::vec4(static_cast<float>(width), static_cast<float>(height),
                           1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
    u.projection = glm::vec4(tanHalf * aspect, tanHalf, nearPlane, farPlane);
    // The tier's own tap count. Doubling it for the mask was tried -- the mask runs at a quarter
    // of the pixels, so it can afford to -- and measured no better: 8.42% of the frame differed
    // from the unmasked reference at twelve taps against 8.50% at twenty-four, for 1.0 ms. The
    // filter is not what half resolution costs here.
    const std::uint32_t taps = std::clamp(quality.shadowPcfTaps, 1u, 32u);
    u.params = glm::vec4(static_cast<float>(lights), static_cast<float>(taps), 0.0f, 0.0f);
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    im.activeThisFrame = true;
    stats_.width = mw;
    stats_.height = mh;
    stats_.lights = lights;
    stats_.taps = taps;
    stats_.maskMs = im.lastMs;
}

void ShadowMaskRenderer::encode(wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frameBindGroup) {
    Impl& im = *impl_;
    if (!im.activeThisFrame) {
        return;
    }
    wgpu::RenderPassColorAttachment attachment{};
    attachment.view = im.target.colorView();
    attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store;
    attachment.clearValue = {1.0, 1.0, 1.0, 0.0};
    wgpu::RenderPassDescriptor desc{};
    desc.label = "shadow-mask-pass";
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &attachment;
    desc.timestampWrites = im.timeline != nullptr
                               ? im.timeline->mark("shadowmask", gpu::FrameTimeline::PassKind::Render)
                               : nullptr;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&desc);
    rp.SetPipeline(im.pipeline);
    rp.SetBindGroup(0, frameBindGroup);
    rp.SetBindGroup(1, im.maskGroup);
    rp.Draw(3);
    rp.End();
    im.passThisFrame = true;
    stats_.maskMs = im.lastMs;
}

void ShadowMaskRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void ShadowMaskRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double ms = im.timeline->msFor("shadowmask");
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
    }
    stats_.maskMs = im.passThisFrame ? im.lastMs : -1.0;
}

const wgpu::TextureView& ShadowMaskRenderer::output() const {
    Impl& im = *impl_;
    return im.activeThisFrame && im.target.valid() ? im.target.colorView() : im.whiteView;
}

const wgpu::TextureView& ShadowMaskRenderer::placeholder() const { return impl_->whiteView; }

bool ShadowMaskRenderer::active() const { return impl_->activeThisFrame; }

} // namespace avgen::rendering
