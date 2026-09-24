#include "rendering/ribbon_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace avgen::rendering {

namespace {

struct RibbonVertexLayout {
    std::array<wgpu::VertexAttribute, 4> attributes{};
    wgpu::VertexBufferLayout layout{};
    RibbonVertexLayout() {
        const std::array<std::size_t, 4> offsets = {
            offsetof(world::RibbonVertex, positionSide), offsetof(world::RibbonVertex, tangentWidth),
            offsetof(world::RibbonVertex, color), offsetof(world::RibbonVertex, profile)};
        for (std::uint32_t i = 0; i < attributes.size(); ++i) {
            attributes[i].format = wgpu::VertexFormat::Float32x4;
            attributes[i].offset = offsets[i];
            attributes[i].shaderLocation = i;
        }
        layout.arrayStride = sizeof(world::RibbonVertex);
        layout.stepMode = wgpu::VertexStepMode::Vertex;
        layout.attributeCount = attributes.size();
        layout.attributes = attributes.data();
    }
};

constexpr std::uint64_t kArenaBytes =
    static_cast<std::uint64_t>(world::kRibbonVertexBudget) * sizeof(world::RibbonVertex);

} // namespace

Result<void> RibbonRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                  wgpu::TextureFormat depthFormat, const wgpu::BindGroupLayout& frameLayout) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    wgpu::PipelineLayoutDescriptor desc{};
    desc.label = "ribbon-pipeline-layout";
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &frameLayout;
    layout_ = context.device().CreatePipelineLayout(&desc);
    return createPipelines(shaders);
}

Result<void> RibbonRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipelines(shaders);
}

Result<void> RibbonRenderer::createPipelines(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("ribbon.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const RibbonVertexLayout vertex;
    const wgpu::Device& device = context_->device();

    const auto make = [&](bool additive) -> Result<wgpu::RenderPipeline> {
        // Colour: added light, or a covering layer.
        wgpu::BlendState color{};
        color.color.operation = wgpu::BlendOperation::Add;
        color.color.srcFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::SrcAlpha;
        color.color.dstFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
        color.alpha.operation = wgpu::BlendOperation::Add;
        color.alpha.srcFactor = wgpu::BlendFactor::One;
        color.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        // Velocity: the strip's own motion mixed in by its coverage, so a faint tail barely moves
        // the motion vectors of what is behind it.
        wgpu::BlendState velocity{};
        velocity.color.operation = wgpu::BlendOperation::Add;
        velocity.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
        velocity.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        velocity.alpha.operation = wgpu::BlendOperation::Add;
        velocity.alpha.srcFactor = wgpu::BlendFactor::One;
        velocity.alpha.dstFactor = wgpu::BlendFactor::Zero;
        // Emission: the same rule as the colour for rgb; the bloom weight in alpha is the larger of
        // what was there and what the strip brings, so a strip never erases a glow behind it.
        wgpu::BlendState emission = color;
        emission.alpha.operation = wgpu::BlendOperation::Max;
        emission.alpha.srcFactor = wgpu::BlendFactor::One;
        emission.alpha.dstFactor = wgpu::BlendFactor::One;

        // All five targets of the scene pass (ADR-035), in its order. The normal and identifier
        // targets stay the opaque geometry's: a normal or an id averaged over a transparency is
        // worse than none (water's rule, ADR-099).
        const auto formats = sceneTargetFormats(hdrFormat_);
        std::array<wgpu::ColorTargetState, kSceneTargetCount> targets{};
        for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
            targets[i] = wgpu::ColorTargetState{};
            targets[i].format = formats[i];
            targets[i].writeMask = (i == 1 || i == 4) ? wgpu::ColorWriteMask::None : wgpu::ColorWriteMask::All;
        }
        targets[0].blend = &color;
        targets[2].blend = &velocity;
        targets[3].blend = &emission;

        wgpu::FragmentState fragment{};
        fragment.module = *module;
        fragment.entryPoint = additive ? "fs_ribbon_additive" : "fs_ribbon_alpha";
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = targets.data();

        wgpu::DepthStencilState depth{};
        depth.format = depthFormat_;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::Less;

        wgpu::RenderPipelineDescriptor desc{};
        desc.label = additive ? "ribbon-additive" : "ribbon-alpha";
        desc.layout = layout_;
        desc.vertex.module = *module;
        desc.vertex.entryPoint = "vs_ribbon";
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers = &vertex.layout;
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
        // A camera-facing strip has no back: which way it winds depends on which way it was flown.
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;

        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = std::string(msg.data, msg.length);
                }
            });
        context_->instance().WaitAny(future, UINT64_MAX);
        if (!error.empty() || !pipeline) {
            return fail("ribbon pipeline ({}): {}", additive ? "additive" : "alpha", error);
        }
        return pipeline;
    };
    auto additive = make(true);
    if (!additive) {
        return std::unexpected(additive.error());
    }
    auto alpha = make(false);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    additive_ = *additive;
    alpha_ = *alpha;
    return {};
}

void RibbonRenderer::draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup,
                          const world::RibbonFrame& frame) {
    stats_ = RibbonStats{};
    // The gate: no strip, nothing -- not a bind, not an upload.
    if (frame.strips.empty() || frame.vertices.empty() || !ready()) {
        return;
    }
    const std::uint32_t count =
        static_cast<std::uint32_t>(std::min<std::size_t>(frame.vertices.size(), world::kRibbonVertexBudget));
    if (!arena_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "ribbon-arena";
        desc.size = kArenaBytes;
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        arena_ = context_->device().CreateBuffer(&desc);
    }
    // Queued ahead of the submit this pass belongs to, so the draw below reads this frame's strips.
    context_->queue().WriteBuffer(arena_, 0, frame.vertices.data(),
                                  static_cast<std::size_t>(count) * sizeof(world::RibbonVertex));
    pass.SetBindGroup(0, frameGroup);
    pass.SetVertexBuffer(0, arena_, 0, static_cast<std::uint64_t>(count) * sizeof(world::RibbonVertex));
    const wgpu::RenderPipeline* bound = nullptr;
    for (const world::RibbonStrip& strip : frame.strips) {
        if (strip.vertexCount < 4 || strip.firstVertex + strip.vertexCount > count) {
            continue;
        }
        const wgpu::RenderPipeline* want = strip.blend == world::RibbonBlend::Alpha ? &alpha_ : &additive_;
        if (want != bound) {
            pass.SetPipeline(*want);
            bound = want;
        }
        pass.Draw(strip.vertexCount, 1, strip.firstVertex, 0);
        ++stats_.draws;
        ++stats_.strips;
    }
    stats_.vertices = count;
}

} // namespace avgen::rendering
