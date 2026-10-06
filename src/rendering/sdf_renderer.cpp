#include "rendering/sdf_renderer.hpp"
#include "gpu/resource_stats.hpp"
#include "rendering/toon_pack.hpp"

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
#include <memory>
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
    // ADR-903: the owning node's emissiveBoost, applied after the program by the shared shading.
    obj.emission = glm::vec4(object.emissionGain, 0.0f, 0.0f, 0.0f);
    {
        const auto toon = packToon(m.toon); // ADR-1071
        obj.toon0 = toon[0];
        obj.toon1 = toon[1];
        obj.toon2 = toon[2];
    }
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
    // ADR-1003: the three raymarch pipelines of one module (the interpreter's, or a compiled tree's).
    struct Pipelines {
        wgpu::RenderPipeline lit;
        wgpu::RenderPipeline depth;
        wgpu::RenderPipeline shadow;
        bool failed = false;
        // ADR-1102: pipelines still being created on Dawn's worker threads. Usable once 0 and not failed.
        int pending = 0;
        std::chrono::steady_clock::time_point started{};
        std::string name;
    };
    struct RaymarchItem {
        std::size_t objectIndex; // into scene.sdfs
        std::uint32_t offset;    // dynamic offset into both uniform buffers
        const Pipelines* compiled = nullptr; // null: the interpreter's pipelines
        // ADR-1160: false for an object whose bounds are off the camera's screen. It is still marched into the
        // shadow maps (a caster out of frame still throws its shadow into frame), but the lit pass and the
        // depth prepass skip it.
        bool onCamera = true;
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
    Result<Pipelines> buildPipelines(const wgpu::ShaderModule& module);
    // ADR-1102: the same three pipelines, created asynchronously into `slot` (kept alive by the shared pointer).
    void buildPipelinesAsync(const wgpu::ShaderModule& module, const std::shared_ptr<Pipelines>& slot);
    const Pipelines* compiledPipelines(const scene::SdfObject& object, const spatial::FieldSet* fields);
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
    // ADR-1002: step statistics. The lit pass atomically accumulates into `stats` (cleared before the
    // pass); a copy lands in one of three MapRead slots and is read a few frames later.
    static constexpr std::uint64_t kStatsBytes = 32; // sumSteps, maxSteps, rays, hits, exhausted, pad x3
    struct StatsSlot {
        wgpu::Buffer read;
        wgpu::Future mapFuture{};
        bool inFlight = false;
        bool ready = false;
        bool failed = false;
    };
    wgpu::Buffer stats;
    std::array<StatsSlot, 3> statsSlots;
    std::size_t nextStatsSlot = 0;
    int copiedStatsSlot = -1;
    SdfStats lastStepStats;
    wgpu::BindGroup sdfGroup;
    wgpu::BindGroup meshGroup;
    // ADR-703: the entity object layout's binding 2 (FXL's per-entity effect records). A meshed SDF
    // is drawn with the entity lit pipelines, whose fragment stage declares it, but it never carries
    // effect lanes (its `fxA` is zero, so the shader never reads the buffer) -- one neutral record
    // satisfies the layout without coupling this renderer to the scene renderer's growing buffer.
    wgpu::Buffer neutralEntityFx;
    gpu::FrameTimeline* timeline = nullptr;
    std::vector<std::uint8_t> objectStaging;
    std::vector<std::uint8_t> sdfStaging;
    std::vector<spatial::SdfNodeGpu> nodeStaging;
    std::vector<spatial::SdfNodeGpu> packScratch;
    std::map<std::string, MeshState> meshes;
    std::vector<RaymarchItem> raymarchItems;
    std::vector<MeshItem> meshItems;
    std::set<std::string> warnedObjects;
    // ADR-1003: compiled variants by spatial::sdfCompileKey, and the resolved pass source they splice.
    std::map<std::uint64_t, std::shared_ptr<Pipelines>> compiledVariants;
    bool asyncCompile = false; // ADR-1102: off by default (offline renders and tests compile before drawing)
    bool prewarm = true;       // ADR-1102: every compile-flagged object compiled as soon as the scene has it
    double pieceSeconds = 0.0; // the frame's render time, for the compile log
    std::string raymarchSource;
    std::vector<spatial::SdfNodeGpu> compileScratch;
    std::uint32_t compilesThisFrame = 0;
    double lastRaymarchMs = -1.0;
    bool passThisFrame = false;
    std::uint64_t frame = 0;
    std::size_t cacheFrames = 120;
    bool initialised = false;
    bool warnedLimit = false;
    // ADR-1165: the low-resolution shadow side. One Depth24Plus array (a layer per shadow view), a 2D view of each
    // layer (the attachment, and the composite's texture), and a composite bind group per layer.
    wgpu::Texture lowShadow;
    std::uint32_t lowShadowSize = 0;
    std::uint32_t lowShadowLayers = 0;
    std::vector<wgpu::TextureView> lowShadowViews;
    std::vector<wgpu::BindGroup> lowShadowGroups;
    wgpu::BindGroupLayout compositeLayout;
    wgpu::RenderPipeline compositePipeline;
    wgpu::Buffer compositeUniforms;
    std::uint32_t compositeFullSize = 0;
    Result<void> createCompositePipeline();
};

SdfRenderer::SdfRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

SdfRenderer::~SdfRenderer() {
    // A pending MapAsync callback holds a raw StatsSlot pointer: let it complete while the slot exists.
    for (auto& slot : impl_->statsSlots) {
        if (slot.inFlight) {
            impl_->context.waitFor(slot.mapFuture, 2'000'000'000ull);
            slot.inFlight = false;
        }
    }
}

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
        // 2 = packed nodes (read-only storage), 3 = field block, 4 = step statistics (ADR-1002).
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
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
        entries[4].binding = 4;
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].buffer.type = wgpu::BufferBindingType::Storage;
        entries[4].buffer.minBindingSize = Impl::kStatsBytes;
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
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "sdf-step-stats";
        desc.size = Impl::kStatsBytes;
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst;
        im.stats = device.CreateBuffer(&desc);
        for (auto& slot : im.statsSlots) {
            wgpu::BufferDescriptor readDesc{};
            readDesc.label = "sdf-step-stats-read";
            readDesc.size = Impl::kStatsBytes;
            readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            slot.read = device.CreateBuffer(&readDesc);
        }
    }
    im.ensureNodeBuffer(kNodeStride * 128);
    auto raymarch = im.shaders.load("sdf_raymarch.wgsl");
    if (!raymarch) {
        return std::unexpected(raymarch.error());
    }
    if (auto r = im.createRaymarchPipeline(*raymarch); !r) {
        return r;
    }
    if (auto r = im.createCompositePipeline(); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

// ADR-1165: the low-resolution shadow composite (sdf_shadow_composite.wgsl).
Result<void> SdfRenderer::Impl::createCompositePipeline() {
    const auto& device = context.device();
    auto module = shaders.load("sdf_shadow_composite.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (!compositeLayout) {
        std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Depth;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = 16;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "sdf-shadow-composite-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        compositeLayout = device.CreateBindGroupLayout(&desc);
        wgpu::BufferDescriptor ub{};
        ub.label = "sdf-shadow-composite-uniforms";
        ub.size = 16;
        ub.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        compositeUniforms = device.CreateBuffer(&ub);
    }
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "sdf-shadow-composite";
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &compositeLayout;
    const wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_composite";
    fragment.targetCount = 0;
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Less; // the shadow passes' own test (the depth-only pipelines)
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "sdf-shadow-composite";
    desc.layout = layout;
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs_composite";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    context.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    compositePipeline = gpu::createRenderPipeline(device, &desc);
    std::string error;
    auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                       [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                           if (type != wgpu::ErrorType::NoError) {
                                               error = gpu::Context::toString(msg);
                                           }
                                       });
    context.waitFor(future);
    if (!error.empty() || !compositePipeline) {
        return fail("pipeline 'sdf-shadow-composite' creation failed: {}", error);
    }
    return {};
}

bool SdfRenderer::prepareLowResShadows(const scene::Scene& scene, std::uint32_t views, std::uint32_t atlasResolution) {
    Impl& im = *impl_;
    stats_.shadowMarchResolution = 0;
    const std::uint32_t scale = std::clamp(sdfShadowScale_, 1u, 8u);
    if (!im.initialised || !im.compositePipeline || scale <= 1 || views == 0 ||
        atlasResolution <= kMinLowResShadow) {
        return false;
    }
    bool anyCaster = false;
    for (const auto& item : im.raymarchItems) {
        anyCaster = anyCaster || (item.objectIndex < scene.sdfs.size() && scene.sdfs[item.objectIndex].castShadows);
    }
    if (!anyCaster) {
        return false;
    }
    const std::uint32_t size = std::max(kMinLowResShadow, atlasResolution / scale);
    if (size >= atlasResolution) {
        return false;
    }
    const auto& device = im.context.device();
    if (!im.lowShadow || im.lowShadowSize != size || im.lowShadowLayers < views) {
        wgpu::TextureDescriptor desc{};
        desc.label = "sdf-shadow-low";
        desc.size = {size, size, views};
        desc.format = im.depthFormat; // the shadow atlas's format (ShadowRenderer::kFormat): the shadow pipelines serve both
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        im.lowShadow = device.CreateTexture(&desc);
        im.lowShadowSize = size;
        im.lowShadowLayers = views;
        im.lowShadowViews.clear();
        im.lowShadowGroups.clear();
        for (std::uint32_t v = 0; v < views; ++v) {
            wgpu::TextureViewDescriptor vd{};
            vd.dimension = wgpu::TextureViewDimension::e2D;
            vd.baseArrayLayer = v;
            vd.arrayLayerCount = 1;
            vd.aspect = wgpu::TextureAspect::DepthOnly;
            im.lowShadowViews.push_back(im.lowShadow.CreateView(&vd));
            std::array<wgpu::BindGroupEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].textureView = im.lowShadowViews.back();
            entries[1].binding = 1;
            entries[1].buffer = im.compositeUniforms;
            entries[1].size = 16;
            wgpu::BindGroupDescriptor gd{};
            gd.label = "sdf-shadow-composite";
            gd.layout = im.compositeLayout;
            gd.entryCount = entries.size();
            gd.entries = entries.data();
            im.lowShadowGroups.push_back(device.CreateBindGroup(&gd));
        }
    }
    if (im.compositeFullSize != atlasResolution) {
        const std::array<float, 4> full{static_cast<float>(atlasResolution), static_cast<float>(atlasResolution), 0.0f,
                                        0.0f};
        im.context.queue().WriteBuffer(im.compositeUniforms, 0, full.data(), sizeof(full));
        im.compositeFullSize = atlasResolution;
    }
    stats_.shadowMarchResolution = size;
    return true;
}

const wgpu::TextureView& SdfRenderer::lowResShadowLayer(std::uint32_t view) const {
    return impl_->lowShadowViews.at(view);
}

void SdfRenderer::drawShadowComposite(wgpu::RenderPassEncoder& pass, std::uint32_t view) {
    Impl& im = *impl_;
    if (view >= im.lowShadowGroups.size()) {
        return;
    }
    pass.SetPipeline(im.compositePipeline);
    pass.SetBindGroup(0, im.lowShadowGroups[view]);
    pass.Draw(3);
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
    if (auto r = im.createRaymarchPipeline(*raymarch); !r) {
        return r;
    }
    return im.createCompositePipeline();
}

Result<wgpu::RenderPipeline> SdfRenderer::Impl::finish(const wgpu::RenderPipelineDescriptor& desc, const char* label) {
    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &desc);
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

Result<SdfRenderer::Impl::Pipelines> SdfRenderer::Impl::buildPipelines(const wgpu::ShaderModule& module) {
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
    depthDesc.vertex.entryPoint = "vs_sdf_shadow"; // ADR-1160: the view's own rect, not the camera's
    auto shadowPipeline = finish(depthDesc, "sdf-raymarch-shadow");
    if (!shadowPipeline) {
        return std::unexpected(shadowPipeline.error());
    }
    Pipelines out;
    out.lit = *pipeline;
    out.depth = *depthPipeline;
    out.shadow = *shadowPipeline;
    return out;
}

void SdfRenderer::Impl::buildPipelinesAsync(const wgpu::ShaderModule& module, const std::shared_ptr<Pipelines>& slot) {
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
    wgpu::FragmentState depthFragment{};
    depthFragment.module = module;
    depthFragment.entryPoint = "fs_sdf_depth";
    wgpu::DepthStencilState depthOnly = depth;
    depthOnly.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor depthDesc = desc;
    depthDesc.label = "sdf-raymarch-depth";
    depthDesc.fragment = &depthFragment;
    depthDesc.depthStencil = &depthOnly;
    wgpu::FragmentState shadowFragment = depthFragment;
    shadowFragment.entryPoint = "fs_sdf_shadow";
    wgpu::RenderPipelineDescriptor shadowDesc = depthDesc;
    shadowDesc.label = "sdf-raymarch-shadow";
    shadowDesc.fragment = &shadowFragment;
    shadowDesc.vertex.entryPoint = "vs_sdf_shadow"; // ADR-1160
    slot->pending = 3;
    const auto& device = context.device();
    const auto make = [&](const wgpu::RenderPipelineDescriptor& d, wgpu::RenderPipeline Pipelines::* member) {
        static_cast<void>(gpu::pipelineCountersNoteAsync());
        device.CreateRenderPipelineAsync(
            &d, wgpu::CallbackMode::AllowProcessEvents,
            [slot, member](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView msg) {
                if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline) {
                    (*slot).*member = std::move(pipeline);
                } else {
                    slot->failed = true;
                    log::warn("sdf '{}': compiling the tree failed, drawing it interpreted: {}", slot->name,
                              gpu::Context::toString(msg));
                }
                if (--slot->pending == 0 && !slot->failed) {
                    log::info("sdf '{}': compiled tree variant ready after {:.1f} ms (on Dawn's workers)", slot->name,
                              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - slot->started)
                                  .count());
                }
            });
    };
    make(desc, &Pipelines::lit);
    make(depthDesc, &Pipelines::depth);
    make(shadowDesc, &Pipelines::shadow);
}

Result<void> SdfRenderer::Impl::createRaymarchPipeline(const wgpu::ShaderModule& module) {
    auto built = buildPipelines(module);
    if (!built) {
        return std::unexpected(built.error());
    }
    raymarchPipeline = built->lit;
    raymarchDepthPipeline = built->depth;
    raymarchShadowPipeline = built->shadow;
    compiledVariants.clear(); // a reload changes the pass source every variant was spliced into
    if (auto source = shaders.loadSource("sdf_raymarch.wgsl")) {
        raymarchSource = std::move(*source);
    }
    return {};
}

// ADR-1003: the compiled pipelines for this object's tree structure, building them on first use.
// Null when the object does not ask for compilation or its variant failed (the interpreter draws it).
const SdfRenderer::Impl::Pipelines* SdfRenderer::Impl::compiledPipelines(const scene::SdfObject& object,
                                                                         const spatial::FieldSet* fields) {
    if (!object.compile || raymarchSource.empty()) {
        return nullptr;
    }
    const std::uint64_t key = spatial::sdfCompileKey(object.tree);
    if (auto it = compiledVariants.find(key); it != compiledVariants.end()) {
        return it->second->failed || it->second->pending > 0 ? nullptr : it->second.get();
    }
    constexpr std::string_view kBegin = "// @@SDF_FIELD_BEGIN@@";
    constexpr std::string_view kEnd = "// @@SDF_FIELD_END@@";
    const auto b = raymarchSource.find(kBegin);
    const auto e = raymarchSource.find(kEnd);
    auto slotPtr = std::make_shared<Pipelines>();
    compiledVariants[key] = slotPtr;
    Pipelines& slot = *slotPtr;
    if (b == std::string::npos || e == std::string::npos || e < b) {
        slot.failed = true;
        log::warn("sdf '{}': the raymarch shader has no sdfField markers; drawing it interpreted", object.name);
        return nullptr;
    }
    const auto start = std::chrono::steady_clock::now();
    const std::string field = spatial::sdfCompileWgsl(object.tree, compileScratch, fields);
    const std::string source = raymarchSource.substr(0, b) + field + raymarchSource.substr(e + kEnd.size());
    auto module = shaders.compile(source, "sdf-raymarch-compiled");
    // ADR-1102: live, a tree the interpreter can also draw compiles on Dawn's workers and is drawn interpreted until
    // its pipelines exist -- no main-thread stall mid-performance. A tree the interpreter cannot draw (ADR-1005) still
    // compiles here and now, because drawing nothing would be worse than a hitch; so does every offline render.
    if (asyncCompile && module && object.tree.validate(spatial::SdfEvaluator::Interpreter)) {
        slot.name = object.name;
        slot.started = start;
        buildPipelinesAsync(*module, slotPtr);
        ++compilesThisFrame;
        return nullptr;
    }
    Result<Pipelines> built = module ? buildPipelines(*module) : Result<Pipelines>(std::unexpected(module.error()));
    ++compilesThisFrame;
    if (!built) {
        slot.failed = true;
        log::warn("sdf '{}': compiling the tree failed, drawing it interpreted: {}", object.name, built.error().message);
        return nullptr;
    }
    slot.lit = built->lit;
    slot.depth = built->depth;
    slot.shadow = built->shadow;
    log::info("sdf '{}': compiled tree variant {:016x} ({} node records) at t={:.2f} s in {:.1f} ms on the main thread",
              object.name, key, compileScratch.size(), pieceSeconds,
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return &slot;
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
        std::array<wgpu::BindGroupEntry, 5> entries{};
        entries[4].binding = 4;
        entries[4].buffer = stats;
        entries[4].size = kStatsBytes;
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
        if (!neutralEntityFx) {
            wgpu::BufferDescriptor bufferDesc{};
            bufferDesc.label = "sdf-neutral-entity-fx";
            bufferDesc.size = 256; // one zero record (world::EntityFxRecord)
            bufferDesc.usage = wgpu::BufferUsage::Storage;
            neutralEntityFx = device.CreateBuffer(&bufferDesc);
        }
        std::array<wgpu::BindGroupEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].buffer = objectUniforms;
        entries[0].size = sizeof(ObjectUniforms);
        entries[1].binding = 2;
        entries[1].buffer = neutralEntityFx;
        entries[1].size = 256;
        wgpu::BindGroupDescriptor desc{};
        desc.label = "sdf-mesh-object-group";
        desc.layout = meshObjectLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
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
    // ADR-1002: map the slot the last pass copied into, and read any slot that has landed.
    if (im.stats) {
        if (im.copiedStatsSlot >= 0) {
            Impl::StatsSlot& slot = im.statsSlots[static_cast<std::size_t>(im.copiedStatsSlot)];
            slot.inFlight = true;
            slot.ready = false;
            slot.failed = false;
            Impl::StatsSlot* raw = &slot;
            slot.mapFuture = slot.read.MapAsync(
                wgpu::MapMode::Read, 0, Impl::kStatsBytes, wgpu::CallbackMode::AllowProcessEvents,
                [](wgpu::MapAsyncStatus status, wgpu::StringView, Impl::StatsSlot* s) {
                    s->ready = status == wgpu::MapAsyncStatus::Success;
                    s->failed = status != wgpu::MapAsyncStatus::Success;
                },
                raw);
            im.copiedStatsSlot = -1;
        }
        im.context.processEvents();
        for (auto& slot : im.statsSlots) {
            if (!slot.inFlight) {
                continue;
            }
            if (slot.ready) {
                const auto* data = static_cast<const std::uint32_t*>(slot.read.GetConstMappedRange(0, Impl::kStatsBytes));
                if (data != nullptr) {
                    const double rays = data[2];
                    im.lastStepStats.sampledRays = data[2];
                    im.lastStepStats.avgSteps = rays > 0.0 ? data[0] / rays : 0.0;
                    im.lastStepStats.maxSteps = data[1];
                    im.lastStepStats.hitRatio = rays > 0.0 ? data[3] / rays : 0.0;
                    im.lastStepStats.exhaustedRatio = rays > 0.0 ? data[4] / rays : 0.0;
                }
                slot.read.Unmap();
                slot.inFlight = false;
                slot.ready = false;
            } else if (slot.failed) {
                slot.inFlight = false;
                slot.failed = false;
            }
        }
    }
    stats_.sampledRays = im.lastStepStats.sampledRays;
    stats_.avgSteps = im.lastStepStats.avgSteps;
    stats_.maxSteps = im.lastStepStats.maxSteps;
    stats_.hitRatio = im.lastStepStats.hitRatio;
    stats_.exhaustedRatio = im.lastStepStats.exhaustedRatio;
}

bool SdfRenderer::hasRaymarchWork() const {
    return !impl_->raymarchItems.empty();
}

std::size_t SdfRenderer::compiledVariantCount() const {
    std::size_t n = 0;
    for (const auto& [key, variant] : impl_->compiledVariants) {
        n += variant->failed || variant->pending > 0 ? 0 : 1;
    }
    return n;
}

std::size_t SdfRenderer::pendingCompiles() const {
    std::size_t n = 0;
    for (const auto& [key, variant] : impl_->compiledVariants) {
        n += variant->pending > 0 ? 1 : 0;
    }
    return n;
}

void SdfRenderer::setAsyncCompile(bool async) { impl_->asyncCompile = async; }

void SdfRenderer::setPrewarm(bool prewarm) { impl_->prewarm = prewarm; }

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
    // ADR-1160: whether any light draws a shadow map this frame. Only then is an off-screen caster worth marching.
    bool anyCaster = false;
    for (const scene::PunctualLight& light : scene.lights) {
        anyCaster = anyCaster || (light.enabled && light.castsShadow && light.shadowStrength > 0.0f);
    }
    im.pieceSeconds = time.renderTime;
    collectTimings();
    // ADR-1102: the pre-warm. Every object that asks for compilation is compiled as soon as the scene has it --
    // visible or not, on screen or not -- so the variant exists before the frame that first shows it. Live, these
    // compile on Dawn's workers (above); a map lookup per object per frame otherwise.
    for (const scene::SdfObject& object : scene.sdfs) {
        if (im.prewarm && object.compile && object.renderMode == scene::SdfRenderMode::Raymarch) {
            static_cast<void>(im.compiledPipelines(object, &scene.fields));
        }
    }
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

        bool drawnOnCamera = true; // ADR-1160: false for a shadow-only raymarch item
        if (object.renderMode == scene::SdfRenderMode::Raymarch) {
            glm::vec4 rect{};
            // ADR-1160: off the camera's screen is not out of the world -- the object can still cast into it. Its
            // camera passes are skipped (`onCamera`); the shadow pass projects its own rect per view.
            const bool onCamera = projectedRect(viewProj * obj.model, object.boundsMin, object.boundsMax, rect);
            if (!onCamera && (!object.castShadows || !anyCaster)) {
                continue; // off screen, and nothing to cast into
            }
            // ADR-1003: a compiled object uploads its per-node parameter table; the interpreter its program.
            const Impl::Pipelines* compiled = im.compiledPipelines(object, &scene.fields);
            int count = 0;
            if (compiled != nullptr) {
                spatial::sdfCompileTable(object.tree, im.packScratch, &scene.fields);
                count = static_cast<int>(im.packScratch.size());
            } else {
                // ADR-1005: a compiled tree may exceed the interpreter's stacks. If its compilation is
                // unavailable it is not drawn, rather than drawn wrong by an overflowing interpreter.
                if (object.compile) {
                    if (auto fits = object.tree.validate(spatial::SdfEvaluator::Interpreter); !fits) {
                        if (im.warnedObjects.insert(object.name).second) {
                            log::warn("sdf '{}' not drawn: its compiled variant is unavailable and {}", object.name,
                                      fits.error().message);
                        }
                        continue;
                    }
                }
                count = spatial::packSdfTree(object.tree, im.packScratch, &scene.fields);
                if (count <= 0 || count > 2 * spatial::kMaxSdfNodes) {
                    continue;
                }
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
            // ADR-1044: a compiled object's surfaces ride after its node records (p0 = colour, p1 = emission).
            if (compiled != nullptr && !object.surfaces.empty()) {
                u.surfaces = glm::uvec4(static_cast<std::uint32_t>(im.packScratch.size()),
                                        static_cast<std::uint32_t>(object.surfaces.size()), 0u, 0u);
                for (const scene::SdfObject::Surface& s : object.surfaces) {
                    spatial::SdfNodeGpu rec{};
                    rec.fieldSlot = -1;
                    rec.p0 = glm::vec4(s.color, 0.0f);
                    rec.p1 = glm::vec4(s.emission, s.staticAmount); // ADR-1054: w = static amount
                    rec.p2 = glm::vec4(s.edge, s.rim); // ADR-1047: edge colour multiplier; ADR-1052: w = rim multiplier
                    im.packScratch.push_back(rec);
                }
            }
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
            const scene::SdfLook& look = object.look;
            u.look0 = glm::vec4(look.aoStrength, look.aoDistance, look.edgeIntensity, look.edgeWidth);
            u.look1 = glm::vec4(look.edgeColor, object.maxDistance);
            u.look2 = glm::vec4(look.shadowStrength, look.shadowSoftness,
                                static_cast<float>(std::clamp(look.shadowSteps, 1, 256)), 1.0f);
            u.look3 = glm::vec4(look.shadowDirection, 0.0f);
            u.look4 = glm::vec4(look.edgePixels, look.edgeThreshold, std::max(look.edgeSoftness, 1e-3f),
                                std::max(look.rimPower, 0.1f));
            u.look5 = glm::vec4(look.rimColor, look.rimIntensity); // ADR-1052
            u.look6 = glm::vec4(std::max(look.staticCell, 1e-4f), std::max(look.staticRate, 0.0f), look.staticRoll, 0.0f);
            { // ADR-1055: the world wave, shared by every object
                const scene::PostSettings& ps = scene.post;
                const bool on = ps.waveIntensity > 0.0f || ps.waveEdgeTint > 0.0f || ps.waveTrail > 0.0f;
                const glm::vec3 dir = glm::length(ps.waveDirection) > 1e-6f ? glm::normalize(ps.waveDirection)
                                                                         : glm::vec3(0.0f, 0.0f, -1.0f);
                u.wave0 = glm::vec4(ps.waveOrigin, ps.waveProgress);
                u.wave1 = glm::vec4(dir, std::max(ps.waveWidth, 1e-3f));
                u.wave2 = glm::vec4(ps.waveColor, ps.waveIntensity);
                u.wave3 = glm::vec4(ps.waveTrailColor, std::clamp(ps.waveTrail, 0.0f, 1.0f));
                u.wave4 = glm::vec4(ps.waveHue, ps.waveHueSpan, std::clamp(ps.waveEdgeTint, 0.0f, 1.0f), on ? 1.0f : 0.0f);
            }
            im.nodeStaging.insert(im.nodeStaging.end(), im.packScratch.begin(), im.packScratch.end());
            std::memcpy(im.sdfStaging.data() + offset, &u, sizeof(u));
            std::memcpy(im.objectStaging.data() + offset, &obj, sizeof(obj));
            im.raymarchItems.push_back(Impl::RaymarchItem{i, offset, compiled, onCamera});
            drawnOnCamera = onCamera;
            if (onCamera) {
                ++stats_.raymarchObjects;
            } else {
                ++stats_.shadowOnlyObjects;
            }
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
        if (drawnOnCamera) {
            ++stats_.objects;
        }
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
    // ADR-1002: a free slot gets this pass's step statistics; with none free (the readback is behind)
    // the pass still accumulates, and nothing is copied.
    Impl::StatsSlot* statsSlot = nullptr;
    if (im.stats) {
        encoder.ClearBuffer(im.stats, 0, Impl::kStatsBytes);
        for (std::size_t k = 0; k < im.statsSlots.size(); ++k) {
            const std::size_t index = (im.nextStatsSlot + k) % im.statsSlots.size();
            if (!im.statsSlots[index].inFlight) {
                statsSlot = &im.statsSlots[index];
                im.copiedStatsSlot = static_cast<int>(index);
                im.nextStatsSlot = (index + 1) % im.statsSlots.size();
                break;
            }
        }
    }
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
    pass.SetBindGroup(0, frameBindGroup);
    pass.SetBindGroup(3, iblBindGroup);
    for (const auto& item : im.raymarchItems) {
        if (item.objectIndex >= scene.sdfs.size() || !item.onCamera) {
            continue; // ADR-1160: a shadow-only item has no pixels on the camera's screen
        }
        pass.SetPipeline(item.compiled != nullptr ? item.compiled->lit : im.raymarchPipeline);
        const std::array<std::uint32_t, 2> offsets = {item.offset, item.offset};
        pass.SetBindGroup(1, im.sdfGroup, offsets.size(), offsets.data());
        pass.SetBindGroup(2, materialBindGroup(scene.sdfs[item.objectIndex].material));
        pass.Draw(6);
    }
    pass.End();
    if (statsSlot != nullptr) {
        encoder.CopyBufferToBuffer(im.stats, 0, statsSlot->read, 0, Impl::kStatsBytes);
    }
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
    for (const auto& item : im.raymarchItems) {
        if (item.compiled != nullptr) {
            pass.SetPipeline(reducedSteps ? item.compiled->shadow : item.compiled->depth);
        } else {
            pass.SetPipeline(reducedSteps ? im.raymarchShadowPipeline : im.raymarchDepthPipeline);
        }
        if (item.objectIndex >= scene.sdfs.size()) {
            continue;
        }
        const scene::SdfObject& object = scene.sdfs[item.objectIndex];
        if (reducedSteps ? !object.castShadows : !object.depthPrepass) {
            continue; // ADR-1002: opted out of this depth-only march
        }
        if (!reducedSteps && !item.onCamera) {
            continue; // ADR-1160: the prepass is the camera's; a shadow-only item is marched for the lights alone
        }
        const std::array<std::uint32_t, 2> offsets = {item.offset, item.offset};
        pass.SetBindGroup(1, im.sdfGroup, offsets.size(), offsets.data());
        pass.SetBindGroup(2, materialBindGroup(scene.sdfs[item.objectIndex].material));
        pass.Draw(6);
    }
}

} // namespace avgen::rendering
