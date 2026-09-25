#pragma once

// SHELL's GPU half (Effect Library Wave 3; world/effects/shell_frame.hpp is the CPU half and
// shaders/shell.wgsl the shading).
//
// Pipelines inside the scene pass, drawn in its blended section beside the particles and the
// ribbons: a shell is light laid over the world, depth-tested against it and never written into its
// depth (phase 2's Portal and Tear interiors excepted, which write it). ONE PIPELINE PER SHADING KIND
// (plasma, shield, barrier; phase 2's beam, glare, ring, bubble, portal, tear -- ADR-118), over:
//   - the canonical meshes, made once from `scene/mesh_generators` the first time a shell draws;
//   - ONE storage buffer of `world::kMaxShells` records, uploaded with one `WriteBuffer`, and one
//     of their side data (hits, revealer positions);
//   - one instanced draw per batch (a run of records sharing a mesh and a shading kind), whose
//     `firstInstance` is the run's start, so `instance_index` addresses the record directly.
//
// Group 0 is the scene's own frame group (camera, fog, and the linear depth at binding 7). Group 1
// is this renderer's (records, side data, and whether the linear depth is this frame's). Group 2 is
// the scene's IBL group, for a shield's sheen. Group 3 is not touched: the lit draws that follow
// keep the IBL the pass bound there.
//
// **Gate.** `draw` with an empty frame returns before touching the pass or the queue -- no pipeline
// bind, no buffer, no upload -- so a scene with no shell renders byte-identically to a build without
// this renderer (tests/rendering/test_shell_gpu.cpp holds it). Buffers and meshes are created the
// first time a shell draws, not at init.

#include "core/error.hpp"
#include "world/effects/shell_frame.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct ShellStats {
    std::uint32_t shells = 0;  // records drawn this frame
    std::uint32_t draws = 0;   // instanced draws issued
    bool linearDepth = false;  // whether this frame's depth terms were live
};

class ShellRenderer {
public:
    ShellRenderer() = default;
    ShellRenderer(const ShellRenderer&) = delete;
    ShellRenderer& operator=(const ShellRenderer&) = delete;

    // `frameLayout` is the scene renderer's group-0 layout; `iblLayout` its IBL group's.
    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                    wgpu::TextureFormat hdrFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout, const wgpu::BindGroupLayout& iblLayout);
    // Rebuilds every pipeline from a reloaded shell.wgsl / shell_fx.wgsl; keeps the old ones on failure.
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders);

    // Uploads `frame` and draws each batch into the open scene pass, binding group 0 to
    // `frameGroup` and group 2 to `iblGroup`. `linearDepthThisFrame` is whether the depth prepass
    // resolved the linear depth this frame; without it the depth terms are off. Nothing at all when
    // the frame is empty.
    void draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup, const wgpu::BindGroup& iblGroup,
              const world::ShellFrame& frame, bool linearDepthThisFrame);

    [[nodiscard]] bool ready() const;
    [[nodiscard]] const ShellStats& stats() const { return stats_; }

private:
    struct Mesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;
    };
    [[nodiscard]] Result<void> createPipelines(gpu::ShaderLibrary& shaders);
    void createResources();

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;
    wgpu::BindGroupLayout groupLayout_;
    wgpu::PipelineLayout layout_;
    std::array<wgpu::RenderPipeline, world::kShellShadingCount> pipelines_{};
    std::array<Mesh, world::kShellMeshCount> meshes_{};
    wgpu::Buffer records_;
    wgpu::Buffer extra_;
    wgpu::Buffer info_;
    wgpu::BindGroup group_;
    float lastInfo_ = -1.0f;
    ShellStats stats_;
};

} // namespace avgen::rendering
