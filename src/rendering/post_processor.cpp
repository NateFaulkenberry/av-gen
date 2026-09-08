#include "rendering/post_processor.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>

namespace avgen::rendering {

PostProcessor::PostProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}

Result<void> PostProcessor::init() {
    const auto& device = context_.device();
    {
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(Uniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[3] = entries[2];
        entries[3].binding = 3;
        entries[4].binding = 4;
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].texture.sampleType = wgpu::TextureSampleType::Depth;
        entries[4].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "post-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pl{};
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &layout_;
        pipelineLayout_ = device.CreatePipelineLayout(&pl);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "post-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        sampler_ = device.CreateSampler(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "post-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxSlots) * kSlotStride;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = device.CreateBuffer(&desc);
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "post-black";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = kHdrFormat;
        black_ = device.CreateTexture(&desc);
        blackView_ = black_.CreateView();
        wgpu::TextureDescriptor ddesc{};
        ddesc.label = "post-depth-placeholder";
        ddesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
        ddesc.dimension = wgpu::TextureDimension::e2D;
        ddesc.size = {1, 1, 1};
        ddesc.format = wgpu::TextureFormat::Depth24Plus;
        depthPlaceholder_ = device.CreateTexture(&ddesc);
        depthPlaceholderView_ = depthPlaceholder_.CreateView();
    }
    auto module = shaders_.load("post.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = createPipelines(*module); !r) {
        return r;
    }
    initialised_ = true;
    return {};
}

Result<void> PostProcessor::reload() {
    auto module = shaders_.load("post.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return createPipelines(*module);
}

Result<wgpu::RenderPipeline> PostProcessor::makePipeline(const wgpu::ShaderModule& module, const char* entry) {
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = entry;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = entry;
    desc.layout = pipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    context_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = context_.device().CreateRenderPipeline(&desc);
    std::string error;
    auto future = context_.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context_.waitFor(future);
    if (!error.empty() || !pipeline) {
        return fail("post pipeline '{}' failed: {}", entry, error);
    }
    return pipeline;
}

Result<void> PostProcessor::createPipelines(const wgpu::ShaderModule& module) {
    auto a = makePipeline(module, "fs_prefilter");
    if (!a) return std::unexpected(a.error());
    auto b = makePipeline(module, "fs_downsample");
    if (!b) return std::unexpected(b.error());
    auto c = makePipeline(module, "fs_upsample");
    if (!c) return std::unexpected(c.error());
    auto d = makePipeline(module, "fs_composite");
    if (!d) return std::unexpected(d.error());
    auto e = makePipeline(module, "fs_dof");
    if (!e) return std::unexpected(e.error());
    auto f = makePipeline(module, "fs_motion_blur");
    if (!f) return std::unexpected(f.error());
    prefilter_ = *a;
    downsample_ = *b;
    upsample_ = *c;
    composite_ = *d;
    dof_ = *e;
    motionBlur_ = *f;
    return {};
}

void PostProcessor::runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline,
                            const wgpu::TextureView& target, const wgpu::TextureView& source,
                            const wgpu::TextureView& second, const wgpu::TextureView& depth, const Uniforms& uniforms) {
    const std::uint32_t offset = (slot_ % kMaxSlots) * kSlotStride;
    ++slot_;
    context_.queue().WriteBuffer(uniforms_, offset, &uniforms, sizeof(uniforms));
    std::array<wgpu::BindGroupEntry, 5> entries{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms_;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    entries[2].binding = 2;
    entries[2].textureView = source ? source : blackView_;
    entries[3].binding = 3;
    entries[3].textureView = second ? second : blackView_;
    entries[4].binding = 4;
    entries[4].textureView = depth ? depth : depthPlaceholderView_;
    wgpu::BindGroupDescriptor bdesc{};
    bdesc.label = "post-bind-group";
    bdesc.layout = layout_;
    bdesc.entryCount = entries.size();
    bdesc.entries = entries.data();
    wgpu::BindGroup group = context_.device().CreateBindGroup(&bdesc);

    wgpu::RenderPassColorAttachment color{};
    color.view = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass{};
    pass.label = "post-pass";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(pipeline);
    rp.SetBindGroup(0, group, 1, &offset);
    rp.Draw(3);
    rp.End();
    ++stats_.passes;
}

wgpu::TextureView PostProcessor::run(wgpu::CommandEncoder& encoder, const PostFrameInputs& in, gpu::TransientPool& pool) {
    stats_ = PostStats{};
    slot_ = 0;
    if (!initialised_ || in.settings == nullptr || in.width == 0 || in.height == 0) {
        return in.sceneHdr;
    }
    const auto& s = *in.settings;
    Uniforms base{};
    base.outputSize = glm::vec2(static_cast<float>(in.width), static_cast<float>(in.height));
    base.texelSize = 1.0f / base.outputSize;
    base.cameraPos = glm::vec4(in.cameraPos, 1.0f);
    base.prevViewProj = in.prevViewProj;
    base.invViewProj = in.invViewProj;
    base.lift = glm::vec4(s.lift, 0.0f);
    base.gamma = glm::vec4(s.gamma, 1.0f);
    base.gain = glm::vec4(s.gain, 1.0f);

    wgpu::TextureView current = in.sceneHdr;
    const float pixelScale = static_cast<float>(in.height) / 720.0f;

    // ---- depth of field ----
    if (s.dofEnabled && s.dofMaxRadius > 0.0f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.focusDistance, s.focusRange, s.dofMaxRadius * pixelScale, 0.0f);
        runPass(encoder, dof_, target.view, current, nullptr, in.depth, u);
        current = target.view;
    }
    // ---- motion blur ----
    if (s.motionBlurAmount > 0.0f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.motionBlurAmount, static_cast<float>(std::clamp<std::uint32_t>(s.motionBlurSamples, 2, 32)), 0.0f, 0.0f);
        runPass(encoder, motionBlur_, target.view, current, nullptr, in.depth, u);
        current = target.view;
    }
    // ---- bloom ----
    wgpu::TextureView bloom;
    const bool bloomOn = s.bloomEnabled && s.bloomIntensity > 0.0f;
    if (bloomOn) {
        const std::uint32_t levels = std::clamp<std::uint32_t>(s.bloomLevels, 1, 8);
        std::vector<gpu::TransientTexture> down;
        std::uint32_t w = std::max(1u, in.width / 2);
        std::uint32_t h = std::max(1u, in.height / 2);
        for (std::uint32_t level = 0; level < levels && w >= 2 && h >= 2; ++level) {
            auto target = pool.acquire(w, h, kHdrFormat, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "bloom-down");
            Uniforms u = base;
            if (level == 0) {
                u.texelSize = 1.0f / base.outputSize;
                u.params0 = glm::vec4(s.bloomThreshold, s.bloomKnee, 0.0f, 0.0f);
                runPass(encoder, prefilter_, target.view, current, nullptr, nullptr, u);
            } else {
                u.texelSize = 1.0f / glm::vec2(static_cast<float>(down.back().width), static_cast<float>(down.back().height));
                runPass(encoder, downsample_, target.view, down.back().view, nullptr, nullptr, u);
            }
            down.push_back(target);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        stats_.bloomLevels = static_cast<std::uint32_t>(down.size());
        // Upsample chain: coarse -> fine, each level = tent(previous) + down[level].
        wgpu::TextureView acc = down.back().view;
        for (int level = static_cast<int>(down.size()) - 2; level >= 0; --level) {
            const auto& d = down[static_cast<std::size_t>(level)];
            auto target = pool.acquire(d.width, d.height, kHdrFormat, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding, "bloom-up");
            Uniforms u = base;
            const auto& coarse = down[static_cast<std::size_t>(level) + 1];
            u.texelSize = 1.0f / glm::vec2(static_cast<float>(coarse.width), static_cast<float>(coarse.height));
            u.params0 = glm::vec4(s.bloomRadius, 0.0f, 0.0f, 0.0f);
            runPass(encoder, upsample_, target.view, acc, d.view, nullptr, u);
            acc = target.view;
        }
        bloom = acc;
    }
    // ---- composite (always: applies grading and lens even without bloom) ----
    {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.bloomIntensity, s.chromaticAberration, s.distortion, 0.0f);
        u.params1 = glm::vec4(s.contrast, s.saturation, s.temperature, s.tint);
        u.params2 = glm::vec4(s.hueShift, bloomOn ? 1.0f : 0.0f, 0.0f, 0.0f);
        runPass(encoder, composite_, target.view, current, bloom, nullptr, u);
        current = target.view;
    }
    return current;
}

} // namespace avgen::rendering
