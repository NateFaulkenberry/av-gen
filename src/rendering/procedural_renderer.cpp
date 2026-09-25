#include "rendering/procedural_renderer.hpp"

#include "rendering/field_uniforms.hpp"
#include "rendering/scene_renderer.hpp" // ObjectUniforms (the shared 512-byte slot layout)
#include "rendering/scene_targets.hpp"  // the five colour targets of the scene pass (ADR-035)
#include "rendering/spline_buffers.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "spatial/vegetation_sim.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <set>
#include <vector>
#include <limits>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace avgen::rendering {

namespace {

constexpr std::uint32_t kMaxProceduralObjects = 256; // 256-byte slots in one uniform buffer
// ADR-287: the shadow caster list's indirect-args and stats slots live in the same two buffers as
// the camera list's, offset by this. One buffer rather than two, because ADR-051 measured the cost
// per distinct indirect buffer a pass reads and this frame reads one in five passes; the slot is an
// offset and costs nothing per draw.
constexpr std::uint32_t kShadowSlotBase = kMaxProceduralObjects;
constexpr std::uint32_t kCullSlots = kMaxProceduralObjects * 2;
constexpr std::uint32_t kObjectStride = SceneRenderer::kObjectStride; // dynamic-offset alignment
constexpr std::uint32_t kInstanceStride = sizeof(scene::InstanceRecord); // 96
constexpr std::uint32_t kEffectorWorkgroup = 64;     // points.wgsl cs_effectors
constexpr std::uint32_t kCullWorkgroup = 64;        // cull.wgsl cs_cull_classify
constexpr std::uint32_t kCullScanBlock = 1024;      // cull.wgsl kScanBlock (256 threads x 4 elements)
constexpr std::uint32_t kCullStatsStride = 8;       // u32 per object slot in the shared stats buffer
constexpr std::uint32_t kIndirectStride = 20;       // drawIndexedIndirect args: five u32
static_assert(scene::kMaxLodLevels == 4, "shaders/cull.wgsl hardcodes kMaxLodLevels to stride the shared indirect buffer");
// Per-level slot in the object's deformer/time buffer. Uniform bind-group offsets must be a
// multiple of 256, so the 672-byte ProceduralUniforms is padded out to 768.
constexpr std::uint32_t kDeformerSlotStride = ((sizeof(ProceduralUniforms) + 255) / 256) * 256;
// Elements per level in the visible list. Storage bind-group offsets must also be 256-byte
// aligned, so each level's slice is a whole number of 64 u32 blocks.
constexpr std::uint32_t kVisibleAlign = 64;
static_assert(kInstanceStride == 96);
static_assert(kMaxShadowCullViews == kMaxShadowViews,
              "the cull pass tests every shadow view the atlas can hold; shaders/cull.wgsl "
              "kMaxShadowCullViews sizes the array");
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

// The culling maths this file used to define -- frustumPlanes, cullProjScale, cullLodLevel,
// instanceBounds, objectFullyCulled -- moved to rendering/visibility.cpp. They need no device,
// and a decision that can only be checked by rendering something is a decision nobody checks;
// they are now in the unit suite rather than only in the GPU one. Every name is re-exported
// through procedural_renderer.hpp, so no caller changed.


struct ProceduralRenderer::Impl {
    struct CachedMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;  // 0 = generation failed (kept so the error is logged once)
        std::uint32_t vertexCount = 0;
        float radius = 1.0f;           // half diagonal of the source bounds (normal epsilon scale)
        // The radius the *cull* uses: the sphere about the source's own origin, which is where
        // shaders/cull.wgsl centres it (rendering/visibility.hpp sourceCullRadius). Equal to
        // `radius` for geometry centred on its origin and larger for anything that stands on it --
        // a tree, a mushroom, a character -- which is the whole reason it is a second number.
        float cullRadius = 1.0f;
        // True when this level is a camera-facing impostor quad rather than geometry in the
        // source's own space (scene::lodLevelIsImpostor). The two are drawn by different vertex
        // paths, and the level index is not the answer: a Mesh source's levels 1-3 have been
        // simplified meshes since ADR-085.
        bool impostor = false;
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
        // ADR-287: the same groups over the shadow caster list's slices of `visible`. A draw group
        // binds ONE level's slice, so the second list needs its own -- this is the "bind groups per
        // object" half of the cost the Shadow Lab priced.
        std::array<wgpu::BindGroup, scene::kMaxLodLevels> shadowGroups{};
        std::array<wgpu::BindGroup, scene::kMaxLodLevels> shadowGroupsLive{};
        wgpu::BindGroup computeGroup;           // effector pass
        bool usesLive = false;                  // this frame's draw reads the live buffer
        // Culling / LOD (ADR-029); allocated only for objects that use it.
        wgpu::Buffer cullUniforms;
        wgpu::Buffer lodIndex;                  // per record: level, or 0xFFFFFFFF when culled
        std::uint64_t lastCullFrame = 0;        // the frame this object's cull dispatches were encoded
        wgpu::Buffer blockSums;                 // kMaxLodLevels x scan blocks
        wgpu::Buffer visible;                   // kMaxLodLevels slices of visibleStride elements
        wgpu::BindGroup cullGroup;              // cull pass over the base records
        wgpu::BindGroup cullGroupLive;          // cull pass over the live records
        // Whole-object record bounds, rebuilt with the instance upload. Lets the CPU prove, before
        // encoding anything, that the cull pass would reject every record (see objectFullyCulled).
        InstanceBounds bounds;
        std::uint32_t cullCapacity = 0;         // records the cull buffers are sized for
        std::uint32_t cullLodCapacity = 0;      // levels the cull buffers are SIZED for (grow-only)
        std::uint32_t cullLodCount = 0;
        // Levels this frame's cull pass actually uses, which is where the shadow caster list's
        // slices begin (shaders/cull.wgsl `sliceOf` reads it out of `counts.y`). Kept apart from
        // the capacity above because the allocation is grow-only and this is not: an object that
        // loses a rung would otherwise have the shader writing its caster list at one slice base
        // while the draw groups read another, and its ecology shadows would quietly stop.
        std::uint32_t visibleStride = 0;        // elements per level in `visible`
        std::uint32_t statsSlot = 0;            // slot in the shared stats buffer
        // ADR-108. The visible lists the draw groups were built against: this object's own, or the
        // lead's when it is a material part sharing one cull. Recorded rather than assumed,
        // because both can change under it -- the lead's buffers are grow-only and are replaced
        // when its record set does, and a part can stop being one between frames. A mismatch here
        // is the invalidation rule: the draw groups are rebuilt, and nothing else has to notice.
        wgpu::Buffer groupVisible;
        std::uint32_t groupVisibleStride = 0;
        std::uint32_t groupVisibleLevels = 0;
        glm::mat4 prevModel{1.0f};              // last frame's object matrix (ADR-035 velocity)
        bool hasPrevModel = false;
        std::uint64_t lastUsed = 0;
        // ADR-056 Tier 1. One buffer holds both halves: the per-record slot map at offset 0 and the
        // compact per-slot bend array at `bendOffset` (256-aligned, because a bind group entry's
        // offset must be). It is bound twice, at bindings 6 and 7, because WGSL cannot read one
        // region as two types. `sim` is the level-of-detail decision and the integrator state; it is
        // keyed to the object's records and rebuilt only when those change.
        wgpu::Buffer dynamics;
        std::uint64_t dynamicsBytes = 0;
        std::uint64_t bendOffset = 0;
        std::uint64_t bendBytes = 0;
        std::uint64_t simVersion = ~0ull;   // structureVersion the sim's grid was built from
        spatial::VegetationSim sim;
        bool simActive = false;             // a live active set this frame
    };
    struct DrawItem {
        std::size_t objectIndex;        // into scene.procedurals
        std::array<const CachedMesh*, scene::kMaxLodLevels> meshes{};
        const ObjectState* state;
        std::uint32_t offset;           // dynamic offset into the object uniform buffer
        std::uint32_t instanceCount;
        std::uint32_t lodCount = 1;
        bool indirect = false;          // draw through the compacted visible lists
        // The cull pass would have zeroed every level's instance count, so nothing is recorded in
        // any pass. Provable on the CPU from the object's whole-record bounds (objectFullyCulled).
        bool fullyCulled = false;
        // The same proof for the shadow caster list, and it is a *different* proof: an object the
        // camera cannot see may still be inside a cascade, and an object the camera sees at 200 m
        // is past every cascade. Before ADR-265 there was one flag, which is exactly the bug -- the
        // camera's verdict standing in for a question nobody asked.
        bool shadowFullyCulled = true;
        // The levels the cull pass could possibly have put a record on, from the same bounds
        // (rendering::objectLevelRange). Levels outside it are provably empty and their indirect
        // draws are not recorded; every level inside it is recorded whether or not the last
        // readback said it was occupied, because that readback is one to three frames old and the
        // frame an instance *arrives* on a level is exactly the frame it is wrong about.
        LevelRange levels{};
        // Effect Library Wave 2 (FXL): the owner's clip is live, so its back faces are drawn -- the
        // depth prepass never culls, and a back face it wrote where the clip removed the front one
        // would otherwise be left unshaded (the clear colour: solid black through a dissolving
        // saucer's holes). The mesh entities' `fxTwoSided`, for procedural nodes.
        bool fxTwoSided = false;
    };
    struct ComputeItem {
        const ObjectState* state;
        std::uint32_t count;
    };
    struct CullItem {
        const ObjectState* state;
        std::uint32_t count;
        std::uint32_t lodCount;
        // The compaction's level axis: `lodCount`, or twice it when this object also builds the
        // shadow caster list. One dispatch chain, two lists.
        std::uint32_t levels;
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
        statsSnapshot.assign(static_cast<std::size_t>(kCullSlots) * kCullStatsStride, 0u);
    }
    ~Impl() {
        // A MapAsync callback carries a raw StatsSlot pointer; let every pending one complete
        // while the slots still exist (the same contract as gpu::FrameTimeline).
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
    // `sharedVisible` (ADR-108): non-null when this object is a material part whose cull is done
    // by another object -- its draws read that object's visible lists, and it allocates no cull
    // buffers of its own.
    void ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive, std::uint32_t lodCount,
                             bool cullActive, std::uint32_t count, std::uint32_t simRecords,
                             std::uint32_t simSlots, const wgpu::Buffer& sharedVisible = nullptr,
                             std::uint32_t sharedStride = 0, std::uint32_t sharedLevels = 0);
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
    // ADR-703 (FXL): this renderer's copy of the frame's per-owner effect records, bound at group 1
    // binding 8 of every object group. Sized for the whole record budget (1 MiB) and never
    // replaced, so the per-object groups cached across frames never go stale on its account.
    wgpu::Buffer entityFx;
    wgpu::Buffer fieldBlock;
    wgpu::Buffer splineTable;
    wgpu::Buffer gridTable; // the simulated-grid table fields.wgsl binds at group 0 binding 15
    wgpu::Buffer emptyVisible;      // inert placeholder at group 1 binding 5 for uncalled objects
    wgpu::Buffer cullStats;         // kMaxProceduralObjects slots of kCullStatsStride u32
    // Every object's drawIndexedIndirect args, kMaxLodLevels to a slot, indexed by the object's
    // stats slot. One buffer rather than one per object, because the cost measured per *buffer* and
    // not per draw (ADR-051): eleven layers issuing 72 indirect draws against eleven buffers cost
    // 1.2 ms more than the same 72 against one, and one layer -- one buffer either way -- costs the
    // same both ways. Whatever the backend does per distinct indirect buffer, it does it in every
    // pass that reads one, and this frame has five.
    wgpu::Buffer indirectArgs;
    // ADR-287: this frame's shadow views, as world-space plane sets, and the uniform the cull pass
    // reads them out of. Set by `setShadowViews` before update(); empty means no shadow maps.
    wgpu::Buffer shadowVolumes;
    std::vector<FrustumPlanes> shadowViewPlanes;
    gpu::FrameTimeline* timeline = nullptr;
    std::vector<std::uint8_t> staging;
    // Reused between objects: the object's LOD uniform slots, laid out contiguously for one write.
    std::vector<std::uint8_t> deformerStaging;
    std::vector<std::uint32_t> statsZero; // the cull-stats prefix cleared each frame
    std::map<std::uint64_t, CachedMesh> meshes;
    std::map<std::string, ObjectState> objects;
    std::vector<DrawItem> items;
    std::vector<ComputeItem> computeItems;
    std::vector<CullItem> cullItems;
    // Parallel to cullItems. The uniforms are staged rather than written as they are built because
    // a material part reached later in the object loop appends itself to its lead's fanout list
    // (ADR-108); they all go up in one sweep after the loop, before the pass is encoded.
    std::vector<CullPassUniforms> cullUniformStaging;
    std::array<StatsSlot, 3> statsSlots{};
    std::vector<std::uint32_t> statsSnapshot; // latest completed stats readback
    int pendingStatsSlot = -1;
    double lastEffectorMs = -1.0;   // the latest completed effector-pass measurement
    double lastCullMs = -1.0;       // the latest completed cull-pass measurement
    bool passThisFrame = false;      // an effector pass was encoded in the current update()
    bool cullPassThisFrame = false;  // a cull pass was encoded in the current update()
    // ADR-056: the disturbance set is frame-global -- a body pushing through the valley pushes
    // through every layer of it -- and so is the ceiling on how many plants may be simulated at
    // once, because what has to stay bounded is the frame and not the meadow.
    wind::DisturbanceField disturbances;
    glm::vec3 lastCameraPosition{0.0f};
    bool hasLastCamera = false;
    int simRemaining = 0;
    std::uint32_t viewportWidth = 1920;
    std::uint32_t viewportHeight = 1080;
    std::uint64_t frame = 0;
    bool initialised = false;
    bool warnedLimit = false;
    // ADR-422: effector skips already reported, as "<object>/<field>/<op>/<reason>", so the log
    // says it once rather than sixty times a second. A message repeated at frame rate is a message
    // nobody reads, which is the same failure as no message at all seen from the other end.
    std::set<std::string> warnedEffectors;
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
        std::array<wgpu::BindGroupLayoutEntry, 9> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(ObjectUniforms);
        // ADR-703 (FXL): the per-owner effect records, read by `fs_proc` at `entityFx[fxA.w]` -- and
        // (Wave 2) by `vs_proc` for the owner's displacement.
        entries[8].binding = 8;
        entries[8].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[8].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[8].buffer.minBindingSize = sizeof(world::EntityFxRecord);
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
        // ADR-056: the Tier 1 slot map and the compact bend array. Objects with no simulated
        // specimens bind the same inert placeholder the visible list uses, and the vertex stage
        // never reads either (ProceduralUniforms::prevInfo.y is 0).
        entries[6].binding = 6;
        entries[6].visibility = wgpu::ShaderStage::Vertex;
        entries[6].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[6].buffer.minBindingSize = 4;
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Vertex;
        entries[7].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[7].buffer.minBindingSize = 16;
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
        // 4 = visible lists, 5 = indirect args, 6 = the shared stats buffer, 7 = the frame's shadow
        // volumes (ADR-287).
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(CullPassUniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.minBindingSize = kInstanceStride;
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Compute;
        entries[7].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[7].buffer.minBindingSize = sizeof(ShadowVolumeUniforms);
        for (std::size_t i = 2; i < 7; ++i) {
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
        // A new buffer is zero-filled: record 0 is neutral without an upload.
        desc.label = "procedural-entity-fx";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = static_cast<std::uint64_t>(world::kMaxEntityFxRecords) * sizeof(world::EntityFxRecord);
        im.entityFx = device.CreateBuffer(&desc);
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
        desc.size = static_cast<std::uint64_t>(kCullSlots) * kCullStatsStride * sizeof(std::uint32_t);
        im.cullStats = device.CreateBuffer(&desc);
        wgpu::BufferDescriptor idesc{};
        idesc.label = "procedural-cull-indirect";
        idesc.usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst |
                      wgpu::BufferUsage::CopySrc;
        idesc.size = static_cast<std::uint64_t>(kCullSlots) * scene::kMaxLodLevels * kIndirectStride;
        im.indirectArgs = device.CreateBuffer(&idesc);
        wgpu::BufferDescriptor sdesc{};
        sdesc.label = "procedural-cull-shadow-volumes";
        sdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        sdesc.size = sizeof(ShadowVolumeUniforms);
        im.shadowVolumes = device.CreateBuffer(&sdesc);
        // Zeroed: a frame that never calls setShadowViews has no shadow views, and an object whose
        // cull group is built before the first write must read "no views" rather than whatever the
        // driver left there.
        const ShadowVolumeUniforms empty{};
        im.context.queue().WriteBuffer(im.shadowVolumes, 0, &empty, sizeof(empty));
        for (Impl::StatsSlot& slot : im.statsSlots) {
            wgpu::BufferDescriptor readDesc{};
            readDesc.label = "procedural-cull-stats-read";
            readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            readDesc.size = desc.size;
            slot.read = device.CreateBuffer(&readDesc);
        }
    }
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

void ProceduralRenderer::setShadowViews(std::span<const FrustumPlanes> views) {
    // Kept rather than uploaded here: update() has to know the count before it decides which
    // objects build a shadow list at all, and the upload is one write it can make beside the rest.
    impl_->shadowViewPlanes.assign(views.begin(),
                                   views.begin() + static_cast<std::ptrdiff_t>(
                                                       std::min<std::size_t>(views.size(), kMaxShadowCullViews)));
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
    cached.cullRadius = sourceCullRadius(lo, hi);
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
    // make the reduced mesh differ from the source's -- and so is the source transform's scale,
    // which is what the impostor is now built at. Two scatter layers over the same asset differ in
    // exactly that: `Composition::flatten` normalises the asset's height onto `sourceTransform`, so
    // a 0.45 m fern layer and a 14 m canopy layer share a `meshHash` and must not share a quad.
    std::uint64_t key = object.meshHash;
    const auto mix = [&key](std::uint64_t v) { key ^= v + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2); };
    mix(0x10D0000ull + static_cast<std::uint64_t>(level));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(object.lod.impostorSize)));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(object.sourceTransform.scale.x)));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(object.sourceTransform.scale.y)));
    mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(object.sourceTransform.scale.z)));
    auto it = meshes.find(key);
    if (it == meshes.end()) {
        // What the level actually comes back as, against the level above it. A LOD level is only
        // worth selecting if it is smaller than its predecessor, and the decimator that builds
        // these is a vertex clustering with no way to guarantee its target: on the Quaternius trees
        // it returns most of the source (ADR-078 predicted exactly this of the preserving
        // simplifier, and it holds for this one too). A ladder whose rungs are all the same height
        // is invisible in every counter the frame prints -- `lod=2/30/124/0` reads like a working
        // LOD system either way -- so the achieved size is logged where a reader can see it. Taken
        // before the emplace: ensureMesh() inserts into the same map.
        const CachedMesh* base = ensureMesh(object);
        const std::uint32_t sourceTris = base != nullptr ? base->indexCount / 3 : 0;
        const CachedMesh uploaded =
            uploadMesh(scene::makeLodMesh(object.source, level, object.lod.impostorSize,
                                          object.sourceTransform.scale),
                       object.name);
        log::debug("procedural '{}': lod {} = {} triangles, {:.0f}% of the source mesh's {}",
                   object.name, level, uploaded.indexCount / 3,
                   sourceTris > 0 ? 100.0 * static_cast<double>(uploaded.indexCount / 3) /
                                        static_cast<double>(sourceTris)
                                  : 0.0,
                   sourceTris);
        it = meshes.emplace(key, uploaded).first;
        it->second.impostor = scene::lodLevelIsImpostor(object.source, level);
    }
    it->second.lastUsed = frame;
    return it->second.indexCount > 0 ? &it->second : nullptr;
}

void ProceduralRenderer::Impl::ensureObjectBuffers(ObjectState& state, std::uint64_t instanceBytes, bool needsLive,
                                                  std::uint32_t lodCount, bool cullActive, std::uint32_t count,
                                                  std::uint32_t simRecords, std::uint32_t simSlots,
                                                  const wgpu::Buffer& sharedVisible, std::uint32_t sharedStride,
                                                  std::uint32_t sharedLevels) {
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
    const bool ownsCull = cullActive && !sharedVisible;
    if (ownsCull && ensureCullBuffers(state, count, lodCount)) {
        rebuildGroup = true; // the draw groups slice `visible`
    }
    // ADR-108: whose visible lists the draws read. `state.visible` for an object that culls itself,
    // the lead's for a material part. Either can be replaced under this object between frames, so
    // the groups carry what they were built from and are rebuilt when it no longer matches.
    const wgpu::Buffer& visibleBuffer = sharedVisible ? sharedVisible : state.visible;
    const std::uint32_t visibleStride = sharedVisible ? sharedStride : state.visibleStride;
    const std::uint32_t visibleLevels = sharedVisible ? sharedLevels : state.cullLodCount;
    if (visibleBuffer.Get() != state.groupVisible.Get() || visibleStride != state.groupVisibleStride ||
        visibleLevels != state.groupVisibleLevels) {
        rebuildGroup = true;
    }
    // ADR-056: one grow-only buffer holding the slot map and the bend array. Allocated only for a
    // layer that asked to be simulated, so a world without Tier 1 has the buffers it always had.
    if (simRecords > 0 && simSlots > 0) {
        const std::uint64_t slotBytes = (static_cast<std::uint64_t>(simRecords) * 4 + 255) / 256 * 256;
        const std::uint64_t bendBytes = std::max<std::uint64_t>(static_cast<std::uint64_t>(simSlots) * 16, 256);
        if (!state.dynamics || state.dynamicsBytes < slotBytes + bendBytes) {
            wgpu::BufferDescriptor desc{};
            desc.label = "procedural-plant-dynamics";
            desc.size = slotBytes + bendBytes;
            desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
            state.dynamics = device.CreateBuffer(&desc);
            state.dynamicsBytes = slotBytes + bendBytes;
            state.bendOffset = slotBytes;
            state.bendBytes = bendBytes;
            state.simVersion = ~0ull; // the slot map in the new buffer is not the one we mirrored
            rebuildGroup = true;
        }
    }
    if (rebuildGroup) {
        // One draw group per (record buffer, LOD level): the level picks its deformer/time slot
        // and its slice of the visible list. Uncalled objects bind the inert placeholder, which
        // the vertex shader never reads (ProceduralUniforms::fieldInfo.w is 0).
        // `listLevel` is the slice of `visible` this group reads: `level` for the camera list and
        // `visibleLevels + level` for the shadow caster list, which is the same layout the cull
        // pass's doubled level axis writes (shaders/cull.wgsl `sliceOf`).
        auto drawGroup = [&](const wgpu::Buffer& records, std::uint64_t bytes, std::uint32_t level,
                             std::uint32_t listLevel, const char* label) {
            std::array<wgpu::BindGroupEntry, 9> entries{};
            entries[0].binding = 0;
            entries[0].buffer = objectUniforms;
            entries[0].size = sizeof(ObjectUniforms);
            entries[8].binding = 8;
            entries[8].buffer = entityFx;
            entries[8].size = entityFx.GetSize();
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
            if (cullActive && visibleBuffer && level < visibleLevels) {
                entries[5].buffer = visibleBuffer;
                entries[5].offset = static_cast<std::uint64_t>(listLevel) * visibleStride * sizeof(std::uint32_t);
                entries[5].size = static_cast<std::uint64_t>(visibleStride) * sizeof(std::uint32_t);
            } else {
                entries[5].buffer = emptyVisible;
                entries[5].size = 256;
            }
            entries[6].binding = 6;
            entries[7].binding = 7;
            if (state.dynamics) {
                entries[6].buffer = state.dynamics;
                entries[6].size = state.bendOffset;
                entries[7].buffer = state.dynamics;
                entries[7].offset = state.bendOffset;
                entries[7].size = state.bendBytes;
            } else {
                entries[6].buffer = emptyVisible;
                entries[6].size = 256;
                entries[7].buffer = emptyVisible;
                entries[7].size = 256;
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
        state.shadowGroups = {};
        state.shadowGroupsLive = {};
        for (std::uint32_t level = 0; level < levels; ++level) {
            state.groups[level] =
                drawGroup(state.instances, state.instanceBytes, level, level, "procedural-object-group");
            if (state.live) {
                state.groupsLive[level] =
                    drawGroup(state.live, state.liveBytes, level, level, "procedural-object-group-live");
            }
            // ADR-287's second list. Built unconditionally for a culled object: whether it casts is
            // a per-frame property and these are cached across frames, so building them only for a
            // caster would rebuild every group the frame a light is switched on.
            if (cullActive && visibleBuffer && level < visibleLevels) {
                state.shadowGroups[level] = drawGroup(state.instances, state.instanceBytes, level,
                                                      visibleLevels + level, "procedural-object-group-shadow");
                if (state.live) {
                    state.shadowGroupsLive[level] =
                        drawGroup(state.live, state.liveBytes, level, visibleLevels + level,
                                  "procedural-object-group-shadow-live");
                }
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
        state.groupVisible = visibleBuffer;
        state.groupVisibleStride = visibleStride;
        state.groupVisibleLevels = visibleLevels;
        // The cull groups reference the record buffers, so they are rebuilt with them.
        state.cullGroup = nullptr;
        state.cullGroupLive = nullptr;
    }
    if (!ownsCull) {
        return;
    }
    auto cullGroup = [&](const wgpu::Buffer& records, std::uint64_t bytes, const char* label) {
        std::array<wgpu::BindGroupEntry, 8> entries{};
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
        entries[5].buffer = indirectArgs;
        entries[5].size = indirectArgs.GetSize();
        entries[6].binding = 6;
        entries[6].buffer = cullStats;
        entries[6].size = cullStats.GetSize();
        // The frame's shadow volumes: one buffer, bound by every object's cull group, rewritten
        // once per frame. Nothing here has to be invalidated when the views move.
        entries[7].binding = 7;
        entries[7].buffer = shadowVolumes;
        entries[7].size = sizeof(ShadowVolumeUniforms);
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
    // ADR-287: every one of these holds two lists. `lodIndex` carries the camera classification at
    // [0, count) and the shadow one at [count, 2 * count); `blockSums` and `visible` are indexed by
    // a level axis that runs 0..2 * lodCount - 1. Allocated for both whether or not this frame's
    // object casts, because whether it does is a per-frame question and these buffers are grow-only
    // -- a scatter that starts casting must not cost a reallocation and a frame of empty lists.
    bool grew = false;
    if (count > state.cullCapacity || lodCount > state.cullLodCapacity || !state.visible) {
        const std::uint32_t levels = std::max(lodCount, state.cullLodCapacity);
        const auto storage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        wgpu::BufferDescriptor desc{};
        desc.usage = storage;
        desc.label = "procedural-lod-index";
        desc.size = static_cast<std::uint64_t>(stride) * 2 * sizeof(std::uint32_t);
        state.lodIndex = device.CreateBuffer(&desc);
        // Zeroed, because classification now reads last frame's level out of this buffer before
        // overwriting it (ADR-082) and a fresh allocation otherwise holds whatever the driver left
        // there -- which would be read as "this instance was already at level 4 billion" and drop
        // the whole object to its coarsest mesh for one frame after every grow.
        {
            const std::vector<std::uint32_t> zeros(static_cast<std::size_t>(stride) * 2, 0u);
            context.queue().WriteBuffer(state.lodIndex, 0, zeros.data(),
                                        static_cast<std::size_t>(stride) * 2 * sizeof(std::uint32_t));
        }
        desc.label = "procedural-cull-blocksums";
        desc.size = static_cast<std::uint64_t>(blocks) * levels * 2 * sizeof(std::uint32_t);
        state.blockSums = device.CreateBuffer(&desc);
        desc.label = "procedural-visible";
        desc.size = static_cast<std::uint64_t>(stride) * levels * 2 * sizeof(std::uint32_t);
        state.visible = device.CreateBuffer(&desc);
        state.cullCapacity = count;
        state.cullLodCapacity = levels;
        state.visibleStride = stride;
        state.cullGroup = nullptr;
        state.cullGroupLive = nullptr;
        grew = true;
    }
    // Not grow-only, and deliberately: this is the number the shader lays its two lists out with,
    // so the draw groups have to be rebuilt against it whenever it moves in either direction.
    if (state.cullLodCount != lodCount) {
        state.cullLodCount = lodCount;
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

// The tier value the shader compares against, mirrored in the header so it need not include
// render_quality.hpp. Pinned here, where both are visible.
static_assert(ProceduralRenderer::MaterialTierFlatValue == static_cast<int>(MaterialTier::Flat),
              "the mirrored flat-tier value drifted from the enum");

void ProceduralRenderer::setFlatTierFromRung(int rung) { flatTierFromRung_ = rung; }

void ProceduralRenderer::setLodHysteresisAllowed(bool allowed) { lodHysteresisAllowed_ = allowed; }

void ProceduralRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void ProceduralRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double effector = im.timeline->msFor("effectors");
        if (effector >= 0.0) {
            im.lastEffectorMs = effector;
        }
        const double cull = im.timeline->msFor("cull");
        if (cull >= 0.0) {
            im.lastCullMs = cull;
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
    im.cullUniformStaging.clear();
    im.passThisFrame = false;
    // ADR-703 (FXL): the frame's effect records, before any object slot names one. Nothing is
    // written on a frame with no lane effect: no object's `fxA` points into the buffer then.
    if (!scene.entityFx.empty()) {
        const std::size_t count = std::min<std::size_t>(scene.entityFx.records.size(), world::kMaxEntityFxRecords);
        im.context.queue().WriteBuffer(im.entityFx, 0, scene.entityFx.records.data(),
                                       count * sizeof(world::EntityFxRecord));
    }
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
    // ADR-287: this frame's shadow views, uploaded once for every object's cull. `SceneRenderer`
    // hands over the plane sets it built for the entity second cull, so the two caster rules are
    // asking about the same volumes and not about two fits of them (§37).
    const std::span<const FrustumPlanes> shadowViewPlanes(im.shadowViewPlanes);
    {
        ShadowVolumeUniforms volumes{};
        volumes.info.x = static_cast<std::uint32_t>(shadowViewPlanes.size());
        for (std::size_t v = 0; v < shadowViewPlanes.size(); ++v) {
            for (std::size_t k = 0; k < 6; ++k) {
                volumes.planes[v * 6 + k] = shadowViewPlanes[v][k];
            }
        }
        queue.WriteBuffer(im.shadowVolumes, 0, &volumes, sizeof(volumes));
    }

    // ---- ADR-056: the wind field, the disturbance set and the frame's simulation budget ----
    const wind::WindUniforms frameWind = wind::packWind(scene.environment.wind);
    const float simDt = std::clamp(static_cast<float>(time.deltaTime), 1e-5f, 0.25f);
    im.disturbances.advance(simDt);
    if (scene.environment.wind.wake.enabled) {
        // The first use case: the camera walking through the vegetation. Its velocity is taken from
        // where it was last frame rather than from any camera rig, so it works for a keyframed
        // camera, a live one and a scripted flythrough alike.
        const glm::vec3 velocity =
            im.hasLastCamera ? (scene.camera.position - im.lastCameraPosition) / simDt : glm::vec3(0.0f);
        im.disturbances.trackBody(scene.camera.position, velocity, scene.environment.wind.wake);
    }
    im.lastCameraPosition = scene.camera.position;
    im.hasLastCamera = true;
    im.simRemaining = std::max(scene.environment.wind.simBudget, 0);

    // ---- ADR-108: the material parts of one asset are ONE spatial instance set ----------------
    // A multi-material glTF arrives here as several objects over the same cloud, the same seed and
    // the same placements, differing only in mesh, material and per-instance colour. Culling each
    // of them separately does the same arithmetic several times and counts one tree as two.
    // `leadOf[i]` names the object whose cull decision object i takes; the rest of this function
    // then culls the lead once and writes every part's indirect args from that one result.
    //
    // A part only joins its lead when the two really are the same placement, because the whole
    // claim rests on that: the same record count, the same object matrix, the same LOD ladder, and
    // neither moving its records on the GPU (an effector pass rewrites positions after the CPU has
    // stopped looking, so a lead's decision would be about records the part does not have).
    // Anything that fails a check culls itself, exactly as it did before this existed.
    constexpr std::size_t kNoLead = static_cast<std::size_t>(-1);
    const std::size_t objectCount = scene.procedurals.size();
    std::vector<std::size_t> leadOf(objectCount, kNoLead);
    std::vector<float> groupRadius(objectCount, 0.0f); // the lead's bound must cover every part
    // The same, for the radius the LOD ladder measures size with. A separate array because it is a
    // separate quantity: `groupRadius` must contain every part about the record position and this
    // one describes how large the asset looks.
    std::vector<float> groupLodRadius(objectCount, 0.0f);
    // A cached mesh's radius is measured on the *uploaded* mesh, which is the source before the
    // object's own `sourceTransform`. That transform is where a scatter layer's size actually lives
    // -- composition.cpp puts the "make this 0.45 m tall" normalisation there deliberately, because
    // `distributionTransform` would scale the placements along with the mesh -- and the draw applies
    // it (`u.sourceMatrix`). The ladder did not, so it compared a projected radius it was not
    // actually computing: only three of Glowmere's thirty procedurals have a unit source scale, and
    // `elder-crown` was sized 8.2x too small (ADR-152). The wind code already travels its bounds
    // through this same matrix for the same reason.
    const auto sourceScaleOf = [](const scene::ProceduralGeometry& g) {
        const glm::mat4 m = g.sourceTransform.matrix();
        return std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                         glm::length(glm::vec3(m[2]))});
    };
    {
        std::map<std::string, std::size_t> byName;
        bool anyPart = false;
        for (std::size_t i = 0; i < objectCount; ++i) {
            byName.emplace(scene.procedurals[i].name, i);
            anyPart = anyPart || !scene.procedurals[i].partOf.empty();
        }
        std::vector<std::uint32_t> fanoutUsed(objectCount, 0);
        for (std::size_t i = 0; anyPart && i < objectCount; ++i) {
            const auto& part = scene.procedurals[i];
            if (part.partOf.empty()) {
                continue;
            }
            const auto found = byName.find(part.partOf);
            if (found == byName.end() || found->second == i) {
                continue;
            }
            const std::size_t lead = found->second;
            const auto& head = scene.procedurals[lead];
            const glm::mat4 partModel = i < objectMatrices.size() ? objectMatrices[i] : glm::mat4(1.0f);
            const glm::mat4 headModel = lead < objectMatrices.size() ? objectMatrices[lead] : glm::mat4(1.0f);
            if (!head.partOf.empty() || head.instances.size() != part.instances.size() ||
                part.instances.empty() || partModel != headModel || !head.effectors.empty() ||
                !part.effectors.empty() || head.lod.lodCount != part.lod.lodCount ||
                head.lod.cull != part.lod.cull || head.lod.lodByScreenSize != part.lod.lodByScreenSize ||
                head.lod.maxDistance != part.lod.maxDistance ||
                head.lod.minScreenRadius != part.lod.minScreenRadius ||
                !std::equal(std::begin(head.lod.lodDistances), std::end(head.lod.lodDistances),
                            std::begin(part.lod.lodDistances)) ||
                head.lod.lodSpread != part.lod.lodSpread ||
                head.lod.lodHysteresis != part.lod.lodHysteresis ||
                fanoutUsed[lead] >= kMaxCullFanout) {
                continue;
            }
            ++fanoutUsed[lead];
            leadOf[i] = lead;
            // The shared bounding sphere has to contain every part, not just the one that happens
            // to be part 0: a trunk's radius would cull a canopy that is still on screen. Resolved
            // here rather than in the loop below because the lead is reached first.
            if (const Impl::CachedMesh* partMesh = im.ensureMesh(part); partMesh != nullptr) {
                groupRadius[lead] = std::max(groupRadius[lead], partMesh->cullRadius * sourceScaleOf(part));
                groupLodRadius[lead] = std::max(groupLodRadius[lead], partMesh->radius * sourceScaleOf(part));
            }
        }
    }
    // Per-object bookkeeping for the pass below, by index into scene.procedurals.
    std::vector<Impl::ObjectState*> stateOf(objectCount, nullptr);
    std::vector<std::size_t> cullItemOf(objectCount, kNoLead);
    std::vector<char> fullyCulledOf(objectCount, 0);
    std::vector<char> shadowFullyCulledOf(objectCount, 1);
    std::vector<LevelRange> levelRangeOf(objectCount);
    // ADR-038's depth bands move the ladder per band; `objectLevelRange` needs the extremes over
    // every band the scene declares. 1.0 seeds them because it is the value a scene with no bands
    // uses, and keeping it in when there are bands only widens the range -- which can make the
    // proof less tight but never wrong, and the shader's band search never returns to 1.0 once a
    // scene declares one.
    float bandDetailMin = 1.0f;
    float bandDetailMax = 1.0f;
    for (const scene::DepthLayer& layer : scene.composition.layers) {
        bandDetailMin = std::min(bandDetailMin, layer.detail);
        bandDetailMax = std::max(bandDetailMax, layer.detail);
    }

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
        // ADR-108. Resolved here rather than in the pre-pass because it needs the lead's GPU state,
        // which exists only once the lead has been through this loop -- and a part whose lead was
        // skipped this frame (invisible, no mesh, past the object limit) must fall back to culling
        // itself rather than read buffers nobody filled.
        const std::size_t leadIndex = leadOf[i];
        const bool isPart = leadIndex != kNoLead && stateOf[leadIndex] != nullptr;
        const bool sharesCull = isPart && cullActive && cullItemOf[leadIndex] != kNoLead;
        const Impl::ObjectState* leadState = sharesCull ? stateOf[leadIndex] : nullptr;
        std::array<const Impl::CachedMesh*, scene::kMaxLodLevels> lodMeshes{};
        lodMeshes[0] = mesh;
        for (std::uint32_t level = 1; level < lodCount; ++level) {
            const Impl::CachedMesh* reduced = im.ensureLodMesh(object, static_cast<int>(level));
            lodMeshes[level] = reduced != nullptr ? reduced : mesh; // a failed level falls back to the source
        }
        // ---- ADR-055 species response ----
        // Resolved here rather than with the rest of the uniforms because Tier 1 (ADR-056) needs the
        // same numbers, earlier: the two tiers must be driven by one transfer function, not two.
        const bool windActive = scene.environment.wind.active() && object.motion.active();
        wind::MotionResponse windResponse;
        float windBaseY = 0.0f;
        float windExtent = 1.0f;
        if (windActive) {
            windResponse = wind::motionResponse(scene.environment.wind, object.motion);
            // The height profile is measured in the space the deformer stack sees, which is after
            // the source transform (the layer's "make this thing 0.45 m tall" scale), so the bounds
            // have to travel through the same matrix.
            const glm::mat4 srcMatrix = object.sourceTransform.matrix();
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 c((corner & 1) != 0 ? mesh->boundsMax.x : mesh->boundsMin.x,
                                  (corner & 2) != 0 ? mesh->boundsMax.y : mesh->boundsMin.y,
                                  (corner & 4) != 0 ? mesh->boundsMax.z : mesh->boundsMin.z);
                const float y = (srcMatrix * glm::vec4(c, 1.0f)).y;
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }
            windBaseY = lo;
            windExtent = std::max(hi - lo, 1e-4f);
        }

        // ---- effectors: the usable ones (enabled, field bound to a slot), in order ----
        EffectorPassUniforms eff{};
        std::uint32_t effectorCount = 0;
        if (fields != nullptr) {
            for (const auto& e : object.effectors) {
                if (effectorCount >= static_cast<std::uint32_t>(spatial::kMaxEffectors)) {
                    break;
                }
                if (!e.enabled) {
                    continue;
                }
                // ADR-422. Three silent skips used to live on this line, and a silently skipped op
                // is the hook somebody reaches for first. `EffectorOp::Velocity` and
                // `::Attribute` exist, serialise, round-trip, appear in the World panel's effector
                // list and are implemented on the CPU path in `spatial::applyEffectors` -- and this
                // pass, which is the only one that runs in a rendered frame, drops them on the
                // floor. An author who sets one gets an effector that is listed, saved, and inert.
                //
                // They are still skipped, because making them work is a real piece of work -- a
                // velocity effector needs somewhere to put a velocity, which is the deformation
                // contract's whole subject (ADR-422's `VertexCorrespondence`) -- but they are no
                // longer skipped QUIETLY. That is the distinction ADR-225 draws: a setting the
                // application does not keep is not a setting, and the honest interim state of one
                // is a stated problem rather than a still picture.
                const auto skip = [&](const char* reason) {
                    const std::string key = object.name + "/" + e.field + "/" +
                                            spatial::effectorOpName(e.op) + "/" + reason;
                    if (im.warnedEffectors.insert(key).second) {
                        log::warn("procedural '{}': effector {} -> {} is skipped by the GPU pass: {}",
                                  object.name, e.field, spatial::effectorOpName(e.op), reason);
                    }
                };
                if (e.op == spatial::EffectorOp::Velocity || e.op == spatial::EffectorOp::Attribute) {
                    skip("this op has no GPU implementation; it runs only on the CPU effector path, "
                         "which no rendered frame takes");
                    continue;
                }
                const int fieldSlot = fields->slotOf(e.field);
                if (fieldSlot < 0) {
                    skip("no field of that name reached the GPU field table");
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
        // ---- ADR-056 Tier 1: does this layer ask to be simulated, and is there room ----
        // An object with effectors is excluded: the GPU moves its records after this point, so the
        // CPU positions the level-of-detail decision reads would be the wrong ones.
        const wind::SimLod& simLod = object.motion.simulate;
        const int simShare = std::min(simLod.budget, im.simRemaining);
        const bool simWanted = simLod.enabled && windActive && !usesLive && simShare > 0;
        im.ensureObjectBuffers(state, instanceBytes, usesLive, lodCount, cullActive, instanceCount,
                               simWanted ? instanceCount : 0u,
                               simWanted ? static_cast<std::uint32_t>(simShare) : 0u,
                               leadState != nullptr ? leadState->visible : wgpu::Buffer(nullptr),
                               leadState != nullptr ? leadState->visibleStride : 0u,
                               leadState != nullptr ? leadState->cullLodCount : 0u);
        state.usesLive = usesLive;
        state.statsSlot = slot;
        if (state.structureVersion != object.structureVersion || state.uploadedCount != object.instances.size()) {
            queue.WriteBuffer(state.instances, 0, object.instances.data(), instanceBytes);
            state.structureVersion = object.structureVersion;
            state.uploadedCount = object.instances.size();
            // The records are what the bounds are made of, so they are rebuilt exactly here and
            // nowhere else: once per scatter, not once per frame.
            state.bounds = instanceBounds(object.instances);
            ++stats_.uploads;
        }
        const glm::mat4 model = i < objectMatrices.size() ? objectMatrices[i] : glm::mat4(1.0f);
        // Empty work, eliminated before it is encoded: when the object's whole record set is
        // outside the frustum, past maxDistance or below minScreenRadius, every level's instance
        // count would come back zero. Its four cull dispatches and its indirect draws in every
        // pass -- prepass, lit, each shadow cascade -- are skipped. An object with effectors is
        // excluded because the GPU moves its records after these bounds were taken.
        // ADR-108: the bound has to cover every material part of the asset, or the trunk's radius
        // culls a canopy that is still on screen. `groupRadius` is zero for an object with no parts.
        const float cullRadius = std::max(mesh->cullRadius * sourceScaleOf(object), groupRadius[i]);
        // What the ladder measures with: the tight sphere about the source's box, in world units.
        // Not the cull's sphere -- see shaders/cull.wgsl's note above `lodRadius` and
        // rendering/visibility.hpp. In source units, because that is what the uniform carries and
        // what the shader multiplies by the record and object scales.
        const float lodSourceRadius = std::max(mesh->radius * sourceScaleOf(object),
                                               groupLodRadius[i]);
        const bool fullyCulled = isPart ? fullyCulledOf[leadIndex] != 0
                                        : (cullActive && !usesLive &&
                                           objectFullyCulled(lodSettings, planes, cullCamera, model,
                                                             state.bounds, cullRadius,
                                                             scene.detailLimits.proceduralDistanceCull));
        if (fullyCulled) {
            ++stats_.culledObjects;
        }
        // ADR-287: does any shadow view reach this object at all? A separate question from the one
        // above and answered separately -- an object behind the camera can be inside a cascade, and
        // an object the camera sees at 200 m is past every cascade, because the cascades stop at the
        // shadow range. An object with effectors can prove nothing (the GPU moves its records after
        // these bounds were taken), so it builds the list rather than skipping it.
        const bool casts = object.castsShadow && !shadowViewPlanes.empty();
        const bool shadowFullyCulled =
            !casts || (isPart ? shadowFullyCulledOf[leadIndex] != 0
                              : (cullActive && !usesLive &&
                                 objectFullyCulledForShadows(lodSettings, shadowViewPlanes, cullCamera, model,
                                                             state.bounds, cullRadius,
                                                             scene.detailLimits.proceduralDistanceCull)));
        // The pass runs when EITHER list has something to say. An object the camera cannot see but
        // a cascade can now costs a cull dispatch it did not used to cost -- that is the price of
        // the second list and it is the whole of it on the CPU side.
        const bool needsCull = cullActive && (!fullyCulled || !shadowFullyCulled);
        // Which rungs this object's records could be on. A part shares its lead's decision
        // (ADR-108), so it shares the range; an object whose records the GPU moves after these
        // bounds were taken can prove nothing and records every level.
        LevelRange levels{};
        levels.highest = static_cast<int>(lodCount) - 1;
        if (cullActive && !usesLive && state.bounds.valid) {
            if (isPart) {
                levels = levelRangeOf[leadIndex];
            } else {
                // ADR-186 again: with the rungs lifted the thresholds uniform is zeroed, which
                // ends the ladder at rung 0. The proof has to be about the ladder the pass was
                // actually given.
                scene::LodSettings effective = lodSettings;
                if (!scene.detailLimits.proceduralLodRungs) {
                    effective.lodDistances[0] = 0.0f;
                    effective.lodDistances[1] = 0.0f;
                    effective.lodDistances[2] = 0.0f;
                }
                // The ladder's radius, because this proof is about rungs.
                levels = objectLevelRange(effective, cullCamera, model, state.bounds, lodSourceRadius,
                                          bandDetailMin, bandDetailMax, lodHysteresisAllowed_);
            }
        }
        levelRangeOf[i] = levels;

        // ---- ADR-056 Tier 1: choose, integrate, upload ----
        // Everything here is proportional to the *active* set. The grid is built once per scatter;
        // the per-frame work is one pass over the plants that hold a slot, a bounded look at the
        // records near the camera, one contiguous write of the bend array and a handful of writes
        // where the active set changed.
        state.simActive = false;
        if (simWanted && state.dynamics && !fullyCulled) {
            if (state.simVersion != object.structureVersion) {
                state.sim.setInstances(object.instances);
                state.simVersion = object.structureVersion;
            }
            spatial::VegetationSim::Frame sf;
            sf.objectToWorld = model;
            sf.cameraPosition = cullCamera.position;
            sf.projScale = cullCamera.projScale;
            sf.sourceRadius = mesh->cullRadius;
            sf.extentY = windExtent;
            sf.renderTime = static_cast<float>(time.renderTime);
            sf.deltaTime = static_cast<float>(time.deltaTime);
            sf.budget = simShare;
            sf.wind = frameWind;
            sf.response = windResponse;
            sf.motion = object.motion;
            sf.disturbances = &im.disturbances;
            state.sim.update(sf);

            // Clamped rather than assumed: the budget a layer is given varies with what the
            // layers before it took, and the buffer only ever grows.
            const std::uint64_t bendUsed =
                std::min(static_cast<std::uint64_t>(state.sim.slotCount()) * 16, state.bendBytes);
            if (bendUsed > 0) {
                queue.WriteBuffer(state.dynamics, state.bendOffset, state.sim.dynamics().data(), bendUsed);
            }
            const std::span<const std::uint32_t> map = state.sim.slots();
            if (state.sim.slotsDirtyAll()) {
                queue.WriteBuffer(state.dynamics, 0, map.data(), static_cast<std::uint64_t>(map.size()) * 4);
                state.sim.markSlotsUploaded();
            } else {
                for (const auto& span : state.sim.dirtySlots()) {
                    queue.WriteBuffer(state.dynamics, static_cast<std::uint64_t>(span.first) * 4,
                                      map.data() + span.first, static_cast<std::uint64_t>(span.count) * 4);
                    ++stats_.simSlotWrites;
                }
            }
            state.simActive = state.sim.activeCount() > 0;
            im.simRemaining -= static_cast<int>(state.sim.activeCount());
            ++stats_.simObjects;
            stats_.simActive += state.sim.activeCount();
            stats_.simAwake += state.sim.awakeCount();
            stats_.simExamined += state.sim.examinedCount();
        } else if (state.sim.activeCount() > 0) {
            // The layer stopped asking. Hand every slot back and let the shader fall through to
            // Tier 0 -- which is what the buffer already says once the map is cleared.
            spatial::VegetationSim::Frame off;
            off.motion = object.motion;
            off.motion.simulate.enabled = false;
            state.sim.update(off);
            const std::span<const std::uint32_t> map = state.sim.slots();
            if (state.dynamics && !map.empty()) {
                queue.WriteBuffer(state.dynamics, 0, map.data(), static_cast<std::uint64_t>(map.size()) * 4);
            }
        }
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
        // ---- ADR-055 Tier 0 vegetation motion ----
        // The whole species model is resolved here, once per draw: two oscillator transfer
        // functions and a phase lag become three gains and a delay, and the vertex stage does
        // arithmetic. windSway.w gates it, and is uniform across the draw, so a boulder layer with
        // no sensitivity costs exactly what it cost before this existed.
        u.windSway = glm::vec4(0.0f);
        u.windTiming = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        u.windPlant = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        if (windActive) {
            const wind::MotionResponse& r = windResponse;
            u.windSway = glm::vec4(r.steadyGain, r.gustGain, r.flutterGain, 1.0f);
            u.windTiming = glm::vec4(r.swayDelay, r.flutterOmega, r.bendCurve, r.bendLimit);
            u.windPlant = glm::vec4(windBaseY, 1.0f / windExtent, windExtent, r.amplitudeVariance);
            ++stats_.windObjects;
        }
        // ADR-057: the living chromatic field. Wavelength is pre-inverted and the drift rate turned
        // into radians per second here, so the vertex stage spends no divides.
        const auto& mv = object.materialVariation;
        u.chroma = glm::vec4(0.0f);
        if (mv.chromaDrift > 0.0f && mv.chromaDriftScale > 1e-3f) {
            u.chroma = glm::vec4(mv.chromaDrift, wind::kTau / mv.chromaDriftScale,
                                 wind::kTau * mv.chromaDriftSpeed, 0.0f);
        }
        // Velocity needs the same chain evaluated at the previous frame's time (ADR-035); prevInfo.y
        // switches the Tier 1 lookup on, and is uniform across the draw.
        u.prevInfo = glm::vec4(static_cast<float>(time.renderTime - time.deltaTime),
                               state.simActive ? 1.0f : 0.0f, 0.0f, 0.0f);
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
        // One slot per LOD level. A level whose mesh is an impostor quad takes the same shader path
        // as a Point source; fieldInfo.w switches the visible-list indirection on.
        //
        // The flag comes from the *mesh* and not from the level index. It used to read
        // `isPoint || level >= 2`, which was true when ADR-029 wrote it and false from ADR-085
        // onwards: that ADR gave a Mesh source simplified meshes at every level, and those were
        // then drawn through the billboard path -- their vertices taken as offsets in the camera's
        // basis, skipping the source transform and the deformer stack. A 14 m production tree drew
        // at rung 2 as a 7.3 m flat shape centred on the foot of its trunk (measured:
        // tests/rendering/test_lod_gpu.cpp).
        // With the defaults (no culling, one level) this is the single write it has always been.
        // The levels differ in one float, and their slots are contiguous, so they go up as one
        // write rather than as lodCount of them (four per object, forty-eight in a world frame).
        im.deformerStaging.assign(static_cast<std::size_t>(lodCount) * kDeformerSlotStride, 0);
        for (std::uint32_t level = 0; level < lodCount; ++level) {
            const bool billboard =
                isPoint || (lodMeshes[level] != nullptr && lodMeshes[level]->impostor);
            u.fieldInfo = glm::vec4(static_cast<float>(emissiveSlot), object.emissiveFieldAmount,
                                    billboard ? 1.0f : 0.0f, cullActive ? 1.0f : 0.0f);
            // ADR-155: this rung's tier. Rung tracks projected size, so demoting from rung N
            // down leaves everything nearer the camera at Full. No hero check is needed and none
            // is possible here -- heroes live on the Composition, not the Scene the renderer sees
            // -- but a hero is by definition large on screen and therefore already at rung 0.
            // `elder-crown` measures ~330 px across in the canonical frame.
            const bool demote =
                flatTierFromRung_ >= 0 && static_cast<int>(level) >= flatTierFromRung_;
            u.prevInfo.z = demote ? static_cast<float>(MaterialTierFlatValue) : 0.0f;
            std::memcpy(im.deformerStaging.data() + static_cast<std::size_t>(level) * kDeformerSlotStride, &u,
                        sizeof(u));
        }
        queue.WriteBuffer(state.deformers, 0, im.deformerStaging.data(), im.deformerStaging.size());
        if (isPoint) {
            ++stats_.pointObjects;
        }

        if (needsCull && sharesCull) {
            // The lead classifies these records; this part needs only its own indirect args
            // written from the lead's per-level counts, with its own mesh's index counts. It is
            // appended to the lead's uniform, which is uploaded after this loop precisely so a
            // part reached later can still get into it.
            CullPassUniforms& lead = im.cullUniformStaging[cullItemOf[leadIndex]];
            const std::uint32_t used = lead.fanoutInfo.x;
            if (used < kMaxCullFanout) {
                CullFanout& entry = lead.fanout[used];
                // x = the part's camera slot, y = its shadow caster list's (ADR-287).
                entry.slot = glm::uvec4(slot, kShadowSlotBase + slot, 0u, 0u);
                entry.indexCounts = glm::uvec4(0u);
                for (std::uint32_t level = 0; level < lodCount; ++level) {
                    entry.indexCounts[static_cast<int>(level)] = lodMeshes[level]->indexCount;
                }
                lead.fanoutInfo.x = used + 1;
            }
        } else if (needsCull) {
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
            // ADR-186: an offline render lifts the distance limits. Zero is already this pass's
            // "no limit" for both, and a zero first threshold already ends the ladder at rung 0
            // (see LodSettings), so lifting them needs no shader change and no second code path --
            // the uniform is simply filled with the values that mean "everything, at full detail".
            //
            // Frustum culling is untouched either way. It removes only what is off screen, which is
            // not a reduction in what the frame shows.
            const bool limitDistance = scene.detailLimits.proceduralDistanceCull;
            const bool limitRungs = scene.detailLimits.proceduralLodRungs;
            cull.limits = glm::vec4(limitDistance ? std::max(lodSettings.maxDistance, 0.0f) : 0.0f,
                                    limitDistance ? std::max(lodSettings.minScreenRadius, 0.0f) : 0.0f,
                                    cullRadius, std::max(objectScale, 1e-6f));
            cull.thresholds = limitRungs ? glm::vec4(lodSettings.lodDistances[0], lodSettings.lodDistances[1],
                                                     lodSettings.lodDistances[2], lodSourceRadius)
                                         : glm::vec4(0.0f, 0.0f, 0.0f, lodSourceRadius);
            cull.stability = glm::vec4(std::clamp(lodSettings.lodSpread, 0.0f, 0.5f),
                                       lodHysteresisAllowed_
                                           ? std::clamp(lodSettings.lodHysteresis, 0.0f, 0.5f)
                                           : 0.0f,
                                       0.0f, 0.0f);
            const std::uint32_t blocks = std::max((instanceCount + kCullScanBlock - 1) / kCullScanBlock, 1u);
            cull.counts = glm::uvec4(instanceCount, lodCount, state.visibleStride, blocks);
            // ADR-038: the composition's depth bands thin instances and move the LOD ladder.
            // With no bands the count is zero and classification is unchanged.
            std::uint32_t bandCount = 0;
            for (const scene::DepthLayer& layer : scene.composition.layers) {
                if (bandCount >= kMaxCullDepthLayers) {
                    break;
                }
                cull.depthLayers[bandCount++] = glm::vec4(layer.start, layer.end, layer.density, layer.detail);
            }
            cull.flags = glm::uvec4(lodSettings.cull ? 1u : 0u, lodSettings.lodByScreenSize ? 1u : 0u, slot, bandCount);
            // ADR-287. The shadow half of the pass runs only for an object some shadow view can
            // reach: a scatter the cascades cannot see pays exactly what it paid before, which is
            // most of an outdoor frame's ecology, because the cascades stop at 77 m.
            cull.shadowInfo = glm::uvec4(shadowFullyCulled ? 0u : 1u, kShadowSlotBase + slot, 0u, 0u);
            cull.indexCounts = glm::uvec4(0u);
            for (std::uint32_t level = 0; level < lodCount; ++level) {
                cull.indexCounts[static_cast<int>(level)] = lodMeshes[level]->indexCount;
            }
            cullItemOf[i] = im.cullItems.size();
            im.cullUniformStaging.push_back(cull);
            const std::uint32_t levelAxis = shadowFullyCulled ? lodCount : lodCount * 2;
            im.cullItems.push_back(Impl::CullItem{&state, instanceCount, lodCount, levelAxis, blocks, usesLive});
            state.lastCullFrame = im.frame;
            ++stats_.cullObjects;
            if (!shadowFullyCulled) {
                ++stats_.shadowCullObjects;
            }
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
        // Tagged, because this `i` counts procedurals and the scene renderer's counts entities.
        // Untagged they are the same small numbers, and a click on a scattered tree resolved as
        // whichever entity shared its index.
        obj.ids = glm::vec4(static_cast<float>(scene::packPickId(scene::PickSpace::Procedural, i)),
                            static_cast<float>(i + 1), 1.0f, 0.0f);
        // ADR-703 (FXL): the owner's effect lanes. Every instance of this object -- and of each of
        // the asset's other material parts, which the builder maps to the same record -- takes
        // them; an object no lane effect touches keeps both zero, the shader's early-out.
        if (const std::uint32_t record = scene.entityFx.recordForProcedural(i);
            record != 0 && record < scene.entityFx.records.size()) {
            obj.fxA = scene.entityFx.records[record].lanes[world::kFxLaneA];
            obj.fxB = scene.entityFx.records[record].lanes[world::kFxLaneB];
        }
        const bool fxTwoSided = (static_cast<std::uint32_t>(obj.fxA.z + 0.5f) & world::kFxClip) != 0;
        const std::uint32_t offset = slot * kObjectStride;
        std::memcpy(im.staging.data() + offset, &obj, sizeof(obj));

        im.items.push_back(Impl::DrawItem{i, lodMeshes, &state, offset, instanceCount, lodCount, cullActive,
                                          fullyCulled, shadowFullyCulled, levels, fxTwoSided});
        ++stats_.objects;
        stats_.sourceVertices += mesh->vertexCount;
        stats_.sourceTriangles += mesh->indexCount / 3;
        // ADR-108: a spatial instance, counted once. The parts of a multi-material asset are one
        // placement drawn several times; adding their record sets up reported one tree as two.
        // `logicalTriangles` below is deliberately NOT deduplicated -- it is the geometry the
        // object contains, and each part contains its own.
        if (!isPart) {
            stats_.instances += object.instances.size();
        }
        stats_.logicalTriangles += static_cast<std::uint64_t>(mesh->indexCount / 3) * object.instances.size();
        stats_.instanceBufferBytes += state.instanceBytes + (usesLive ? state.liveBytes : 0);
        stats_.deformers += enabled;
        stateOf[i] = &state;
        fullyCulledOf[i] = fullyCulled ? 1 : 0;
        shadowFullyCulledOf[i] = shadowFullyCulled ? 1 : 0;
        ++slot;
    }
    // ADR-108: the cull uniforms, once every material part that joins a lead has been seen.
    for (std::size_t c = 0; c < im.cullItems.size(); ++c) {
        queue.WriteBuffer(im.cullItems[c].state->cullUniforms, 0, &im.cullUniformStaging[c],
                          sizeof(CullPassUniforms));
    }
    if (slot > 0) {
        queue.WriteBuffer(im.objectUniforms, 0, im.staging.data(), static_cast<std::size_t>(slot) * kObjectStride);
    }

    // ---- the effector pass: one compute pass, one dispatch per object with effectors ----
    if (!im.computeItems.empty()) {
        wgpu::ComputePassDescriptor desc{};
        desc.label = "procedural-effectors";
        desc.timestampWrites = im.timeline != nullptr ? im.timeline->mark("effectors", gpu::FrameTimeline::PassKind::Compute) : nullptr;
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&desc);
        cp.SetPipeline(im.effectorPipeline);
        for (const auto& item : im.computeItems) {
            cp.SetBindGroup(0, item.state->computeGroup);
            cp.DispatchWorkgroups((item.count + kEffectorWorkgroup - 1) / kEffectorWorkgroup);
        }
        cp.End();
        ++stats_.effectorDispatches;
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
            im.statsZero.assign(static_cast<std::size_t>(slot) * kCullStatsStride, 0u);
            queue.WriteBuffer(im.cullStats, 0, im.statsZero.data(), statsBytes);
            // ADR-287: and the shadow caster list's prefix, which is the same slots offset by
            // kShadowSlotBase. An object that stopped casting between frames must not leave its
            // last caster count sitting where a reader will add it up.
            queue.WriteBuffer(im.cullStats,
                              static_cast<std::uint64_t>(kShadowSlotBase) * kCullStatsStride *
                                  sizeof(std::uint32_t),
                              im.statsZero.data(), statsBytes);
        }
        wgpu::ComputePassDescriptor desc{};
        desc.label = "procedural-cull";
        desc.timestampWrites = im.timeline != nullptr ? im.timeline->mark("cull", gpu::FrameTimeline::PassKind::Compute) : nullptr;
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
                // `levels` is the doubled axis when this object builds a shadow caster list too
                // (ADR-287): the compaction runs once per list per rung, and the classification --
                // the per-record half -- still runs once.
                cp.DispatchWorkgroups(oneBlock ? 1u : item.blocks, item.levels);
            }
        };
        scanStage(im.cullReducePipeline, false);
        scanStage(im.cullTopPipeline, true);
        scanStage(im.cullScatterPipeline, false);
        cp.End();
        ++stats_.cullDispatches;
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
    // Aggregate whatever the latest completed readback holds. Over the *leads* only: a part shares
    // its lead's spatial instances, so adding its counts here would report one culled tree twice.
    for (const auto& item : im.cullItems) {
        const std::size_t base = static_cast<std::size_t>(item.state->statsSlot) * kCullStatsStride;
        if (base + kCullStatsStride > im.statsSnapshot.size() || im.statsSnapshot[base + 5] == 0) {
            continue;
        }
        std::uint64_t visible = 0;
        for (std::size_t level = 0; level < 4; ++level) {
            const std::uint32_t count = im.statsSnapshot[base + level];
            stats_.lodCounts[level] += count;
            visible += count;
        }
        const std::uint64_t records = im.statsSnapshot[base + 4];
        stats_.visibleInstances += visible;
        stats_.culledInstances += records > visible ? records - visible : 0;
        // ADR-287: the caster list, from its own slot. Counted separately and never folded into
        // the line above, because the two being one number is the defect this fixed.
        const std::size_t shadowBase =
            (static_cast<std::size_t>(item.state->statsSlot) + kShadowSlotBase) * kCullStatsStride;
        if (shadowBase + kCullStatsStride > im.statsSnapshot.size() || im.statsSnapshot[shadowBase + 5] == 0) {
            continue;
        }
        for (std::size_t level = 0; level < 4; ++level) {
            const std::uint32_t count = im.statsSnapshot[shadowBase + level];
            stats_.shadowLodCounts[level] += count;
            stats_.shadowInstances += count;
        }
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
    drawImpl(pass, scene, materialBindGroup, true, false);
}

void ProceduralRenderer::drawShadow(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                                    const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup) {
    drawImpl(pass, scene, materialBindGroup, true, true);
}

void ProceduralRenderer::draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup) {
    drawImpl(pass, scene, materialBindGroup, false, false);
}

void ProceduralRenderer::drawImpl(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                                  const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                                  bool depthOnly, bool shadowPass) {
    Impl& im = *impl_;
    if (!im.initialised || im.items.empty()) {
        return;
    }
    // Redundant state is not free and it is not needed: an object's LOD levels share a material
    // and usually a pipeline, and consecutive scatter layers often share a pipeline too. Only what
    // actually changes is set. The trackers start null because nothing may be assumed about the
    // pass before this renderer's first draw in it.
    wgpu::RenderPipeline boundPipeline;
    wgpu::BindGroup boundMaterial;
    wgpu::Buffer boundVertices;
    wgpu::Buffer boundIndices;
    // Which of this frame's three submission budgets this pass spends. The depth prepass and the
    // shadow views draw the same instances as the camera does; charging them all to one number
    // would make a shadow change read as a scene change.
    SubmittedGeometry& submitted = shadowPass  ? stats_.submittedShadow
                                   : depthOnly ? stats_.submittedDepth
                                               : stats_.submittedCamera;
    const auto setPipeline = [&](const wgpu::RenderPipeline& p) {
        if (p.Get() != boundPipeline.Get()) {
            pass.SetPipeline(p);
            boundPipeline = p;
            ++stats_.state.pipelineBinds;
        } else {
            ++stats_.state.redundantBindsAvoided;
        }
    };
    const auto setMaterial = [&](const wgpu::BindGroup& g) {
        if (g.Get() != boundMaterial.Get()) {
            pass.SetBindGroup(2, g);
            boundMaterial = g;
            ++stats_.state.bindGroupBinds;
        } else {
            ++stats_.state.redundantBindsAvoided;
        }
    };
    const auto setMesh = [&](const Impl::CachedMesh& mesh) {
        if (mesh.vertices.Get() != boundVertices.Get()) {
            pass.SetVertexBuffer(0, mesh.vertices);
            boundVertices = mesh.vertices;
            ++stats_.state.vertexBufferBinds;
        } else {
            ++stats_.state.redundantBindsAvoided;
        }
        if (mesh.indices.Get() != boundIndices.Get()) {
            pass.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            boundIndices = mesh.indices;
            ++stats_.state.indexBufferBinds;
        } else {
            ++stats_.state.redundantBindsAvoided;
        }
    };
    for (const auto& item : im.items) {
        if (item.objectIndex >= scene.procedurals.size()) {
            continue;
        }
        const auto& object = scene.procedurals[item.objectIndex];
        if (shadowPass && !object.castsShadow) {
            continue;
        }
        // Nothing survived this object's cull, and the CPU knew it before the pass was encoded.
        // ADR-287: the shadow passes ask the shadow list's proof, which is a different one. This
        // used to be `item.fullyCulled` in every pass, and that single line is what made a tree the
        // camera cannot see stop casting.
        if (shadowPass ? item.shadowFullyCulled : item.fullyCulled) {
            stats_.skippedIndirectDraws += item.lodCount;
            continue;
        }
        const auto& material = object.material;
        // Point billboards face the camera by construction: never cull them.
        const bool twoSided =
            material.doubleSided || object.source.kind == scene::PrimitiveKind::Point || item.fxTwoSided;
        // Which of the two compacted lists this pass draws: the camera's, or the shadow caster
        // list's own slices of the same `visible` buffer.
        const auto& groups = shadowPass
                                 ? (item.state->usesLive ? item.state->shadowGroupsLive : item.state->shadowGroups)
                                 : (item.state->usesLive ? item.state->groupsLive : item.state->groups);
        const std::uint32_t argsSlot =
            item.state->statsSlot + (shadowPass ? kShadowSlotBase : 0u);
        // One lookup per object: every LOD level of it draws the same material.
        const wgpu::BindGroup materialGroup = materialBindGroup(material);
        if (!item.indirect) {
            // An object with no cull pass has one list and one group: the visible-list binding is
            // the inert placeholder the vertex stage never reads, so the shadow pass draws the same
            // group the camera does. There is no second list here because there is no first one.
            setPipeline(depthOnly ? im.pipelineDepth : (twoSided ? im.pipelineNoCull : im.pipelineCull));
            const auto& direct = item.state->usesLive ? item.state->groupsLive : item.state->groups;
            pass.SetBindGroup(1, direct[0], 1, &item.offset);
            ++stats_.state.bindGroupBinds;
            setMaterial(materialGroup);
            setMesh(*item.meshes[0]);
            pass.DrawIndexed(item.meshes[0]->indexCount, item.instanceCount);
            // A direct draw's instance count is the CPU's own number, so this one is exact.
            submitted.record(item.meshes[0]->indexCount, item.instanceCount, false);
            if (!depthOnly) {
                ++stats_.drawCalls;
            }
            continue;
        }
        // One indirect draw per LOD level, instance counts written by the cull pass. A level whose
        // mesh is an impostor quad is never back-face culled -- it faces the camera by
        // construction. A level that is a simplified *mesh* is culled like any other mesh, which
        // is every level of every imported asset since ADR-085.
        for (std::uint32_t level = 0; level < item.lodCount; ++level) {
            const Impl::CachedMesh* mesh = item.meshes[level];
            if (mesh == nullptr || !groups[level]) {
                continue;
            }
            // A level no record can be on this frame is not recorded at all: its indirect draw
            // would read an instance count of zero.
            //
            // This used to be decided from how many consecutive frames the level had been empty in
            // the last completed cull readback -- which lags the drawn frame by one to three, so
            // the rule was wrong at exactly the moment it mattered: the frame an instance arrives
            // on a level it has not been on. The object was then drawn by nothing at all until the
            // readback caught up. `objectLevelRange` replaces the guess with a proof over the same
            // bounds `fullyCulled` above uses, so it can never be wrong about an arrival, and in a
            // scatter whose whole cloud is near the camera it skips more than the guess did.
            if (!item.levels.contains(static_cast<int>(level))) {
                ++stats_.skippedIndirectDraws;
                continue;
            }
            setPipeline(depthOnly ? im.pipelineDepth
                                  : (twoSided || mesh->impostor ? im.pipelineNoCull : im.pipelineCull));
            pass.SetBindGroup(1, groups[level], 1, &item.offset);
            ++stats_.state.bindGroupBinds;
            setMaterial(materialGroup);
            setMesh(*mesh);
            const std::uint64_t argsOffset =
                (static_cast<std::uint64_t>(argsSlot) * scene::kMaxLodLevels + level) * kIndirectStride;
            pass.DrawIndexedIndirect(im.indirectArgs, argsOffset);
            ++stats_.indirectDraws; // every pass, because every pass pays for it
            const std::size_t base = static_cast<std::size_t>(argsSlot) * kCullStatsStride;
            const bool haveCounts = base + kCullStatsStride <= im.statsSnapshot.size() &&
                                    im.statsSnapshot[base + 5] != 0;
            if (haveCounts && im.statsSnapshot[base + level] == 0) {
                ++stats_.emptyIndirectDraws;
            }
            // The instance count of this draw is written by the cull pass, on the GPU, after the
            // draw is recorded. The CPU cannot have it without stalling the frame it is measuring,
            // so what is counted is the last completed readback's -- exact in a still scene, one to
            // three frames behind under a moving camera, and flagged as such either way. Before the
            // first readback lands there is no number at all, and none is invented.
            if (haveCounts) {
                submitted.record(mesh->indexCount, im.statsSnapshot[base + level], true);
            } else {
                submitted.recordUnmeasured();
            }
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
    // `groupVisible` rather than `visible`: a material part served by another object's cull owns no
    // visible list, but the fanout writes its stats slot like any other (ADR-108).
    if (it == im.objects.end() || !it->second.groupVisible) {
        return fail("procedural object '{}' has no cull state", name);
    }
    const auto slotAt = [&](std::uint32_t slot) -> Result<std::array<std::uint32_t, kCullStatsStride>> {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(slot) * kCullStatsStride * sizeof(std::uint32_t);
        auto data = gpu::readBuffer(im.context, im.cullStats, offset, kCullStatsStride * sizeof(std::uint32_t));
        if (!data) {
            return std::unexpected(data.error());
        }
        std::array<std::uint32_t, kCullStatsStride> raw{};
        std::memcpy(raw.data(), data->data(), raw.size() * sizeof(std::uint32_t));
        return raw;
    };
    auto raw = slotAt(it->second.statsSlot);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    CullCounts counts;
    counts.records = (*raw)[4];
    for (std::size_t level = 0; level < counts.lod.size(); ++level) {
        counts.lod[level] = (*raw)[level];
        counts.visible += (*raw)[level];
    }
    counts.culled = counts.records > counts.visible ? counts.records - counts.visible : 0;
    // ADR-287's second list, from its own slot. The zeroing sweep in update() clears both prefixes
    // each frame, so an object that is not casting reads zero here rather than reading history.
    auto shadowRaw = slotAt(it->second.statsSlot + kShadowSlotBase);
    if (!shadowRaw) {
        return std::unexpected(shadowRaw.error());
    }
    for (std::size_t level = 0; level < counts.shadowLod.size(); ++level) {
        counts.shadowLod[level] = (*shadowRaw)[level];
        counts.shadowVisible += (*shadowRaw)[level];
    }
    return counts;
}

Result<std::array<std::uint32_t, 5>> ProceduralRenderer::readIndirectArgs(const std::string& name, int level,
                                                                         bool shadowList) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    // A material part has its own args slot -- written by its lead's cull (ADR-108) -- but no
    // visible list of its own, so the draws' list is what says whether it has cull state at all.
    if (it == im.objects.end() || !it->second.groupVisible) {
        return fail("procedural object '{}' has no cull state", name);
    }
    const Impl::ObjectState& state = it->second;
    if (level < 0 || static_cast<std::uint32_t>(level) >= state.groupVisibleLevels) {
        return fail("procedural object '{}' has no LOD level {}", name, level);
    }
    const std::uint64_t argsSlot = static_cast<std::uint64_t>(state.statsSlot) + (shadowList ? kShadowSlotBase : 0u);
    const std::uint64_t offset =
        (argsSlot * scene::kMaxLodLevels + static_cast<std::uint64_t>(level)) * kIndirectStride;
    auto data = gpu::readBuffer(im.context, im.indirectArgs, offset, kIndirectStride);
    if (!data) {
        return std::unexpected(data.error());
    }
    std::array<std::uint32_t, 5> args{};
    std::memcpy(args.data(), data->data(), sizeof(args));
    return args;
}

Result<ProceduralRenderer::InstanceLevels> ProceduralRenderer::readLodLevels(const std::string& name) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    // A material part shares its lead's decision and owns no `lodIndex` of its own (ADR-108), so
    // the caller is told to ask the lead rather than handed the part's unwritten buffer.
    if (it == im.objects.end() || !it->second.lodIndex) {
        return fail("procedural object '{}' has no per-instance LOD state", name);
    }
    const Impl::ObjectState& state = it->second;
    const std::uint32_t count = static_cast<std::uint32_t>(state.uploadedCount);
    InstanceLevels out;
    out.fresh = state.lastCullFrame == im.frame && im.frame != 0;
    if (count == 0) {
        return out;
    }
    auto data = gpu::readBuffer(im.context, state.lodIndex, 0,
                                static_cast<std::uint64_t>(count) * sizeof(std::uint32_t));
    if (!data) {
        return std::unexpected(data.error());
    }
    std::vector<std::uint32_t> raw(count);
    std::memcpy(raw.data(), data->data(), data->size());
    out.level.reserve(count);
    for (const std::uint32_t v : raw) {
        out.level.push_back(v == 0xFFFFFFFFu ? -1 : static_cast<int>(v));
    }
    return out;
}

Result<std::vector<std::uint32_t>> ProceduralRenderer::readVisibleIndices(const std::string& name, int level,
                                                                         bool shadowList) {
    Impl& im = *impl_;
    const auto it = im.objects.find(name);
    // The list the object's draws read: its own, or its lead's when it is a material part.
    if (it == im.objects.end() || !it->second.groupVisible) {
        return fail("procedural object '{}' has no cull state", name);
    }
    const Impl::ObjectState& state = it->second;
    if (level < 0 || static_cast<std::uint32_t>(level) >= state.groupVisibleLevels) {
        return fail("procedural object '{}' has no LOD level {}", name, level);
    }
    auto counts = readCullCounts(name);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    const std::uint32_t count = shadowList ? counts->shadowLod[static_cast<std::size_t>(level)]
                                          : counts->lod[static_cast<std::size_t>(level)];
    if (count == 0) {
        return std::vector<std::uint32_t>{};
    }
    // ADR-287: the shadow caster list's slices sit above the camera's in the same buffer, at
    // `groupVisibleLevels + level` -- the layout shaders/cull.wgsl's doubled level axis writes.
    const std::uint64_t listLevel =
        static_cast<std::uint64_t>(level) + (shadowList ? state.groupVisibleLevels : 0u);
    const std::uint64_t offset = listLevel * state.groupVisibleStride * sizeof(std::uint32_t);
    auto data = gpu::readBuffer(im.context, state.groupVisible, offset,
                                static_cast<std::uint64_t>(count) * sizeof(std::uint32_t));
    if (!data) {
        return std::unexpected(data.error());
    }
    std::vector<std::uint32_t> indices(count);
    std::memcpy(indices.data(), data->data(), data->size());
    return indices;
}

} // namespace avgen::rendering
