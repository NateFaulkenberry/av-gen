#include "rendering/scene_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <array>
#include <cstring>

namespace avgen::rendering {

SceneRenderer::SceneRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders), timer_(std::make_unique<gpu::GpuTimer>(context)) {
    objectStaging_.resize(static_cast<std::size_t>(kMaxObjects) * kObjectStride);
}

SceneRenderer::~SceneRenderer() = default;

Result<void> SceneRenderer::init() {
    const auto& device = context_.device();

    // ---- bind group layouts ----
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.minBindingSize = sizeof(FrameUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "frame-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        frameLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(ObjectUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "object-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        objectLayout_ = device.CreateBindGroupLayout(&desc);
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
        std::array<wgpu::BindGroupLayout, 2> layouts = {frameLayout_, objectLayout_};
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

    // ---- uniform buffers ----
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "frame-uniforms";
        desc.size = sizeof(FrameUniforms);
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        frameUniforms_ = device.CreateBuffer(&desc);
        desc.label = "object-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxObjects) * kObjectStride;
        objectUniforms_ = device.CreateBuffer(&desc);
        desc.label = "tonemap-uniforms";
        desc.size = sizeof(TonemapUniforms);
        tonemapUniforms_ = device.CreateBuffer(&desc);
    }
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = frameUniforms_;
        entry.size = sizeof(FrameUniforms);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "frame-bind-group";
        desc.layout = frameLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        frameBindGroup_ = device.CreateBindGroup(&desc);
    }
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = objectUniforms_;
        entry.size = sizeof(ObjectUniforms);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "object-bind-group";
        desc.layout = objectLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        objectBindGroup_ = device.CreateBindGroup(&desc);
    }

    if (auto r = createPipelines(); !r) {
        return r;
    }
    if (context_.errorCount() > 0) {
        return fail("renderer initialisation raised {} GPU error(s): {}", context_.errorCount(),
                    context_.lastError());
    }
    initialised_ = true;
    return {};
}

Result<void> SceneRenderer::createPipelines() {
    auto meshModule = shaders_.load("mesh.wgsl");
    if (!meshModule) {
        return std::unexpected(meshModule.error());
    }
    auto gridModule = shaders_.load("grid.wgsl");
    if (!gridModule) {
        return std::unexpected(gridModule.error());
    }
    auto tonemapModule = shaders_.load("tonemap.wgsl");
    if (!tonemapModule) {
        return std::unexpected(tonemapModule.error());
    }
    tonemapModule_ = *tonemapModule;

    auto lit = createScenePipeline(*meshModule, false, true, wgpu::CullMode::Back, "lit-pipeline");
    if (!lit) {
        return std::unexpected(lit.error());
    }
    litPipeline_ = *lit;
    auto grid = createScenePipeline(*gridModule, true, false, wgpu::CullMode::None, "grid-pipeline");
    if (!grid) {
        return std::unexpected(grid.error());
    }
    gridPipeline_ = *grid;
    return {};
}

Result<wgpu::RenderPipeline> SceneRenderer::createScenePipeline(const wgpu::ShaderModule& module, bool additive,
                                                                bool depthWrite, wgpu::CullMode cull,
                                                                const char* label) {
    std::array<wgpu::VertexAttribute, 3> attributes{};
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(scene::Vertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x3;
    attributes[1].offset = offsetof(scene::Vertex, normal);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Float32x2;
    attributes[2].offset = offsetof(scene::Vertex, uv);
    attributes[2].shaderLocation = 2;
    static_assert(sizeof(scene::Vertex) == 32);

    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = sizeof(scene::Vertex);
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.attributeCount = attributes.size();
    vertexLayout.attributes = attributes.data();

    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::One;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::Zero;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.blend = additive ? &blend : nullptr;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = depthWrite ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = label;
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexLayout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = cull;
    desc.depthStencil = &depth;
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
    return pipeline;
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
        return fail("tonemap pipeline for format {} failed: {}", key, error);
    }
    tonemapPipelines_[key] = pipeline;
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

        // Index buffers must be a multiple of 4 bytes; uint32 indices always are.
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

Result<void> SceneRenderer::render(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                                   const gpu::TargetView& target) {
    if (!initialised_) {
        return fail("renderer not initialised");
    }
    if (!hdr_.valid() || target.width != hdr_.width() || target.height != hdr_.height()) {
        if (auto r = resize(target.width, target.height); !r) {
            return r;
        }
    }
    uploadMeshes(scene);
    ensureTonemapBindGroup();

    const auto& queue = context_.queue();
    const float aspect = static_cast<float>(hdr_.width()) / static_cast<float>(hdr_.height());

    // ---- frame uniforms ----
    FrameUniforms frame{};
    frame.viewProj = scene.camera.projection(aspect) * scene.camera.view();
    frame.cameraPos = glm::vec4(scene.camera.position, 1.0f);
    frame.lightDir = glm::vec4(glm::normalize(scene.light.direction), 0.0f);
    frame.lightColor = glm::vec4(scene.light.color * scene.light.intensity, 1.0f);
    frame.params = glm::vec4(static_cast<float>(time.renderTime), scene.environment.gridIntensity,
                             scene.environment.brightness, 0.0f);
    queue.WriteBuffer(frameUniforms_, 0, &frame, sizeof(frame));

    // ---- object uniforms (one 256-byte slot per visible entity) ----
    struct DrawItem {
        std::uint32_t offset;
        const scene::Entity* entity;
    };
    std::vector<DrawItem> draws;
    draws.reserve(scene.entities.size());
    std::uint32_t objectIndex = 0;
    for (const auto& entity : scene.entities) {
        if (!entity.visible || entity.mesh >= meshes_.size() || meshes_[entity.mesh].indexCount == 0) {
            continue;
        }
        if (objectIndex >= kMaxObjects) {
            log::warn("more than {} visible entities; extra entities skipped", kMaxObjects);
            break;
        }
        ObjectUniforms obj{};
        obj.model = entity.transform.matrix();
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        obj.baseColor = glm::vec4(entity.material.baseColor, 1.0f);
        obj.emissive = glm::vec4(entity.material.emissiveColor, entity.material.emissiveIntensity);
        obj.material = glm::vec4(entity.material.roughness, entity.material.metallic, 0.0f, 0.0f);
        const std::uint32_t offset = objectIndex * kObjectStride;
        std::memcpy(objectStaging_.data() + offset, &obj, sizeof(obj));
        draws.push_back({offset, &entity});
        ++objectIndex;
    }
    if (objectIndex > 0) {
        queue.WriteBuffer(objectUniforms_, 0, objectStaging_.data(), static_cast<std::size_t>(objectIndex) * kObjectStride);
    }

    TonemapUniforms tonemap{};
    tonemap.exposure = scene.environment.brightness;
    queue.WriteBuffer(tonemapUniforms_, 0, &tonemap, sizeof(tonemap));

    stats_.drawCalls = 0;
    stats_.triangles = 0;
    stats_.entities = objectIndex;

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
        rp.SetBindGroup(0, frameBindGroup_);
        // Opaque first, then additive grid so blending sees the lit result.
        for (int phase = 0; phase < 2; ++phase) {
            const bool wantGrid = phase == 1;
            rp.SetPipeline(wantGrid ? gridPipeline_ : litPipeline_);
            for (const auto& item : draws) {
                const bool isGrid = item.entity->style == scene::MeshStyle::Grid;
                if (isGrid != wantGrid) {
                    continue;
                }
                const GpuMesh& mesh = meshes_[item.entity->mesh];
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                rp.SetVertexBuffer(0, mesh.vertices);
                rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
                rp.DrawIndexed(mesh.indexCount);
                ++stats_.drawCalls;
                stats_.triangles += mesh.indexCount / 3;
            }
        }
        rp.End();
    }

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
        rp.SetBindGroup(0, tonemapBindGroup_);
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
        ++stats_.triangles;
    }
    timer_->resolve(encoder);
    return {};
}

Result<gpu::Image8> SceneRenderer::renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                 std::uint32_t width, std::uint32_t height) {
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
    if (auto r = render(encoder, scene, time, target); !r) {
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
