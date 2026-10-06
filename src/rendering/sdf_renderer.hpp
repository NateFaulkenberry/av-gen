#pragma once

// GPU side of SDF objects (ADR-027). Two paths per scene::SdfObject:
//
// * Raymarch: every frame the object's tree is packed (spatial::packSdfTree, node parameters are
//   live) into one storage buffer shared by all objects, its transform/material/march settings
//   go to 256-byte uniform slots, and one 6-vertex quad over the projected bounds is drawn by
//   shaders/sdf_raymarch.wgsl in a dedicated render pass on the scene's HDR colour + depth
//   (load/store, depth test + write) right after the opaque meshes and procedural instances,
//   so it composes with them and with what follows (skybox, grid, particles, blended meshes).
//   The pass carries its own timestamps (SdfStats::raymarchMs).
// * Mesh: the object's `mesh` (surface nets, filled by SdfObject::rebuild and cached by
//   `meshHash`) is uploaded when its hash changes and drawn inside the lit pass with the entity
//   PBR shader (pbr.wgsl) and the same frame/material/IBL bind groups as entities. The lit
//   pipelines and the group-1 layout belong to SceneRenderer and are handed over by init() and
//   setMeshPipelines(); only the uniform buffer and its bind group are this renderer's own, so
//   a meshed SDF object shades exactly like an entity and pbr.wgsl is compiled once.
//
// The scene is const here: the engine side calls SdfObject::rebuild() before rendering; a Mesh
// object with an empty mesh is skipped. Per-object GPU state is keyed by the object's name.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;
class ParticleRenderer;

struct SdfStats {
    std::uint32_t objects = 0;          // visible SDF objects drawn this frame (both modes)
    std::uint32_t raymarchObjects = 0;  // drawn by the raymarch pass
    std::uint32_t meshObjects = 0;      // drawn as meshes
    std::uint32_t packedNodes = 0;      // packed records uploaded this frame (raymarch objects)
    std::uint32_t meshTriangles = 0;    // triangles of the drawn meshed objects
    std::uint32_t meshUploads = 0;      // mesh buffers (re)uploaded this frame
    std::uint32_t densityObjects = 0;   // ADR-1142: raymarch objects drawn in density mode this frame
    double raymarchMs = -1.0;           // GPU time of the last measured raymarch pass (-1 = none / unavailable)
    double cpuUpdateMs = 0.0;           // packing + upload time this frame
    // ADR-1002: march step statistics of the lit raymarch pass, sampled on every 4th pixel in x and y
    // (every raymarched object's quad), read back a few frames late. `sampledRays` 0 = none yet.
    std::uint32_t sampledRays = 0;
    double avgSteps = 0.0;              // mean march steps per sampled ray
    std::uint32_t maxSteps = 0;         // the most steps any sampled ray took
    double hitRatio = 0.0;              // share of sampled rays that hit a surface
    double exhaustedRatio = 0.0;        // share that stopped on the step budget (neither hit nor left)
};

// Group 1 binding 1 of the raymarch pass (144 bytes, in a 256-byte dynamic-offset slot). Mirrors
// shaders/sdf_raymarch.wgsl `SdfObjectUniforms`.
struct SdfObjectUniforms {
    glm::mat4 worldToLocal;
    glm::vec4 boundsMin;   // xyz
    glm::vec4 boundsMax;   // xyz
    glm::uvec4 info;       // node offset, node count, max steps, 0
    glm::vec4 march;       // epsilon, step scale, normal epsilon, time
    glm::vec4 rect;        // NDC rect: xmin, ymin, xmax, ymax
    // ADR-1002 (scene::SdfLook and the march cap)
    glm::vec4 look0;       // ao strength, ao distance, edge intensity, edge width
    glm::vec4 look1;       // edge colour rgb, max distance (0 = bounds only)
    glm::vec4 look2;       // shadow strength, shadow softness, shadow steps, 1 = collect step statistics
    glm::vec4 look3;       // shadow direction (world, towards the light), 0
    glm::uvec4 surfaces;   // ADR-1044: x = the surface records' index after the node offset, y = their count
    glm::vec4 look4;       // ADR-1047: edge width in pixels (0 = look0.w in local units), threshold, softness;
                           // ADR-1052: w = rim power
    glm::vec4 look5;       // ADR-1052: rim colour rgb, rim intensity (0 = off)
    glm::vec4 look6;       // ADR-1054: static cell (local units), rate (Hz), roll strength, 0
    // ADR-1055: the world wave (scene-wide, copied into every object): origin + progress, direction + width,
    // colour + intensity, trail colour + trail, (hue, hue span, edge tint, 1 = on).
    glm::vec4 wave0;
    glm::vec4 wave1;
    glm::vec4 wave2;
    glm::vec4 wave3;
    glm::vec4 wave4;
    // ADR-1142: density iso (+) SDF. density0.w = 1 is density mode; all zero is the object as it was.
    glm::vec4 density0;    // iso, sharpness, one density cell in local units, 1 = density mode
    glm::vec4 density1;    // the volume's world-space min corner, 0
    glm::vec4 density2;    // 1 / the volume's world-space extent, 0
};
static_assert(sizeof(SdfObjectUniforms) == 400);

class SdfRenderer {
public:
    SdfRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~SdfRenderer();
    SdfRenderer(const SdfRenderer&) = delete;
    SdfRenderer& operator=(const SdfRenderer&) = delete;

    // Creates the raymarch pipeline (sdf_raymarch.wgsl) for the given colour/depth formats and
    // the frame/material/IBL layouts shared with the entity pipelines (groups 0, 2, 3); the
    // raymarch group 1 is this renderer's own, the mesh group 1 uses the entity `objectLayout`
    // so meshed objects can be drawn with SceneRenderer's lit pipelines (setMeshPipelines).
    // `fieldBlock` is the FieldUniforms buffer (a zeroed private one is created when null).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& objectLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, wgpu::Buffer fieldBlock = nullptr);
    // The entity lit pipelines (pbr.wgsl, opaque cull / no cull) Mesh-mode objects draw with.
    // Call after init() and again whenever SceneRenderer rebuilds them (shader reload).
    void setMeshPipelines(const wgpu::RenderPipeline& cull, const wgpu::RenderPipeline& noCull);
    [[nodiscard]] Result<void> reload(); // hot reload of sdf_raymarch.wgsl (keeps buffers)

    // Per frame, before the lit pass: packs and uploads the node records of visible Raymarch
    // objects, uploads changed meshes of Mesh objects, writes the per-object uniforms.
    // `viewProj` and the target size compute the screen rect each raymarch draw covers.
    // `fields` resolves DisplaceField names to slots (null: those displacements are 0).
    // `particles` resolves an ADR-1142 density source to its system's volume (ADR-1141); null, or a
    // system that has no resolved volume, draws that object as no matter at all, i.e. nothing.
    void update(const scene::Scene& scene, const FrameTime& time, const glm::mat4& viewProj,
                const FieldUniforms* fields = nullptr, const ParticleRenderer* particles = nullptr);
    // Inside the lit pass (frame and IBL groups already set): draws the Mesh-mode objects with
    // the entity PBR pipeline; sets its own group 1 and the material group via `materialBindGroup`.
    // `depthOnlyPipeline` (optional) replaces the lit pipelines, for the depth prepass and the
    // shadow passes: a meshed SDF casts exactly the shadow its surface would receive.
    void drawMeshes(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                    const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                    const wgpu::RenderPipeline* depthOnlyPipeline = nullptr);
    // True when this frame's update() found Raymarch objects to draw (the caller splits the lit
    // pass around encodeRaymarchPass only then, keeping the no-SDF frame unchanged).
    [[nodiscard]] bool hasRaymarchWork() const;
    // ADR-1091: compiled tree variants alive (ADR-1003), failed ones excluded. For the live profiler's shader count.
    [[nodiscard]] std::size_t compiledVariantCount() const;
    // ADR-1102: variants still compiling on Dawn's workers, and the switch that allows it (the live editor and the live
    // profile turn it on; offline renders and tests keep the synchronous compile so no frame draws a stand-in).
    [[nodiscard]] std::size_t pendingCompiles() const;
    void setAsyncCompile(bool async);
    // ADR-1150: density-mode objects skip empty 8^3 blocks through the volume's coarse occupancy grid. On
    // by default; off draws the same surface by stepping every block (tests and the cost measurement).
    void setDensityOccupancySkipping(bool on);
    void setPrewarm(bool prewarm); // default on; off = compile at first use only (the pre-ADR-1102 behaviour)
    // Encodes the raymarch render pass onto `color`/`depth` (both loaded and stored) with the
    // frame/IBL bind groups given; one draw per Raymarch object. Call between the lit pass's
    // opaque phase and its transparent phase.
    // `auxTargets` are the auxiliary colour targets of the scene pass (ADR-035), in order; the
    // pass attaches colour plus those, matching the pipeline's five declared targets.
    void encodeRaymarchPass(wgpu::CommandEncoder& encoder, const wgpu::TextureView& color,
                            const wgpu::TextureView& depth, const wgpu::BindGroup& frameBindGroup,
                            const wgpu::BindGroup& iblBindGroup, const scene::Scene& scene,
                            const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                            const wgpu::TextureView* auxTargets = nullptr, std::uint32_t auxCount = 0);
    // Raymarched objects in a depth-only pass. The prepass (`reducedSteps` false) marches exactly
    // as the lit pass does, so the depth it writes matches; the shadow maps (`reducedSteps` true)
    // march a quarter of the steps at a looser epsilon, which is all a caster silhouette needs
    // (ADR-034). The caller owns the pass and has already bound group 0 and group 3.
    void drawRaymarchDepth(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
                           const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup,
                           bool reducedSteps = false);
    // Pumps the raymarch-pass timer after the frame's command buffer was submitted (update()
    // also does this at the start of the next frame).
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed.
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    [[nodiscard]] const SdfStats& stats() const { return stats_; }

    static constexpr std::uint32_t kMaxObjects = 256;   // 256-byte uniform slots
    static constexpr std::uint32_t kObjectStride = 768; // ADR-1143: holds the 528-byte ObjectUniforms
    static_assert(kObjectStride % 256 == 0);
    static_assert(sizeof(SdfObjectUniforms) <= kObjectStride);

    // §16: the step budget the shadow-map march gets, from `QualitySettings::sdfShadowSteps`.
    // Applied per frame rather than at construction, because the tier can change between frames --
    // the same reason `setLodHysteresisAllowed` is called that way. Capped by the object's own
    // `maxSteps` in the shader, so a cheap object never gets an expensive shadow.
    void setSdfShadowSteps(std::uint32_t steps) { sdfShadowSteps_ = steps; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SdfStats stats_;
    std::uint32_t sdfShadowSteps_ = 24; // the QualitySettings default
};

} // namespace avgen::rendering
