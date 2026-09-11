#pragma once

// The GPU half of the 2D composition (ADR-083).
//
// One vertex buffer, one storage buffer of per-frame item styles, one atlas, one render pass.
// Layers are drawn in stack order into the tone-mapped target; adjacent layers that share a blend
// mode and whose geometry is contiguous become a single draw, so a composition of a hundred plain
// text layers is one draw call, not a hundred.
//
// The cost model, which is the point of the whole arrangement:
//   * changing what a layer *says* rebuilds its geometry (rare, authoring-time);
//   * changing where it is, how big, what colour, how opaque -- everything that animates -- costs
//     96 bytes of upload per item and nothing else;
//   * a layer outside its time range, or keyframed to nothing, is not drawn at all.

#include "comp/layer_stack.hpp"
#include "core/error.hpp"
#include "gpu/render_target.hpp"
#include "rendering/frame_overlay.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct CompositionStats {
    std::uint32_t layers = 0;    // layers that contributed a draw this frame
    std::uint32_t draws = 0;
    std::uint32_t items = 0;
    std::uint32_t vertices = 0;
    std::uint32_t glyphs = 0;    // distinct glyphs resident in the atlas
    std::uint32_t atlasRows = 0; // rows of the atlas in use
    std::uint64_t geometryRebuilds = 0;
    double gpuMs = -1.0;
    double cpuBuildMs = 0.0;
};

class CompositionRenderer final : public FrameOverlay {
public:
    CompositionRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~CompositionRenderer() override;
    CompositionRenderer(const CompositionRenderer&) = delete;
    CompositionRenderer& operator=(const CompositionRenderer&) = delete;

    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload(); // hot reload of composite.wgsl

    // What to draw next frame, and the second to evaluate it at. That second is the engine's
    // timeline clock, not a wall clock and not a frame counter, which is what makes a lyric land
    // in the same place in an offline render as it does in live playback.
    void setInput(comp::LayerStack* stack, double seconds) {
        stack_ = stack;
        seconds_ = seconds;
    }
    void setTimeline(gpu::FrameTimeline* timeline) { timeline_ = timeline; }

    void encodeOverlay(wgpu::CommandEncoder& encoder, const gpu::TargetView& target) override;
    // Reads this frame's GPU time off the shared timeline. Call after Submit(), like the rest.
    void collectTimings();

    [[nodiscard]] const CompositionStats& stats() const { return stats_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

private:
    struct Uniforms {
        float targetSize[2];
        float atlasSize[2];
    };
    static constexpr std::uint32_t kItemStride = sizeof(comp::LayerItem);

    Result<wgpu::RenderPipeline> pipelineFor(wgpu::TextureFormat format, comp::BlendMode blend);
    void ensureAtlas();
    void ensureVertices();
    void ensureItems(std::uint32_t count);
    void rebuildBindGroup();

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    gpu::FrameTimeline* timeline_ = nullptr;
    comp::LayerStack* stack_ = nullptr;
    double seconds_ = 0.0;
    bool initialised_ = false;

    wgpu::ShaderModule module_;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    std::unordered_map<std::uint64_t, wgpu::RenderPipeline> pipelines_;
    wgpu::BindGroup bindGroup_;
    wgpu::Buffer uniforms_;
    wgpu::Buffer vertices_;
    std::uint64_t vertexCapacity_ = 0;
    std::uint64_t uploadedVertexVersion_ = 0;
    wgpu::Buffer items_;
    std::uint64_t itemCapacity_ = 0;
    wgpu::Texture atlas_;
    wgpu::TextureView atlasView_;
    std::uint64_t uploadedAtlasVersion_ = 0;
    wgpu::Sampler sampler_;
    CompositionStats stats_;
};

} // namespace avgen::rendering
