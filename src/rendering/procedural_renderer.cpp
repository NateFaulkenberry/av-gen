#include "rendering/procedural_renderer.hpp"

#include "rendering/field_uniforms.hpp"
#include "rendering/scene_renderer.hpp" // ObjectUniforms (the shared 512-byte slot layout)
#include "rendering/scene_targets.hpp"  // the five colour targets of the scene pass (ADR-035)
#include "rendering/spline_buffers.hpp"

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
constexpr std::uint32_t kObjectStride = SceneRenderer::kObjectStride; // dynamic-offset alignment
constexpr std::uint32_t kInstanceStride = sizeof(scene::InstanceRecord); // 96
constexpr std::uint32_t kEffectorWorkgroup = 64;     // points.wgsl cs_effectors
constexpr std::uint32_t kCullWorkgroup = 64;        // cull.wgsl cs_cull_classify
constexpr std::uint32_t kCullScanBlock = 1024;      // cull.wgsl kScanBlock (256 threads x 4 elements)
constexpr std::uint32_t kCullStatsStride = 8;       // u32 per object slot in the shared stats buffer
constexpr std::uint32_t kIndirectStride = 20;       // drawIndexedIndirect args: five u32
// Per-level slot in the object's deformer/time buffer. Uniform bind-group offsets must be a
// multiple of 256, so the 672-byte ProceduralUniforms is padded out to 768.
constexpr std::uint32_t kDeformerSlotStride = ((sizeof(ProceduralUniforms) + 255) / 256) * 256;
// Elements per level in the visible list. Storage bind-group offsets must also be 256-byte
// aligned, so each level's slice is a whole number of 64 u32 blocks.
constexpr std::uint32_t kVisibleAlign = 64;
static_assert(kInstanceStride == 96);
static_assert(sizeof(ObjectUniforms) <= kObjectStride);
static_assert(kDeformerSlotStride % 256 == 0);

std::uint32_t alignUp(std::uint32_t value, std::uint32_t multiple) {
    return ((value + multiple - 1) / multiple) * multiple;
}

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
// disabled so the shader skips it); `splineSlot` and `pathScale` are the resolved spline slot
// and final scale of a Path deformer (unbound or world-space Path deformers are disabled).
DeformerUniform packDeformer(const scene::Deformer& d, int fieldSlot, int splineSlot, float pathScale) {
    DeformerUniform u{};
    if (!d.enabled || (d.kind == scene::DeformerKind::Field && fieldSlot < 0) ||
        (d.kind == scene::DeformerKind::Path && (splineSlot < 0 || d.space == scene::DeformSpace::World))) {
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
    case scene::DeformerKind::Path:
        u.params = glm::vec4(static_cast<float>(splineSlot), pathScale, d.pathOffset, d.pathRoll);
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

// ---- culling maths, shared with shaders/cull.wgsl and the tests ---------------------------------

FrustumPlanes frustumPlanes(const glm::mat4& m) {
    // Gribb-Hartmann on a 0..1 depth range. glm is column major, so row i is
    // (m[0][i], m[1][i], m[2][i], m[3][i]); the near plane is row 2 alone (z >= 0).
    const auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);
    FrustumPlanes planes{{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2}};
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-12f) {
            plane /= length;
        }
    }
    return planes;
}

float cullProjScale(float fovYRadians, std::uint32_t viewportHeight) {
    const float tangent = std::tan(std::max(fovYRadians, 1e-4f) * 0.5f);
    return static_cast<float>(viewportHeight) / (2.0f * std::max(tangent, 1e-6f));
}

int cullLodLevel(const scene::LodSettings& lod, const FrustumPlanes& planes, const CullCamera& camera,
                 glm::vec3 center, float radius) {
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    const float distance = glm::length(center - camera.position);
    const float screenRadius = radius / std::max(distance, 1e-4f) * camera.projScale;
    if (lod.cull) {
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                return -1;
            }
        }
        if (lod.maxDistance > 0.0f && distance - radius > lod.maxDistance) {
            return -1;
        }
        if (lod.minScreenRadius > 0.0f && screenRadius < lod.minScreenRadius) {
            return -1;
        }
    }
    // A threshold of 0 ends the ladder, so an unconfigured object stays at LOD0.
    int level = 0;
    for (int k = 0; k + 1 < lodCount && k < 3; ++k) {
        const float threshold = lod.lodDistances[k];
        if (!(threshold > 0.0f)) {
            break;
        }
        const bool take = lod.lodByScreenSize ? screenRadius <= threshold : distance >= threshold;
        if (!take) {
            break;
        }
        level = k + 1;
    }
    return level;
}

struct ProceduralRenderer::Impl {
    struct CachedMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;  // 0 = generation failed (kept so the error is logged once)
        std::uint32_t vertexCount = 0;
        float radius = 1.0f;           // half diagonal of the source bounds (normal epsilon scale)
        glm::vec3 boundsMin{0.0f};     // source mesh bounds (object space; Path deformer "fit" extent)
        glm::vec3 boundsMax{0.0f};
        std::uint64_t lastUsed = 0;
    };
    struct ObjectState {
        std::uint64_t structureVersion = ~0ull; // of the uploaded instances
        std::size_t uploadedCount = 0;
        wgpu::Buffer instances;                 // base records
        std::uint64_t instanceBytes = 0;
        wgpu::Buffer live;                      // effector pass output (same layout), when used
        std::uint64_t liveBytes = 0;
        wgpu::Buffer deformers;                 // kMaxLodLevels slots of kDeformerSlotStride bytes
        wgpu::Buffer effectorUniforms;
        // Draw groups per LOD level, reading the base / live records. Level 0 of an object without
        // culling is exactly the pre-culling group (visible list = the shared inert placeholder).
        std::array<wgpu::BindGroup, scene::kMaxLodLevels> groups{};
        std::array<wgpu::BindGroup, scene::kMaxLodLevels> groupsLive{};
        wgpu::BindGroup computeGroup;           // effector pass
        bool usesLive = false;                  // this frame's draw reads the live buffer
        // Culling / LOD (ADR-029); allocated only for objects that use it.
        wgpu::Buffer cullUniforms;
        wgpu::Buffer lodIndex;                  // per record: level, or 0xFFFFFFFF when culled
        wgpu::Buffer blockSums;                 // kMaxLodLevels x scan blocks
        wgpu::Buffer visible;                   // kMaxLodLevels slices of visibleStride elements
        wgpu::Buffer indirect;                  // kMaxLodLevels x drawIndexedIndirect args
        wgpu::BindGroup cullGroup;              // cull pass over the base records
        wgpu::BindGroup cullGroupLive;          // cull pass over the live records
        std::uint32_t cullCapacity = 0;         // records the cull buffers are sized for
        std::uint32_t cullLodCount = 0;         // levels the cull buffers are sized for
        std::uint32_t visibleStride = 0;        // elements per level in `visible`
        std::uint32_t statsSlot = 0;            // slot in the shared stats buffer
        glm::mat4 prevModel{1.0f};              // last frame's object matrix (ADR-035 velocity)
        bool hasPrevModel = false;
        std::uint64_t lastUsed = 0;
    };
    struct DrawItem {
        std::size_t objectIndex;        // into scene.procedurals
        std::array<const CachedMesh*, scene::kMaxLodLevels> meshes{};
        const ObjectState* state;
        std::uint32_t offset;           // dynamic offset into the object uniform buffer
        std::uint32_t instanceCount;
        std::uint32_t lodCount = 1;
        bool indirect = false;          // draw through the compacted visible lists
    };
    struct ComputeItem {
        const ObjectState* state;
        std::uint32_t count;
    };
    struct CullItem {
        const ObjectState* state;
        std::uint32_t count;
        std::uint32_t lodCount;
        std::uint32_t blocks;
        bool usesLive;
    };
    // Asynchronous readback of the shared cull stats buffer (the UI numbers may lag a frame or
    // two; nothing in the render path ever waits for them).
    struct StatsSlot {
        wgpu::Buffer read;
        wgpu::Future mapFuture{};
        bool inFlight = false;
        bool ready = false;
        bool failed = false;
    };

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {
        staging.resize(static_cast<std::size_t>(kMaxProceduralObjects) * kObjectStride);
        statsSnapshot.assign(static_cast<std::size_t>(kMaxProceduralObjects) * kCullStatsStride, 0u);
    }
    ~Impl() {
        // A MapAsync callback carries a raw StatsSlot pointer; let every pending one complete
        // while the slots still exist (the same contract as gpu::GpuTimer).
        for (StatsSlot& slot : statsSlots) {
            if (slot.inFlight) {
                context.waitFor(slot.mapFuture, 2'000'000'000ull);
                slot.inFlight = false;
            }
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Result<wgpu::RenderPipeline> createPipeline(const wgpu::ShaderModule& module, bool cull,
                                                bool depthOnly = false);
    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<void> createComputePipeline(const wgpu::ShaderModule& module);
    Result<void> createCullPipelines(const wgpu::ShaderModule& module);
    Result<wgpu::ComputePipeline> makeCompute(const wgpu::PipelineLayout& layout, const wgpu::ShaderModule& module,
                                              const char* entry, const char* label);
    CachedMesh uploadMesh(const Result<scene::MeshData>& mesh, const std::string& name);
    const CachedMesh* ensureMesh(const scene::ProceduralGeometry& object);
    // The mesh of one LOD level, cached under a key derived from the object's meshHash.
    const CachedMesh* ensureLodMesh(const scene::ProceduralGeometry& object, int level);
    // Everything the object needs this frame, in dependency order: record/uniform buffers, the
    // cull buffers (when culling is on) and then every bind group that references them.
    void ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive, std::uint32_t lodCount,
                             bool cullActive, std::uint32_t count);
    // Allocates (grow-only) the cull buffers; returns true when a buffer was replaced.
    bool ensureCullBuffers(ObjectState& state, std::uint32_t count, std::uint32_t lodCount);
    void pumpStats();

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
    std::uint32_t sampleCount = 1;
    wgpu::BindGroupLayout objectLayout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline pipelineCull;
    wgpu::RenderPipeline pipelineDepth; // depth-only: the prepass and the shadow passes (ADR-034)
    wgpu::RenderPipeline pipelineNoCull;
    wgpu::BindGroupLayout computeLayout;
    wgpu::PipelineLayout computePipelineLayout;
    wgpu::ComputePipeline effectorPipeline;
    wgpu::BindGroupLayout cullLayout;
    wgpu::PipelineLayout cullPipelineLayout;
    wgpu::ComputePipeline cullClassifyPipeline;
    wgpu::ComputePipeline cullReducePipeline;
    wgpu::ComputePipeline cullTopPipeline;
    wgpu::ComputePipeline cullScatterPipeline;
    wgpu::Buffer objectUniforms;
    wgpu::Buffer fieldBlock;
    wgpu::Buffer splineTable;
    wgpu::Buffer gridTable; // the simulated-grid table fields.wgsl binds at group 0 binding 15
    wgpu::Buffer emptyVisible;      // inert placeholder at group 1 binding 5 for uncalled objects
    wgpu::Buffer cullStats;         // kMaxProceduralObjects slots of kCullStatsStride u32
    std::unique_ptr<gpu::GpuTimer> effectorTimer;
    std::unique_ptr<gpu::GpuTimer> cullTimer;
    std::vector<std::uint8_t> staging;
    std::map<std::uint64_t, CachedMesh> meshes;
    std::map<std::string, ObjectState> objects;
    std::vector<DrawItem> items;
    std::vector<ComputeItem> computeItems;
    std::vector<CullItem> cullItems;
    std::array<StatsSlot, 3> statsSlots{};
    std::vector<std::uint32_t> statsSnapshot; // latest completed stats readback
    int pendingStatsSlot = -1;
    double lastEffectorMs = -1.0;   // the latest completed effector-pass measurement
    double lastCullMs = -1.0;       // the latest completed cull-pass measurement
    bool passThisFrame = false;      // an effector pass was encoded in the current update()
    bool cullPassThisFrame = false;  // a cull pass was encoded in the current update()
    std::uint32_t viewportWidth = 1920;
    std::uint32_t viewportHeight = 1080;
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
                                      wgpu::Buffer fieldBlock, wgpu::Buffer splineTable,
                                      wgpu::Buffer gridTable) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.depthFormat = depthFormat;
    im.sampleCount = std::max<std::uint32_t>(sampleCount, 1);
    im.fieldBlock = std::move(fieldBlock);
    im.splineTable = std::move(splineTable);
    im.gridTable = std::move(gridTable);
    if (!im.gridTable) {
        // Standalone use without a SceneRenderer: an empty grid table (no Grid field can bind).
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-empty-grid-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kGridBufferSize;
        im.gridTable = device.CreateBuffer(&desc);
    }
    if (!im.splineTable) {
        // Standalone use without a SceneRenderer: an empty spline table (every slot invalid).
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-empty-spline-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = SplineBuffers::kBufferSize;
        im.splineTable = device.CreateBuffer(&desc);
        const SplineInfoGpu zero{};
        im.context.queue().WriteBuffer(im.splineTable, 0, &zero, sizeof(zero));
    }
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
        // (read-only storage), 2 = deformer/time block, 3 = field block, 4 = spline tables,
        // 5 = the LOD level's compacted visible list (ADR-029; inert when the object is not culled).
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
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
        entries[4].binding = 4;
        entries[4].visibility = wgpu::ShaderStage::Vertex;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[4].buffer.minBindingSize = SplineBuffers::kBufferSize;
        entries[5].binding = 5;
        entries[5].visibility = wgpu::ShaderStage::Vertex;
        entries[5].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[5].buffer.minBindingSize = 4;
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
        // Effector pass: 0 = params, 1 = base records, 2 = live records, 3 = field block,
        // 15 = the simulated-grid table (declared by fields.wgsl; ADR-032).
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
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
        entries[4].binding = 15;
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        // Cull pass (ADR-029): 0 = params, 1 = records, 2 = lod index, 3 = block sums,
        // 4 = visible lists, 5 = indirect args, 6 = the shared stats buffer.
        std::array<wgpu::BindGroupLayoutEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(CullPassUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.minBindingSize = kInstanceStride;
        for (std::size_t i = 2; i < entries.size(); ++i) {
            entries[i].binding = static_cast<std::uint32_t>(i);
            entries[i].visibility = wgpu::ShaderStage::Compute;
            entries[i].buffer.type = wgpu::BufferBindingType::Storage;
            entries[i].buffer.minBindingSize = 4;
        }
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "procedural-cull-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.cullLayout = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.label = "procedural-cull-pipeline-layout";
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &im.cullLayout;
        im.cullPipelineLayout = device.CreatePipelineLayout(&pdesc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-object-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(kMaxProceduralObjects) * kObjectStride;
        im.objectUniforms = device.CreateBuffer(&desc);
    }
    {
        // The inert visible list bound to every uncalled object: never read (fieldInfo.w is 0).
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-empty-visible";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = 256;
        im.emptyVisible = device.CreateBuffer(&desc);
        const std::array<std::uint32_t, 64> zero{};
        im.context.queue().WriteBuffer(im.emptyVisible, 0, zero.data(), zero.size() * sizeof(std::uint32_t));
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-cull-stats";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        desc.size = static_cast<std::uint64_t>(kMaxProceduralObjects) * kCullStatsStride * sizeof(std::uint32_t);
        im.cullStats = device.CreateBuffer(&desc);
        for (Impl::StatsSlot& slot : im.statsSlots) {
            wgpu::BufferDescriptor readDesc{};
            readDesc.label = "procedural-cull-stats-read";
            readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            readDesc.size = desc.size;
            slot.read = device.CreateBuffer(&readDesc);
        }
    }
    im.effectorTimer = std::make_unique<gpu::GpuTimer>(im.context);
    im.cullTimer = std::make_unique<gpu::GpuTimer>(im.context);
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
    auto cull = im.shaders.load("cull.wgsl");
    if (!cull) {
        return std::unexpected(cull.error());
    }
    if (auto r = im.createCullPipelines(*cull); !r) {
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
    if (auto r = impl_->createComputePipeline(*points); !r) {
        return r;
    }
    auto cull = impl_->shaders.load("cull.wgsl");
    if (!cull) {
        return std::unexpected(cull.error());
    }
    return impl_->createCullPipelines(*cull);
}

void ProceduralRenderer::setViewport(std::uint32_t width, std::uint32_t height) {
    impl_->viewportWidth = std::max(width, 1u);
    impl_->viewportHeight = std::max(height, 1u);
}

Result<void> ProceduralRenderer::Impl::createPipelines(const wgpu::ShaderModule& module) {
    auto cull = createPipeline(module, true);
    if (!cull) return std::unexpected(cull.error());
    auto noCull = createPipeline(module, false);
    if (!noCull) return std::unexpected(noCull.error());
    auto depthOnly = createPipeline(module, false, true);
    if (!depthOnly) return std::unexpected(depthOnly.error());
    pipelineCull = *cull;
    pipelineNoCull = *noCull;
    pipelineDepth = *depthOnly;
    return {};
}

Result<wgpu::ComputePipeline> ProceduralRenderer::Impl::makeCompute(const wgpu::PipelineLayout& layout,
                                                                   const wgpu::ShaderModule& module,
                                                                   const char* entry, const char* label) {
    wgpu::ComputePipelineDescriptor desc{};
    desc.label = label;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = entry;
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
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    return pipeline;
}

Result<void> ProceduralRenderer::Impl::createComputePipeline(const wgpu::ShaderModule& module) {
    auto pipeline = makeCompute(computePipelineLayout, module, "cs_effectors", "procedural-effectors");
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    effectorPipeline = *pipeline;
    return {};
}

// The four stages of shaders/cull.wgsl; all four are replaced together so a failed hot reload
// leaves the previous set in place.
Result<void> ProceduralRenderer::Impl::createCullPipelines(const wgpu::ShaderModule& module) {
    auto classify = makeCompute(cullPipelineLayout, module, "cs_cull_classify", "procedural-cull-classify");
    if (!classify) return std::unexpected(classify.error());
    auto reduce = makeCompute(cullPipelineLayout, module, "cs_cull_reduce", "procedural-cull-reduce");
    if (!reduce) return std::unexpected(reduce.error());
    auto top = makeCompute(cullPipelineLayout, module, "cs_cull_top", "procedural-cull-top");
    if (!top) return std::unexpected(top.error());
    auto scatter = makeCompute(cullPipelineLayout, module, "cs_cull_scatter", "procedural-cull-scatter");
    if (!scatter) return std::unexpected(scatter.error());
    cullClassifyPipeline = *classify;
    cullReducePipeline = *reduce;
    cullTopPipeline = *top;
    cullScatterPipeline = *scatter;
    return {};
}

Result<wgpu::RenderPipeline> ProceduralRenderer::Impl::createPipeline(const wgpu::ShaderModule& module, bool cull,
                                                                      bool depthOnly) {
    VertexLayoutStorage vertex;
    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, colorFormat, nullptr);
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = depthOnly ? "fs_proc_depth" : "fs_proc";
    fragment.targetCount = depthOnly ? 0 : kSceneTargetCount;
    fragment.targets = depthOnly ? nullptr : colorTargets.data();
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = depthOnly ? wgpu::CompareFunction::Less : wgpu::CompareFunction::LessEqual;

    const char* label = depthOnly ? "procedural-depth" : (cull ? "procedural-opaque" : "procedural-opaque-twosided");
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
    desc.multisample.count = depthOnly ? 1 : sampleCount;
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

ProceduralRenderer::Impl::CachedMesh ProceduralRenderer::Impl::uploadMesh(const Result<scene::MeshData>& mesh,
                                                                         const std::string& name) {
    CachedMesh cached;
    if (!mesh) {
        log::warn("procedural '{}': source mesh not generated: {}", name, mesh.error().message);
        return cached;
    }
    if (!mesh->valid()) {
        log::warn("procedural '{}': generated source mesh is invalid", name);
        return cached;
    }
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
    cached.boundsMin = lo;
    cached.boundsMax = hi;
    return cached;
}

const ProceduralRenderer::Impl::CachedMesh* ProceduralRenderer::Impl::ensureMesh(const scene::ProceduralGeometry& object) {
    auto it = meshes.find(object.meshHash);
    if (it == meshes.end()) {
        // Point sources are the billboard quad regardless of the generator (the vertex shader
        // builds the camera-facing quad from its XY); everything else goes through makeSourceMesh.
        Result<scene::MeshData> mesh = object.source.kind == scene::PrimitiveKind::Point
                                           ? Result<scene::MeshData>(scene::makePointQuad(object.source.pointSize))
                                           : scene::makeSourceMesh(object.source);
        it = meshes.emplace(object.meshHash, uploadMesh(mesh, object.name)).first;
    }
    it->second.lastUsed = frame;
    return it->second.indexCount > 0 ? &it->second : nullptr;
}

const ProceduralRenderer::Impl::CachedMesh* ProceduralRenderer::Impl::ensureLodMesh(const scene::ProceduralGeometry& object,
                                                                                    int level) {
    if (level <= 0) {
        return ensureMesh(object);
    }
    // A cache key of its own: the level and (for the impostor levels) the billboard size are what
    // make the reduced mesh differ from the source's.
    std::uint64_t key = object.meshHash;
    const auto mix = [&key](std::uint64_t v) { key ^= v + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2); };
    mix(0x10D0000ull + static_cast<std::uint64_t>(level));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(object.lod.impostorSize)));
    auto it = meshes.find(key);
    if (it == meshes.end()) {
        it = meshes.emplace(key, uploadMesh(scene::makeLodMesh(object.source, level, object.lod.impostorSize),
                                            object.name))
                 .first;
    }
    it->second.lastUsed = frame;
    return it->second.indexCount > 0 ? &it->second : nullptr;
}

void ProceduralRenderer::Impl::ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive,
                                                  std::uint32_t lodCount, bool cullActive, std::uint32_t count) {
    const auto& device = context.device();
    const std::uint32_t levels = std::clamp(lodCount, 1u, static_cast<std::uint32_t>(scene::kMaxLodLevels));
    // Missing group for the top level this frame needs (a fresh object, or lodCount grew).
    bool rebuildGroup = !state.groups[levels - 1];
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
        desc.size = static_cast<std::uint64_t>(scene::kMaxLodLevels) * kDeformerSlotStride;
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
    if (cullActive && ensureCullBuffers(state, count, lodCount)) {
        rebuildGroup = true; // the draw groups slice `visible`
    }
    if (rebuildGroup) {
        // One draw group per (record buffer, LOD level): the level picks its deformer/time slot
        // and its slice of the visible list. Uncalled objects bind the inert placeholder, which
        // the vertex shader never reads (ProceduralUniforms::fieldInfo.w is 0).
        auto drawGroup = [&](const wgpu::Buffer& records, std::uint64_t bytes, std::uint32_t level, const char* label) {
            std::array<wgpu::BindGroupEntry, 6> entries{};
            entries[0].binding = 0;
            entries[0].buffer = objectUniforms;
            entries[0].size = sizeof(ObjectUniforms);
            entries[1].binding = 1;
            entries[1].buffer = records;
            entries[1].size = bytes;
            entries[2].binding = 2;
            entries[2].buffer = state.deformers;
            entries[2].offset = static_cast<std::uint64_t>(level) * kDeformerSlotStride;
            entries[2].size = sizeof(ProceduralUniforms);
            entries[3].binding = 3;
            entries[3].buffer = fieldBlock;
            entries[3].size = FieldUniforms::kBufferSize;
            entries[4].binding = 4;
            entries[4].buffer = splineTable;
            entries[4].size = SplineBuffers::kBufferSize;
            entries[5].binding = 5;
            if (cullActive && state.visible && level < state.cullLodCount) {
                entries[5].buffer = state.visible;
                entries[5].offset = static_cast<std::uint64_t>(level) * state.visibleStride * sizeof(std::uint32_t);
                entries[5].size = static_cast<std::uint64_t>(state.visibleStride) * sizeof(std::uint32_t);
            } else {
                entries[5].buffer = emptyVisible;
                entries[5].size = 256;
            }
            wgpu::BindGroupDescriptor desc{};
            desc.label = label;
            desc.layout = objectLayout;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            return device.CreateBindGroup(&desc);
        };
        state.groups = {};
        state.groupsLive = {};
        for (std::uint32_t level = 0; level < levels; ++level) {
            state.groups[level] = drawGroup(state.instances, state.instanceBytes, level, "procedural-object-group");
            if (state.live) {
                state.groupsLive[level] =
                    drawGroup(state.live, state.liveBytes, level, "procedural-object-group-live");
            }
        }
        state.computeGroup = nullptr;
        if (state.live) {
            std::array<wgpu::BindGroupEntry, 5> entries{};
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
            entries[4].binding = 15;
            entries[4].buffer = gridTable;
            entries[4].size = FieldUniforms::kGridBufferSize;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "procedural-effector-group";
            desc.layout = computeLayout;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            state.computeGroup = device.CreateBindGroup(&desc);
        }
        // The cull groups reference the record buffers, so they are rebuilt with them.
        state.cullGroup = nullptr;
        state.cullGroupLive = nullptr;
    }
    if (!cullActive) {
        return;
    }
    auto cullGroup = [&](const wgpu::Buffer& records, std::uint64_t bytes, const char* label) {
        std::array<wgpu::BindGroupEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].buffer = state.cullUniforms;
        entries[0].size = sizeof(CullPassUniforms);
        entries[1].binding = 1;
        entries[1].buffer = records;
        entries[1].size = bytes;
        entries[2].binding = 2;
        entries[2].buffer = state.lodIndex;
        entries[2].size = state.lodIndex.GetSize();
        entries[3].binding = 3;
        entries[3].buffer = state.blockSums;
        entries[3].size = state.blockSums.GetSize();
        entries[4].binding = 4;
        entries[4].buffer = state.visible;
        entries[4].size = state.visible.GetSize();
        entries[5].binding = 5;
        entries[5].buffer = state.indirect;
        entries[5].size = state.indirect.GetSize();
        entries[6].binding = 6;
        entries[6].buffer = cullStats;
        entries[6].size = cullStats.GetSize();
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = cullLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    if (!state.cullGroup) {
        state.cullGroup = cullGroup(state.instances, state.instanceBytes, "procedural-cull-group");
    }
    if (state.live && !state.cullGroupLive) {
        state.cullGroupLive = cullGroup(state.live, state.liveBytes, "procedural-cull-group-live");
    }
}

// Allocates the culling buffers for `count` records over `lodCount` levels. Grow-only: they are
// replaced when the object needs more room, and `state.visibleStride`/`cullLodCount` then describe
// what they hold. Returns true when a buffer was replaced (the bind groups must follow).
bool ProceduralRenderer::Impl::ensureCullBuffers(ObjectState& state, std::uint32_t count, std::uint32_t lodCount) {
    const auto& device = context.device();
    const std::uint32_t stride = alignUp(std::max(count, 1u), kVisibleAlign);
    const std::uint32_t blocks = std::max((count + kCullScanBlock - 1) / kCullScanBlock, 1u);
    bool grew = false;
    if (count > state.cullCapacity || lodCount > state.cullLodCount || !state.visible) {
        const auto storage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        wgpu::BufferDescriptor desc{};
        desc.usage = storage;
        desc.label = "procedural-lod-index";
        desc.size = static_cast<std::uint64_t>(stride) * sizeof(std::uint32_t);
        state.lodIndex = device.CreateBuffer(&desc);
        desc.label = "procedural-cull-blocksums";
        desc.size = static_cast<std::uint64_t>(blocks) * lodCount * sizeof(std::uint32_t);
        state.blockSums = device.CreateBuffer(&desc);
        desc.label = "procedural-visible";
        desc.size = static_cast<std::uint64_t>(stride) * lodCount * sizeof(std::uint32_t);
        state.visible = device.CreateBuffer(&desc);
        wgpu::BufferDescriptor idesc{};
        idesc.label = "procedural-cull-indirect";
        idesc.usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst |
                      wgpu::BufferUsage::CopySrc;
        idesc.size = static_cast<std::uint64_t>(scene::kMaxLodLevels) * kIndirectStride;
        state.indirect = device.CreateBuffer(&idesc);
        state.cullCapacity = count;
        state.cullLodCount = lodCount;
        state.visibleStride = stride;
        state.cullGroup = nullptr;
        state.cullGroupLive = nullptr;
        grew = true;
    }
    if (!state.cullUniforms) {
        wgpu::BufferDescriptor desc{};
        desc.label = "procedural-cull-params";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(CullPassUniforms);
        state.cullUniforms = device.CreateBuffer(&desc);
    }
    return grew;
}

// Starts the pending stats copy's map and harvests whatever finished. Never blocks.
void ProceduralRenderer::Impl::pumpStats() {
    if (pendingStatsSlot >= 0) {
        StatsSlot& slot = statsSlots[static_cast<std::size_t>(pendingStatsSlot)];
        slot.inFlight = true;
        slot.ready = false;
        slot.failed = false;
        StatsSlot* raw = &slot;
        slot.mapFuture = slot.read.MapAsync(
            wgpu::MapMode::Read, 0, slot.read.GetSize(), wgpu::CallbackMode::AllowProcessEvents,
            [](wgpu::MapAsyncStatus status, wgpu::StringView, StatsSlot* target) {
                target->ready = status == wgpu::MapAsyncStatus::Success;
                target->failed = status != wgpu::MapAsyncStatus::Success;
            },
            raw);
        pendingStatsSlot = -1;
    }
    bool anyInFlight = false;
    for (const StatsSlot& slot : statsSlots) {
        anyInFlight = anyInFlight || slot.inFlight;
    }
    if (!anyInFlight) {
        return;
    }
    context.processEvents();
    for (StatsSlot& slot : statsSlots) {
        if (!slot.inFlight) {
            continue;
        }
        if (slot.ready) {
            const std::uint64_t bytes = slot.read.GetSize();
            const void* data = slot.read.GetConstMappedRange(0, bytes);
            if (data != nullptr) {
                std::memcpy(statsSnapshot.data(), data, static_cast<std::size_t>(bytes));
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

void ProceduralRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.effectorTimer) {
        const double ms = im.effectorTimer->collect();
        if (ms >= 0.0) {
            im.lastEffectorMs = ms;
        }
    }
    if (im.cullTimer) {
        const double ms = im.cullTimer->collect();
        if (ms >= 0.0) {
            im.lastCullMs = ms;
        }
    }
    im.pumpStats();
    stats_.effectorPassMs = im.passThisFrame ? im.lastEffectorMs : -1.0;
    stats_.cullMs = im.cullPassThisFrame ? im.lastCullMs : -1.0;
}

void ProceduralRenderer::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                                const std::vector<glm::mat4>& objectMatrices, const FrameTime& time,
                                const FieldUniforms* fields, const SplineBuffers* splines) {
    const auto start = std::chrono::steady_clock::now();
    Impl& im = *impl_;
    stats_ = ProceduralStats{};
    im.items.clear();
    im.computeItems.clear();
    im.cullItems.clear();
    im.passThisFrame = false;
    im.cullPassThisFrame = false;
    if (!im.initialised) {
        return;
    }
    ++im.frame;
    // Harvest the previous frame's effector/cull timings (their command buffer was submitted by
    // now) and whatever cull-stats readback completed.
    collectTimings();
    const auto& queue = im.context.queue();
    // The frustum of this frame's camera (ADR-029). Free unless an object actually culls.
    const float aspect = static_cast<float>(im.viewportWidth) / static_cast<float>(std::max(im.viewportHeight, 1u));
    const glm::mat4 viewProj = scene.camera.projection(aspect) * scene.camera.view();
    const FrustumPlanes planes = frustumPlanes(viewProj);
    CullCamera cullCamera;
    cullCamera.position = scene.camera.position;
    cullCamera.projScale = cullProjScale(scene.camera.effectiveFovY(), im.viewportHeight);
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
        // ---- culling / LOD (ADR-029) ----
        const scene::LodSettings& lodSettings = object.lod;
        const std::uint32_t lodCount =
            static_cast<std::uint32_t>(std::clamp(lodSettings.lodCount, 1, scene::kMaxLodLevels));
        const std::uint32_t instanceCount = static_cast<std::uint32_t>(object.instances.size());
        const bool cullActive = lodSettings.cull || lodCount > 1;
        std::array<const Impl::CachedMesh*, scene::kMaxLodLevels> lodMeshes{};
        lodMeshes[0] = mesh;
        for (std::uint32_t level = 1; level < lodCount; ++level) {
            const Impl::CachedMesh* reduced = im.ensureLodMesh(object, static_cast<int>(level));
            lodMeshes[level] = reduced != nullptr ? reduced : mesh; // a failed level falls back to the source
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
        im.ensureObjectBuffers(state, instanceBytes, usesLive, lodCount, cullActive, instanceCount);
        state.usesLive = usesLive;
        state.statsSlot = slot;
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
                int splineSlot = -1;
                float pathScale = 0.0f;
                if (deformer.kind == scene::DeformerKind::Path && splines != nullptr &&
                    deformer.space == scene::DeformSpace::Local) {
                    splineSlot = splines->slotOf(deformer.spline);
                    if (splineSlot >= 0) {
                        // "fit" (pathScale 0): the source extent along the axis maps to the length.
                        pathScale = deformer.pathScale;
                        if (pathScale == 0.0f) {
                            const glm::vec3 axis = safeNormalize(deformer.axis, glm::vec3(0.0f, 1.0f, 0.0f));
                            const float extent = glm::dot(glm::abs(axis), mesh->boundsMax - mesh->boundsMin);
                            const float length = splines->header().info[splineSlot].x;
                            pathScale = length / std::max(extent, 1e-6f);
                        }
                        if (deformer.enabled) {
                            ++stats_.pathDeformers;
                        }
                    }
                }
                u.deformers[d] = packDeformer(deformer, fieldSlot, splineSlot, pathScale);
                enabled += deformer.enabled ? 1u : 0u;
            } else {
                u.deformers[d].axisKind = glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);
            }
        }
        u.timeInfo = glm::vec4(static_cast<float>(time.renderTime), static_cast<float>(deformerCount),
                               1e-3f * mesh->radius, static_cast<float>(object.instances.size()));
        // Velocity needs the same chain evaluated at the previous frame's time (ADR-035).
        u.prevInfo = glm::vec4(static_cast<float>(time.renderTime - time.deltaTime), 0.0f, 0.0f, 0.0f);
        // Step 1 of the transform chain. It is a uniform rather than a baked mesh because
        // source/position|rotation|scale animate; the shader applies it before the deformers so
        // the GPU matches ProceduralGeometry::instanceMatrix().
        u.sourceMatrix = object.sourceTransform.matrix();
        u.sourceNormalMatrix = glm::transpose(glm::inverse(u.sourceMatrix));
        int emissiveSlot = -1;
        if (fields != nullptr && !object.emissiveField.empty() && object.emissiveFieldAmount != 0.0f) {
            emissiveSlot = fields->slotOf(object.emissiveField);
        }
        const bool isPoint = object.source.kind == scene::PrimitiveKind::Point;
        // One slot per LOD level: levels 2 and 3 are camera-facing billboards, so they take the
        // same shader path as a Point source; fieldInfo.w switches the visible-list indirection on.
        // With the defaults (no culling, one level) this is the single write it has always been.
        for (std::uint32_t level = 0; level < lodCount; ++level) {
            const bool billboard = isPoint || level >= 2;
            u.fieldInfo = glm::vec4(static_cast<float>(emissiveSlot), object.emissiveFieldAmount,
                                    billboard ? 1.0f : 0.0f, cullActive ? 1.0f : 0.0f);
            queue.WriteBuffer(state.deformers, static_cast<std::uint64_t>(level) * kDeformerSlotStride, &u, sizeof(u));
        }
        if (isPoint) {
            ++stats_.pointObjects;
        }

        if (cullActive) {
            CullPassUniforms cull{};
            cull.objectToWorld = model;
            for (std::size_t k = 0; k < planes.size(); ++k) {
                cull.planes[k] = planes[k];
            }
            cull.cameraPos = glm::vec4(cullCamera.position, cullCamera.projScale);
            // The object matrix's largest column length: the instance scale is in the record, this
            // is the rest of the world scale the bounding sphere has to account for.
            float objectScale = 0.0f;
            for (int c = 0; c < 3; ++c) {
                objectScale = std::max(objectScale, glm::length(glm::vec3(model[c])));
            }
            cull.limits = glm::vec4(std::max(lodSettings.maxDistance, 0.0f),
                                    std::max(lodSettings.minScreenRadius, 0.0f), mesh->radius,
                                    std::max(objectScale, 1e-6f));
            cull.thresholds = glm::vec4(lodSettings.lodDistances[0], lodSettings.lodDistances[1],
                                        lodSettings.lodDistances[2], 0.0f);
            const std::uint32_t blocks = std::max((instanceCount + kCullScanBlock - 1) / kCullScanBlock, 1u);
            cull.counts = glm::uvec4(instanceCount, lodCount, state.visibleStride, blocks);
            cull.flags = glm::uvec4(lodSettings.cull ? 1u : 0u, lodSettings.lodByScreenSize ? 1u : 0u, slot, 0u);
            cull.indexCounts = glm::uvec4(0u);
            for (std::uint32_t level = 0; level < lodCount; ++level) {
                cull.indexCounts[static_cast<int>(level)] = lodMeshes[level]->indexCount;
            }
            queue.WriteBuffer(state.cullUniforms, 0, &cull, sizeof(cull));
            im.cullItems.push_back(Impl::CullItem{&state, instanceCount, lodCount, blocks, usesLive});
            ++stats_.cullObjects;
        }

        // ---- object slot: exactly the entity ObjectUniforms fields ----
        const auto& m = object.material;
        ObjectUniforms obj{};
        obj.model = model;
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        obj.prevModel = state.hasPrevModel ? state.prevModel : model;
        state.prevModel = model;
        state.hasPrevModel = true;
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
        // x = the ADR-030 `objectId` material input; y = material id and z = bloom weight feed the
        // identifier and emission targets (ADR-035).
        obj.ids = glm::vec4(static_cast<float>(i), static_cast<float>(i + 1), 1.0f, 0.0f);
        const std::uint32_t offset = slot * kObjectStride;
        std::memcpy(im.staging.data() + offset, &obj, sizeof(obj));

        im.items.push_back(Impl::DrawItem{i, lodMeshes, &state, offset, instanceCount, lodCount, cullActive});
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

    // ---- the cull pass: classify, then a stable per-level compaction (cull.wgsl) ----
    // Encoded after the effector pass so it sees the live records; dispatches inside one pass are
    // ordered, so reduce feeds top feeds scatter, and the y dimension of a dispatch is the LOD level.
    if (!im.cullItems.empty()) {
        // Every slot the shader writes is written unconditionally, but slots of objects that
        // stopped culling must not linger: zero the used prefix each frame (a few KB).
        const std::size_t statsBytes = static_cast<std::size_t>(slot) * kCullStatsStride * sizeof(std::uint32_t);
        if (statsBytes > 0) {
            const std::vector<std::uint32_t> zero(static_cast<std::size_t>(slot) * kCullStatsStride, 0u);
            queue.WriteBuffer(im.cullStats, 0, zero.data(), statsBytes);
        }
        wgpu::ComputePassDescriptor desc{};
        desc.label = "procedural-cull";
        desc.timestampWrites = im.cullTimer->passWrites();
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&desc);
        cp.SetPipeline(im.cullClassifyPipeline);
        for (const auto& item : im.cullItems) {
            cp.SetBindGroup(0, item.usesLive ? item.state->cullGroupLive : item.state->cullGroup);
            cp.DispatchWorkgroups((item.count + kCullWorkgroup - 1) / kCullWorkgroup);
        }
        const auto scanStage = [&](const wgpu::ComputePipeline& pipeline, bool oneBlock) {
            cp.SetPipeline(pipeline);
            for (const auto& item : im.cullItems) {
                cp.SetBindGroup(0, item.usesLive ? item.state->cullGroupLive : item.state->cullGroup);
                cp.DispatchWorkgroups(oneBlock ? 1u : item.blocks, item.lodCount);
            }
        };
        scanStage(im.cullReducePipeline, false);
        scanStage(im.cullTopPipeline, true);
        scanStage(im.cullScatterPipeline, false);
        cp.End();
        im.cullTimer->resolve(encoder);
        im.cullPassThisFrame = true;
        stats_.cullMs = im.lastCullMs;
        // Non-blocking stats readback: copy into a free ring slot, mapped by collectTimings().
        for (std::size_t i = 0; i < im.statsSlots.size(); ++i) {
            if (!im.statsSlots[i].inFlight) {
                encoder.CopyBufferToBuffer(im.cullStats, 0, im.statsSlots[i].read, 0, im.cullStats.GetSize());
                im.pendingStatsSlot = static_cast<int>(i);
                break;
            }
        }
    }
    // Aggregate whatever the latest completed readback holds for the objects culling this frame.
    for (const auto& item : im.cullItems) {
        const std::size_t base = static_cast<std::size_t>(item.state->statsSlot) * kCullStatsStride;
        if (base + kCullStatsStride > im.statsSnapshot.size() || im.statsSnapshot[base + 5] == 0) {
            continue;
        }
        std::uint64_t visible = 0;
        for (std::size_t level = 0; level < 4; ++level) {
            stats_.lodCounts[level] += im.statsSnapshot[base + level];
            visible += im.statsSnapshot[base + level];
        }
        const std::uint64_t records = im.statsSnapshot[base + 4];
        stats_.visibleInstances += visible;
        stats_.culledInstances += records > visible ? records - visible : 0;
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

void ProceduralRenderer::drawDepthOnly(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                                       const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup) {
    drawImpl(pass, scene, materialBindGroup, true);
}

void ProceduralRenderer::draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup) {
    drawImpl(pass, scene, materialBindGroup, false);
}

void ProceduralRenderer::drawImpl(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                                  const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                                  bool depthOnly) {
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
        const auto& groups = item.state->usesLive ? item.state->groupsLive : item.state->groups;
        if (!item.indirect) {
            pass.SetPipeline(depthOnly ? im.pipelineDepth : (twoSided ? im.pipelineNoCull : im.pipelineCull));
            pass.SetBindGroup(1, groups[0], 1, &item.offset);
            pass.SetBindGroup(2, materialBindGroup(material));
            pass.SetVertexBuffer(0, item.meshes[0]->vertices);
            pass.SetIndexBuffer(item.meshes[0]->indices, wgpu::IndexFormat::Uint32);
            pass.DrawIndexed(item.meshes[0]->indexCount, item.instanceCount);
            if (!depthOnly) {
                ++stats_.drawCalls;
            }
            continue;
        }
        // One indirect draw per LOD level, instance counts written by the cull pass. Levels 2 and
        // 3 are camera-facing billboards, so they are never back-face culled.
        for (std::uint32_t level = 0; level < item.lodCount; ++level) {
            const Impl::CachedMesh* mesh = item.meshes[level];
            if (mesh == nullptr || !groups[level]) {
                continue;
            }
            pass.SetPipeline(depthOnly ? im.pipelineDepth
                                       : (twoSided || level >= 2 ? im.pipelineNoCull : im.pipelineCull));
            pass.SetBindGroup(1, groups[level], 1, &item.offset);
            pass.SetBindGroup(2, materialBindGroup(material));
            pass.SetVertexBuffer(0, mesh->vertices);
            pass.SetIndexBuffer(mesh->indices, wgpu::IndexFormat::Uint32);
            pass.DrawIndexedIndirect(item.state->indirect, static_cast<std::uint64_t>(level) * kIndirectStride);
            if (!depthOnly) {
                ++stats_.drawCalls;
            }
        }
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

Result<CullCounts> ProceduralRenderer::readCullCounts(const std::string& name) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    if (it == im.objects.end() || !it->second.visible) {
        return fail("procedural object '{}' has no cull state", name);
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(it->second.statsSlot) * kCullStatsStride * sizeof(std::uint32_t);
    auto data = gpu::readBuffer(im.context, im.cullStats, offset, kCullStatsStride * sizeof(std::uint32_t));
    if (!data) {
        return std::unexpected(data.error());
    }
    std::array<std::uint32_t, kCullStatsStride> raw{};
    std::memcpy(raw.data(), data->data(), raw.size() * sizeof(std::uint32_t));
    CullCounts counts;
    counts.records = raw[4];
    for (std::size_t level = 0; level < counts.lod.size(); ++level) {
        counts.lod[level] = raw[level];
        counts.visible += raw[level];
    }
    counts.culled = counts.records > counts.visible ? counts.records - counts.visible : 0;
    return counts;
}

Result<std::vector<std::uint32_t>> ProceduralRenderer::readVisibleIndices(const std::string& name, int level) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    if (it == im.objects.end() || !it->second.visible) {
        return fail("procedural object '{}' has no cull state", name);
    }
    const Impl::ObjectState& state = it->second;
    if (level < 0 || static_cast<std::uint32_t>(level) >= state.cullLodCount) {
        return fail("procedural object '{}' has no LOD level {}", name, level);
    }
    auto counts = readCullCounts(name);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    const std::uint32_t count = counts->lod[static_cast<std::size_t>(level)];
    if (count == 0) {
        return std::vector<std::uint32_t>{};
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(level) * state.visibleStride * sizeof(std::uint32_t);
    auto data = gpu::readBuffer(im.context, state.visible, offset, static_cast<std::uint64_t>(count) * sizeof(std::uint32_t));
    if (!data) {
        return std::unexpected(data.error());
    }
    std::vector<std::uint32_t> indices(count);
    std::memcpy(indices.data(), data->data(), data->size());
    return indices;
}

} // namespace avgen::rendering
