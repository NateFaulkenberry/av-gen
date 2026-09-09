#include "rendering/output_mapper.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <array>
#include <cstring>

namespace avgen::rendering {

OutputMapper::OutputMapper(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}

Result<void> OutputMapper::init() {
    if (initialised_) {
        return {};
    }
    auto module = shaders_.load("output_map.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    module_ = *module;
    const auto& device = context_.device();
    {
        std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[2].buffer.hasDynamicOffset = true;
        entries[2].buffer.minBindingSize = sizeof(OutputMapUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "output-map-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pl{};
        pl.label = "output-map-pipeline-layout";
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &layout_;
        pipelineLayout_ = device.CreatePipelineLayout(&pl);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "output-map-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        sampler_ = device.CreateSampler(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "output-map-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxDrawsInFlight) * kSlotStride;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = device.CreateBuffer(&desc);
    }
    pipelines_.clear();
    initialised_ = true;
    return {};
}

Result<wgpu::RenderPipeline> OutputMapper::pipelineFor(wgpu::TextureFormat format, bool blit) {
    const std::uint32_t key = (static_cast<std::uint32_t>(format) << 1) | (blit ? 1u : 0u);
    if (auto it = pipelines_.find(key); it != pipelines_.end()) {
        return it->second;
    }
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module_;
    fragment.entryPoint = blit ? "fs_blit" : "fs_map";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    const char* label = blit ? "output-blit-pipeline" : "output-map-pipeline";
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = label;
    desc.layout = pipelineLayout_;
    desc.vertex.module = module_;
    desc.vertex.entryPoint = "vs_main";
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
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    pipelines_[key] = pipeline;
    return pipeline;
}

OutputMapUniforms OutputMapper::uniformsFor(const OutputMapping& mapping) {
    OutputMapUniforms u{};
    const glm::mat3 inv = inverseHomography(mapping.corners);
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            u.inv[col][row] = inv[col][row];
        }
        u.inv[col][3] = 0.0f;
    }
    u.crop[0] = mapping.crop.x;
    u.crop[1] = mapping.crop.y;
    u.crop[2] = mapping.crop.w;
    u.crop[3] = mapping.crop.h;
    u.blend[0] = mapping.blend.left;
    u.blend[1] = mapping.blend.right;
    u.blend[2] = mapping.blend.top;
    u.blend[3] = mapping.blend.bottom;
    u.params[0] = mapping.blendGamma > 0.0f ? mapping.blendGamma : 1.0f;
    u.params[1] = mapping.brightness;
    u.params[2] = mapping.gamma > 0.0f ? 1.0f / mapping.gamma : 1.0f;
    u.params[3] = static_cast<float>((mapping.flipX ? 1u : 0u) | (mapping.flipY ? 2u : 0u));
    return u;
}

Result<void> OutputMapper::draw(wgpu::CommandEncoder& encoder, const wgpu::TextureView& source,
                                const wgpu::TextureView& target, std::uint32_t targetWidth,
                                std::uint32_t targetHeight, const OutputMapping& mapping,
                                wgpu::TextureFormat targetFormat) {
    if (!initialised_) {
        return fail("OutputMapper::draw before init");
    }
    if (!source || !target || targetWidth == 0 || targetHeight == 0) {
        return fail("OutputMapper::draw: missing views or empty target");
    }
    const bool blit = fastPath_ && mapping.isIdentity();
    auto pipeline = pipelineFor(targetFormat, blit);
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }

    const std::uint32_t slot = slot_;
    slot_ = (slot_ + 1) % kMaxDrawsInFlight;
    const OutputMapUniforms uniforms = uniformsFor(mapping);
    context_.queue().WriteBuffer(uniforms_, static_cast<std::uint64_t>(slot) * kSlotStride, &uniforms,
                                 sizeof(uniforms));

    std::array<wgpu::BindGroupEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].textureView = source;
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    entries[2].binding = 2;
    entries[2].buffer = uniforms_;
    entries[2].size = sizeof(OutputMapUniforms);
    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.label = "output-map-bind-group";
    bgDesc.layout = layout_;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    wgpu::BindGroup bindGroup = context_.device().CreateBindGroup(&bgDesc);

    wgpu::RenderPassColorAttachment color{};
    color.view = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.0, 0.0, 0.0, 1.0};
    wgpu::RenderPassDescriptor pass{};
    pass.label = blit ? "output-blit" : "output-map";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetViewport(0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0.0f, 1.0f);
    rp.SetPipeline(*pipeline);
    const std::uint32_t offset = slot * kSlotStride;
    rp.SetBindGroup(0, bindGroup, 1, &offset);
    rp.Draw(3);
    rp.End();
    ++draws_;
    return {};
}

} // namespace avgen::rendering
