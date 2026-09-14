#include "rendering/sdf_renderer.hpp"

#include "rendering/field_uniforms.hpp"
#include "rendering/scene_renderer.hpp" // ObjectUniforms (the shared 512-byte slot layout)
#include "rendering/scene_targets.hpp"  // the five colour targets of the scene pass (ADR-035)

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "spatial/sdf.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

constexpr std::uint32_t kObjectStride = SdfRenderer::kObjectStride;
constexpr std::uint32_t kNodeStride = sizeof(spatial::SdfNodeGpu); // 112
static_assert(kNodeStride == 112);
static_assert(sizeof(ObjectUniforms) <= kObjectStride);
static_assert(sizeof(SdfObjectUniforms) <= kObjectStride);


// The NDC rectangle covered by the projected local AABB; the full screen when a corner is at or
// behind the eye plane. Returns false when the box is entirely off screen.
bool projectedRect(const glm::mat4& clipFromLocal, const glm::vec3& lo, const glm::vec3& hi, glm::vec4& rect) {
    glm::vec2 mn(std::numeric_limits<float>::max());
    glm::vec2 mx(-std::numeric_limits<float>::max());
    bool allBeyondFar = true;
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
        const glm::vec4 clip = clipFromLocal * glm::vec4(corner, 1.0f);
        if (clip.w <= 1e-5f) {
            rect = glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f);
            return true;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        mn = glm::min(mn, glm::vec2(ndc));
        mx = glm::max(mx, glm::vec2(ndc));
        if (ndc.z <= 1.0f) {
            allBeyondFar = false;
        }
    }
    if (allBeyondFar) {
        return false;
    }
    mn = glm::max(mn, glm::vec2(-1.0f));
    mx = glm::min(mx, glm::vec2(1.0f));
    if (!(mn.x < mx.x && mn.y < mx.y)) {
        return false;
    }
    rect = glm::vec4(mn, mx);
    return true;
}

ObjectUniforms objectUniformsFor(const scene::SdfObject& object, std::size_t objectId) {
    const auto& m = object.material;
    ObjectUniforms obj{};
    obj.model = object.transform.matrix();
    obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
    obj.baseColor = glm::vec4(m.baseColor, m.opacity);
    obj.emissive = glm::vec4(m.emissiveColor, m.emissiveIntensity);
    obj.material = glm::vec4(m.roughness, m.metallic, m.normalScale, m.occlusionStrength);
    // No UVs on either path: textures are never sampled (mask 0). Blend materials draw opaque.
    const float alphaMode = m.alphaMode == scene::AlphaMode::Mask ? 1.0f : 0.0f;
    obj.flags = glm::vec4(alphaMode, m.alphaCutoff, m.unlit ? 1.0f : 0.0f, 0.0f);
    obj.prevModel = obj.model; // SDF transforms are static within a frame; the camera supplies the motion
    // x = the ADR-030 `objectId` input; y = material id, z = bloom weight (ADR-035 targets).
    obj.ids = glm::vec4(static_cast<float>(scene::packPickId(scene::PickSpace::Sdf, objectId)),
                        static_cast<float>(objectId + 1), 1.0f, 0.0f);
    return obj;
}

} // namespace

struct SdfRenderer::Impl {
    struct MeshState {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;
        std::uint64_t meshHash = 0;
        std::uint64_t lastUsed = 0;
    };
    struct RaymarchItem {
        std::size_t objectIndex; // into scene.sdfs
        std::uint32_t offset;    // dynamic offset into both uniform buffers
    };
    struct MeshItem {
        std::size_t objectIndex;
        const MeshState* mesh;
        std::uint32_t offset;
    };

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {
        objectStaging.resize(static_cast<std::size_t>(kMaxObjects) * kObjectStride);
        sdfStaging.resize(static_cast<std::size_t>(kMaxObjects) * kObjectStride);
    }

    Result<wgpu::RenderPipeline> finish(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    Result<void> createRaymarchPipeline(const wgpu::ShaderModule& module);
    void ensureNodeBuffer(std::uint64_t bytes);
    void rebuildGroups();

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout materialLayout;
    wgpu::BindGroupLayout iblLayout;
    wgpu::BindGroupLayout sdfLayout;        // raymarch group 1 (this renderer's own)
    wgpu::BindGroupLayout meshObjectLayout; // mesh group 1 = SceneRenderer's entity object layout
    wgpu::PipelineLayout raymarchLayout;
    wgpu::RenderPipeline raymarchPipeline;
    wgpu::RenderPipeline raymarchDepthPipeline;
    wgpu::RenderPipeline raymarchShadowPipeline;
    wgpu::RenderPipeline meshCull;   // SceneRenderer's lit opaque pipelines (pbr.wgsl)
    wgpu::RenderPipeline meshNoCull;
    wgpu::Buffer objectUniforms;
    wgpu::Buffer sdfUniforms;
    wgpu::Buffer nodes;
    std::uint64_t nodeBytes = 0;
    wgpu::Buffer fieldBlock;
    wgpu::BindGroup sdfGroup;
    wgpu::BindGroup meshGroup;
    gpu::FrameTimeline* timeline = nullptr;
    std::vector<std::uint8_t> objectStaging;
    std::vector<std::uint8_t> sdfStaging;
    std::vector<spatial::SdfNodeGpu> nodeStaging;
    std::vector<spatial::SdfNodeGpu> packScratch;
    std::map<std::string, MeshState> meshes;
    std::vector<RaymarchItem> raymarchItems;
    std::vector<MeshItem> meshItems;
    std::set<std::string> warnedObjects;
    double lastRaymarchMs = -1.0;
    bool passThisFrame = false;
    std::uint64_t frame = 0;
    std::size_t cacheFrames = 120;
    bool initialised = false;
    bool warnedLimit = false;
};

SdfRenderer::SdfRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

SdfRenderer::~SdfRenderer() = default;

Result<void> SdfRenderer::init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                               const wgpu::BindGroupLayout& frameLayout, const wgpu::BindGroupLayout& objectLayout,
                               const wgpu::BindGroupLayout& materialLayout, const wgpu::BindGroupLayout& iblLayout,
                               wgpu::Buffer fieldBlock) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.depthFormat = depthFormat;
    im.frameLayout = frameLayout;
    im.meshObjectLayout = objectLayout; // SceneRenderer's entity group 1: its lit pipelines expect it
    im.materialLayout = materialLayout;
    im.iblLayout = iblLayout;
    im.fieldBlock = std::move(fieldBlock);
    if (!im.fieldBlock) {
        wgpu::BufferDescriptor desc{};
        desc.label = "sdf-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        im.fieldBlock = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        im.context.queue().WriteBuffer(im.fieldBlock, 0, &zero, sizeof(zero));
    }
    {
        // Raymarch group 1: 0 = ObjectUniforms (dynamic), 1 = SdfObjectUniforms (dynamic),
        // 2 = packed nodes (read-only storage), 3 = field block.
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(ObjectUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.hasDynamicOffset = true;
        entries[1].buffer.minBindingSize = sizeof(SdfObjectUniforms);
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].buffer.minBindingSize = kNodeStride;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.minBindingSize = FieldUniforms::kBufferSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "sdf-object-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.sdfLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout, im.sdfLayout, materialLayout, iblLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "sdf-raymarch-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.raymarchLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(kMaxObjects) * kObjectStride;
        desc.label = "sdf-object-uniforms";
        im.objectUniforms = device.CreateBuffer(&desc);
        desc.label = "sdf-march-uniforms";
        im.sdfUniforms = device.CreateBuffer(&desc);
    }
    im.ensureNodeBuffer(kNodeStride * 128);
    auto raymarch = im.shaders.load("sdf_raymarch.wgsl");
    if (!raymarch) {
        return std::unexpected(raymarch.error());
    }
    if (auto r = im.createRaymarchPipeline(*raymarch); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

void SdfRenderer::setMeshPipelines(const wgpu::RenderPipeline& cull, const wgpu::RenderPipeline& noCull) {
    impl_->meshCull = cull;
    impl_->meshNoCull = noCull;
}

Result<void> SdfRenderer::reload() {
    Impl& im = *impl_;
    auto raymarch = im.shaders.load("sdf_raymarch.wgsl");
    if (!raymarch) {
        return std::unexpected(raymarch.error());
    }
    return im.createRaymarchPipeline(*raymarch);
}

Result<wgpu::RenderPipeline> SdfRenderer::Impl::finish(const wgpu::RenderPipelineDescriptor& desc, const char* label) {
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

Result<void> SdfRenderer::Impl::createRaymarchPipeline(const wgpu::ShaderModule& module) {
    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, colorFormat, nullptr);
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_sdf";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = colorTargets.data();
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "sdf-raymarch";
    desc.layout = raymarchLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_sdf";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    auto pipeline = finish(desc, "sdf-raymarch");
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    // The depth-only variant (ADR-034): the same quad and the same tree at a quarter of the steps,
    // so a raymarched SDF appears in the depth prepass and in the shadow maps.
    wgpu::FragmentState depthFragment{};
    depthFragment.module = module;
    depthFragment.entryPoint = "fs_sdf_depth";
    depthFragment.targetCount = 0;
    depthFragment.targets = nullptr;
    wgpu::DepthStencilState depthOnly = depth;
    depthOnly.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor depthDesc = desc;
    depthDesc.label = "sdf-raymarch-depth";
    depthDesc.fragment = &depthFragment;
    depthDesc.depthStencil = &depthOnly;
    auto depthPipeline = finish(depthDesc, "sdf-raymarch-depth");
    if (!depthPipeline) {
        return std::unexpected(depthPipeline.error());
    }
    depthFragment.entryPoint = "fs_sdf_shadow";
    depthDesc.label = "sdf-raymarch-shadow";
    auto shadowPipeline = finish(depthDesc, "sdf-raymarch-shadow");
    if (!shadowPipeline) {
        return std::unexpected(shadowPipeline.error());
    }
    raymarchPipeline = *pipeline;
    raymarchDepthPipeline = *depthPipeline;
    raymarchShadowPipeline = *shadowPipeline;
    return {};
}

void SdfRenderer::Impl::ensureNodeBuffer(std::uint64_t bytes) {
    if (nodes && nodeBytes >= bytes) {
        return;
    }
    wgpu::BufferDescriptor desc{};
    desc.label = "sdf-nodes";
    desc.size = std::max<std::uint64_t>(bytes, kNodeStride);
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    nodes = context.device().CreateBuffer(&desc);
    nodeBytes = desc.size;
    rebuildGroups();
}

void SdfRenderer::Impl::rebuildGroups() {
    const auto& device = context.device();
    {
        std::array<wgpu::BindGroupEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].buffer = objectUniforms;
        entries[0].size = sizeof(ObjectUniforms);
        entries[1].binding = 1;
        entries[1].buffer = sdfUniforms;
        entries[1].size = sizeof(SdfObjectUniforms);
        entries[2].binding = 2;
        entries[2].buffer = nodes;
        entries[2].size = nodeBytes;
        entries[3].binding = 3;
        entries[3].buffer = fieldBlock;
        entries[3].size = FieldUniforms::kBufferSize;
        wgpu::BindGroupDescriptor desc{};
        desc.label = "sdf-object-group";
        desc.layout = sdfLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        sdfGroup = device.CreateBindGroup(&desc);
    }
    if (!meshGroup) {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = objectUniforms;
        entry.size = sizeof(ObjectUniforms);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "sdf-mesh-object-group";
        desc.layout = meshObjectLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        meshGroup = device.CreateBindGroup(&desc);
    }
}

void SdfRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void SdfRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double ms = im.timeline->msFor("sdf");
        if (ms >= 0.0) {
            im.lastRaymarchMs = ms;
        }
    }
    stats_.raymarchMs = im.passThisFrame ? im.lastRaymarchMs : -1.0;
}

bool SdfRenderer::hasRaymarchWork() const {
    return !impl_->raymarchItems.empty();
}

void SdfRenderer::update(const scene::Scene& scene, const FrameTime& time, const glm::mat4& viewProj,
                         const FieldUniforms* fields) {
    const auto start = std::chrono::steady_clock::now();
    Impl& im = *impl_;
    stats_ = SdfStats{};
    im.raymarchItems.clear();
    im.meshItems.clear();
    im.nodeStaging.clear();
    im.passThisFrame = false;
    if (!im.initialised) {
        return;
    }
    ++im.frame;
    collectTimings();
    const auto& queue = im.context.queue();
    std::uint32_t slot = 0;
    for (std::size_t i = 0; i < scene.sdfs.size(); ++i) {
        const scene::SdfObject& object = scene.sdfs[i];
        if (!object.visible) {
            continue;
        }
        if (slot >= kMaxObjects) {
            if (!im.warnedLimit) {
                log::warn("more than {} visible sdf objects; extra objects skipped", kMaxObjects);
                im.warnedLimit = true;
            }
            break;
        }
        if (auto ok = object.validate(); !ok) {
            if (im.warnedObjects.insert(object.name).second) {
                log::warn("sdf '{}' not drawn: {}", object.name, ok.error().message);
            }
            continue;
        }
        const ObjectUniforms obj = objectUniformsFor(object, i);
        const std::uint32_t offset = slot * kObjectStride;

        if (object.renderMode == scene::SdfRenderMode::Raymarch) {
            glm::vec4 rect{};
            if (!projectedRect(viewProj * obj.model, object.boundsMin, object.boundsMax, rect)) {
                continue; // off screen
            }
            const int count = spatial::packSdfTree(object.tree, im.packScratch, &scene.fields);
            if (count <= 0 || count > 2 * spatial::kMaxSdfNodes) {
                continue;
            }
            // packSdfTree resolves DisplaceField references to FieldSet indices; the GPU slot of
            // an enabled, uploaded field is the same index. Disabled, missing or unbound: -1.
            for (auto& g : im.packScratch) {
                if (g.kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::DisplaceField) && g.fieldSlot >= 0) {
                    int gpuSlot = -1;
                    if (fields != nullptr && static_cast<std::size_t>(g.fieldSlot) < scene.fields.fields.size()) {
                        gpuSlot = fields->slotOf(scene.fields.fields[static_cast<std::size_t>(g.fieldSlot)].name);
                    }
                    g.fieldSlot = gpuSlot;
                }
            }
            SdfObjectUniforms u{};
            u.worldToLocal = glm::inverse(obj.model);
            u.boundsMin = glm::vec4(object.boundsMin, 0.0f);
            u.boundsMax = glm::vec4(object.boundsMax, 0.0f);
            // w is the shadow march's step budget (ADR-034 / §16). It was 0 and the shader derived
            // the count as `maxSteps / 4`, so `QualitySettings::sdfShadowSteps` -- which the tier
            // table sets to 16, 24, 32 and 48 -- was read by nothing. Found by the §15 parity audit
            // grepping every quality field for a reader.
            u.info = glm::uvec4(static_cast<std::uint32_t>(im.nodeStaging.size()), static_cast<std::uint32_t>(count),
                                static_cast<std::uint32_t>(std::clamp(object.maxSteps, 1, 1024)),
                                std::clamp(sdfShadowSteps_, 8u, 1024u));
            u.march = glm::vec4(object.epsilon, object.stepScale, object.normalEpsilon, static_cast<float>(time.renderTime));
            u.rect = rect;
            im.nodeStaging.insert(im.nodeStaging.end(), im.packScratch.begin(), im.packScratch.end());
            std::memcpy(im.sdfStaging.data() + offset, &u, sizeof(u));
            std::memcpy(im.objectStaging.data() + offset, &obj, sizeof(obj));
            im.raymarchItems.push_back(Impl::RaymarchItem{i, offset});
            ++stats_.raymarchObjects;
            stats_.packedNodes += static_cast<std::uint32_t>(count);
        } else {
            if (!object.mesh.valid() || object.mesh.indices.empty()) {
                continue; // not rebuilt yet, or the surface lies outside the bounds
            }
            std::string key = object.name;
            if (auto existing = im.meshes.find(key); existing != im.meshes.end() && existing->second.lastUsed == im.frame) {
                key += "#" + std::to_string(i);
            }
            Impl::MeshState& state = im.meshes[key];
            state.lastUsed = im.frame;
            const std::uint64_t hash = object.meshHash != 0 ? object.meshHash : object.structuralHash();
            if (!state.vertices || state.meshHash != hash) {
                const auto& device = im.context.device();
                wgpu::BufferDescriptor vdesc{};
                vdesc.label = "sdf-mesh-vertices";
                vdesc.size = object.mesh.vertices.size() * sizeof(scene::Vertex);
                vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
                state.vertices = device.CreateBuffer(&vdesc);
                queue.WriteBuffer(state.vertices, 0, object.mesh.vertices.data(), vdesc.size);
                wgpu::BufferDescriptor idesc{};
                idesc.label = "sdf-mesh-indices";
                idesc.size = object.mesh.indices.size() * sizeof(std::uint32_t);
                idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
                state.indices = device.CreateBuffer(&idesc);
                queue.WriteBuffer(state.indices, 0, object.mesh.indices.data(), idesc.size);
                state.indexCount = static_cast<std::uint32_t>(object.mesh.indices.size());
                state.meshHash = hash;
                ++stats_.meshUploads;
            }
            std::memcpy(im.objectStaging.data() + offset, &obj, sizeof(obj));
            im.meshItems.push_back(Impl::MeshItem{i, &state, offset});
            ++stats_.meshObjects;
            stats_.meshTriangles += state.indexCount / 3;
        }
        ++stats_.objects;
        ++slot;
    }
    if (slot > 0) {
        queue.WriteBuffer(im.objectUniforms, 0, im.objectStaging.data(), static_cast<std::size_t>(slot) * kObjectStride);
    }
    if (!im.raymarchItems.empty()) {
        queue.WriteBuffer(im.sdfUniforms, 0, im.sdfStaging.data(), static_cast<std::size_t>(slot) * kObjectStride);
        const std::uint64_t bytes = static_cast<std::uint64_t>(im.nodeStaging.size()) * kNodeStride;
        im.ensureNodeBuffer(bytes);
        queue.WriteBuffer(im.nodes, 0, im.nodeStaging.data(), bytes);
    }
    for (auto it = im.meshes.begin(); it != im.meshes.end();) {
        it = it->second.lastUsed + im.cacheFrames < im.frame ? im.meshes.erase(it) : std::next(it);
    }
    stats_.cpuUpdateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void SdfRenderer::drawMeshes(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                             const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                             const wgpu::RenderPipeline* depthOnlyPipeline) {
    Impl& im = *impl_;
    if (!im.initialised || im.meshItems.empty() || !im.meshCull || !im.meshNoCull) {
        return;
    }
    for (const auto& item : im.meshItems) {
        if (item.objectIndex >= scene.sdfs.size()) {
            continue;
        }
        const auto& material = scene.sdfs[item.objectIndex].material;
        pass.SetPipeline(depthOnlyPipeline != nullptr ? *depthOnlyPipeline
                                                      : (material.doubleSided ? im.meshNoCull : im.meshCull));
        pass.SetBindGroup(1, im.meshGroup, 1, &item.offset);
        pass.SetBindGroup(2, materialBindGroup(material));
        pass.SetVertexBuffer(0, item.mesh->vertices);
        pass.SetIndexBuffer(item.mesh->indices, wgpu::IndexFormat::Uint32);
        pass.DrawIndexed(item.mesh->indexCount);
    }
}

void SdfRenderer::encodeRaymarchPass(wgpu::CommandEncoder& encoder, const wgpu::TextureView& color,
                                     const wgpu::TextureView& depth, const wgpu::BindGroup& frameBindGroup,
                                     const wgpu::BindGroup& iblBindGroup, const scene::Scene& scene,
                                     const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                                     const wgpu::TextureView* auxTargets, std::uint32_t auxCount) {
    Impl& im = *impl_;
    if (!im.initialised || im.raymarchItems.empty()) {
        return;
    }
    std::array<wgpu::RenderPassColorAttachment, kSceneTargetCount> attachments{};
    attachments[0].view = color;
    attachments[0].loadOp = wgpu::LoadOp::Load;
    attachments[0].storeOp = wgpu::StoreOp::Store;
    const std::uint32_t count = 1 + std::min<std::uint32_t>(auxCount, kSceneTargetCount - 1);
    for (std::uint32_t i = 1; i < count; ++i) {
        attachments[i].view = auxTargets[i - 1];
        attachments[i].loadOp = wgpu::LoadOp::Load;
        attachments[i].storeOp = wgpu::StoreOp::Store;
    }
    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depth;
    depthAttachment.depthLoadOp = wgpu::LoadOp::Load;
    depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor desc{};
    desc.label = "sdf-raymarch-pass";
    desc.colorAttachmentCount = count;
    desc.colorAttachments = attachments.data();
    desc.depthStencilAttachment = &depthAttachment;
    desc.timestampWrites = im.timeline != nullptr ? im.timeline->mark("sdf") : nullptr;
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
    pass.SetPipeline(im.raymarchPipeline);
    pass.SetBindGroup(0, frameBindGroup);
    pass.SetBindGroup(3, iblBindGroup);
    for (const auto& item : im.raymarchItems) {
        if (item.objectIndex >= scene.sdfs.size()) {
            continue;
        }
        const std::array<std::uint32_t, 2> offsets = {item.offset, item.offset};
        pass.SetBindGroup(1, im.sdfGroup, offsets.size(), offsets.data());
        pass.SetBindGroup(2, materialBindGroup(scene.sdfs[item.objectIndex].material));
        pass.Draw(6);
    }
    pass.End();
    im.passThisFrame = true;
    stats_.raymarchMs = im.lastRaymarchMs;
}

void SdfRenderer::drawRaymarchDepth(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                                    const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                                    bool reducedSteps) {
    Impl& im = *impl_;
    if (!im.initialised || im.raymarchItems.empty()) {
        return;
    }
    pass.SetPipeline(reducedSteps ? im.raymarchShadowPipeline : im.raymarchDepthPipeline);
    for (const auto& item : im.raymarchItems) {
        if (item.objectIndex >= scene.sdfs.size()) {
            continue;
        }
        const std::array<std::uint32_t, 2> offsets = {item.offset, item.offset};
        pass.SetBindGroup(1, im.sdfGroup, offsets.size(), offsets.data());
        pass.SetBindGroup(2, materialBindGroup(scene.sdfs[item.objectIndex].material));
        pass.Draw(6);
    }
}

} // namespace avgen::rendering
