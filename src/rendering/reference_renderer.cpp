#include "rendering/reference_renderer.hpp"

#include "core/log.hpp"
#include "gpu/readback.hpp"

#include <array>
#include <cstring>

namespace avgen::rendering {
namespace {

constexpr wgpu::TextureFormat kColorFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth32Float;
// One 256-byte slot per object, which is WebGPU's minimum dynamic-offset alignment.
constexpr std::uint32_t kStride = 256;
constexpr std::size_t kMaxObjects = 1024;

struct ObjectUniforms {
    glm::mat4 mvp{1.0f};
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};
static_assert(sizeof(ObjectUniforms) <= kStride);

} // namespace

struct ReferenceRenderer::Impl {
    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::RenderPipeline pipeline;
    wgpu::BindGroupLayout layout;
    wgpu::BindGroup bindGroup;
    wgpu::Buffer uniforms;
    std::vector<std::uint8_t> staging;
    // Meshes are uploaded per call and thrown away. A cache would be a second thing that can hold a
    // stale buffer, which is one of the defects this path exists to rule out.
    std::vector<wgpu::Buffer> scratch;
    wgpu::Texture color;
    wgpu::Texture depth;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    void ensureTargets(std::uint32_t w, std::uint32_t h) {
        if (color && width == w && height == h) {
            return;
        }
        width = w;
        height = h;
        wgpu::TextureDescriptor desc{};
        desc.label = "reference-color";
        desc.size = {w, h, 1};
        desc.format = kColorFormat;
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        color = context.device().CreateTexture(&desc);
        desc.label = "reference-depth";
        desc.format = kDepthFormat;
        desc.usage = wgpu::TextureUsage::RenderAttachment;
        depth = context.device().CreateTexture(&desc);
    }
};

ReferenceRenderer::ReferenceRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}
ReferenceRenderer::~ReferenceRenderer() = default;

Result<void> ReferenceRenderer::init() {
    auto module = impl_->shaders.load("reference.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::Device& device = impl_->context.device();
    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(ObjectUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "reference-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        impl_->layout = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "reference-uniforms";
        desc.size = static_cast<std::uint64_t>(kStride) * kMaxObjects;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        impl_->uniforms = device.CreateBuffer(&desc);
        impl_->staging.assign(static_cast<std::size_t>(desc.size), 0);

        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = impl_->uniforms;
        entry.size = sizeof(ObjectUniforms);
        wgpu::BindGroupDescriptor bg{};
        bg.label = "reference-bind-group";
        bg.layout = impl_->layout;
        bg.entryCount = 1;
        bg.entries = &entry;
        impl_->bindGroup = device.CreateBindGroup(&bg);
    }

    std::array<wgpu::VertexAttribute, 2> attributes{};
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(scene::Vertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x3;
    attributes[1].offset = offsetof(scene::Vertex, normal);
    attributes[1].shaderLocation = 1;
    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = sizeof(scene::Vertex);
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.attributeCount = attributes.size();
    vertexLayout.attributes = attributes.data();

    wgpu::ColorTargetState target{};
    target.format = kColorFormat;
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "reference-pipeline-layout";
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &impl_->layout;
    const wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&layoutDesc);

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "reference-pipeline";
    desc.layout = pipelineLayout;
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexLayout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    // No back-face culling: a comparison about *coverage* must not disagree because one path
    // discarded a winding the other kept.
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    impl_->pipeline = device.CreateRenderPipeline(&desc);
    if (impl_->pipeline == nullptr) {
        return fail("the reference pipeline could not be created");
    }
    return {};
}

Result<gpu::Image8> ReferenceRenderer::renderToImage(const scene::Scene& scene, std::uint32_t width,
                                                     std::uint32_t height) {
    if (impl_->pipeline == nullptr) {
        return fail("the reference renderer was not initialised");
    }
    if (width == 0 || height == 0) {
        return fail("the reference renderer needs a non-empty target");
    }
    counts_ = Counts{};
    impl_->ensureTargets(width, height);
    impl_->scratch.clear();

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 view = scene.camera.view();
    const glm::mat4 projection = scene.camera.projection(aspect);
    const glm::mat4 viewProjection = projection * view;

    struct Draw {
        std::uint32_t offset;
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount;
    };
    std::vector<Draw> draws;
    const wgpu::Device& device = impl_->context.device();

    // Scene order, no sorting, no culling. Everything this path refuses is counted rather than
    // quietly dropped.
    for (const scene::Entity& entity : scene.entities) {
        if (!entity.visible) {
            ++counts_.skippedInvisible;
            continue;
        }
        if (entity.rig != scene::kInvalidRig) {
            ++counts_.skippedSkinned;
            continue;
        }
        if (entity.style == scene::MeshStyle::Water) {
            ++counts_.skippedWater;
            continue;
        }
        if (entity.material.alphaMode == scene::AlphaMode::Blend) {
            ++counts_.skippedBlended;
            continue;
        }
        if (entity.mesh >= scene.meshes.size() || scene.meshes[entity.mesh].indices.empty()) {
            ++counts_.skippedNoMesh;
            continue;
        }
        if (draws.size() >= kMaxObjects) {
            log::warn("reference renderer: more than {} objects; the rest are skipped", kMaxObjects);
            break;
        }
        const scene::MeshData& mesh = scene.meshes[entity.mesh];
        const glm::mat4 model = entity.transform.matrix();

        ObjectUniforms uniforms;
        uniforms.mvp = viewProjection * model;
        uniforms.model = model;
        uniforms.color = glm::vec4(entity.material.baseColor, 1.0f);
        const auto slot = static_cast<std::uint32_t>(draws.size());
        std::memcpy(impl_->staging.data() + static_cast<std::size_t>(slot) * kStride, &uniforms,
                    sizeof(uniforms));

        wgpu::BufferDescriptor vdesc{};
        vdesc.label = "reference-vertices";
        vdesc.size = mesh.vertices.size() * sizeof(scene::Vertex);
        vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer vertices = device.CreateBuffer(&vdesc);
        device.GetQueue().WriteBuffer(vertices, 0, mesh.vertices.data(), vdesc.size);

        wgpu::BufferDescriptor idesc{};
        idesc.label = "reference-indices";
        idesc.size = mesh.indices.size() * sizeof(std::uint32_t);
        idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer indices = device.CreateBuffer(&idesc);
        device.GetQueue().WriteBuffer(indices, 0, mesh.indices.data(), idesc.size);

        impl_->scratch.push_back(vertices);
        impl_->scratch.push_back(indices);
        draws.push_back({slot * kStride, vertices, indices,
                         static_cast<std::uint32_t>(mesh.indices.size())});
        ++counts_.drawn;
    }
    if (!draws.empty()) {
        device.GetQueue().WriteBuffer(impl_->uniforms, 0, impl_->staging.data(),
                                      static_cast<std::uint64_t>(draws.size()) * kStride);
    }

    wgpu::CommandEncoderDescriptor encoderDesc{};
    encoderDesc.label = "reference-encoder";
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder(&encoderDesc);
    {
        wgpu::RenderPassColorAttachment colorAttachment{};
        colorAttachment.view = impl_->color.CreateView();
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = impl_->depth.CreateView();
        depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
        depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "reference-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colorAttachment;
        pass.depthStencilAttachment = &depthAttachment;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(impl_->pipeline);
        for (const Draw& draw : draws) {
            rp.SetBindGroup(0, impl_->bindGroup, 1, &draw.offset);
            rp.SetVertexBuffer(0, draw.vertices);
            rp.SetIndexBuffer(draw.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(draw.indexCount);
        }
        rp.End();
    }
    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);
    return gpu::readTexture8(impl_->context, impl_->color, width, height, false);
}

} // namespace avgen::rendering
