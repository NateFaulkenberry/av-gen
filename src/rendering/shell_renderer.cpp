#include "rendering/shell_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"
#include "scene/mesh_generators.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace avgen::rendering {

namespace {

constexpr std::uint64_t kRecordBytes = static_cast<std::uint64_t>(world::kMaxShells) * sizeof(world::ShellInstance);
constexpr std::uint64_t kExtraBytes = static_cast<std::uint64_t>(world::kMaxShellExtra) * sizeof(glm::vec4);

struct MeshVertexLayout {
    std::array<wgpu::VertexAttribute, 3> attributes{};
    wgpu::VertexBufferLayout layout{};
    MeshVertexLayout() {
        const std::array<std::size_t, 3> offsets = {offsetof(scene::Vertex, position), offsetof(scene::Vertex, normal),
                                                    offsetof(scene::Vertex, uv)};
        for (std::uint32_t i = 0; i < attributes.size(); ++i) {
            attributes[i].format = i == 2 ? wgpu::VertexFormat::Float32x2 : wgpu::VertexFormat::Float32x3;
            attributes[i].offset = offsets[i];
            attributes[i].shaderLocation = i;
        }
        layout.arrayStride = sizeof(scene::Vertex);
        layout.stepMode = wgpu::VertexStepMode::Vertex;
        layout.attributeCount = attributes.size();
        layout.attributes = attributes.data();
    }
};

// The canonical meshes, in `world::ShellMesh` order. Tessellated for a smooth silhouette and smooth
// interpolated normals (a rim term is a function of the normal, and a coarse sphere shows its facets
// in the rim) -- not for detail, which the fragment stage makes.
scene::MeshData canonicalMesh(world::ShellMesh mesh) {
    switch (mesh) {
    case world::ShellMesh::Sphere: return scene::makeIcosphere(1.0f, 4);
    case world::ShellMesh::Box: return scene::makeCube(1.0f);
    case world::ShellMesh::Quad: return scene::makeQuad(1.0f);
    case world::ShellMesh::Disc: return scene::makeDisc(1.0f, 64);
    case world::ShellMesh::Cylinder: return scene::makeCylinder(1.0f, 1.0f, 64, false);
    case world::ShellMesh::Cone: return scene::makeCone(1.0f, 1.0f, 64, false);
    }
    return scene::makeIcosphere(1.0f, 4);
}

const char* entryPoint(world::ShellShading s) {
    switch (s) {
    case world::ShellShading::Plasma: return "fs_plasma";
    case world::ShellShading::Shield: return "fs_shield";
    case world::ShellShading::Barrier: return "fs_barrier";
    }
    return "fs_shield";
}

} // namespace

bool ShellRenderer::ready() const {
    return std::all_of(pipelines_.begin(), pipelines_.end(), [](const wgpu::RenderPipeline& p) { return static_cast<bool>(p); });
}

Result<void> ShellRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                 wgpu::TextureFormat depthFormat, const wgpu::BindGroupLayout& frameLayout,
                                 const wgpu::BindGroupLayout& iblLayout) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    const wgpu::Device& device = context.device();

    std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[0].buffer.minBindingSize = sizeof(world::ShellInstance);
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].buffer.minBindingSize = sizeof(glm::vec4);
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Fragment;
    entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[2].buffer.minBindingSize = sizeof(glm::vec4);
    wgpu::BindGroupLayoutDescriptor groupDesc{};
    groupDesc.label = "shell-group-layout";
    groupDesc.entryCount = entries.size();
    groupDesc.entries = entries.data();
    groupLayout_ = device.CreateBindGroupLayout(&groupDesc);

    const std::array<wgpu::BindGroupLayout, 3> layouts = {frameLayout, groupLayout_, iblLayout};
    wgpu::PipelineLayoutDescriptor desc{};
    desc.label = "shell-pipeline-layout";
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    layout_ = device.CreatePipelineLayout(&desc);
    return createPipelines(shaders);
}

Result<void> ShellRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipelines(shaders);
}

Result<void> ShellRenderer::createPipelines(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("shell.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const MeshVertexLayout vertex;
    const wgpu::Device& device = context_->device();

    // Light added to the frame, in every shading kind.
    wgpu::BlendState color{};
    color.color.operation = wgpu::BlendOperation::Add;
    color.color.srcFactor = wgpu::BlendFactor::One;
    color.color.dstFactor = wgpu::BlendFactor::One;
    color.alpha.operation = wgpu::BlendOperation::Add;
    color.alpha.srcFactor = wgpu::BlendFactor::Zero;
    color.alpha.dstFactor = wgpu::BlendFactor::One;
    // Velocity: the camera's motion at the shell, mixed in by its coverage, so a faint shield barely
    // moves the motion vectors of what is behind it.
    wgpu::BlendState velocity{};
    velocity.color.operation = wgpu::BlendOperation::Add;
    velocity.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    velocity.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    velocity.alpha.operation = wgpu::BlendOperation::Add;
    velocity.alpha.srcFactor = wgpu::BlendFactor::One;
    velocity.alpha.dstFactor = wgpu::BlendFactor::Zero;
    // Emission: added like the colour; the bloom weight is the larger of what was there and what the
    // shell brings, so a shell never erases a glow behind it.
    wgpu::BlendState emission = color;
    emission.alpha.operation = wgpu::BlendOperation::Max;
    emission.alpha.srcFactor = wgpu::BlendFactor::One;
    emission.alpha.dstFactor = wgpu::BlendFactor::One;

    // All five targets of the scene pass (ADR-035), in its order. The normal and identifier targets
    // stay the opaque geometry's: a normal or an id averaged over a transparency is worse than none.
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

    std::array<wgpu::RenderPipeline, world::kShellShadingCount> built{};
    for (std::size_t k = 0; k < world::kShellShadingCount; ++k) {
        const auto shading = static_cast<world::ShellShading>(k);
        wgpu::FragmentState fragment{};
        fragment.module = *module;
        fragment.entryPoint = entryPoint(shading);
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = targets.data();

        wgpu::DepthStencilState depth{};
        depth.format = depthFormat_;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::Less;

        wgpu::RenderPipelineDescriptor desc{};
        const std::string label = std::string("shell-") + world::shellShadingName(shading);
        desc.label = label.c_str();
        desc.layout = layout_;
        desc.vertex.module = *module;
        desc.vertex.entryPoint = "vs_shell";
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers = &vertex.layout;
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.frontFace = wgpu::FrontFace::CCW;
        // A plasma orb is marched from where the ray enters it, its front faces; a shield and a
        // barrier are sheets seen from both sides (the far side of a shield dimmer).
        desc.primitive.cullMode =
            shading == world::ShellShading::Plasma ? wgpu::CullMode::Back : wgpu::CullMode::None;
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
            return fail("shell pipeline ({}): {}", world::shellShadingName(shading), error);
        }
        built[k] = pipeline;
    }
    pipelines_ = built;
    return {};
}

void ShellRenderer::createResources() {
    const wgpu::Device& device = context_->device();
    wgpu::BufferDescriptor desc{};
    desc.label = "shell-records";
    desc.size = kRecordBytes;
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    records_ = device.CreateBuffer(&desc);
    desc.label = "shell-extra";
    desc.size = kExtraBytes;
    extra_ = device.CreateBuffer(&desc);
    desc.label = "shell-info";
    desc.size = sizeof(glm::vec4);
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    info_ = device.CreateBuffer(&desc);

    std::array<wgpu::BindGroupEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].buffer = records_;
    entries[0].size = kRecordBytes;
    entries[1].binding = 1;
    entries[1].buffer = extra_;
    entries[1].size = kExtraBytes;
    entries[2].binding = 2;
    entries[2].buffer = info_;
    entries[2].size = sizeof(glm::vec4);
    wgpu::BindGroupDescriptor group{};
    group.label = "shell-group";
    group.layout = groupLayout_;
    group.entryCount = entries.size();
    group.entries = entries.data();
    group_ = device.CreateBindGroup(&group);

    for (std::size_t m = 0; m < world::kShellMeshCount; ++m) {
        const scene::MeshData data = canonicalMesh(static_cast<world::ShellMesh>(m));
        Mesh& mesh = meshes_[m];
        wgpu::BufferDescriptor vb{};
        vb.label = "shell-mesh-vertices";
        vb.size = data.vertices.size() * sizeof(scene::Vertex);
        vb.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        mesh.vertices = device.CreateBuffer(&vb);
        context_->queue().WriteBuffer(mesh.vertices, 0, data.vertices.data(), vb.size);
        wgpu::BufferDescriptor ib{};
        ib.label = "shell-mesh-indices";
        ib.size = data.indices.size() * sizeof(std::uint32_t);
        ib.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        mesh.indices = device.CreateBuffer(&ib);
        context_->queue().WriteBuffer(mesh.indices, 0, data.indices.data(), ib.size);
        mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
    }
}

void ShellRenderer::draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup,
                         const wgpu::BindGroup& iblGroup, const world::ShellFrame& frame, bool linearDepthThisFrame) {
    stats_ = ShellStats{};
    // The gate: no shell, nothing -- not a bind, not an upload.
    if (frame.instances.empty() || frame.batches.empty() || !ready()) {
        return;
    }
    if (!records_) {
        createResources();
    }
    // The report the builder turns into a status reason (one frame late; the pixels use the flag).
    world::reportShellLinearDepth(linearDepthThisFrame);

    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(frame.instances.size(), world::kMaxShells));
    const auto extra = static_cast<std::uint32_t>(std::min<std::size_t>(frame.extra.size(), world::kMaxShellExtra));
    // Queued ahead of the submit this pass belongs to, so the draws below read this frame's shells.
    context_->queue().WriteBuffer(records_, 0, frame.instances.data(),
                                  static_cast<std::size_t>(count) * sizeof(world::ShellInstance));
    if (extra > 0) {
        context_->queue().WriteBuffer(extra_, 0, frame.extra.data(), static_cast<std::size_t>(extra) * sizeof(glm::vec4));
    }
    const float depthFlag = linearDepthThisFrame ? 1.0f : 0.0f;
    if (depthFlag != lastInfo_) {
        const glm::vec4 info(depthFlag, 0.0f, 0.0f, 0.0f);
        context_->queue().WriteBuffer(info_, 0, &info, sizeof(info));
        lastInfo_ = depthFlag;
    }
    pass.SetBindGroup(0, frameGroup);
    pass.SetBindGroup(1, group_);
    pass.SetBindGroup(2, iblGroup);
    const wgpu::RenderPipeline* bound = nullptr;
    std::size_t boundMesh = world::kShellMeshCount;
    for (const world::ShellBatch& batch : frame.batches) {
        if (batch.count == 0 || batch.first + batch.count > count) {
            continue;
        }
        const wgpu::RenderPipeline* want = &pipelines_[static_cast<std::size_t>(batch.shading)];
        if (want != bound) {
            pass.SetPipeline(*want);
            bound = want;
        }
        const auto m = static_cast<std::size_t>(batch.mesh);
        if (m != boundMesh) {
            pass.SetVertexBuffer(0, meshes_[m].vertices);
            pass.SetIndexBuffer(meshes_[m].indices, wgpu::IndexFormat::Uint32);
            boundMesh = m;
        }
        pass.DrawIndexed(meshes_[m].indexCount, batch.count, 0, 0, batch.first);
        ++stats_.draws;
        stats_.shells += batch.count;
    }
    stats_.linearDepth = linearDepthThisFrame;
}

} // namespace avgen::rendering
