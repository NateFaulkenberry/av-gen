#pragma once

// ADR-1850: the Strange Attractor's Environment. One fullscreen pass that ray-marches the scene's baked height field
// (scene::AttractorWorld) and its sky, lights and fogs it, and writes the scene's HDR target and depth buffer, so
// AV Gen's bloom, tonemap and outputs treat it like any other geometry.

#include "rendering/environment_renderer.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

class AttractorRenderer final : public EnvironmentRenderer {
public:
    [[nodiscard]] std::string_view name() const override { return "attractorWorld"; }
    [[nodiscard]] bool wants(const scene::Scene& scene) const override;
    void encode(const EnvironmentFrame& frame) override;
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders) override;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat depthFormat);
    [[nodiscard]] bool ready() const { return static_cast<bool>(pipeline_); }
    // How many times a world has been uploaded (tests: a look change must not re-upload).
    [[nodiscard]] std::uint64_t uploads() const { return uploads_; }

private:
    [[nodiscard]] Result<void> createPipeline(gpu::ShaderLibrary& shaders);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;
    wgpu::BindGroupLayout layout_;
    wgpu::RenderPipeline pipeline_;
    wgpu::Buffer uniforms_;
    wgpu::Texture height_;
    wgpu::Texture masks_;
    wgpu::Texture flow_; // ADR-1854: the river's downstream direction
    wgpu::Sampler sampler_;
    std::uint64_t worldHash_ = 0;
    std::uint64_t uploads_ = 0;
};

} // namespace avgen::rendering
