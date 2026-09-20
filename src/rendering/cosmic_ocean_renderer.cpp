#include "rendering/cosmic_ocean_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <array>

namespace avgen::rendering {

struct CosmicOceanRenderer::Impl {
    gpu::Context& context;
    gpu::ShaderLibrary& shaders;

    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Depth24Plus;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout oceanLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer uniforms;
    wgpu::BindGroup bindGroup;
    bool live = false;

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    [[nodiscard]] Result<wgpu::RenderPipeline> createPipeline(const wgpu::ShaderModule& module) const {
        // The same additive blend the atmosphere layer uses, on the same two targets. Additive and
        // not `SrcAlpha` because everything this effect produces is *emitted radiance*: a star is a
        // light source and a nebula is glowing gas, and neither occludes what is behind it in any
        // sense a blend factor could express. The flat clear colour the scene starts from is two
        // orders of magnitude below the effect's own deep-space term, so adding rather than
        // replacing changes nothing visible and keeps the composite associative with the comet and
        // the aurora already in the frame.
        wgpu::BlendState additive{};
        additive.color.operation = wgpu::BlendOperation::Add;
        additive.color.srcFactor = wgpu::BlendFactor::One;
        additive.color.dstFactor = wgpu::BlendFactor::One;
        additive.alpha.operation = wgpu::BlendOperation::Add;
        additive.alpha.srcFactor = wgpu::BlendFactor::One;
        additive.alpha.dstFactor = wgpu::BlendFactor::One;

        std::array<wgpu::ColorTargetState, kSceneTargetCount> targets{};
        fillSceneTargets(targets, colorFormat, nullptr);
        for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
            // Targets 1, 2 and 4 are masked off, for the reason `water_renderer.cpp` gives: a
            // normal or a velocity averaged over a transparency is worse than none at all, and the
            // identifier target must keep whatever geometry wrote it so picking still works through
            // the sky.
            targets[i].writeMask =
                (i == 0 || i == 3) ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
        }
        targets[0].blend = &additive;
        targets[3].blend = &additive;

        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_cosmic";
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = targets.data();

        // Tested so the island and the tree occlude the sky behind them -- which is the whole of
        // "the cosmos must be behind the subject", with no horizon to author because the depth
        // buffer already is one. Never written, so the water and the particles drawn after this
        // still composite over it.
        wgpu::DepthStencilState depth{};
        depth.format = depthFormat;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::LessEqual;

        wgpu::RenderPipelineDescriptor desc{};
        desc.label = "cosmic-ocean-pipeline";
        desc.layout = pipelineLayout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_cosmic";
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        wgpu::RenderPipeline p = context.device().CreateRenderPipeline(&desc);
        if (p == nullptr) {
            return fail("cosmic ocean: failed to create the render pipeline");
        }
        return p;
    }
};

CosmicOceanRenderer::CosmicOceanRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}
CosmicOceanRenderer::~CosmicOceanRenderer() = default;

bool CosmicOceanRenderer::ready() const { return impl_->pipeline != nullptr; }

Result<void> CosmicOceanRenderer::init(wgpu::TextureFormat colorFormat,
                                       wgpu::TextureFormat depthFormat,
                                       const wgpu::BindGroupLayout& frameLayout) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.depthFormat = depthFormat;
    im.frameLayout = frameLayout;

    {
        wgpu::BufferDescriptor desc{};
        desc.label = "cosmic-ocean-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(world::CosmicOceanGpu);
        im.uniforms = device.CreateBuffer(&desc);
        // Zeroed at creation, and the zero block is *off*: `cosmicOceanAt` returns on
        // `master.x <= 1e-5` before it reads anything else. So a frame recorded before the first
        // update cannot draw garbage.
        const world::CosmicOceanGpu zero{};
        im.context.queue().WriteBuffer(im.uniforms, 0, &zero, sizeof(zero));
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 1> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(world::CosmicOceanGpu);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.oceanLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.oceanLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = im.uniforms;
        entry.size = sizeof(world::CosmicOceanGpu);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "cosmic-ocean-bind-group";
        desc.layout = im.oceanLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        im.bindGroup = device.CreateBindGroup(&desc);
    }

    auto module = im.shaders.load("cosmic_ocean.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    auto pipeline = im.createPipeline(*module);
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    im.pipeline = *pipeline;
    return {};
}

Result<void> CosmicOceanRenderer::reload() {
    Impl& im = *impl_;
    auto module = im.shaders.load("cosmic_ocean.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    // The old pipeline is kept on failure, so a typo saved mid-session leaves the last good sky on
    // screen with an error in the log rather than a black frame with none.
    auto pipeline = im.createPipeline(*module);
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    im.pipeline = *pipeline;
    return {};
}

void CosmicOceanRenderer::update(const world::CosmicOceanGpu& block, bool live,
                                 std::uint32_t dropped) {
    Impl& im = *impl_;
    stats_.dropped = dropped;
    im.live = live && im.pipeline != nullptr;
    stats_.drawn = im.live;
    if (!im.live) {
        // Deliberately no upload. The `--disable cosmic` arm has to remove the fragment work *and*
        // the uniform content, or it is measuring the same frame through one more branch -- which
        // is the vacuous A/B ADR-182 names and `scene_renderer.cpp` already guards against for the
        // atmospheric layer.
        return;
    }
    im.context.queue().WriteBuffer(im.uniforms, 0, &block, sizeof(block));
}

void CosmicOceanRenderer::draw(wgpu::RenderPassEncoder& pass) {
    Impl& im = *impl_;
    if (!im.live) {
        return;
    }
    pass.SetPipeline(im.pipeline);
    pass.SetBindGroup(1, im.bindGroup);
    pass.Draw(3);
}

} // namespace avgen::rendering
