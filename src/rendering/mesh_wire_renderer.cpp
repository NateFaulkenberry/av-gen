#include "rendering/mesh_wire_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

struct ParamsGpu {
    glm::vec4 color;
    glm::vec4 shape;
    glm::vec4 crease;
    glm::vec4 wave;
    glm::vec4 origin;
};
static_assert(sizeof(ParamsGpu) == 80);

struct DrawGpu {
    glm::mat4 model;
    glm::mat4 normalModel;
};
static_assert(sizeof(DrawGpu) == 128);
constexpr std::uint64_t kDrawStride = 256; // WebGPU's minimum uniform offset alignment

} // namespace

Result<void> MeshWireRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat depthFormat, const wgpu::BindGroupLayout& frameLayout) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    const wgpu::Device& device = context.device();
    std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.minBindingSize = sizeof(ParamsGpu);
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Vertex;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Vertex;
    entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Vertex;
    entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[3].buffer.hasDynamicOffset = true;
    entries[3].buffer.minBindingSize = sizeof(DrawGpu);
    wgpu::BindGroupLayoutDescriptor ld{};
    ld.label = "mesh-wire-group";
    ld.entryCount = entries.size();
    ld.entries = entries.data();
    groupLayout_ = device.CreateBindGroupLayout(&ld);
    const std::array<wgpu::BindGroupLayout, 2> layouts{frameLayout, groupLayout_};
    wgpu::PipelineLayoutDescriptor desc{};
    desc.label = "mesh-wire-pipeline-layout";
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    layout_ = device.CreatePipelineLayout(&desc);
    wgpu::BufferDescriptor pd{};
    pd.label = "mesh-wire-params";
    pd.size = sizeof(ParamsGpu);
    pd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    params_ = device.CreateBuffer(&pd);
    return createPipeline(shaders);
}

Result<void> MeshWireRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipeline(shaders);
}

Result<void> MeshWireRenderer::createPipeline(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("mesh_wire.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::Device& device = context_->device();
    // Added light (ribbon.wgsl's additive rule): colour and emission add; velocity, normal and ids stay the surface's.
    wgpu::BlendState color{};
    color.color.operation = wgpu::BlendOperation::Add;
    color.color.srcFactor = wgpu::BlendFactor::One;
    color.color.dstFactor = wgpu::BlendFactor::One;
    color.alpha.operation = wgpu::BlendOperation::Add;
    color.alpha.srcFactor = wgpu::BlendFactor::Zero;
    color.alpha.dstFactor = wgpu::BlendFactor::One;
    wgpu::BlendState emission = color;
    emission.alpha.operation = wgpu::BlendOperation::Max;
    emission.alpha.srcFactor = wgpu::BlendFactor::One;
    emission.alpha.dstFactor = wgpu::BlendFactor::One;
    const auto formats = sceneTargetFormats(hdrFormat_);
    std::array<wgpu::ColorTargetState, kSceneTargetCount> targets{};
    for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
        targets[i] = wgpu::ColorTargetState{};
        targets[i].format = formats[i];
        targets[i].writeMask = (i == 0 || i == 3) ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
    }
    targets[0].blend = &color;
    targets[3].blend = &emission;
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_mesh_wire";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = targets.data();
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat_;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "mesh-wire";
    desc.layout = layout_;
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs_mesh_wire";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &desc);
    std::string error;
    auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                       [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                           if (type != wgpu::ErrorType::NoError) {
                                               error = std::string(msg.data, msg.length);
                                           }
                                       });
    context_->instance().WaitAny(future, UINT64_MAX);
    if (!error.empty() || !pipeline) {
        return fail("mesh wire pipeline: {}", error);
    }
    pipeline_ = pipeline;
    return {};
}

void MeshWireRenderer::draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup,
                            const scene::Scene& scene, const std::function<MeshWireGeometry(scene::MeshId)>& geometry) {
    stats_ = MeshWireStats{};
    const scene::MeshWire& w = scene.meshWire;
    // The gate: nothing to show, nothing done -- not a bind, not an upload.
    if (!w.active() || !ready()) {
        return;
    }
    const glm::vec3 eye = scene.camera.position;
    glm::vec3 forward = scene.camera.target - eye;
    forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    const float fadeFar = std::max(w.fadeFar, w.fadeNear);
    // The wave's origin: `waveAhead` metres in front of the camera along the ground.
    glm::vec2 flat(forward.x, forward.z);
    flat = glm::length(flat) > 1e-5f ? glm::normalize(flat) : glm::vec2(0.0f, -1.0f);
    const glm::vec3 origin = eye + glm::vec3(flat.x, 0.0f, flat.y) * w.waveAhead;

    struct Item {
        scene::MeshId mesh;
        glm::mat4 model;
        std::uint32_t triangles;
    };
    std::vector<Item> items;
    for (const scene::Entity& e : scene.entities) {
        if (!e.visible || e.mesh == scene::kInvalidMesh || !scene::meshWireTargets(w, e.name)) {
            continue;
        }
        const MeshWireGeometry g = geometry(e.mesh);
        if (!g.vertices || !g.indices || g.indexCount < 3) {
            continue;
        }
        const glm::mat4 model = e.transform.matrix();
        // Cull: the mesh's bounding sphere beyond the fade, or entirely behind the eye.
        const auto& [lo, hi] = scene.meshBounds(e.mesh);
        const glm::vec3 centre = glm::vec3(model * glm::vec4(0.5f * (lo + hi), 1.0f));
        const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                      glm::length(glm::vec3(model[2]))});
        const float radius = 0.5f * glm::length(hi - lo) * scale;
        const glm::vec3 toCentre = centre - eye;
        if (glm::length(toCentre) - radius > fadeFar || glm::dot(toCentre, forward) < -radius) {
            ++stats_.culled;
            continue;
        }
        items.push_back({e.mesh, model, g.indexCount / 3u});
    }
    if (items.empty()) {
        return;
    }
    const wgpu::Device& device = context_->device();
    if (items.size() > drawCapacity_) {
        drawCapacity_ = static_cast<std::uint32_t>(std::max<std::size_t>(items.size(), 64) * 2);
        wgpu::BufferDescriptor bd{};
        bd.label = "mesh-wire-draws";
        bd.size = kDrawStride * drawCapacity_;
        bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        draws_ = device.CreateBuffer(&bd);
        groups_.clear(); // they name the old buffer
    }
    ParamsGpu p{};
    p.color = glm::vec4(glm::max(w.color, glm::vec3(0.0f)), std::clamp(w.opacity, 0.0f, 1.0f));
    p.shape = glm::vec4(w.width, w.fadeNear, fadeFar, w.minPixels);
    p.crease = glm::vec4(w.creaseLow, w.creaseHigh, w.reveal, std::max(w.intensity, 0.0f));
    p.wave = glm::vec4(w.wave, w.waveRadius, w.waveWidth, std::max(w.waveGain, 0.0f));
    p.origin = glm::vec4(origin, w.clipBelow);
    context_->queue().WriteBuffer(params_, 0, &p, sizeof(p));
    std::vector<std::uint8_t> staging(kDrawStride * items.size(), 0);
    for (std::size_t i = 0; i < items.size(); ++i) {
        DrawGpu d{items[i].model, glm::inverseTranspose(items[i].model)};
        std::memcpy(staging.data() + kDrawStride * i, &d, sizeof(d));
    }
    context_->queue().WriteBuffer(draws_, 0, staging.data(), staging.size());

    pass.SetPipeline(pipeline_);
    pass.SetBindGroup(0, frameGroup);
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto it = groups_.find(items[i].mesh);
        if (it == groups_.end()) {
            const MeshWireGeometry g = geometry(items[i].mesh);
            std::array<wgpu::BindGroupEntry, 4> be{};
            be[0].binding = 0;
            be[0].buffer = params_;
            be[0].size = sizeof(ParamsGpu);
            be[1].binding = 1;
            be[1].buffer = g.vertices;
            be[1].size = g.vertexBytes;
            be[2].binding = 2;
            be[2].buffer = g.indices;
            be[2].size = static_cast<std::uint64_t>(g.indexCount) * sizeof(std::uint32_t);
            be[3].binding = 3;
            be[3].buffer = draws_;
            be[3].size = sizeof(DrawGpu);
            wgpu::BindGroupDescriptor gd{};
            gd.label = "mesh-wire-mesh";
            gd.layout = groupLayout_;
            gd.entryCount = be.size();
            gd.entries = be.data();
            it = groups_.emplace(items[i].mesh, device.CreateBindGroup(&gd)).first;
        }
        const auto offset = static_cast<std::uint32_t>(kDrawStride * i);
        pass.SetBindGroup(1, it->second, 1, &offset);
        pass.Draw(18, items[i].triangles, 0, 0);
        ++stats_.draws;
        stats_.triangles += items[i].triangles;
    }
}

} // namespace avgen::rendering
