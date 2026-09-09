#include "rendering/procedural_renderer.hpp"

#include "rendering/field_uniforms.hpp"
#include "rendering/scene_renderer.hpp" // ObjectUniforms (the shared 256-byte slot layout)

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>

namespace avgen::rendering {

namespace {

constexpr std::uint32_t kMaxProceduralObjects = 256; // 256-byte slots in one uniform buffer
constexpr std::uint32_t kObjectStride = 256;         // dynamic-offset alignment
constexpr std::uint32_t kInstanceStride = sizeof(scene::InstanceRecord); // 96
constexpr std::uint32_t kEffectorWorkgroup = 64;     // points.wgsl cs_effectors
static_assert(kInstanceStride == 96);
static_assert(sizeof(ObjectUniforms) <= kObjectStride);

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

glm::vec3 safeNormalize(glm::vec3 v, glm::vec3 fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v / std::sqrt(len2) : fallback;
}

// Packs one scene deformer into its uniform slot (see procedural.wgsl DeformerUniform).
// `fieldSlot` is the resolved slot of a Field deformer's field (-1 = unbound: the slot is
// disabled so the shader skips it).
DeformerUniform packDeformer(const scene::Deformer& d, int fieldSlot) {
    DeformerUniform u{};
    if (!d.enabled || (d.kind == scene::DeformerKind::Field && fieldSlot < 0)) {
        u.axisKind = glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);
        return u;
    }
    const float code = static_cast<float>(d.kind) + (d.space == scene::DeformSpace::World ? 8.0f : 0.0f);
    u.axisKind = glm::vec4(safeNormalize(d.axis, glm::vec3(0.0f, 1.0f, 0.0f)), code);
    u.centerAmount = glm::vec4(d.center, d.amount);
    const float freqOrScale = d.kind == scene::DeformerKind::Sine ? d.frequency : d.scale;
    u.params = glm::vec4(freqOrScale, d.speed, d.phase, std::max(d.falloff, 0.0f));
    glm::vec3 extra{0.0f};
    switch (d.kind) {
    case scene::DeformerKind::Bend:
    case scene::DeformerKind::Sine:
        extra = d.displacementAxis;
        break;
    case scene::DeformerKind::Noise:
        extra = d.axisMask;
        break;
    case scene::DeformerKind::Field:
        u.params = glm::vec4(static_cast<float>(fieldSlot), d.alongNormal ? 1.0f : 0.0f, 0.0f, 0.0f);
        break;
    case scene::DeformerKind::Twist:
    case scene::DeformerKind::Displacement:
        break;
    }
    u.extra = glm::vec4(extra, std::bit_cast<float>(d.seed));
    return u;
}

// The rotation-only part of a matrix (normalised columns; scale sign kept).
glm::mat3 rotationOf(const glm::mat4& m) {
    glm::mat3 r(1.0f);
    for (int c = 0; c < 3; ++c) {
        r[c] = safeNormalize(glm::vec3(m[c]), glm::vec3(c == 0 ? 1.0f : 0.0f, c == 1 ? 1.0f : 0.0f, c == 2 ? 1.0f : 0.0f));
    }
    return r;
}

} // namespace

struct ProceduralRenderer::Impl {
    struct CachedMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;  // 0 = generation failed (kept so the error is logged once)
        std::uint32_t vertexCount = 0;
        float radius = 1.0f;           // half diagonal of the source bounds (normal epsilon scale)
        std::uint64_t lastUsed = 0;
    };
    struct ObjectState {
        std::uint64_t structureVersion = ~0ull; // of the uploaded instances
        std::size_t uploadedCount = 0;
        wgpu::Buffer instances;                 // base records
        std::uint64_t instanceBytes = 0;
        wgpu::Buffer live;                      // effector pass output (same layout), when used
        std::uint64_t liveBytes = 0;
        wgpu::Buffer deformers;
        wgpu::Buffer effectorUniforms;
        wgpu::BindGroup group;                  // draw group reading the base records
        wgpu::BindGroup groupLive;              // draw group reading the live records
        wgpu::BindGroup computeGroup;           // effector pass
        bool usesLive = false;                  // this frame's draw reads the live buffer
        std::uint64_t lastUsed = 0;
    };
    struct DrawItem {
        std::size_t objectIndex;        // into scene.procedurals
        const CachedMesh* mesh;
        const ObjectState* state;
        std::uint32_t offset;           // dynamic offset into the object uniform buffer
        std::uint32_t instanceCount;
    };
    struct ComputeItem {
        const ObjectState* state;
        std::uint32_t count;
    };

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {
        staging.resize(static_cast<std::size_t>(kMaxProceduralObjects) * kObjectStride);
    }

    Result<wgpu::RenderPipeline> createPipeline(const wgpu::ShaderModule& module, bool cull);
    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<void> createComputePipeline(const wgpu::ShaderModule& module);
    const CachedMesh* ensureMesh(const scene::ProceduralGeometry& object);
    void ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
    std::uint32_t sampleCount = 1;
    wgpu::BindGroupLayout objectLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline pipelineCull;
    wgpu::RenderPipeline pipelineNoCull;
    wgpu::BindGroupLayout computeLayout;
    wgpu::PipelineLayout computePipelineLayout;
    wgpu::ComputePipeline effectorPipeline;
    wgpu::Buffer objectUniforms;
    wgpu::Buffer fieldBlock;
    std::unique_ptr<gpu::GpuTimer> effectorTimer;
    std::vector<std::uint8_t> staging;
    std::map<std::uint64_t, CachedMesh> meshes;
    std::map<std::string, ObjectState> objects;
    std::vector<DrawItem> items;
    std::vector<ComputeItem> computeItems;
    double lastEffectorMs = -1.0;   // the latest completed effector-pass measurement
    bool passThisFrame = false;      // an effector pass was encoded in the current update()
    std::uint64_t frame = 0;
    bool initialised = false;
    bool warnedLimit = false;
};

ProceduralRenderer::ProceduralRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

ProceduralRenderer::~ProceduralRenderer() = default;

Result<void> ProceduralRenderer::init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                      const wgpu::BindGroupLayout& frameLayout,
                                      const wgpu::BindGroupLayout& materialLayout,
                                      const wgpu::BindGroupLayout& iblLayout, std::uint32_t sampleCount,
                                      wgpu::Buffer fieldBlock) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.depthFormat = depthFormat;
    im.sampleCount = std::max<std::uint32_t>(sampleCount, 1);
    im.fieldBlock = std::move(fieldBlock);
    if (!im.fieldBlock) {
        // Standalone use without a SceneRenderer: an empty field block (count 0).
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        im.fieldBlock = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        im.context.queue().WriteBuffer(im.fieldBlock, 0, &zero, sizeof(zero));
    }
    {
        // Group 1: 0 = object uniforms (dynamic offset, 256-byte slots), 1 = instance records
        // (read-only storage), 2 = deformer/time block, 3 = field block.
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(ObjectUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Vertex;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.minBindingSize = kInstanceStride;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[2].buffer.minBindingSize = sizeof(ProceduralUniforms);
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.minBindingSize = FieldUniforms::kBufferSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "procedural-object-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.objectLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout, im.objectLayout, materialLayout, iblLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "procedural-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        // Effector pass: 0 = params, 1 = base records, 2 = live records, 3 = field block.
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(EffectorPassUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.minBindingSize = kInstanceStride;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Compute;
        entries[2].buffer.type = wgpu::BufferBindingType::Storage;
        entries[2].buffer.minBindingSize = kInstanceStride;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Compute;
        entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.minBindingSize = FieldUniforms::kBufferSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "procedural-effector-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.computeLayout = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.label = "procedural-effector-pipeline-layout";
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &im.computeLayout;
        im.computePipelineLayout = device.CreatePipelineLayout(&pdesc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-object-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(kMaxProceduralObjects) * kObjectStride;
        im.objectUniforms = device.CreateBuffer(&desc);
    }
    im.effectorTimer = std::make_unique<gpu::GpuTimer>(im.context);
    auto module = im.shaders.load("procedural.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipelines(*module); !r) {
        return r;
    }
    auto points = im.shaders.load("points.wgsl");
    if (!points) {
        return std::unexpected(points.error());
    }
    if (auto r = im.createComputePipeline(*points); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> ProceduralRenderer::reload() {
    auto module = impl_->shaders.load("procedural.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = impl_->createPipelines(*module); !r) {
        return r;
    }
    auto points = impl_->shaders.load("points.wgsl");
    if (!points) {
        return std::unexpected(points.error());
    }
    return impl_->createComputePipeline(*points);
}

Result<void> ProceduralRenderer::Impl::createPipelines(const wgpu::ShaderModule& module) {
    auto cull = createPipeline(module, true);
    if (!cull) return std::unexpected(cull.error());
    auto noCull = createPipeline(module, false);
    if (!noCull) return std::unexpected(noCull.error());
    pipelineCull = *cull;
    pipelineNoCull = *noCull;
    return {};
}

Result<void> ProceduralRenderer::Impl::createComputePipeline(const wgpu::ShaderModule& module) {
    wgpu::ComputePipelineDescriptor desc{};
    desc.label = "procedural-effectors";
    desc.layout = computePipelineLayout;
    desc.compute.module = module;
    desc.compute.entryPoint = "cs_effectors";
    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !pipeline) {
        return fail("pipeline 'procedural-effectors' creation failed: {}", error);
    }
    effectorPipeline = pipeline;
    return {};
}

Result<wgpu::RenderPipeline> ProceduralRenderer::Impl::createPipeline(const wgpu::ShaderModule& module, bool cull) {
    VertexLayoutStorage vertex;
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = colorFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_proc";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Less;

    const char* label = cull ? "procedural-opaque" : "procedural-opaque-twosided";
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = label;
    desc.layout = pipelineLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_proc";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = cull ? wgpu::CullMode::Back : wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = sampleCount;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;

    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !pipeline) {
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    return pipeline;
}

const ProceduralRenderer::Impl::CachedMesh* ProceduralRenderer::Impl::ensureMesh(const scene::ProceduralGeometry& object) {
    auto it = meshes.find(object.meshHash);
    if (it == meshes.end()) {
        CachedMesh cached;
        // Point sources are the billboard quad regardless of the generator (the vertex shader
        // builds the camera-facing quad from its XY); everything else goes through makeSourceMesh.
        Result<scene::MeshData> mesh = object.source.kind == scene::PrimitiveKind::Point
                                           ? Result<scene::MeshData>(scene::makePointQuad(object.source.pointSize))
                                           : scene::makeSourceMesh(object.source);
        if (!mesh) {
            log::warn("procedural '{}': source mesh not generated: {}", object.name, mesh.error().message);
        } else if (!mesh->valid()) {
            log::warn("procedural '{}': generated source mesh is invalid", object.name);
        } else {
            const auto& device = context.device();
            wgpu::BufferDescriptor vdesc{};
            vdesc.label = "procedural-vertices";
            vdesc.size = mesh->vertices.size() * sizeof(scene::Vertex);
            vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
            cached.vertices = device.CreateBuffer(&vdesc);
            context.queue().WriteBuffer(cached.vertices, 0, mesh->vertices.data(), vdesc.size);
            wgpu::BufferDescriptor idesc{};
            idesc.label = "procedural-indices";
            idesc.size = mesh->indices.size() * sizeof(std::uint32_t);
            idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
            cached.indices = device.CreateBuffer(&idesc);
            context.queue().WriteBuffer(cached.indices, 0, mesh->indices.data(), idesc.size);
            cached.indexCount = static_cast<std::uint32_t>(mesh->indices.size());
            cached.vertexCount = static_cast<std::uint32_t>(mesh->vertices.size());
            const auto [lo, hi] = mesh->bounds();
            cached.radius = std::max(0.5f * glm::length(hi - lo), 1e-4f);
        }
        it = meshes.emplace(object.meshHash, std::move(cached)).first;
    }
    it->second.lastUsed = frame;
    return it->second.indexCount > 0 ? &it->second : nullptr;
}

void ProceduralRenderer::Impl::ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive) {
    const auto& device = context.device();
    bool rebuildGroup = !state.group;
    if (!state.instances || state.instanceBytes < instanceBytes) {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-instances";
        desc.size = instanceBytes;
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        state.instances = device.CreateBuffer(&desc);
        state.instanceBytes = instanceBytes;
        state.structureVersion = ~0ull; // force an upload into the new buffer
        state.uploadedCount = 0;
        rebuildGroup = true;
    }
    if (needsLive && (!state.live || state.liveBytes < instanceBytes)) {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-instances-live";
        desc.size = instanceBytes;
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        state.live = device.CreateBuffer(&desc);
        state.liveBytes = instanceBytes;
        rebuildGroup = true;
    }
    if (!state.deformers) {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-deformers";
        desc.size = sizeof(ProceduralUniforms);
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        state.deformers = device.CreateBuffer(&desc);
        rebuildGroup = true;
    }
    if (needsLive && !state.effectorUniforms) {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-effectors";
        desc.size = sizeof(EffectorPassUniforms);
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        state.effectorUniforms = device.CreateBuffer(&desc);
        rebuildGroup = true;
    }
    if (rebuildGroup) {
        auto drawGroup = [&](const wgpu::Buffer& records, std::uint64_t bytes, const char* label) {
            std::array<wgpu::BindGroupEntry, 4> entries{};
            entries[0].binding = 0;
            entries[0].buffer = objectUniforms;
            entries[0].size = sizeof(ObjectUniforms);
            entries[1].binding = 1;
            entries[1].buffer = records;
            entries[1].size = bytes;
            entries[2].binding = 2;
            entries[2].buffer = state.deformers;
            entries[2].size = sizeof(ProceduralUniforms);
            entries[3].binding = 3;
            entries[3].buffer = fieldBlock;
            entries[3].size = FieldUniforms::kBufferSize;
            wgpu::BindGroupDescriptor desc{};
            desc.label = label;
            desc.layout = objectLayout;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            return device.CreateBindGroup(&desc);
        };
        state.group = drawGroup(state.instances, state.instanceBytes, "procedural-object-group");
        state.groupLive = nullptr;
        state.computeGroup = nullptr;
        if (state.live) {
            state.groupLive = drawGroup(state.live, state.liveBytes, "procedural-object-group-live");
            std::array<wgpu::BindGroupEntry, 4> entries{};
            entries[0].binding = 0;
            entries[0].buffer = state.effectorUniforms;
            entries[0].size = sizeof(EffectorPassUniforms);
            entries[1].binding = 1;
            entries[1].buffer = state.instances;
            entries[1].size = state.instanceBytes;
            entries[2].binding = 2;
            entries[2].buffer = state.live;
            entries[2].size = state.liveBytes;
            entries[3].binding = 3;
            entries[3].buffer = fieldBlock;
            entries[3].size = FieldUniforms::kBufferSize;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "procedural-effector-group";
            desc.layout = computeLayout;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            state.computeGroup = device.CreateBindGroup(&desc);
        }
    }
}

void ProceduralRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.effectorTimer) {
        const double ms = im.effectorTimer->collect();
        if (ms >= 0.0) {
            im.lastEffectorMs = ms;
        }
    }
    stats_.effectorPassMs = im.passThisFrame ? im.lastEffectorMs : -1.0;
}

void ProceduralRenderer::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                                const std::vector<glm::mat4>& objectMatrices, const FrameTime& time,
                                const FieldUniforms* fields) {
    const auto start = std::chrono::steady_clock::now();
    Impl& im = *impl_;
    stats_ = ProceduralStats{};
    im.items.clear();
    im.computeItems.clear();
    im.passThisFrame = false;
    if (!im.initialised) {
        return;
    }
    ++im.frame;
    // Harvest the previous frame's effector timing (its command buffer was submitted by now).
    collectTimings();
    const auto& queue = im.context.queue();
    std::uint32_t slot = 0;
    for (std::size_t i = 0; i < scene.procedurals.size(); ++i) {
        const auto& object = scene.procedurals[i];
        if (!object.visible || object.instances.empty() || object.meshHash == 0) {
            continue;
        }
        if (slot >= kMaxProceduralObjects) {
            if (!im.warnedLimit) {
                log::warn("more than {} visible procedural objects; extra objects skipped", kMaxProceduralObjects);
                im.warnedLimit = true;
            }
            break;
        }
        const Impl::CachedMesh* mesh = im.ensureMesh(object);
        if (mesh == nullptr) {
            continue;
        }
        // ---- effectors: the usable ones (enabled, field bound to a slot), in order ----
        EffectorPassUniforms eff{};
        std::uint32_t effectorCount = 0;
        if (fields != nullptr) {
            for (const auto& e : object.effectors) {
                if (effectorCount >= static_cast<std::uint32_t>(spatial::kMaxEffectors)) {
                    break;
                }
                if (!e.enabled || e.op == spatial::EffectorOp::Velocity || e.op == spatial::EffectorOp::Attribute) {
                    continue;
                }
                const int fieldSlot = fields->slotOf(e.field);
                if (fieldSlot < 0) {
                    continue;
                }
                spatial::EffectorGpu& g = eff.effectors[effectorCount++];
                g.op = static_cast<std::uint32_t>(e.op);
                g.blend = static_cast<std::uint32_t>(e.blend);
                g.fieldSlot = fieldSlot;
                g.strength = e.strength;
                g.axisWeight = glm::vec4(e.axis, e.weight);
                g.scaleAxisPad = glm::vec4(e.scaleAxis, 0.0f);
            }
        }
        const bool usesLive = effectorCount > 0;

        // Per-object state keyed by name; a duplicate name in the same frame gets its index appended.
        std::string key = object.name;
        if (auto existing = im.objects.find(key); existing != im.objects.end() && existing->second.lastUsed == im.frame) {
            key += "#" + std::to_string(i);
        }
        Impl::ObjectState& state = im.objects[key];
        state.lastUsed = im.frame;
        const std::uint64_t instanceBytes = static_cast<std::uint64_t>(object.instances.size()) * kInstanceStride;
        im.ensureObjectBuffers(state, instanceBytes, usesLive);
        state.usesLive = usesLive;
        if (state.structureVersion != object.structureVersion || state.uploadedCount != object.instances.size()) {
            queue.WriteBuffer(state.instances, 0, object.instances.data(), instanceBytes);
            state.structureVersion = object.structureVersion;
            state.uploadedCount = object.instances.size();
            ++stats_.uploads;
        }
        const glm::mat4 model = i < objectMatrices.size() ? objectMatrices[i] : glm::mat4(1.0f);
        if (usesLive) {
            eff.objectToWorld = model;
            eff.worldToObjectRotation = glm::mat4(glm::transpose(rotationOf(model)));
            eff.info = glm::uvec4(static_cast<std::uint32_t>(object.instances.size()), effectorCount, 0u, 0u);
            queue.WriteBuffer(state.effectorUniforms, 0, &eff, sizeof(eff));
            im.computeItems.push_back(Impl::ComputeItem{&state, static_cast<std::uint32_t>(object.instances.size())});
            ++stats_.effectorObjects;
            stats_.effectorInstances += object.instances.size();
            stats_.effectors += effectorCount;
        }

        // ---- deformer/time block (every frame) ----
        ProceduralUniforms u{};
        const std::size_t deformerCount = std::min<std::size_t>(object.deformers.size(), scene::kMaxDeformers);
        std::uint32_t enabled = 0;
        for (std::size_t d = 0; d < static_cast<std::size_t>(scene::kMaxDeformers); ++d) {
            if (d < deformerCount) {
                const auto& deformer = object.deformers[d];
                int fieldSlot = -1;
                if (deformer.kind == scene::DeformerKind::Field && fields != nullptr) {
                    fieldSlot = fields->slotOf(deformer.field);
                    if (deformer.enabled && fieldSlot >= 0) {
                        ++stats_.fieldDeformers;
                    }
                }
                u.deformers[d] = packDeformer(deformer, fieldSlot);
                enabled += deformer.enabled ? 1u : 0u;
            } else {
                u.deformers[d].axisKind = glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);
            }
        }
        u.timeInfo = glm::vec4(static_cast<float>(time.renderTime), static_cast<float>(deformerCount),
                               1e-3f * mesh->radius, static_cast<float>(object.instances.size()));
        int emissiveSlot = -1;
        if (fields != nullptr && !object.emissiveField.empty() && object.emissiveFieldAmount != 0.0f) {
            emissiveSlot = fields->slotOf(object.emissiveField);
        }
        const bool isPoint = object.source.kind == scene::PrimitiveKind::Point;
        u.fieldInfo = glm::vec4(static_cast<float>(emissiveSlot), object.emissiveFieldAmount, isPoint ? 1.0f : 0.0f, 0.0f);
        queue.WriteBuffer(state.deformers, 0, &u, sizeof(u));
        if (isPoint) {
            ++stats_.pointObjects;
        }

        // ---- object slot: exactly the entity ObjectUniforms fields ----
        const auto& m = object.material;
        ObjectUniforms obj{};
        obj.model = model;
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        obj.baseColor = glm::vec4(m.baseColor, m.opacity);
        obj.emissive = glm::vec4(m.emissiveColor, m.emissiveIntensity);
        obj.material = glm::vec4(m.roughness, m.metallic, m.normalScale, m.occlusionStrength);
        std::uint32_t mask = 0;
        auto has = [&](const scene::TextureRef& ref) {
            return ref.valid() && ref.texture < scene.textures.size() && !scene.textures[ref.texture].isHdr();
        };
        if (has(m.baseColorTexture)) mask |= 1;
        if (has(m.metallicRoughnessTexture)) mask |= 2;
        if (has(m.normalTexture)) mask |= 4;
        if (has(m.emissiveTexture)) mask |= 8;
        if (has(m.occlusionTexture)) mask |= 16;
        // Blend materials are drawn opaque in this phase: alpha mode 0 keeps the shader's opaque path.
        const float alphaMode = m.alphaMode == scene::AlphaMode::Mask ? 1.0f : 0.0f;
        obj.flags = glm::vec4(alphaMode, m.alphaCutoff, m.unlit ? 1.0f : 0.0f, static_cast<float>(mask));
        const std::uint32_t offset = slot * kObjectStride;
        std::memcpy(im.staging.data() + offset, &obj, sizeof(obj));

        im.items.push_back(Impl::DrawItem{i, mesh, &state, offset, static_cast<std::uint32_t>(object.instances.size())});
        ++stats_.objects;
        stats_.sourceVertices += mesh->vertexCount;
        stats_.sourceTriangles += mesh->indexCount / 3;
        stats_.instances += object.instances.size();
        stats_.logicalTriangles += static_cast<std::uint64_t>(mesh->indexCount / 3) * object.instances.size();
        stats_.instanceBufferBytes += state.instanceBytes + (usesLive ? state.liveBytes : 0);
        stats_.deformers += enabled;
        ++slot;
    }
    if (slot > 0) {
        queue.WriteBuffer(im.objectUniforms, 0, im.staging.data(), static_cast<std::size_t>(slot) * kObjectStride);
    }

    // ---- the effector pass: one compute pass, one dispatch per object with effectors ----
    if (!im.computeItems.empty()) {
        wgpu::ComputePassDescriptor desc{};
        desc.label = "procedural-effectors";
        desc.timestampWrites = im.effectorTimer->passWrites();
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&desc);
        cp.SetPipeline(im.effectorPipeline);
        for (const auto& item : im.computeItems) {
            cp.SetBindGroup(0, item.state->computeGroup);
            cp.DispatchWorkgroups((item.count + kEffectorWorkgroup - 1) / kEffectorWorkgroup);
        }
        cp.End();
        im.effectorTimer->resolve(encoder);
        im.passThisFrame = true;
        stats_.effectorPassMs = im.lastEffectorMs;
    }

    // ---- drop GPU state nobody used for cacheFrames_ frames ----
    for (auto it = im.meshes.begin(); it != im.meshes.end();) {
        it = it->second.lastUsed + cacheFrames_ < im.frame ? im.meshes.erase(it) : std::next(it);
    }
    for (auto it = im.objects.begin(); it != im.objects.end();) {
        it = it->second.lastUsed + cacheFrames_ < im.frame ? im.objects.erase(it) : std::next(it);
    }
    stats_.sourceMeshes = static_cast<std::uint32_t>(im.meshes.size());
    stats_.cpuUpdateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ProceduralRenderer::draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup) {
    Impl& im = *impl_;
    if (!im.initialised || im.items.empty()) {
        return;
    }
    for (const auto& item : im.items) {
        if (item.objectIndex >= scene.procedurals.size()) {
            continue;
        }
        const auto& object = scene.procedurals[item.objectIndex];
        const auto& material = object.material;
        // Point billboards face the camera by construction: never cull them.
        const bool twoSided = material.doubleSided || object.source.kind == scene::PrimitiveKind::Point;
        pass.SetPipeline(twoSided ? im.pipelineNoCull : im.pipelineCull);
        pass.SetBindGroup(1, item.state->usesLive ? item.state->groupLive : item.state->group, 1, &item.offset);
        pass.SetBindGroup(2, materialBindGroup(material));
        pass.SetVertexBuffer(0, item.mesh->vertices);
        pass.SetIndexBuffer(item.mesh->indices, wgpu::IndexFormat::Uint32);
        pass.DrawIndexed(item.mesh->indexCount, item.instanceCount);
    }
}

Result<std::vector<scene::InstanceRecord>> ProceduralRenderer::readInstanceRecords(const std::string& name) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    if (it == im.objects.end() || !it->second.instances) {
        return fail("procedural object '{}' has no GPU state", name);
    }
    const Impl::ObjectState& state = it->second;
    const wgpu::Buffer& source = state.usesLive ? state.live : state.instances;
    const std::uint64_t bytes = static_cast<std::uint64_t>(state.uploadedCount) * kInstanceStride;
    auto data = gpu::readBuffer(im.context, source, 0, bytes);
    if (!data) {
        return std::unexpected(data.error());
    }
    std::vector<scene::InstanceRecord> records(state.uploadedCount);
    std::memcpy(records.data(), data->data(), static_cast<std::size_t>(bytes));
    return records;
}

} // namespace avgen::rendering
