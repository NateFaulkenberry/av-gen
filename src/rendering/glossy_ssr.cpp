#include "rendering/glossy_ssr.hpp"

#include "gpu/resource_stats.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::rendering {

namespace {
// The parameters' block, as shaders/glossy_ssr.wgsl declares `SsrParams`.
struct SsrParamsBlock {
    float a[4];
    float b[4];
};
constexpr wgpu::TextureFormat kPyramidFormat = wgpu::TextureFormat::R32Float;
} // namespace

GlossySsr::GlossySsr(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}

Result<void> GlossySsr::init(const wgpu::BindGroupLayout& frameLayout, const wgpu::BindGroupLayout& objectLayout,
                             const wgpu::BindGroupLayout& materialLayout) {
    const auto& device = context_.device();
    {
        // The IBL group's six entries exactly as SceneRenderer's `iblLayout_` declares them, then the pyramid and the
        // parameters. A layout of its own, so the scene layout stays as it was.
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[1].texture.viewDimension = wgpu::TextureViewDimension::Cube;
        entries[2] = entries[1];
        entries[2].binding = 2;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[4] = entries[3];
        entries[4].binding = 4;
        entries[5].binding = 5;
        entries[5].visibility = wgpu::ShaderStage::Fragment;
        entries[5].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[6].binding = 6;
        entries[6].visibility = wgpu::ShaderStage::Fragment;
        entries[6].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[6].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Fragment;
        entries[7].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[7].buffer.minBindingSize = sizeof(SsrParamsBlock);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "glossy-ssr-group-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        groupLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout, objectLayout, materialLayout, groupLayout_};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "glossy-ssr-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        pipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Fragment;
        entry.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "hiz-reduce-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        reduceLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "glossy-ssr-params";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(SsrParamsBlock);
        params_ = device.CreateBuffer(&desc);
        writeParams();
    }
    auto module = shaders_.load("hiz_reduce.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "hiz-reduce-pipeline-layout";
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &reduceLayout_;
    wgpu::ColorTargetState target{};
    target.format = kPyramidFormat;
    target.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_hiz";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "hiz-reduce";
    desc.layout = device.CreatePipelineLayout(&layoutDesc);
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs_hiz";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    reducePipeline_ = gpu::createRenderPipeline(device, &desc);
    if (!reducePipeline_) {
        return fail("glossy SSR: the Hi-Z reduce pipeline failed");
    }
    return {};
}

void GlossySsr::writeParams() {
    SsrParamsBlock block{};
    block.a[0] = settings_.reach;
    block.a[1] = settings_.roughnessFadeEnd;
    block.a[2] = settings_.roughnessFadeStart;
    block.a[3] = static_cast<float>(settings_.iterations);
    block.b[0] = static_cast<float>(levels_);
    block.b[1] = settings_.thicknessPerMetre;
    block.b[2] = settings_.thicknessFloor;
    context_.device().GetQueue().WriteBuffer(params_, 0, &block, sizeof(block));
}

Result<void> GlossySsr::ensurePyramid(std::uint32_t width, std::uint32_t height) {
    if (pyramid_ && width == width_ && height == height_) {
        return {};
    }
    width_ = width;
    height_ = height;
    // Every level halves exactly: the first is the depth's size rounded up to a multiple of 2^levels, halved, so a
    // texel of level k covers exactly 2^k x 2^k pixels (the march's cells are powers of two).
    const std::uint32_t levels = std::max(settings_.maxLevels, 1u);
    const std::uint32_t block = 1u << levels;
    const std::uint32_t w1 = (width + block - 1) / block * (block / 2);
    const std::uint32_t h1 = (height + block - 1) / block * (block / 2);
    levels_ = levels;
    wgpu::TextureDescriptor desc{};
    desc.label = "glossy-ssr-hiz";
    desc.size = {w1, h1, 1};
    desc.format = kPyramidFormat;
    desc.mipLevelCount = levels;
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    pyramid_ = context_.device().CreateTexture(&desc);
    pyramidView_ = pyramid_.CreateView();
    mipViews_.clear();
    for (std::uint32_t m = 0; m < levels; ++m) {
        wgpu::TextureViewDescriptor view{};
        view.baseMipLevel = m;
        view.mipLevelCount = 1;
        view.dimension = wgpu::TextureViewDimension::e2D;
        mipViews_.push_back(pyramid_.CreateView(&view));
    }
    reduceGroups_.assign(levels, wgpu::BindGroup{});
    reduceSource_ = nullptr;
    group_ = nullptr;
    writeParams();
    return {};
}

void GlossySsr::encodePyramid(wgpu::CommandEncoder& encoder, const wgpu::TextureView& linearDepth,
                              std::uint32_t width, std::uint32_t height, const Mark& mark) {
    if (!reducePipeline_ || !ensurePyramid(width, height)) {
        return;
    }
    for (std::uint32_t m = 0; m < levels_; ++m) {
        const wgpu::TextureView& source = m == 0 ? linearDepth : mipViews_[m - 1];
        if (!reduceGroups_[m] || (m == 0 && reduceSource_.Get() != linearDepth.Get())) {
            wgpu::BindGroupEntry entry{};
            entry.binding = 0;
            entry.textureView = source;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "hiz-reduce-group";
            desc.layout = reduceLayout_;
            desc.entryCount = 1;
            desc.entries = &entry;
            reduceGroups_[m] = context_.device().CreateBindGroup(&desc);
            if (m == 0) {
                reduceSource_ = linearDepth;
            }
        }
        wgpu::RenderPassColorAttachment colour{};
        colour.view = mipViews_[m];
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {1.0e7, 0.0, 0.0, 0.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "hiz-reduce-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        pass.timestampWrites = mark ? mark() : nullptr;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(reducePipeline_);
        rp.SetBindGroup(0, reduceGroups_[m]);
        rp.Draw(3);
        rp.End();
    }
}

const wgpu::BindGroup& GlossySsr::bindGroup(const std::array<wgpu::BindGroupEntry, 6>& ibl) {
    bool same = group_ && groupPyramid_ == pyramidView_.Get() && groupSampler0_ == ibl[0].sampler.Get() &&
                groupSampler5_ == ibl[5].sampler.Get();
    for (std::size_t i = 1; same && i < 5; ++i) {
        same = groupIbl_[i] == ibl[i].textureView.Get();
    }
    if (same) {
        return group_;
    }
    std::array<wgpu::BindGroupEntry, 8> entries{};
    for (std::size_t i = 0; i < 6; ++i) {
        entries[i] = ibl[i];
        groupIbl_[i] = ibl[i].textureView.Get();
    }
    groupSampler0_ = ibl[0].sampler.Get();
    groupSampler5_ = ibl[5].sampler.Get();
    entries[6].binding = 6;
    entries[6].textureView = pyramidView_;
    entries[7].binding = 7;
    entries[7].buffer = params_;
    entries[7].size = sizeof(SsrParamsBlock);
    groupPyramid_ = pyramidView_.Get();
    wgpu::BindGroupDescriptor desc{};
    desc.label = "glossy-ssr-group";
    desc.layout = groupLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    group_ = context_.device().CreateBindGroup(&desc);
    return group_;
}

} // namespace avgen::rendering
