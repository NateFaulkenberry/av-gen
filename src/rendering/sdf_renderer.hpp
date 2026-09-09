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
#include <functional>
#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;

struct SdfStats {
    std::uint32_t objects = 0;          // visible SDF objects drawn this frame (both modes)
    std::uint32_t raymarchObjects = 0;  // drawn by the raymarch pass
    std::uint32_t meshObjects = 0;      // drawn as meshes
    std::uint32_t packedNodes = 0;      // packed records uploaded this frame (raymarch objects)
    std::uint32_t meshTriangles = 0;    // triangles of the drawn meshed objects
    std::uint32_t meshUploads = 0;      // mesh buffers (re)uploaded this frame
    double raymarchMs = -1.0;           // GPU time of the last measured raymarch pass (-1 = none / unavailable)
    double cpuUpdateMs = 0.0;           // packing + upload time this frame
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
};
static_assert(sizeof(SdfObjectUniforms) == 144);

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
    void update(const scene::Scene& scene, const FrameTime& time, const glm::mat4& viewProj,
                const FieldUniforms* fields = nullptr);
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
    void collectTimings();

    [[nodiscard]] const SdfStats& stats() const { return stats_; }

    static constexpr std::uint32_t kMaxObjects = 256;   // 256-byte uniform slots
    static constexpr std::uint32_t kObjectStride = 512;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SdfStats stats_;
};

} // namespace avgen::rendering
