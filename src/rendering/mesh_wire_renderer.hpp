#pragma once

// ADR-1862: MESH WIRE's GPU half (scene/mesh_wire.hpp has the design; shaders/mesh_wire.wgsl the drawing).
//
// A pipeline inside the scene pass, in its blended section beside the ribbons: lines are light laid over the world,
// depth-tested against it and never written into its depth. Group 0 is the scene's frame group (camera, fog); group 1
// is per mesh: the settings, the mesh's own vertex and index buffers as storage, and a per-draw transform at a dynamic
// offset. There is no edge list: each triangle draws the edges it owns straight from the buffers the lit pass draws.
//
// **Gate.** With `scene.meshWire.active()` false, `draw` returns before touching the pass or the queue -- no bind, no
// upload -- so a scene without the block renders byte-identically to a build without this renderer.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct MeshWireStats {
    std::uint32_t draws = 0;     // meshes drawn this frame
    std::uint64_t triangles = 0; // triangle instances submitted
    std::uint32_t culled = 0;    // target meshes skipped (beyond the fade or outside the frustum)
};

// What the renderer needs of a mesh the scene renderer has uploaded: its buffers (created with Storage usage) and
// counts. Null buffers mean "not resident".
struct MeshWireGeometry {
    wgpu::Buffer vertices;
    wgpu::Buffer indices;
    std::uint64_t vertexBytes = 0;
    std::uint32_t indexCount = 0;
};

class MeshWireRenderer {
public:
    MeshWireRenderer() = default;
    MeshWireRenderer(const MeshWireRenderer&) = delete;
    MeshWireRenderer& operator=(const MeshWireRenderer&) = delete;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat depthFormat, const wgpu::BindGroupLayout& frameLayout);
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders);

    // Draws every visible entity `scene.meshWire` targets into the open scene pass. `geometry` returns a mesh's
    // resident buffers. Nothing at all when the block is inactive.
    void draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup, const scene::Scene& scene,
              const std::function<MeshWireGeometry(scene::MeshId)>& geometry);

    // The scene renderer re-uploaded its meshes: every cached bind group names a dead buffer.
    void invalidate() { groups_.clear(); }

    [[nodiscard]] bool ready() const { return static_cast<bool>(pipeline_); }
    [[nodiscard]] const MeshWireStats& stats() const { return stats_; }

private:
    [[nodiscard]] Result<void> createPipeline(gpu::ShaderLibrary& shaders);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;
    wgpu::BindGroupLayout groupLayout_;
    wgpu::PipelineLayout layout_;
    wgpu::RenderPipeline pipeline_;
    wgpu::Buffer params_;
    wgpu::Buffer draws_;              // per-draw transforms at kDrawStride
    std::uint32_t drawCapacity_ = 0;
    std::unordered_map<scene::MeshId, wgpu::BindGroup> groups_;
    MeshWireStats stats_;
};

} // namespace avgen::rendering
