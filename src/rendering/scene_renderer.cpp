#include "rendering/scene_renderer.hpp"

#include "rendering/environment.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "gpu/texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

namespace {

// Hash of the material inputs that select a bind group (textures + sampler settings).
std::uint64_t materialKey(const scene::Material& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    };
    for (const auto* ref : {&m.baseColorTexture, &m.metallicRoughnessTexture, &m.normalTexture, &m.emissiveTexture,
                            &m.occlusionTexture}) {
        mix(ref->texture);
    }
    mix(static_cast<std::uint64_t>(m.baseColorTexture.wrapU) | (static_cast<std::uint64_t>(m.baseColorTexture.wrapV) << 4) |
        (m.baseColorTexture.linearFilter ? 1ull << 8 : 0ull));
    return h;
}

} // namespace

SceneRenderer::SceneRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders), timer_(std::make_unique<gpu::GpuTimer>(context)),
      samplers_(std::make_unique<gpu::SamplerCache>(context)),
      environment_(std::make_unique<EnvironmentProcessor>(context, shaders)),
      shaderStack_(std::make_unique<ShaderStack>(context, shaders)),
      particles_(std::make_unique<ParticleRenderer>(context, shaders)),
      postProcessor_(std::make_unique<PostProcessor>(context, shaders)), pool_(std::make_unique<gpu::TransientPool>(context)) {
    objectStaging_.resize(static_cast<std::size_t>(kMaxObjects) * kObjectStride);
}

SceneRenderer::~SceneRenderer() = default;

Result<void> SceneRenderer::init() {
    const auto& device = context_.device();

    // ---- bind group layouts ----
    auto uniformLayout = [&](const char* label, std::uint64_t minSize, bool dynamic) {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = dynamic;
        entry.buffer.minBindingSize = minSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = label;
        desc.entryCount = 1;
        desc.entries = &entry;
        return device.CreateBindGroupLayout(&desc);
    };
    frameLayout_ = uniformLayout("frame-layout", sizeof(FrameUniforms), false);
    objectLayout_ = uniformLayout("object-layout", sizeof(ObjectUniforms), true);
    {
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
        for (std::uint32_t i = 1; i < 6; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "material-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        materialLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
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
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "ibl-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        iblLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = sizeof(TonemapUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "tonemap-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        tonemapLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout_, objectLayout_, materialLayout_, iblLayout_};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "scene-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        scenePipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "tonemap-pipeline-layout";
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &tonemapLayout_;
        tonemapPipelineLayout_ = device.CreatePipelineLayout(&desc);
    }

    // ---- uniform buffers and bind groups ----
    {
        wgpu::BufferDescriptor desc{};
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.label = "frame-uniforms";
        desc.size = sizeof(FrameUniforms);
        frameUniforms_ = device.CreateBuffer(&desc);
        desc.label = "object-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxObjects) * kObjectStride;
        objectUniforms_ = device.CreateBuffer(&desc);
        desc.label = "tonemap-uniforms";
        desc.size = sizeof(TonemapUniforms);
        tonemapUniforms_ = device.CreateBuffer(&desc);
    }
    auto bufferGroup = [&](const char* label, const wgpu::BindGroupLayout& layout, const wgpu::Buffer& buffer,
                           std::uint64_t size) {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = size;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = layout;
        desc.entryCount = 1;
        desc.entries = &entry;
        return device.CreateBindGroup(&desc);
    };
    frameBindGroup_ = bufferGroup("frame-bind-group", frameLayout_, frameUniforms_, sizeof(FrameUniforms));
    objectBindGroup_ = bufferGroup("object-bind-group", objectLayout_, objectUniforms_, sizeof(ObjectUniforms));

    // ---- default textures ----
    whiteSrgb_ = gpu::solidTexture(context_, 255, 255, 255, 255, true, "default-white-srgb");
    whiteLinear_ = gpu::solidTexture(context_, 255, 255, 255, 255, false, "default-white");
    flatNormal_ = gpu::solidTexture(context_, 128, 128, 255, 255, false, "default-normal");
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "default-black-cube";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 6};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        blackCube_.texture = device.CreateTexture(&desc);
        blackCube_.format = desc.format;
        blackCube_.width = blackCube_.height = 1;
        wgpu::TextureViewDescriptor viewDesc{};
        viewDesc.label = "default-black-cube";
        viewDesc.dimension = wgpu::TextureViewDimension::Cube;
        viewDesc.arrayLayerCount = 6;
        blackCubeView_ = blackCube_.texture.CreateView(&viewDesc);
        blackLut_ = gpu::solidTexture(context_, 0, 0, 0, 255, false, "default-lut");
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "ibl-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        iblSampler_ = device.CreateSampler(&desc);
    }
    rebuildIblBindGroup();

    if (auto r = createPipelines(); !r) {
        return r;
    }
    if (auto r = environment_->init(); !r) {
        return r;
    }
    if (auto r = particles_->init(); !r) {
        return r;
    }
    if (auto r = postProcessor_->init(); !r) {
        return r;
    }
    if (context_.errorCount() > 0) {
        return fail("renderer initialisation raised {} GPU error(s): {}", context_.errorCount(),
                    context_.lastError());
    }
    initialised_ = true;
    return {};
}

void SceneRenderer::updateEnvironment(const scene::Scene& scene) {
    const scene::TextureId id = scene.environment.environmentMap;
    const bool valid = id != scene::kInvalidTexture && id < scene.textures.size() && scene.textures[id].isHdr();
    if (!valid) {
        if (ibl_.valid) {
            setIbl(IblResources{});
        }
        environmentTexture_ = scene::kInvalidTexture;
        return;
    }
    if (id == environmentTexture_ && scene.textureVersion == environmentVersion_) {
        return;
    }
    auto ibl = environment_->process(scene.textures[id]);
    if (!ibl) {
        log::error("environment: {}", ibl.error().message);
        setIbl(IblResources{});
    } else {
        setIbl(*ibl);
    }
    environmentTexture_ = id;
    environmentVersion_ = scene.textureVersion;
}

Result<void> SceneRenderer::createPipelines() {
    auto pbr = shaders_.load("pbr.wgsl");
    if (!pbr) return std::unexpected(pbr.error());
    auto grid = shaders_.load("grid.wgsl");
    if (!grid) return std::unexpected(grid.error());
    auto sky = shaders_.load("skybox.wgsl");
    if (!sky) return std::unexpected(sky.error());
    auto tonemap = shaders_.load("tonemap.wgsl");
    if (!tonemap) return std::unexpected(tonemap.error());
    tonemapModule_ = *tonemap;

    auto a = createLitPipeline(*pbr, LitVariant::OpaqueCull);
    if (!a) return std::unexpected(a.error());
    litOpaqueCull_ = *a;
    auto b = createLitPipeline(*pbr, LitVariant::OpaqueNoCull);
    if (!b) return std::unexpected(b.error());
    litOpaqueNoCull_ = *b;
    auto c = createLitPipeline(*pbr, LitVariant::Blend);
    if (!c) return std::unexpected(c.error());
    litBlend_ = *c;
    auto g = createGridPipeline(*grid);
    if (!g) return std::unexpected(g.error());
    gridPipeline_ = *g;
    auto s = createSkyboxPipeline(*sky);
    if (!s) return std::unexpected(s.error());
    skyboxPipeline_ = *s;
    return {};
}

Result<wgpu::RenderPipeline> SceneRenderer::finishPipeline(const wgpu::RenderPipelineDescriptor& desc,
                                                          const char* label) {
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
    return pipeline;
}

namespace {
struct VertexLayoutStorage {
    std::array<wgpu::VertexAttribute, 3> attributes{};
    wgpu::VertexBufferLayout layout{};
    VertexLayoutStorage() {
        attributes[0].format = wgpu::VertexFormat::Float32x3;
        attributes[0].offset = offsetof(scene::Vertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x3;
        attributes[1].offset = offsetof(scene::Vertex, normal);
        attributes[1].shaderLocation = 1;
        attributes[2].format = wgpu::VertexFormat::Float32x2;
        attributes[2].offset = offsetof(scene::Vertex, uv);
        attributes[2].shaderLocation = 2;
        layout.arrayStride = sizeof(scene::Vertex);
        layout.stepMode = wgpu::VertexStepMode::Vertex;
        layout.attributeCount = attributes.size();
        layout.attributes = attributes.data();
    }
};
static_assert(sizeof(scene::Vertex) == 32);
} // namespace

Result<wgpu::RenderPipeline> SceneRenderer::createLitPipeline(const wgpu::ShaderModule& module, LitVariant variant) {
    VertexLayoutStorage vertex;
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.blend = variant == LitVariant::Blend ? &blend : nullptr;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = variant == LitVariant::Blend ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor desc{};
    const char* label = variant == LitVariant::OpaqueCull ? "pbr-opaque" : variant == LitVariant::OpaqueNoCull ? "pbr-opaque-twosided" : "pbr-blend";
    desc.label = label;
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = variant == LitVariant::OpaqueCull ? wgpu::CullMode::Back : wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, label);
}

Result<wgpu::RenderPipeline> SceneRenderer::createGridPipeline(const wgpu::ShaderModule& module) {
    VertexLayoutStorage vertex;
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::One;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::Zero;
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "grid-pipeline";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "grid-pipeline");
}

Result<wgpu::RenderPipeline> SceneRenderer::createSkyboxPipeline(const wgpu::ShaderModule& module) {
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_sky";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "skybox-pipeline";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_sky";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "skybox-pipeline");
}

Result<wgpu::RenderPipeline> SceneRenderer::tonemapPipelineFor(wgpu::TextureFormat format) {
    const auto key = static_cast<std::uint32_t>(format);
    if (auto it = tonemapPipelines_.find(key); it != tonemapPipelines_.end()) {
        return it->second;
    }
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = tonemapModule_;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "tonemap-pipeline";
    desc.layout = tonemapPipelineLayout_;
    desc.vertex.module = tonemapModule_;
    desc.vertex.entryPoint = "vs_main";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    auto pipeline = finishPipeline(desc, "tonemap-pipeline");
    if (pipeline) {
        tonemapPipelines_[key] = *pipeline;
    }
    return pipeline;
}

Result<void> SceneRenderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return fail("resize to zero size ({}x{})", width, height);
    }
    if (hdr_.valid() && hdr_.width() == width && hdr_.height() == height) {
        return {};
    }
    gpu::RenderTargetDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.colorFormat = kHdrFormat;
    desc.depthFormat = kDepthFormat;
    desc.label = "hdr-target";
    auto target = gpu::RenderTarget::create(context_, desc);
    if (!target) {
        return std::unexpected(target.error());
    }
    hdr_ = std::move(*target);
    tonemapBindGroup_ = nullptr;
    tonemapBoundView_ = nullptr;
    tonemapGroups_.clear();
    stats_.width = width;
    stats_.height = height;
    log::debug("HDR target resized to {}x{}", width, height);
    return {};
}

void SceneRenderer::ensureTonemapBindGroup() {
    if (tonemapBindGroup_ && tonemapBoundView_.Get() == hdr_.colorView().Get()) {
        return;
    }
    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].textureView = hdr_.colorView();
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    wgpu::BindGroupDescriptor desc{};
    desc.label = "tonemap-bind-group";
    desc.layout = tonemapLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    tonemapBindGroup_ = context_.device().CreateBindGroup(&desc);
    tonemapBoundView_ = hdr_.colorView();
}

void SceneRenderer::setIbl(const IblResources& ibl) {
    ibl_ = ibl;
    rebuildIblBindGroup();
}

void SceneRenderer::rebuildIblBindGroup() {
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].sampler = iblSampler_;
    entries[1].binding = 1;
    entries[1].textureView = ibl_.valid ? ibl_.irradiance : blackCubeView_;
    entries[2].binding = 2;
    entries[2].textureView = ibl_.valid ? ibl_.prefiltered : blackCubeView_;
    entries[3].binding = 3;
    entries[3].textureView = ibl_.valid ? ibl_.brdfLut : blackLut_.view;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "ibl-bind-group";
    desc.layout = iblLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    iblBindGroup_ = context_.device().CreateBindGroup(&desc);
}

Result<void> SceneRenderer::reloadEngineShaders() {
    Result<void> first{};
    auto keep = [&](const char* what, Result<void> r) {
        if (!r && first) {
            first = std::unexpected(Error{std::string(what) + ": " + r.error().message});
        }
    };
    if (auto pbr = shaders_.load("pbr.wgsl")) {
        auto a = createLitPipeline(*pbr, LitVariant::OpaqueCull);
        auto b = createLitPipeline(*pbr, LitVariant::OpaqueNoCull);
        auto c = createLitPipeline(*pbr, LitVariant::Blend);
        if (a && b && c) {
            litOpaqueCull_ = *a;
            litOpaqueNoCull_ = *b;
            litBlend_ = *c;
        } else {
            keep("pbr.wgsl", std::unexpected((!a ? a : !b ? b : c).error()));
        }
    } else {
        keep("pbr.wgsl", std::unexpected(pbr.error()));
    }
    if (auto grid = shaders_.load("grid.wgsl")) {
        if (auto g = createGridPipeline(*grid)) {
            gridPipeline_ = *g;
        } else {
            keep("grid.wgsl", std::unexpected(g.error()));
        }
    } else {
        keep("grid.wgsl", std::unexpected(grid.error()));
    }
    if (auto sky = shaders_.load("skybox.wgsl")) {
        if (auto sp = createSkyboxPipeline(*sky)) {
            skyboxPipeline_ = *sp;
        } else {
            keep("skybox.wgsl", std::unexpected(sp.error()));
        }
    } else {
        keep("skybox.wgsl", std::unexpected(sky.error()));
    }
    if (auto tonemap = shaders_.load("tonemap.wgsl")) {
        tonemapModule_ = *tonemap;
        tonemapPipelines_.clear();
    } else {
        keep("tonemap.wgsl", std::unexpected(tonemap.error()));
    }
    if (auto r = particles_->reload(); !r) {
        keep("particles.wgsl", r);
    }
    if (auto r = postProcessor_->reload(); !r) {
        keep("post.wgsl", r);
    }
    ++engineReloads_;
    if (first) {
        log::info("engine shaders reloaded");
    } else {
        log::error("engine shader reload kept previous pipelines: {}", first.error().message);
    }
    return first;
}

void SceneRenderer::updateSpectrum(const analysis::AnalysisFrame* frame) {
    if (frame == nullptr || frame->spectrum.empty()) {
        return;
    }
    const std::size_t bins = frame->spectrum.size();
    if (!spectrum_.valid() || spectrumBins_ != bins) {
        wgpu::TextureDescriptor desc{};
        desc.label = "audio-spectrum";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {static_cast<std::uint32_t>(bins), 1, 1};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        spectrum_.texture = context_.device().CreateTexture(&desc);
        spectrum_.view = spectrum_.texture.CreateView();
        spectrum_.width = static_cast<std::uint32_t>(bins);
        spectrum_.height = 1;
        spectrum_.format = desc.format;
        spectrumBins_ = bins;
        spectrumStaging_.resize(bins * 4);
    }
    for (std::size_t i = 0; i < bins; ++i) {
        spectrumStaging_[i * 4 + 0] = gpu::floatToHalf(frame->spectrum[i]);
        spectrumStaging_[i * 4 + 1] = gpu::floatToHalf(i < frame->magnitude.size() ? frame->magnitude[i] : 0.0f);
        spectrumStaging_[i * 4 + 2] = gpu::floatToHalf(0.0f);
        spectrumStaging_[i * 4 + 3] = gpu::floatToHalf(1.0f);
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = spectrum_.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<std::uint32_t>(bins * 8);
    layout.rowsPerImage = 1;
    wgpu::Extent3D extent{static_cast<std::uint32_t>(bins), 1, 1};
    context_.queue().WriteTexture(&dst, spectrumStaging_.data(), spectrumStaging_.size() * 2, &layout, &extent);
}

Result<void> SceneRenderer::ensurePostTargets(std::uint32_t width, std::uint32_t height) {
    for (auto& t : post_) {
        if (t.valid() && t.width() == width && t.height() == height) {
            continue;
        }
        gpu::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.colorFormat = kHdrFormat;
        desc.depthFormat = wgpu::TextureFormat::Undefined;
        desc.label = "post-target";
        auto made = gpu::RenderTarget::create(context_, desc);
        if (!made) {
            return std::unexpected(made.error());
        }
        t = std::move(*made);
    }
    return {};
}

wgpu::BindGroup SceneRenderer::tonemapBindGroupFor(const wgpu::TextureView& view) {
    auto it = tonemapGroups_.find(view.Get());
    if (it != tonemapGroups_.end()) {
        return it->second;
    }
    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    wgpu::BindGroupDescriptor desc{};
    desc.label = "tonemap-bind-group";
    desc.layout = tonemapLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    wgpu::BindGroup group = context_.device().CreateBindGroup(&desc);
    tonemapGroups_[view.Get()] = group;
    return group;
}

void SceneRenderer::uploadMeshes(const scene::Scene& scene) {
    if (scene.meshVersion == meshVersion_ && meshes_.size() == scene.meshes.size()) {
        return;
    }
    meshes_.clear();
    meshes_.reserve(scene.meshes.size());
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const auto& mesh = scene.meshes[i];
        GpuMesh gpuMesh;
        if (!mesh.valid()) {
            log::warn("mesh {} is invalid and will not be drawn", i);
            meshes_.push_back(std::move(gpuMesh));
            continue;
        }
        wgpu::BufferDescriptor vdesc{};
        vdesc.label = "mesh-vertices";
        vdesc.size = mesh.vertices.size() * sizeof(scene::Vertex);
        vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        gpuMesh.vertices = context_.device().CreateBuffer(&vdesc);
        context_.queue().WriteBuffer(gpuMesh.vertices, 0, mesh.vertices.data(), vdesc.size);
        wgpu::BufferDescriptor idesc{};
        idesc.label = "mesh-indices";
        idesc.size = mesh.indices.size() * sizeof(std::uint32_t);
        idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        gpuMesh.indices = context_.device().CreateBuffer(&idesc);
        context_.queue().WriteBuffer(gpuMesh.indices, 0, mesh.indices.data(), idesc.size);
        gpuMesh.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        meshes_.push_back(std::move(gpuMesh));
    }
    meshVersion_ = scene.meshVersion;
}

void SceneRenderer::uploadTextures(const scene::Scene& scene) {
    if (scene.textureVersion == textureVersion_ && textures_.size() == scene.textures.size()) {
        return;
    }
    textures_.clear();
    textures_.reserve(scene.textures.size());
    for (std::size_t i = 0; i < scene.textures.size(); ++i) {
        // Environment maps are consumed by the EnvironmentProcessor, not bound as material textures.
        if (scene.textures[i].isHdr()) {
            textures_.push_back(gpu::GpuTexture{});
            continue;
        }
        auto tex = gpu::uploadTexture(context_, scene.textures[i], true);
        if (!tex) {
            log::warn("texture {} not uploaded: {}", i, tex.error().message);
            textures_.push_back(gpu::GpuTexture{});
            continue;
        }
        textures_.push_back(std::move(*tex));
    }
    materialBindGroups_.clear();
    textureVersion_ = scene.textureVersion;
    stats_.textures = static_cast<std::uint32_t>(textures_.size());
}

const gpu::GpuTexture& SceneRenderer::textureOrDefault(const scene::TextureRef& ref,
                                                        const gpu::GpuTexture& fallback) const {
    if (ref.valid() && ref.texture < textures_.size() && textures_[ref.texture].valid()) {
        return textures_[ref.texture];
    }
    return fallback;
}

const wgpu::BindGroup& SceneRenderer::materialBindGroup(const scene::Material& material) {
    const std::uint64_t key = materialKey(material);
    if (auto it = materialBindGroups_.find(key); it != materialBindGroups_.end()) {
        return it->second;
    }
    std::array<wgpu::BindGroupEntry, 6> entries{};
    entries[0].binding = 0;
    entries[0].sampler = samplers_->get(material.baseColorTexture);
    entries[1].binding = 1;
    entries[1].textureView = textureOrDefault(material.baseColorTexture, whiteSrgb_).view;
    entries[2].binding = 2;
    entries[2].textureView = textureOrDefault(material.metallicRoughnessTexture, whiteLinear_).view;
    entries[3].binding = 3;
    entries[3].textureView = textureOrDefault(material.normalTexture, flatNormal_).view;
    entries[4].binding = 4;
    entries[4].textureView = textureOrDefault(material.emissiveTexture, whiteSrgb_).view;
    entries[5].binding = 5;
    entries[5].textureView = textureOrDefault(material.occlusionTexture, whiteLinear_).view;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "material-bind-group";
    desc.layout = materialLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return materialBindGroups_.emplace(key, context_.device().CreateBindGroup(&desc)).first->second;
}

Result<void> SceneRenderer::render(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                                   const gpu::TargetView& target, const ShaderFrameInputs* shaderInputs) {
    if (!initialised_) {
        return fail("renderer not initialised");
    }
    if (!hdr_.valid() || target.width != hdr_.width() || target.height != hdr_.height()) {
        if (auto r = resize(target.width, target.height); !r) {
            return r;
        }
    }
    uploadMeshes(scene);
    uploadTextures(scene);
    updateEnvironment(scene);
    ensureTonemapBindGroup();

    // ---- user shader layers: sync GPU objects, spectrum texture, background intermediate passes ----
    const shaders::ShaderLayerSet* layerSet = shaderInputs ? shaderInputs->layers : nullptr;
    ShaderFrameContext shaderCtx;
    shaderCtx.width = hdr_.width();
    shaderCtx.height = hdr_.height();
    shaderCtx.frameIndex = time.frameIndex;
    if (layerSet != nullptr) {
        shaderStack_->sync(*layerSet);
        updateSpectrum(shaderInputs->frame);
        shaderCtx.audioSpectrum = spectrum_.view;
        for (const auto& layer : layerSet->layers()) {
            if (layer->enabled && layer->stage == shaders::LayerStage::Background) {
                if (auto* gpuLayer = shaderStack_->find(layer->id)) {
                    gpuLayer->renderPasses(encoder, *layer, shaderCtx);
                }
            }
        }
    }

    const auto& queue = context_.queue();
    const float aspect = static_cast<float>(hdr_.width()) / static_cast<float>(hdr_.height());
    const glm::mat4 view = scene.camera.view();
    const glm::mat4 proj = scene.camera.projection(aspect);

    // ---- frame uniforms ----
    FrameUniforms frame{};
    frame.viewProj = proj * view;
    frame.invViewProj = glm::inverse(frame.viewProj);
    frame.cameraPos = glm::vec4(scene.camera.position, 1.0f);
    const bool ibl = ibl_.valid && scene.environment.environmentMap != scene::kInvalidTexture;
    frame.params = glm::vec4(static_cast<float>(time.renderTime), scene.environment.gridIntensity,
                             scene.environment.brightness, scene.environment.environmentIntensity);
    std::uint32_t lightCount = 0;
    for (const auto& light : scene.lights) {
        if (!light.enabled || lightCount >= kMaxLights) {
            continue;
        }
        LightUniform& u = frame.lights[lightCount++];
        u.positionType = glm::vec4(light.position, static_cast<float>(light.type));
        const glm::vec3 dir = glm::length(light.direction) > 1e-6f ? glm::normalize(light.direction) : glm::vec3(0, -1, 0);
        u.directionRange = glm::vec4(dir, light.range);
        u.colorIntensity = glm::vec4(light.color * light.intensity, 1.0f);
        const float cosOuter = std::cos(light.outerConeAngle);
        const float cosInner = std::cos(light.innerConeAngle);
        u.cone = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 0.0f);
    }
    frame.envParams = glm::vec4(scene.environment.environmentRotation,
                                static_cast<float>(ibl ? ibl_.prefilteredMips - 1 : 0), static_cast<float>(lightCount),
                                ibl ? 1.0f : 0.0f);
    frame.skyParams = glm::vec4(scene.environment.backgroundColor, scene.environment.skyboxBlur);
    queue.WriteBuffer(frameUniforms_, 0, &frame, sizeof(frame));

    // ---- object uniforms (one 256-byte slot per visible entity) ----
    struct DrawItem {
        std::uint32_t offset;
        const scene::Entity* entity;
        float viewDepth;
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> grid;
    std::vector<DrawItem> blended;
    std::uint32_t objectIndex = 0;
    for (const auto& entity : scene.entities) {
        if (!entity.visible || entity.mesh >= meshes_.size() || meshes_[entity.mesh].indexCount == 0) {
            continue;
        }
        if (objectIndex >= kMaxObjects) {
            log::warn("more than {} visible entities; extra entities skipped", kMaxObjects);
            break;
        }
        const auto& m = entity.material;
        ObjectUniforms obj{};
        obj.model = entity.transform.matrix();
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        obj.baseColor = glm::vec4(m.baseColor, m.opacity);
        obj.emissive = glm::vec4(m.emissiveColor, m.emissiveIntensity);
        obj.material = glm::vec4(m.roughness, m.metallic, m.normalScale, m.occlusionStrength);
        std::uint32_t mask = 0;
        auto has = [&](const scene::TextureRef& ref) {
            return ref.valid() && ref.texture < textures_.size() && textures_[ref.texture].valid();
        };
        if (has(m.baseColorTexture)) mask |= 1;
        if (has(m.metallicRoughnessTexture)) mask |= 2;
        if (has(m.normalTexture)) mask |= 4;
        if (has(m.emissiveTexture)) mask |= 8;
        if (has(m.occlusionTexture)) mask |= 16;
        obj.flags = glm::vec4(static_cast<float>(m.alphaMode), m.alphaCutoff, m.unlit ? 1.0f : 0.0f,
                              static_cast<float>(mask));
        const std::uint32_t offset = objectIndex * kObjectStride;
        std::memcpy(objectStaging_.data() + offset, &obj, sizeof(obj));
        const float depth = -(view * glm::vec4(entity.transform.position, 1.0f)).z;
        DrawItem item{offset, &entity, depth};
        if (entity.style == scene::MeshStyle::Grid) {
            grid.push_back(item);
        } else if (m.alphaMode == scene::AlphaMode::Blend) {
            blended.push_back(item);
        } else {
            opaque.push_back(item);
        }
        ++objectIndex;
    }
    if (objectIndex > 0) {
        queue.WriteBuffer(objectUniforms_, 0, objectStaging_.data(),
                          static_cast<std::size_t>(objectIndex) * kObjectStride);
    }
    std::stable_sort(blended.begin(), blended.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.viewDepth > b.viewDepth; });

    TonemapUniforms tonemap{};
    tonemap.exposure = scene.environment.brightness;
    tonemap.operatorId = static_cast<float>(scene.post.tonemap);
    tonemap.vignette = scene.post.vignette;
    tonemap.grain = scene.post.grain;
    tonemap.size[0] = static_cast<float>(hdr_.width());
    tonemap.size[1] = static_cast<float>(hdr_.height());
    tonemap.seed = static_cast<float>(time.frameIndex % 1024);
    queue.WriteBuffer(tonemapUniforms_, 0, &tonemap, sizeof(tonemap));

    stats_.drawCalls = 0;
    stats_.triangles = 0;
    stats_.entities = objectIndex;
    stats_.lights = lightCount;
    stats_.ibl = ibl;

    // ---- particle simulation (compute) ----
    particles_->update(encoder, scene, time, view, proj);
    stats_.particles = particles_->stats();

    // ---- pass 1: scene -> HDR ----
    {
        wgpu::RenderPassColorAttachment color{};
        color.view = hdr_.colorView();
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        const auto& bg = scene.environment.backgroundColor;
        color.clearValue = {static_cast<double>(bg.r), static_cast<double>(bg.g), static_cast<double>(bg.b), 1.0};
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = hdr_.depthView();
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "scene-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &color;
        pass.depthStencilAttachment = &depth;
        pass.timestampWrites = timer_->beginWrites();

        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        // Background user shaders first (fullscreen, no depth write), then the scene on top.
        if (layerSet != nullptr) {
            for (const auto& layer : layerSet->layers()) {
                if (layer->enabled && layer->stage == shaders::LayerStage::Background) {
                    if (auto* gpuLayer = shaderStack_->find(layer->id)) {
                        gpuLayer->drawOutput(rp, kHdrFormat, true, *layer, shaderCtx);
                        ++stats_.drawCalls;
                    }
                }
            }
        }
        rp.SetBindGroup(0, frameBindGroup_);
        rp.SetBindGroup(3, iblBindGroup_);
        auto drawItems = [&](const std::vector<DrawItem>& items, bool lit) {
            for (const auto& item : items) {
                const GpuMesh& mesh = meshes_[item.entity->mesh];
                if (lit) {
                    const auto& m = item.entity->material;
                    rp.SetPipeline(m.alphaMode == scene::AlphaMode::Blend ? litBlend_
                                   : m.doubleSided                        ? litOpaqueNoCull_
                                                                          : litOpaqueCull_);
                    rp.SetBindGroup(2, materialBindGroup(m));
                } else {
                    rp.SetPipeline(gridPipeline_);
                    rp.SetBindGroup(2, materialBindGroup(item.entity->material));
                }
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                rp.SetVertexBuffer(0, mesh.vertices);
                rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
                rp.DrawIndexed(mesh.indexCount);
                ++stats_.drawCalls;
                stats_.triangles += mesh.indexCount / 3;
            }
        };
        drawItems(opaque, true);
        if (scene.environment.showSkybox && ibl) {
            rp.SetPipeline(skyboxPipeline_);
            const std::uint32_t zeroOffset = 0; // layout requires group 1; the skybox ignores it
            rp.SetBindGroup(1, objectBindGroup_, 1, &zeroOffset);
            rp.SetBindGroup(2, materialBindGroup(scene::Material{}));
            rp.Draw(3);
            ++stats_.drawCalls;
            ++stats_.triangles;
        }
        drawItems(grid, false);
        particles_->draw(rp, scene);
        stats_.drawCalls += particles_->stats().systems;
        drawItems(blended, true);
        rp.End();
    }

    // ---- post layers: HDR -> ping-pong HDR ----
    wgpu::TextureView finalHdr = hdr_.colorView();
    if (layerSet != nullptr) {
        int ping = 0;
        for (const auto& layer : layerSet->layers()) {
            if (!layer->enabled || layer->stage != shaders::LayerStage::Post) {
                continue;
            }
            auto* gpuLayer = shaderStack_->find(layer->id);
            if (gpuLayer == nullptr) {
                continue;
            }
            if (auto r = ensurePostTargets(hdr_.width(), hdr_.height()); !r) {
                return r;
            }
            ShaderFrameContext postCtx = shaderCtx;
            postCtx.inputImage = finalHdr;
            gpuLayer->renderPasses(encoder, *layer, postCtx);
            wgpu::RenderPassColorAttachment color{};
            color.view = post_[ping].colorView();
            color.loadOp = wgpu::LoadOp::Clear;
            color.storeOp = wgpu::StoreOp::Store;
            wgpu::RenderPassDescriptor pass{};
            pass.label = "post-layer-pass";
            pass.colorAttachmentCount = 1;
            pass.colorAttachments = &color;
            wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
            gpuLayer->drawOutput(rp, kHdrFormat, false, *layer, postCtx);
            rp.End();
            ++stats_.drawCalls;
            finalHdr = post_[ping].colorView();
            ping = 1 - ping;
        }
    }

    // ---- built-in post chain: DoF, motion blur, bloom, grading ----
    {
        PostFrameInputs postIn;
        postIn.sceneHdr = finalHdr;
        postIn.depth = hdr_.depthView();
        postIn.width = hdr_.width();
        postIn.height = hdr_.height();
        postIn.prevViewProj = havePrevViewProj_ ? prevViewProj_ : frame.viewProj;
        postIn.invViewProj = frame.invViewProj;
        postIn.cameraPos = scene.camera.position;
        postIn.frameIndex = time.frameIndex;
        postIn.settings = &scene.post;
        finalHdr = postProcessor_->run(encoder, postIn, *pool_);
        stats_.post = postProcessor_->stats();
        stats_.transientTextures = static_cast<std::uint32_t>(pool_->size());
    }
    prevViewProj_ = frame.viewProj;
    havePrevViewProj_ = true;

    // ---- pass 2: tonemap -> target ----
    {
        auto pipeline = tonemapPipelineFor(target.format);
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        wgpu::RenderPassColorAttachment color{};
        color.view = target.view;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "tonemap-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &color;
        pass.timestampWrites = timer_->endWrites();
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(*pipeline);
        rp.SetBindGroup(0, finalHdr.Get() == hdr_.colorView().Get() ? tonemapBindGroup_ : tonemapBindGroupFor(finalHdr));
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
        ++stats_.triangles;
    }
    timer_->resolve(encoder);
    pool_->endFrame();
    return {};
}

Result<gpu::Image8> SceneRenderer::renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                 std::uint32_t width, std::uint32_t height,
                                                 const ShaderFrameInputs* shaderInputs) {
    wgpu::TextureDescriptor desc{};
    desc.label = "render-to-image";
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {width, height, 1};
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::Texture texture = context_.device().CreateTexture(&desc);
    if (!texture) {
        return fail("cannot create {}x{} readback texture", width, height);
    }
    gpu::TargetView target{texture.CreateView(), wgpu::TextureFormat::RGBA8Unorm, width, height};
    if (auto r = resize(width, height); !r) {
        return std::unexpected(r.error());
    }
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    if (auto r = render(encoder, scene, time, target, shaderInputs); !r) {
        return std::unexpected(r.error());
    }
    wgpu::CommandBuffer commands = encoder.Finish();
    context_.queue().Submit(1, &commands);
    stats_.gpuFrameMs = timer_->collect();
    context_.waitForQueue();
    stats_.gpuFrameMs = timer_->collect();
    return gpu::readTexture8(context_, texture, width, height, false);
}

} // namespace avgen::rendering
