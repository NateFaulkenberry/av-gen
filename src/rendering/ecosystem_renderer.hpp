#pragma once

// ADR-1200: the ecosystem's GPU half (scene/ecosystem.hpp is the model; shaders/ecosystem.wgsl the kernels).
//
// The first ENVIRONMENT renderer, and the seam every later one uses: a specialised renderer owned by
// SceneRenderer that reads its own slice of scene::Scene, runs its own passes, and writes the shared HDR
// and emission targets with the shared depth -- so post, bloom, tonemap, every output path, the live
// quality ladder and any conventional content (a HYBRID scene) work with it unchanged.
//
// Per frame, after the lit pass (it needs the prepass's linear depth):
//   1. clear the accumulation buffer and the counters;
//   2. cs_emit per layer: every (host instance, template point), placed, lit, depth-tested, then splatted
//      into a u32 fixed-point accumulation buffer, or appended to the sprite list when it is big on screen;
//   3. a resolve pass adds the accumulation into HDR and emission (additive);
//   4. a sprite pass draws the near emitters as soft discs (indirect, hardware depth test, no depth write).
// Nothing at all -- no buffer, no pass -- when the scene has no active ecosystem (the gate).

#include "core/error.hpp"
#include "rendering/environment_renderer.hpp"
#include "rendering/field_uniforms.hpp"
#include "scene/ecosystem.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
class FrameTimeline;
} // namespace avgen::gpu

namespace avgen::scene {
struct Scene;
}

namespace avgen::rendering {

struct EcosystemStats {
    std::uint32_t layers = 0;      // drawn this frame
    std::uint64_t hosts = 0;       // host instances carrying a template
    std::uint64_t candidates = 0;  // host instances x template points (the dispatch)
    std::uint32_t dispatches = 0;
};

class EcosystemRenderer final : public EnvironmentRenderer {
public:
    EcosystemRenderer() = default;
    EcosystemRenderer(const EcosystemRenderer&) = delete;
    EcosystemRenderer& operator=(const EcosystemRenderer&) = delete;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat depthFormat);
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders) override;

    // EnvironmentRenderer: "ecosystem"; wants a scene whose ecosystem block is active; encodes compute, resolve and
    // sprites for the frame.
    [[nodiscard]] std::string_view name() const override { return "ecosystem"; }
    [[nodiscard]] bool wants(const scene::Scene& scene) const override;
    void encode(const EnvironmentFrame& frame) override;

    [[nodiscard]] const EcosystemStats& stats() const { return stats_; }
    [[nodiscard]] bool ready() const { return static_cast<bool>(emit_); }

private:
    struct LayerGpu {
        std::uint64_t key = 0;          // hosts' identity + template hash: rebuilt when it moves
        std::uint32_t hostCount = 0;
        std::uint32_t pointCount = 0;
        float boundRadius = 0.0f;
        wgpu::Buffer uniforms;
        wgpu::Buffer hosts;
        wgpu::Buffer points;
        wgpu::BindGroup group;
    };

    [[nodiscard]] Result<void> createPipelines(gpu::ShaderLibrary& shaders);
    void ensureTargets(std::uint32_t width, std::uint32_t height, std::uint32_t maxSprites);
    void syncLayer(std::size_t index, const scene::Scene& scene, const scene::EmitterLayer& layer);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;

    wgpu::BindGroupLayout computeLayout0_;
    wgpu::BindGroupLayout emptyLayout_;
    wgpu::BindGroupLayout layerLayout_;
    wgpu::BindGroupLayout resolveLayout_;
    wgpu::BindGroupLayout spriteLayout_;
    wgpu::ComputePipeline emit_;
    wgpu::ComputePipeline spriteArgs_;
    wgpu::RenderPipeline resolve_;
    wgpu::RenderPipeline sprite_;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t maxSprites_ = 0;
    wgpu::Buffer accum_;
    wgpu::Buffer sprites_;
    wgpu::Buffer counters_;
    wgpu::Buffer spriteArgsBuffer_;
    wgpu::Buffer frameBuffer_; // EcoFrame
    std::vector<LayerGpu> layers_;
    EcosystemStats stats_;
};

} // namespace avgen::rendering
