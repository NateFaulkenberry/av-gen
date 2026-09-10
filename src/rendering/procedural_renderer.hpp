#pragma once

// GPU side of procedural geometry (ADR-023/025/029): per object a source mesh (uploaded when
// its hash changes), an instance storage buffer (uploaded when structureVersion changes), a
// 256-byte object uniform slot (material, object matrix) and a deformer uniform block (per
// frame); one DrawIndexed(indexCount, instanceCount) per visible object inside the scene's lit
// pass, with the same frame/material/IBL bind groups as entities. Shader: shaders/procedural.wgsl.
//
// Fields: objects with at least one usable effector get an effector compute pass
// (shaders/points.wgsl) that turns the base record buffer into a "live" buffer the draw reads;
// Field deformers, the emissive field and Point sources are resolved into the per-object
// uniforms. The FieldBlock uniform (rendering/field_uniforms.hpp) is bound to both the draw
// (group 1 binding 3) and the effector pass (group 0 binding 3).
//
// Culling and LOD (ADR-029): an object with scene::LodSettings::cull or lodCount > 1 gets a second
// compute pass (shaders/cull.wgsl) after the effector pass. It classifies every record against the
// frustum/distance/screen-size limits, picks a LOD level and compacts the survivors per level with
// the same stable prefix-sum scan the particles use; the draw is then one drawIndexedIndirect per
// level, reading its instance through the level's visible list (group 1 binding 5) and using that
// level's mesh. Every object's indirect args live in one shared buffer, kMaxLodLevels slots each,
// addressed by the object's stats slot (ADR-051): a draw against one of eleven separate buffers
// measured about a tenth of a millisecond more per buffer than the same draw against a shared one. Objects with the defaults (cull off, lodCount 1) keep the direct draw and every
// buffer byte-identical.
//
// Splines (ADR-026): the SplineBuffers storage buffer (rendering/spline_buffers.hpp) is bound to
// the draw at group 1 binding 4; Path deformers are resolved to a spline slot and a final
// pathScale (units of arc length per object unit; "fit" divides the length by the source
// extent along the deformer axis) in the per-object deformer block.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "spatial/effector.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;
class SplineBuffers;

struct ProceduralStats {
    std::uint32_t objects = 0;          // visible procedural objects drawn this frame
    std::uint32_t sourceMeshes = 0;     // cached source meshes
    std::uint32_t sourceVertices = 0;   // sum over drawn objects
    std::uint32_t sourceTriangles = 0;
    std::uint64_t instances = 0;        // sum over drawn objects
    std::uint64_t logicalTriangles = 0; // sourceTriangles * instances
    std::uint64_t instanceBufferBytes = 0;
    std::uint32_t deformers = 0;        // enabled deformers over drawn objects
    std::uint32_t windObjects = 0;      // ADR-055: drawn objects whose vertex stage sways this frame
    double cpuUpdateMs = 0.0;           // rebuild + upload time this frame
    std::uint32_t uploads = 0;          // instance buffer uploads this frame
    std::uint32_t drawCalls = 0;        // draws issued: one per object, or one per populated LOD level
    // Submission accounting (the world optimisation spec's P1/P2). `indirectDraws` counts every
    // indirect draw actually recorded, over every pass -- the camera's, the depth prepass's and
    // each shadow cascade's -- because that is the number the frame pays for, not the number the
    // camera pass alone issues. `emptyIndirectDraws` is how many of those had no instances in the
    // last completed cull readback: the CPU cannot know a level is empty when it records the draw,
    // because the count is written by the GPU, so this is the measure of what that costs.
    std::uint32_t indirectDraws = 0;
    std::uint32_t emptyIndirectDraws = 0;
    std::uint32_t skippedIndirectDraws = 0; // levels not recorded because they have been empty
    // Objects whose whole record set is provably outside the frustum / distance / screen-size
    // limits this frame. Their cull dispatches and every one of their indirect draws, in every
    // pass, are skipped: the GPU would have written zero to each level's instance count.
    std::uint32_t culledObjects = 0;
    // Fields (ADR-025)
    std::uint32_t effectorObjects = 0;   // objects that ran the effector pass this frame
    std::uint64_t effectorInstances = 0; // records processed by the effector pass this frame
    std::uint32_t effectors = 0;         // usable effectors over those objects
    std::uint32_t effectorDispatches = 0; // effector compute passes encoded this frame
    std::uint32_t cullDispatches = 0;     // cull compute passes encoded this frame
    double effectorPassMs = -1.0;        // GPU time of the last measured effector pass (-1 = none / unavailable)
    std::uint32_t pointObjects = 0;      // objects drawn as Point billboards
    std::uint32_t fieldDeformers = 0;    // enabled Field deformers bound to a slot
    std::uint32_t pathDeformers = 0;     // enabled Path deformers bound to a spline slot (ADR-026)
    // Culling and LOD (ADR-029). Counts come from an asynchronous readback of the cull pass's
    // stats buffer, so they lag the drawn frame by a frame or two; they cover only the objects
    // whose cull pass ran (visibleInstances + culledInstances = those objects' instance total).
    std::uint32_t cullObjects = 0;        // objects whose cull pass was encoded this frame
    std::uint64_t culledInstances = 0;    // instances rejected by frustum / distance / screen size
    std::uint64_t visibleInstances = 0;   // instances that survived, over all LOD levels
    std::uint64_t lodCounts[4] = {0, 0, 0, 0}; // survivors per LOD level
    double cullMs = -1.0;                 // GPU time of the last measured cull pass (-1 = none / unavailable)
};

// The six frustum planes of a view-projection in world space, in the order left, right, bottom,
// top, near, far; xyz is a unit normal pointing inwards, w the plane offset (a point p is inside
// when dot(n, p) + w >= 0). Gribb-Hartmann on a 0..1 depth clip range (WebGPU/Metal).
using FrustumPlanes = std::array<glm::vec4, 6>;
[[nodiscard]] FrustumPlanes frustumPlanes(const glm::mat4& viewProj);

// The camera terms the cull pass needs beyond the planes.
struct CullCamera {
    glm::vec3 position{0.0f};
    float projScale = 1.0f; // viewportHeight / (2 tan(fovY / 2)): pixels per world unit at 1 unit
};
[[nodiscard]] float cullProjScale(float fovYRadians, std::uint32_t viewportHeight);

// CPU reference of the per-instance decision in shaders/cull.wgsl: the LOD level 0..lodCount-1,
// or -1 when the instance is culled. `center`/`radius` are the world bounding sphere. Shared with
// the tests, which compare the GPU's compacted lists against it.
[[nodiscard]] int cullLodLevel(const scene::LodSettings& lod, const FrustumPlanes& planes, const CullCamera& camera,
                               glm::vec3 center, float radius);

// The whole object's records reduced to two numbers that do not change until the record set does:
// the AABB of the instance positions (record space, before the object matrix) and the largest
// |scale| any record carries. Cached per object and recomputed on a structureVersion change.
struct InstanceBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    float maxAbsScale = 1.0f;
    bool valid = false;
};
[[nodiscard]] InstanceBounds instanceBounds(const std::vector<scene::InstanceRecord>& records);

// True when shaders/cull.wgsl is certain to reject *every* record of the object: the conservative
// whole-object bound fails the same frustum / maxDistance / minScreenRadius tests the per-instance
// path applies, so every LOD level's instance count will be zero. Skipping the object's cull
// dispatches and all of its indirect draws is then bit-exact -- there is nothing to pop in,
// because a level that would have drawn nothing draws nothing either way.
//
// Only valid when the records the GPU culls are the ones these bounds were built from: an object
// with effectors moves its records on the GPU, so the caller must not use this for those.
[[nodiscard]] bool objectFullyCulled(const scene::LodSettings& lod, const FrustumPlanes& planes,
                                     const CullCamera& camera, const glm::mat4& objectToWorld,
                                     const InstanceBounds& bounds, float sourceRadius);

// Per-object result of the cull pass (blocking readback; tests and tools).
struct CullCounts {
    std::uint32_t records = 0;  // instances the pass looked at
    std::uint32_t visible = 0;  // sum of `lod`
    std::uint32_t culled = 0;   // records - visible
    std::array<std::uint32_t, 4> lod{};
};

// The deformer record as the shader sees it (64 bytes, std140-compatible). Mirrors
// shaders/procedural.wgsl `DeformerUniform`.
struct DeformerUniform {
    glm::vec4 axisKind;      // xyz = unit axis, w = kind code: DeformerKind (0..6) + 8 for world space, -1 = disabled slot
    glm::vec4 centerAmount;  // xyz = center, w = amount
    glm::vec4 params;        // x = frequency (sine) | spatial scale (noise, displacement) | field slot (field) | spline slot (path), y = speed | alongNormal (field) | resolved pathScale (path), z = phase | pathOffset (path), w = falloff | pathRoll (path)
    glm::vec4 extra;         // xyz = displacementAxis (sine, bend direction) | axisMask (noise); w = seed as float bits
};
static_assert(sizeof(DeformerUniform) == 64);

// Group 1 binding 2 (one buffer per object, written every frame). The object/parent matrix and
// the material live in the 256-byte ObjectUniforms slot at group 1 binding 0 (same layout as
// entities), so this block stays small and std140-trivial.
struct ProceduralUniforms {
    glm::vec4 timeInfo;      // x = render time, y = deformer count, z = epsilon for normals, w = instance count
    glm::vec4 fieldInfo;     // x = emissive field slot (-1 none), y = emissive field amount, z = point source (1/0), w = 0
    glm::vec4 prevInfo;      // x = last frame's render time (ADR-035 velocity: deformation motion), yzw = 0
    // ADR-055 Tier 0 vegetation motion: the species response, resolved on the CPU by
    // wind::motionResponse so the vertex stage evaluates no physics at all. windSway.w gates the
    // whole path, and is uniform across a draw, so a boulder pays nothing.
    glm::vec4 windSway;      // x = steady gain, y = gust gain, z = flutter gain, w = 1 when it sways
    glm::vec4 windTiming;    // x = sway delay (s), y = flutter omega (rad/s), z = bend curve exponent,
                             // w = bend limit (fraction of the plant's height)
    glm::vec4 windPlant;     // x = base y (post-source object space), y = 1 / extent y, z = extent y,
                             // w = per-instance amplitude variance
    // Step 1 of the transform chain (procedural.hpp): the source mesh's own placement, applied
    // before the deformer stack so it matches ProceduralGeometry::instanceMatrix() on the CPU.
    // It lives in the uniform rather than baked into the mesh because source/position|rotation|scale
    // are live parameters.
    glm::mat4 sourceMatrix;
    glm::mat4 sourceNormalMatrix; // inverse transpose of sourceMatrix
    DeformerUniform deformers[scene::kMaxDeformers];
};
static_assert(sizeof(ProceduralUniforms) == 48 + 48 + 128 + 64 * scene::kMaxDeformers);

// The effector pass parameters (shaders/points.wgsl `PointsParams`, 528 bytes).
struct EffectorPassUniforms {
    glm::mat4 objectToWorld;
    glm::mat4 worldToObjectRotation; // transpose of the rotation-only part of objectToWorld
    glm::uvec4 info;                 // x = record count, y = effector count
    spatial::EffectorGpu effectors[spatial::kMaxEffectors];
};
static_assert(sizeof(EffectorPassUniforms) == 128 + 16 + 48 * spatial::kMaxEffectors);

// The cull pass parameters (shaders/cull.wgsl `CullParams`, 256 bytes).
inline constexpr std::uint32_t kMaxCullDepthLayers = 6; // shaders/cull.wgsl CullParams

struct CullPassUniforms {
    glm::mat4 objectToWorld;
    glm::vec4 planes[6];      // frustum planes (left, right, bottom, top, near, far)
    glm::vec4 cameraPos;      // xyz = camera position, w = projScale
    glm::vec4 limits;         // x = maxDistance, y = minScreenRadius, z = source radius, w = object scale
    glm::vec4 thresholds;     // xyz = lodDistances, w = 0
    glm::uvec4 counts;        // x = record count, y = lod count, z = visible stride, w = scan blocks
    glm::uvec4 flags;         // x = cull enabled, y = thresholds are screen radii, z = stats slot, w = 0
    glm::uvec4 indexCounts;   // index count of each level's mesh
    // ADR-038 depth layers, as (start, end, density, detail). `flags.w` holds the count, so a
    // scene with no layers classifies exactly as it did before they existed.
    glm::vec4 depthLayers[kMaxCullDepthLayers];
};
static_assert(sizeof(CullPassUniforms) == 256 + 16 * kMaxCullDepthLayers);

class ProceduralRenderer {
public:
    ProceduralRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ProceduralRenderer();
    ProceduralRenderer(const ProceduralRenderer&) = delete;
    ProceduralRenderer& operator=(const ProceduralRenderer&) = delete;

    // Creates pipelines for the given colour/depth formats and the frame/material/IBL bind group
    // layouts shared with the entity pipelines (group 0 frame, group 2 material, group 3 IBL);
    // group 1 is this renderer's own (object uniforms + instances + deformers + field block +
    // spline tables). `fieldBlock` is the FieldUniforms buffer and `splineTable` the
    // SplineBuffers buffer (zeroed private ones are created when null).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, std::uint32_t sampleCount = 1,
                                    wgpu::Buffer fieldBlock = nullptr, wgpu::Buffer splineTable = nullptr,
                                    wgpu::Buffer gridTable = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of procedural.wgsl / points.wgsl / cull.wgsl (keeps buffers)

    // Per frame, before the lit pass: uploads source meshes (cached by `meshHash`) and instance
    // buffers that changed, writes the deformer/time and object uniforms, and encodes the
    // effector compute pass for objects with effectors. The scene is const here, so the engine
    // side must call `ProceduralGeometry::rebuild()` before rendering; `structureVersion` and
    // `meshHash` are the change signals this renderer keys uploads on (an object with an empty
    // `instances` or a zero `meshHash` is skipped). Per-object GPU state is keyed by the object's
    // name. `objectMatrices[i]` is the parent matrix for scene.procedurals[i] (identity when the
    // scene places them itself; missing entries = identity). `fields` resolves field names to
    // slots (null = no fields: effectors, Field deformers and emissive fields are inert);
    // `splines` resolves Path deformer spline names to slots (null = Path deformers are inert).
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                const std::vector<glm::mat4>& objectMatrices, const FrameTime& time,
                const FieldUniforms* fields = nullptr, const SplineBuffers* splines = nullptr);
    // Inside the lit pass (frame and IBL bind groups already set by the caller): sets its own
    // pipeline and group 1, the material group via `materialBindGroup`, and draws every visible
    // object. Opaque objects only in this phase (blend materials are drawn opaque).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup);
    // The same draws with the depth-only pipeline, for the depth prepass and the shadow passes
    // (ADR-034): the same vertex stage and the same instance buffer, so instanced procedural
    // geometry casts shadows without a second data path.
    // The shadow passes: like drawDepthOnly, but objects that do not cast are left out.
    void drawShadow(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                    const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup);
    void drawDepthOnly(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                       const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup);
    // Pumps the effector-pass timer after the frame's command buffer was submitted (update()
    // also does this at the start of the next frame).
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed.
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    // The viewport the cull pass reasons about: the aspect of the frustum planes and the pixel
    // scale of `minScreenRadius` / screen-size LOD. Call it once per frame before update() with
    // the render target size; the default is 1920x1080. Ignored entirely when no object has
    // culling or LOD enabled.
    void setViewport(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] const ProceduralStats& stats() const { return stats_; }
    // Drops cached meshes not used for `frames` frames (called by update).
    void setMeshCacheLimit(std::size_t frames) { cacheFrames_ = frames; }

    // Blocking readback of the records the draw reads for the object named `name` (the live
    // buffer after the effector pass, else the base buffer). Tests and tools only.
    [[nodiscard]] Result<std::vector<scene::InstanceRecord>> readInstanceRecords(const std::string& name);
    // Blocking readback of the last cull pass's per-level counts for the object named `name`.
    // Tests and tools only.
    [[nodiscard]] Result<CullCounts> readCullCounts(const std::string& name);
    // Blocking readback of the compacted visible list of one LOD level (ascending record indices).
    [[nodiscard]] Result<std::vector<std::uint32_t>> readVisibleIndices(const std::string& name, int level);
    // Blocking readback of the drawIndexedIndirect args the draw will read for one LOD level:
    // {indexCount, instanceCount, firstIndex, baseVertex, firstInstance}. The bytes the draw
    // addresses, not the counts the cull pass believes it wrote -- since every object's args now
    // share one buffer, those are two different claims and only the first one draws anything.
    // Tests and tools only.
    [[nodiscard]] Result<std::array<std::uint32_t, 5>> readIndirectArgs(const std::string& name, int level);

private:
    void drawImpl(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                  const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                  bool depthOnly, bool shadowPass);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    ProceduralStats stats_;
    std::size_t cacheFrames_ = 120;
};

} // namespace avgen::rendering
