#include "rendering/composition_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"

#include <chrono>

namespace avgen::rendering {

namespace {

constexpr std::uint64_t kMinVertices = 4096;
constexpr std::uint64_t kMinItems = 64;

// Premultiplied source over destination, per blend mode. The fragment shader always emits
// premultiplied colour, so these are the only four states the pass ever needs.
wgpu::BlendState blendStateFor(comp::BlendMode mode) {
    wgpu::BlendState state{};
    state.alpha.operation = wgpu::BlendOperation::Add;
    state.color.operation = wgpu::BlendOperation::Add;
    switch (mode) {
    case comp::BlendMode::Normal:
        state.color.srcFactor = wgpu::BlendFactor::One;
        state.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        state.alpha.srcFactor = wgpu::BlendFactor::One;
        state.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        break;
    case comp::BlendMode::Additive:
        state.color.srcFactor = wgpu::BlendFactor::One;
        state.color.dstFactor = wgpu::BlendFactor::One;
        state.alpha.srcFactor = wgpu::BlendFactor::One;
        state.alpha.dstFactor = wgpu::BlendFactor::One;
        break;
    case comp::BlendMode::Screen:
        // dst + src - src*dst, which premultiplied is src * 1 + dst * (1 - src).
        state.color.srcFactor = wgpu::BlendFactor::One;
        state.color.dstFactor = wgpu::BlendFactor::OneMinusSrc;
        state.alpha.srcFactor = wgpu::BlendFactor::One;
        state.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        break;
    case comp::BlendMode::Multiply:
        state.color.srcFactor = wgpu::BlendFactor::Dst;
        state.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        state.alpha.srcFactor = wgpu::BlendFactor::One;
        state.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        break;
    }
    return state;
}

std::uint64_t pipelineKey(wgpu::TextureFormat format, comp::BlendMode blend) {
    return (static_cast<std::uint64_t>(format) << 8) | static_cast<std::uint64_t>(blend);
}

} // namespace

CompositionRenderer::CompositionRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders) {}

CompositionRenderer::~CompositionRenderer() = default;

Result<void> CompositionRenderer::init() {
    auto module = shaders_.load("composite.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    module_ = *module;

    std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.minBindingSize = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].buffer.minBindingSize = kItemStride;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Fragment;
    entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Fragment;
    entries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
    {
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "composite-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        layout_ = context_.device().CreateBindGroupLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "composite-pipeline-layout";
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &layout_;
        pipelineLayout_ = context_.device().CreatePipelineLayout(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "composite-uniforms";
        desc.size = sizeof(Uniforms);
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = context_.device().CreateBuffer(&desc);
    }
    {
        // The glyph atlas is one texture for every font and every size in the composition.
        wgpu::TextureDescriptor desc{};
        desc.label = "composite-glyph-atlas";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {comp::GlyphAtlas::kSize, comp::GlyphAtlas::kSize, 1};
        desc.format = wgpu::TextureFormat::R8Unorm;
        atlas_ = context_.device().CreateTexture(&desc);
        if (atlas_ == nullptr) {
            return fail("cannot create the {}x{} glyph atlas", comp::GlyphAtlas::kSize, comp::GlyphAtlas::kSize);
        }
        atlasView_ = atlas_.CreateView();
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "composite-sampler";
        // Clamped, because a glyph that wrapped would sample its neighbour across the atlas.
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        sampler_ = context_.device().CreateSampler(&desc);
    }
    ensureVertices();
    ensureItems(static_cast<std::uint32_t>(kMinItems));
    rebuildBindGroup();
    initialised_ = true;
    return {};
}

Result<void> CompositionRenderer::reload() {
    auto module = shaders_.load("composite.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    module_ = *module;
    pipelines_.clear();
    return {};
}

Result<wgpu::RenderPipeline> CompositionRenderer::pipelineFor(wgpu::TextureFormat format, comp::BlendMode blend) {
    const std::uint64_t key = pipelineKey(format, blend);
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) {
        return it->second;
    }
    std::array<wgpu::VertexAttribute, 3> attributes{};
    attributes[0].format = wgpu::VertexFormat::Float32x2;
    attributes[0].offset = offsetof(comp::LayerVertex, local);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(comp::LayerVertex, uv);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Uint32;
    attributes[2].offset = offsetof(comp::LayerVertex, item);
    attributes[2].shaderLocation = 2;
    wgpu::VertexBufferLayout buffer{};
    buffer.arrayStride = sizeof(comp::LayerVertex);
    buffer.stepMode = wgpu::VertexStepMode::Vertex;
    buffer.attributeCount = attributes.size();
    buffer.attributes = attributes.data();

    const wgpu::BlendState blendState = blendStateFor(blend);
    wgpu::ColorTargetState target{};
    target.format = format;
    target.blend = &blendState;
    target.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module_;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "composite-pipeline";
    desc.layout = pipelineLayout_;
    desc.vertex.module = module_;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &buffer;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1; // Apple TBDR: no MSAA anywhere. Text edges come from the field.
    desc.fragment = &fragment;
    wgpu::RenderPipeline pipeline = context_.device().CreateRenderPipeline(&desc);
    if (pipeline == nullptr) {
        return fail("cannot create the composition pipeline");
    }
    pipelines_[key] = pipeline;
    return pipeline;
}

void CompositionRenderer::ensureVertices() {
    const std::uint64_t needed =
        stack_ == nullptr ? kMinVertices : std::max<std::uint64_t>(kMinVertices, stack_->vertices().size());
    if (vertices_ != nullptr && vertexCapacity_ >= needed) {
        return;
    }
    std::uint64_t capacity = std::max<std::uint64_t>(kMinVertices, vertexCapacity_ * 2);
    while (capacity < needed) {
        capacity *= 2;
    }
    wgpu::BufferDescriptor desc{};
    desc.label = "composite-vertices";
    desc.size = capacity * sizeof(comp::LayerVertex);
    desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertices_ = context_.device().CreateBuffer(&desc);
    vertexCapacity_ = capacity;
    uploadedVertexVersion_ = 0; // force a re-upload into the new buffer
}

void CompositionRenderer::ensureItems(std::uint32_t count) {
    const std::uint64_t needed = std::max<std::uint64_t>(kMinItems, count);
    if (items_ != nullptr && itemCapacity_ >= needed) {
        return;
    }
    std::uint64_t capacity = std::max<std::uint64_t>(kMinItems, itemCapacity_ * 2);
    while (capacity < needed) {
        capacity *= 2;
    }
    wgpu::BufferDescriptor desc{};
    desc.label = "composite-items";
    desc.size = capacity * kItemStride;
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    items_ = context_.device().CreateBuffer(&desc);
    itemCapacity_ = capacity;
    bindGroup_ = nullptr;
}

void CompositionRenderer::rebuildBindGroup() {
    if (items_ == nullptr) {
        return;
    }
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms_;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].buffer = items_;
    entries[1].size = itemCapacity_ * kItemStride;
    entries[2].binding = 2;
    entries[2].textureView = atlasView_;
    entries[3].binding = 3;
    entries[3].sampler = sampler_;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "composite-bind-group";
    desc.layout = layout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    bindGroup_ = context_.device().CreateBindGroup(&desc);
}

void CompositionRenderer::ensureAtlas() {
    if (stack_ == nullptr) {
        return;
    }
    const comp::GlyphAtlas& atlas = stack_->atlas();
    if (atlas.version() == uploadedAtlasVersion_) {
        return;
    }
    // Only the rows the shelf packer has reached. A fresh composition with one word uploads a few
    // kilobytes, not the whole four megabytes.
    const std::uint32_t rows = std::min(atlas.height(), std::max(1u, atlas.usedRows() + 1));
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = atlas_;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = atlas.width();
    layout.rowsPerImage = rows;
    const wgpu::Extent3D extent{atlas.width(), rows, 1};
    context_.queue().WriteTexture(&destination, atlas.texels().data(),
                                  static_cast<std::size_t>(atlas.width()) * rows, &layout, &extent);
    uploadedAtlasVersion_ = atlas.version();
}

void CompositionRenderer::encodeOverlay(wgpu::CommandEncoder& encoder, const gpu::TargetView& target) {
    stats_.layers = 0;
    stats_.draws = 0;
    stats_.items = 0;
    stats_.vertices = 0;
    // Ask, if somebody set a provider. A render loop then cannot forget to push.
    if (provider_) {
        const Input input = provider_();
        stack_ = input.stack;
        seconds_ = input.seconds;
    }
    if (!initialised_ || stack_ == nullptr || target.view == nullptr || target.width == 0 || target.height == 0) {
        return;
    }
    const auto cpuStart = std::chrono::steady_clock::now();
    const comp::Frame frame{target.width, target.height};
    const std::uint64_t versionBefore = stack_->vertexVersion();
    const comp::CompositionFrame& built = stack_->build(frame, seconds_);
    if (stack_->vertexVersion() != versionBefore) {
        ++stats_.geometryRebuilds;
    }
    stats_.cpuBuildMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
    stats_.glyphs = static_cast<std::uint32_t>(stack_->atlas().glyphCount());
    stats_.atlasRows = stack_->atlas().usedRows();
    stats_.layers = built.drawnLayers;
    stats_.items = static_cast<std::uint32_t>(built.items.size());
    stats_.vertices = static_cast<std::uint32_t>(stack_->vertices().size());
    if (built.draws.empty()) {
        return;
    }

    ensureAtlas();
    ensureVertices();
    ensureItems(static_cast<std::uint32_t>(built.items.size()));
    if (bindGroup_ == nullptr) {
        rebuildBindGroup();
    }
    if (stack_->vertexVersion() != uploadedVertexVersion_ && !stack_->vertices().empty()) {
        context_.queue().WriteBuffer(vertices_, 0, stack_->vertices().data(),
                                     stack_->vertices().size() * sizeof(comp::LayerVertex));
        uploadedVertexVersion_ = stack_->vertexVersion();
    }
    context_.queue().WriteBuffer(items_, 0, built.items.data(), built.items.size() * kItemStride);
    const Uniforms uniforms{{static_cast<float>(target.width), static_cast<float>(target.height)},
                            {static_cast<float>(comp::GlyphAtlas::kSize), static_cast<float>(comp::GlyphAtlas::kSize)}};
    context_.queue().WriteBuffer(uniforms_, 0, &uniforms, sizeof(uniforms));

    wgpu::RenderPassColorAttachment colour{};
    colour.view = target.view;
    // Load, never clear: the tone map has already written the picture this draws over.
    colour.loadOp = wgpu::LoadOp::Load;
    colour.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass{};
    pass.label = "composition-pass";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &colour;
    if (timeline_ != nullptr) {
        pass.timestampWrites = timeline_->mark("composition", gpu::FrameTimeline::PassKind::Render);
    }
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetBindGroup(0, bindGroup_);
    rp.SetVertexBuffer(0, vertices_);
    comp::BlendMode current = comp::BlendMode::Normal;
    bool havePipeline = false;
    for (const comp::CompositionDraw& draw : built.draws) {
        if (!havePipeline || draw.blend != current) {
            auto pipeline = pipelineFor(target.format, draw.blend);
            if (!pipeline) {
                log::error("composition: {}", pipeline.error().message);
                break;
            }
            rp.SetPipeline(*pipeline);
            current = draw.blend;
            havePipeline = true;
        }
        rp.Draw(draw.vertexCount, 1, draw.firstVertex, 0);
        ++stats_.draws;
    }
    rp.End();
}

void CompositionRenderer::collectTimings() {
    stats_.gpuMs = timeline_ == nullptr ? -1.0 : timeline_->msFor("composition");
}

} // namespace avgen::rendering
