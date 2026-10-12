#pragma once

// ADR-1289: the cloud Environment. A fullscreen pass over the lit frame that ray-marches the scene's authored cloud
// lobes (scene::Clouds) against the linear depth and composites them, premultiplied, into HDR.

#include "rendering/environment_renderer.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

class CloudRenderer final : public EnvironmentRenderer {
public:
    [[nodiscard]] std::string_view name() const override { return "clouds"; }
    [[nodiscard]] bool wants(const scene::Scene& scene) const override;
    void encode(const EnvironmentFrame& frame) override;
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders) override;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat);
    [[nodiscard]] bool ready() const { return static_cast<bool>(pipeline_); }

private:
    [[nodiscard]] Result<void> createPipeline(gpu::ShaderLibrary& shaders);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::BindGroupLayout layout_;
    wgpu::RenderPipeline pipeline_;
    wgpu::Buffer uniforms_;
    wgpu::Buffer lobes_;
    std::uint64_t lobesKey_ = 0;
    std::uint32_t lobeCount_ = 0;
};

} // namespace avgen::rendering
