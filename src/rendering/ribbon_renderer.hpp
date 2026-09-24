#pragma once

// RIBBON's GPU half (Effect Library Wave 1; see world/effects/ribbon_frame.hpp for the CPU half and
// shaders/ribbon.wgsl for the shading).
//
// A pipeline inside the scene pass, drawn in its blended section beside the particles: a strip is
// light (or paint) laid over the world, depth-tested against it and never written into its depth.
// Two pipelines, additive and alpha, over one vertex arena of `world::kRibbonVertexBudget` vertices
// that the frame's strips are copied into with one `WriteBuffer`. Group 0 is the scene's own frame
// group, so the strip reads the same camera, the same previous view-projection and the same fog as
// everything else in the pass; there is no group of its own.
//
// **Gate.** `draw` with an empty frame returns before touching the pass or the queue -- no pipeline
// bind, no buffer, no upload -- so a scene with no strip renders byte-identically to a build without
// this renderer (tests/rendering/test_ribbon_gpu.cpp holds it). The arena is created the first time
// a strip is drawn, not at init, so it costs nothing on a scene that never has one.

#include "core/error.hpp"
#include "world/effects/ribbon_frame.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct RibbonStats {
    std::uint32_t strips = 0;   // drawn this frame
    std::uint32_t vertices = 0; // uploaded this frame
    std::uint32_t draws = 0;
};

class RibbonRenderer {
public:
    RibbonRenderer() = default;
    RibbonRenderer(const RibbonRenderer&) = delete;
    RibbonRenderer& operator=(const RibbonRenderer&) = delete;

    // `frameLayout` is the scene renderer's group-0 layout.
    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                    wgpu::TextureFormat hdrFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout);
    // Rebuilds both pipelines from a reloaded ribbon.wgsl; keeps the old ones on failure.
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders);

    // Uploads `frame`'s vertices and draws each strip into the open scene pass, rebinding group 0
    // to `frameGroup` (the particles bind their own layout there). Nothing at all when the frame is
    // empty.
    void draw(wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& frameGroup, const world::RibbonFrame& frame);

    [[nodiscard]] bool ready() const { return static_cast<bool>(additive_) && static_cast<bool>(alpha_); }
    [[nodiscard]] const RibbonStats& stats() const { return stats_; }

private:
    [[nodiscard]] Result<void> createPipelines(gpu::ShaderLibrary& shaders);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;
    wgpu::PipelineLayout layout_;
    wgpu::RenderPipeline additive_;
    wgpu::RenderPipeline alpha_;
    wgpu::Buffer arena_;
    RibbonStats stats_;
};

} // namespace avgen::rendering
