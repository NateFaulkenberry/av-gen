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

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "spatial/effector.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;

struct ProceduralStats {
    std::uint32_t objects = 0;          // visible procedural objects drawn this frame
    std::uint32_t sourceMeshes = 0;     // cached source meshes
    std::uint32_t sourceVertices = 0;   // sum over drawn objects
    std::uint32_t sourceTriangles = 0;
    std::uint64_t instances = 0;        // sum over drawn objects
    std::uint64_t logicalTriangles = 0; // sourceTriangles * instances
    std::uint64_t instanceBufferBytes = 0;
    std::uint32_t deformers = 0;        // enabled deformers over drawn objects
    double cpuUpdateMs = 0.0;           // rebuild + upload time this frame
    std::uint32_t uploads = 0;          // instance buffer uploads this frame
    // Fields (ADR-025)
    std::uint32_t effectorObjects = 0;   // objects that ran the effector pass this frame
    std::uint64_t effectorInstances = 0; // records processed by the effector pass this frame
    std::uint32_t effectors = 0;         // usable effectors over those objects
    double effectorPassMs = -1.0;        // GPU time of the last measured effector pass (-1 = none / unavailable)
    std::uint32_t pointObjects = 0;      // objects drawn as Point billboards
    std::uint32_t fieldDeformers = 0;    // enabled Field deformers bound to a slot
};

// The deformer record as the shader sees it (64 bytes, std140-compatible). Mirrors
// shaders/procedural.wgsl `DeformerUniform`.
struct DeformerUniform {
    glm::vec4 axisKind;      // xyz = unit axis, w = kind code: DeformerKind (0..5) + 8 for world space, -1 = disabled slot
    glm::vec4 centerAmount;  // xyz = center, w = amount
    glm::vec4 params;        // x = frequency (sine) | spatial scale (noise, displacement) | field slot (field), y = speed | alongNormal (field), z = phase, w = falloff
    glm::vec4 extra;         // xyz = displacementAxis (sine, bend direction) | axisMask (noise); w = seed as float bits
};
static_assert(sizeof(DeformerUniform) == 64);

// Group 1 binding 2 (one buffer per object, written every frame). The object/parent matrix and
// the material live in the 256-byte ObjectUniforms slot at group 1 binding 0 (same layout as
// entities), so this block stays small and std140-trivial.
struct ProceduralUniforms {
    glm::vec4 timeInfo;      // x = render time, y = deformer count, z = epsilon for normals, w = instance count
    glm::vec4 fieldInfo;     // x = emissive field slot (-1 none), y = emissive field amount, z = point source (1/0), w = 0
    DeformerUniform deformers[scene::kMaxDeformers];
};
static_assert(sizeof(ProceduralUniforms) == 32 + 64 * scene::kMaxDeformers);

// The effector pass parameters (shaders/points.wgsl `PointsParams`, 528 bytes).
struct EffectorPassUniforms {
    glm::mat4 objectToWorld;
    glm::mat4 worldToObjectRotation; // transpose of the rotation-only part of objectToWorld
    glm::uvec4 info;                 // x = record count, y = effector count
    spatial::EffectorGpu effectors[spatial::kMaxEffectors];
};
static_assert(sizeof(EffectorPassUniforms) == 128 + 16 + 48 * spatial::kMaxEffectors);

class ProceduralRenderer {
public:
    ProceduralRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ProceduralRenderer();
    ProceduralRenderer(const ProceduralRenderer&) = delete;
    ProceduralRenderer& operator=(const ProceduralRenderer&) = delete;

    // Creates pipelines for the given colour/depth formats and the frame/material/IBL bind group
    // layouts shared with the entity pipelines (group 0 frame, group 2 material, group 3 IBL);
    // group 1 is this renderer's own (object uniforms + instances + deformers + field block).
    // `fieldBlock` is the FieldUniforms buffer (a zeroed private one is created when null).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, std::uint32_t sampleCount = 1,
                                    wgpu::Buffer fieldBlock = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of procedural.wgsl / points.wgsl (keeps buffers)

    // Per frame, before the lit pass: uploads source meshes (cached by `meshHash`) and instance
    // buffers that changed, writes the deformer/time and object uniforms, and encodes the
    // effector compute pass for objects with effectors. The scene is const here, so the engine
    // side must call `ProceduralGeometry::rebuild()` before rendering; `structureVersion` and
    // `meshHash` are the change signals this renderer keys uploads on (an object with an empty
    // `instances` or a zero `meshHash` is skipped). Per-object GPU state is keyed by the object's
    // name. `objectMatrices[i]` is the parent matrix for scene.procedurals[i] (identity when the
    // scene places them itself; missing entries = identity). `fields` resolves field names to
    // slots (null = no fields: effectors, Field deformers and emissive fields are inert).
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                const std::vector<glm::mat4>& objectMatrices, const FrameTime& time,
                const FieldUniforms* fields = nullptr);
    // Inside the lit pass (frame and IBL bind groups already set by the caller): sets its own
    // pipeline and group 1, the material group via `materialBindGroup`, and draws every visible
    // object. Opaque objects only in this phase (blend materials are drawn opaque).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup);
    // Pumps the effector-pass timer after the frame's command buffer was submitted (update()
    // also does this at the start of the next frame).
    void collectTimings();

    [[nodiscard]] const ProceduralStats& stats() const { return stats_; }
    // Drops cached meshes not used for `frames` frames (called by update).
    void setMeshCacheLimit(std::size_t frames) { cacheFrames_ = frames; }

    // Blocking readback of the records the draw reads for the object named `name` (the live
    // buffer after the effector pass, else the base buffer). Tests and tools only.
    [[nodiscard]] Result<std::vector<scene::InstanceRecord>> readInstanceRecords(const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ProceduralStats stats_;
    std::size_t cacheFrames_ = 120;
};

} // namespace avgen::rendering
