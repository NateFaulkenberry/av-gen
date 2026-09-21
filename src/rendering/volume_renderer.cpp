#include "rendering/volume_renderer.hpp"

#include "core/vortex.hpp"

#include "rendering/field_uniforms.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::rendering {

namespace {
// Mirrors ParticleRenderer::kMaxGlowSystems / kGlowBufferSize (ADR-040). Kept as plain constants
// rather than an include so the volume renderer does not depend on the particle renderer.
constexpr std::uint32_t kMaxParticleGlowSystems = 8;
constexpr std::uint64_t kParticleGlowBytes = kMaxParticleGlowSystems * 32;
} // namespace

namespace {

// ADR-139: the march target's size at a given resolution scale. 0.5 reproduces the historical
// `(v + 1) / 2` exactly for every v, so the shipped half-resolution march is bit-identical to
// what it was before the scale became a parameter.
std::uint32_t scaledOf(std::uint32_t v, float scale) {
    const float s = std::clamp(scale, 0.05f, 1.0f);
    const auto scaled = static_cast<std::uint32_t>(std::ceil(static_cast<float>(v) * s - 1e-4f));
    return std::max<std::uint32_t>(scaled, 1);
}

} // namespace

struct VolumeRenderer::Impl {
    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> finish(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    Result<void> ensureTarget(std::uint32_t width, std::uint32_t height, float scale);
    void rebuildGroups(const wgpu::TextureView& sceneDepth);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout volumeLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline marchPipeline;
    wgpu::RenderPipeline compositePipeline;
    wgpu::Buffer uniforms;
    wgpu::Buffer fieldBlock;
    wgpu::Buffer particleGlow; // ADR-040: the emissive aggregates of the particle systems
    gpu::RenderTarget half;
    wgpu::BindGroup marchGroup;
    wgpu::BindGroup compositeGroup;
    wgpu::Texture placeholder;      // 1x1 stand-in for binding 4 in the march pass
    wgpu::TextureView placeholderView;
    wgpu::TextureView boundDepth;
    gpu::FrameTimeline* timeline = nullptr;
    double lastMs = -1.0;
    double lastMarchMs = -1.0;
    double lastCompositeMs = -1.0;
    bool passThisFrame = false;
    bool activeThisFrame = false;
    bool initialised = false;
};

VolumeRenderer::VolumeRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

VolumeRenderer::~VolumeRenderer() = default;

bool VolumeRenderer::enabled(const scene::Environment& environment) {
    // ADR-387: the vortex moved out of `Environment` and into the atmospheric effects, so this
    // overload can no longer see it. Kept for the fog-only callers; `enabled(scene)` below is the
    // one that knows about both.
    return environment.volumeDensity > 0.0f;
}

bool VolumeRenderer::enabled(const scene::Scene& scene) {
    return scene.environment.volumeDensity > 0.0f || scene.atmospherics.mediumCount > 0;
}

const gpu::RenderTarget& VolumeRenderer::target() const {
    return impl_->half;
}

Result<void> VolumeRenderer::init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                  const wgpu::BindGroupLayout& frameLayout, wgpu::Buffer fieldBlock,
                                  wgpu::Buffer particleGlow) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.frameLayout = frameLayout;
    im.fieldBlock = std::move(fieldBlock);
    if (!im.fieldBlock) {
        wgpu::BufferDescriptor desc{};
        desc.label = "volume-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        im.fieldBlock = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        im.context.queue().WriteBuffer(im.fieldBlock, 0, &zero, sizeof(zero));
    }
    im.particleGlow = std::move(particleGlow);
    if (!im.particleGlow) {
        wgpu::BufferDescriptor desc{};
        desc.label = "volume-empty-particle-glow";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = kParticleGlowBytes;
        im.particleGlow = device.CreateBuffer(&desc);
        const std::array<float, kParticleGlowBytes / sizeof(float)> zero{};
        im.context.queue().WriteBuffer(im.particleGlow, 0, zero.data(), kParticleGlowBytes);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "volume-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(VolumeUniforms);
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        // Group 1: 1 = VolumeUniforms, 2 = field block, 3 = scene depth, 4 = the half-res march
        // result. Binding 0 stays free (common.wgsl declares `object` there and nothing reads it).
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        entries[0].binding = 1;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(VolumeUniforms);
        entries[1].binding = 2;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = FieldUniforms::kBufferSize;
        entries[2].binding = 3;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].texture.sampleType = wgpu::TextureSampleType::Depth;
        entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[3].binding = 4;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[4].binding = 5; // ADR-040 particle emissive aggregates
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[4].buffer.minBindingSize = kParticleGlowBytes;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "volume-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.volumeLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.volumeLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "volume-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        // The march pass renders into the half-res texture, so it cannot also bind it; it binds
        // this 1x1 placeholder at binding 4 (fs_volume never reads it).
        wgpu::TextureDescriptor desc{};
        desc.label = "volume-placeholder";
        desc.usage = wgpu::TextureUsage::TextureBinding;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = colorFormat;
        im.placeholder = device.CreateTexture(&desc);
        im.placeholderView = im.placeholder.CreateView();
    }
    auto module = im.shaders.load("volume.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipelines(*module); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> VolumeRenderer::reload() {
    auto module = impl_->shaders.load("volume.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return impl_->createPipelines(*module);
}

Result<wgpu::RenderPipeline> VolumeRenderer::Impl::finish(const wgpu::RenderPipelineDescriptor& desc,
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

Result<void> VolumeRenderer::Impl::createPipelines(const wgpu::ShaderModule& module) {
    // The march pass writes the half-res target unblended.
    wgpu::ColorTargetState marchTarget{};
    marchTarget.format = colorFormat;
    marchTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState marchFragment{};
    marchFragment.module = module;
    marchFragment.entryPoint = "fs_volume";
    marchFragment.targetCount = 1;
    marchFragment.targets = &marchTarget;
    wgpu::RenderPipelineDescriptor marchDesc{};
    marchDesc.label = "volume-march";
    marchDesc.layout = pipelineLayout;
    marchDesc.vertex.module = module;
    marchDesc.vertex.entryPoint = "vs_volume";
    marchDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    marchDesc.primitive.cullMode = wgpu::CullMode::None;
    marchDesc.multisample.count = 1;
    marchDesc.multisample.mask = 0xFFFFFFFFu;
    marchDesc.fragment = &marchFragment;
    auto march = finish(marchDesc, "volume-march");
    if (!march) {
        return std::unexpected(march.error());
    }

    // The composite pass adds the in-scattered light and attenuates what is already there:
    // rgb = scatter + hdr * transmittance, alpha untouched.
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::SrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::Zero;
    blend.alpha.dstFactor = wgpu::BlendFactor::One;
    wgpu::ColorTargetState compositeTarget{};
    compositeTarget.format = colorFormat;
    compositeTarget.blend = &blend;
    compositeTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState compositeFragment{};
    compositeFragment.module = module;
    compositeFragment.entryPoint = "fs_composite";
    compositeFragment.targetCount = 1;
    compositeFragment.targets = &compositeTarget;
    wgpu::RenderPipelineDescriptor compositeDesc = marchDesc;
    compositeDesc.label = "volume-composite";
    compositeDesc.fragment = &compositeFragment;
    auto composite = finish(compositeDesc, "volume-composite");
    if (!composite) {
        return std::unexpected(composite.error());
    }
    marchPipeline = *march;
    compositePipeline = *composite;
    return {};
}

Result<void> VolumeRenderer::Impl::ensureTarget(std::uint32_t width, std::uint32_t height, float scale) {
    const std::uint32_t hw = scaledOf(width, scale);
    const std::uint32_t hh = scaledOf(height, scale);
    if (half.valid() && half.width() == hw && half.height() == hh) {
        return {};
    }
    gpu::RenderTargetDesc desc{};
    desc.width = hw;
    desc.height = hh;
    desc.colorFormat = colorFormat;
    desc.depthFormat = wgpu::TextureFormat::Undefined;
    desc.extraColorUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
    desc.label = "volume-half";
    auto made = gpu::RenderTarget::create(context, desc);
    if (!made) {
        return std::unexpected(made.error());
    }
    half = std::move(*made);
    marchGroup = nullptr;
    compositeGroup = nullptr;
    return {};
}

void VolumeRenderer::Impl::rebuildGroups(const wgpu::TextureView& sceneDepth) {
    if (marchGroup && compositeGroup && boundDepth.Get() == sceneDepth.Get()) {
        return;
    }
    const auto& device = context.device();
    auto make = [&](const wgpu::TextureView& volumeView, const char* label) {
        std::array<wgpu::BindGroupEntry, 5> entries{};
        entries[0].binding = 1;
        entries[0].buffer = uniforms;
        entries[0].size = sizeof(VolumeUniforms);
        entries[1].binding = 2;
        entries[1].buffer = fieldBlock;
        entries[1].size = FieldUniforms::kBufferSize;
        entries[2].binding = 3;
        entries[2].textureView = sceneDepth;
        entries[3].binding = 4;
        entries[3].textureView = volumeView;
        entries[4].binding = 5;
        entries[4].buffer = this->particleGlow;
        entries[4].size = kParticleGlowBytes;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = volumeLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    marchGroup = make(placeholderView, "volume-march-group");
    compositeGroup = make(half.colorView(), "volume-composite-group");
    boundDepth = sceneDepth;
}

void VolumeRenderer::update(const scene::Scene& scene, const FrameTime& time, std::uint32_t width,
                            std::uint32_t height, const wgpu::TextureView& sceneDepth, const FieldUniforms* fields,
                            std::uint32_t particleGlowSystems, const QualitySettings& quality) {
    Impl& im = *impl_;
    collectTimings();
    im.activeThisFrame = false;
    im.passThisFrame = false;
    stats_ = VolumeStats{};
    const scene::Environment& env = scene.environment;
    if (!im.initialised || !enabled(scene) || width == 0 || height == 0) {
        return;
    }
    // ADR-139: the tier decides how finely the volume is sampled; the scene decides what the
    // volume *is*. Both axes are clamped to a usable range here rather than trusted.
    const float resolutionScale = std::clamp(quality.volumeResolutionScale, 0.05f, 1.0f);
    if (auto r = im.ensureTarget(width, height, resolutionScale); !r) {
        log::warn("volumetrics disabled this frame: {}", r.error().message);
        return;
    }
    im.rebuildGroups(sceneDepth);

    const float stepScale = std::clamp(quality.volumeStepScale, 0.05f, 4.0f);
    const int steps =
        std::clamp(static_cast<int>(std::lround(static_cast<float>(env.volumeSteps) * stepScale)), 1, 256);
    const int densitySlot = (fields != nullptr && !env.volumeDensityField.empty())
                                ? fields->slotOf(env.volumeDensityField)
                                : -1;
    const int colorSlot =
        (fields != nullptr && !env.volumeColorField.empty()) ? fields->slotOf(env.volumeColorField) : -1;
    VolumeUniforms u{};
    u.params0 = glm::vec4(std::max(env.volumeDensity, 0.0f), env.fogHeight, std::max(env.fogHeightFalloff, 0.0f),
                          std::max(env.volumeScattering, 0.0f));
    u.params1 = glm::vec4(std::max(env.volumeAbsorption, 0.0f), env.volumeAnisotropy,
                          std::max(env.volumeEmission, 0.0f), std::max(env.volumeMaxDistance, 0.01f));
    u.noiseParams = glm::vec4(env.volumeNoiseAmount, env.volumeNoiseScale, env.volumeNoiseSpeed,
                              static_cast<float>(time.renderTime));
    u.info = glm::vec4(static_cast<float>(steps), static_cast<float>(densitySlot), static_cast<float>(colorSlot),
                       static_cast<float>(time.frameNonce() % 4096u));
    u.sizes = glm::vec4(static_cast<float>(im.half.width()), static_cast<float>(im.half.height()),
                        static_cast<float>(width), static_cast<float>(height));
    // ADR-461: `.z` is the march's start jitter, 0..1. Carried in a slot that was already
    // there and zero rather than growing `VolumeUniforms`, which has a `sizeof` assertion on it.
    u.depthParams = glm::vec4(scene.camera.nearPlane, scene.camera.farPlane,
                              std::clamp(scene.environment.volumeJitter, 0.0f, 1.0f), 0.0f);
    u.fogColor = glm::vec4(env.fogColor, 0.0f);
    // ADR-040: the march reads this many entries from the particle glow table.
    const std::uint32_t glowSystems = std::min(particleGlowSystems, kMaxParticleGlowSystems);
    u.glow = glm::vec4(static_cast<float>(glowSystems),
                       std::max(scene.environment.volumeLocalLights, 0.0f), 0.0f, 0.0f);
    // ADR-562: the media, copied straight in. There is no assembly here any more -- each kind's
    // `packMedium` produced these lanes in `buildAtmosphericFrame`, so this site cannot disagree
    // with the field the CPU sampler and the shader share. That was the point: ADR-401 found
    // `packVortex` with one caller because this file kept its own copy of the clamps, ADR-561 found
    // the conversion in the same place, and ADR-562 found a third copy in `engine.cpp` that had
    // fallen seven members behind. Copying opaque lanes is the shape that cannot rot.
    //
    // Slots past `mediumCount` stay zero, and lane 0's `.w` (the radius) at zero is the per-slot
    // gate the march tests, so an unused slot costs one comparison.
    {
        u.mediaInfo = glm::vec4(static_cast<float>(scene.atmospherics.mediumCount), 0.0f, 0.0f, 0.0f);
        for (std::size_t slot = 0; slot < world::kMaxMedia; ++slot) {
            const bool live = slot < scene.atmospherics.mediumCount;
            for (std::size_t lane = 0; lane < world::kMediumLanes; ++lane) {
                u.media[slot * world::kMediumLanes + lane] =
                    live ? scene.atmospherics.media[slot].lane[lane] : glm::vec4(0.0f);
            }
        }
    }
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    im.activeThisFrame = true;
    stats_.steps = static_cast<std::uint32_t>(steps);
    stats_.glowSystems = glowSystems;
    stats_.halfResolution = im.half.width() < width || im.half.height() < height;
    stats_.resolutionScale = resolutionScale;
    stats_.marchWidth = im.half.width();
    stats_.marchHeight = im.half.height();
    stats_.volumeMs = im.lastMs;
}

void VolumeRenderer::encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& color,
                            const wgpu::BindGroup& frameBindGroup) {
    Impl& im = *impl_;
    if (!im.activeThisFrame) {
        return;
    }
    {
        wgpu::RenderPassColorAttachment attachment{};
        attachment.view = im.half.colorView();
        attachment.loadOp = wgpu::LoadOp::Clear;
        attachment.storeOp = wgpu::StoreOp::Store;
        attachment.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor desc{};
        desc.label = "volume-march-pass";
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &attachment;
        // ADR-140: the two passes carry two labels. `FrameTimeline::msForPrefix("volume")` still
        // sums them, so every existing whole-volume reader is unchanged, but the march and the
        // composite can now be read apart -- which is the only way to tell what the resolution
        // scale of ADR-139 actually buys, since it moves one of them and not the other.
        desc.timestampWrites =
            im.timeline != nullptr ? im.timeline->mark("volume.march", gpu::FrameTimeline::PassKind::Render)
                                   : nullptr;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
        pass.SetPipeline(im.marchPipeline);
        pass.SetBindGroup(0, frameBindGroup);
        pass.SetBindGroup(1, im.marchGroup);
        pass.Draw(3);
        pass.End();
    }
    {
        wgpu::RenderPassColorAttachment attachment{};
        attachment.view = color;
        attachment.loadOp = wgpu::LoadOp::Load;
        attachment.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor desc{};
        desc.label = "volume-composite-pass";
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &attachment;
        desc.timestampWrites =
            im.timeline != nullptr ? im.timeline->mark("volume.composite", gpu::FrameTimeline::PassKind::Render)
                                   : nullptr;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
        pass.SetPipeline(im.compositePipeline);
        pass.SetBindGroup(0, frameBindGroup);
        pass.SetBindGroup(1, im.compositeGroup);
        pass.Draw(3);
        pass.End();
    }
    im.passThisFrame = true;
    stats_.volumeMs = im.lastMs;
    stats_.marchMs = im.lastMarchMs;
    stats_.compositeMs = im.lastCompositeMs;
}

void VolumeRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void VolumeRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        // ADR-140. The prefix keeps the whole-volume number exactly what it was; the two labels
        // are read separately beside it.
        const double ms = im.timeline->msForPrefix("volume");
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
        const double march = im.timeline->msFor("volume.march");
        if (march >= 0.0) {
            im.lastMarchMs = march;
        }
        const double composite = im.timeline->msFor("volume.composite");
        if (composite >= 0.0) {
            im.lastCompositeMs = composite;
        }
    }
    stats_.volumeMs = im.passThisFrame ? im.lastMs : -1.0;
    stats_.marchMs = im.passThisFrame ? im.lastMarchMs : -1.0;
    stats_.compositeMs = im.passThisFrame ? im.lastCompositeMs : -1.0;
}

} // namespace avgen::rendering
