#pragma once

// GPU side of procedural geometry (ADR-023): per object a source mesh (uploaded when its hash
// changes), an instance storage buffer (uploaded when structureVersion changes), a 256-byte
// object uniform slot (material, object matrix) and a deformer uniform block (per frame); one
// DrawIndexed(indexCount, instanceCount) per visible object inside the scene's lit pass, with
// the same frame/material/IBL bind groups as entities. Shader: shaders/procedural.wgsl.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

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
};

// The deformer record as the shader sees it (64 bytes, std140-compatible).
struct DeformerUniform {
    glm::vec4 axisKind;      // xyz axis, w = kind (float) or -1 when disabled
    glm::vec4 centerAmount;  // xyz center, w = amount
    glm::vec4 params;        // x = frequency|scale, y = speed, z = phase, w = falloff
    glm::vec4 extra;         // xyz displacement axis | axis mask, w = space (0 local, 1 world) + seed*2 ... see shader
};
static_assert(sizeof(DeformerUniform) == 64);

struct ProceduralUniforms {
    glm::mat4 object;        // parent/object matrix (distribution transform included on the CPU records? no: see below)
    glm::vec4 timeInfo;      // x = render time, y = deformer count, z = epsilon for normals, w = instance count
    DeformerUniform deformers[scene::kMaxDeformers];
};

class ProceduralRenderer {
public:
    ProceduralRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ProceduralRenderer();
    ProceduralRenderer(const ProceduralRenderer&) = delete;
    ProceduralRenderer& operator=(const ProceduralRenderer&) = delete;

    // Creates pipelines for the given colour/depth formats and the frame/material/IBL bind group
    // layouts shared with the entity pipelines (group 0 frame, group 2 material, group 3 IBL);
    // group 1 is this renderer's own (object uniforms + instances + deformers).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, std::uint32_t sampleCount = 1);
    [[nodiscard]] Result<void> reload(); // hot reload of procedural.wgsl (keeps buffers)

    // Per frame, before the lit pass: rebuilds dirty objects (calls rebuild()), uploads meshes
    // and instance buffers that changed, writes uniforms. `objectMatrices[i]` is the parent
    // matrix for scene.procedurals[i] (identity when the scene places them itself).
    void update(const scene::Scene& scene, const std::vector<glm::mat4>& objectMatrices, const FrameTime& time);
    // Inside the lit pass (frame and IBL bind groups already set by the caller): sets its own
    // pipeline and group 1, the material group via `materialBindGroup`, and draws every visible
    // object. Opaque objects only in this phase (blend materials are drawn opaque).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene,
              const std::function<wgpu::BindGroup(const scene::Material&)>& materialBindGroup);

    [[nodiscard]] const ProceduralStats& stats() const { return stats_; }
    // Drops cached meshes not used for `frames` frames (called by update).
    void setMeshCacheLimit(std::size_t frames) { cacheFrames_ = frames; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ProceduralStats stats_;
    std::size_t cacheFrames_ = 120;
};

} // namespace avgen::rendering
