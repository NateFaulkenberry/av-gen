#pragma once

// Draws a source texture (RGBA8/BGRA8 with TextureBinding usage) onto a target view through an
// OutputMapping (milestone 1.2): crop, projective warp, soft-edge blend, brightness, gamma,
// flips. One fullscreen pass per draw; identity mappings take a plain-blit fast path. Used for
// every output window and for the main window (identity).

#include "core/error.hpp"
#include "rendering/output_mapping.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <unordered_map>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

// GPU mirror of OutputMapUniforms in shaders/output_map.wgsl.
struct OutputMapUniforms {
    float inv[3][4];   // columns of the target -> unit-square homography, xyz used
    float crop[4];
    float blend[4];
    float params[4];   // blendGamma, brightness, 1 / gamma, flags
};
static_assert(sizeof(OutputMapUniforms) == 96);

class OutputMapper {
public:
    OutputMapper(gpu::Context& context, gpu::ShaderLibrary& shaders);

    // Loads shaders/output_map.wgsl and creates the layouts. Pipelines are built per target format.
    [[nodiscard]] Result<void> init();

    // Encodes one render pass drawing `source` into `target` (`targetWidth` x `targetHeight`
    // pixels) through `mapping`. The target is cleared to black first.
    [[nodiscard]] Result<void> draw(wgpu::CommandEncoder& encoder, const wgpu::TextureView& source,
                                    const wgpu::TextureView& target, std::uint32_t targetWidth,
                                    std::uint32_t targetHeight, const OutputMapping& mapping,
                                    wgpu::TextureFormat targetFormat = wgpu::TextureFormat::BGRA8Unorm);

    // Identity mappings use a plain blit unless disabled (tests exercise the full path with it).
    void setFastPathEnabled(bool enabled) { fastPath_ = enabled; }
    [[nodiscard]] bool fastPathEnabled() const { return fastPath_; }
    [[nodiscard]] std::uint32_t drawCount() const { return draws_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

    static OutputMapUniforms uniformsFor(const OutputMapping& mapping);
    static constexpr std::uint32_t kMaxDrawsInFlight = 64;
    static constexpr std::uint32_t kSlotStride = 256;

private:
    Result<wgpu::RenderPipeline> pipelineFor(wgpu::TextureFormat format, bool blit);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    wgpu::ShaderModule module_;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::Sampler sampler_;
    wgpu::Buffer uniforms_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> pipelines_;
    std::uint32_t slot_ = 0;
    std::uint32_t draws_ = 0;
    bool fastPath_ = true;
    bool initialised_ = false;
};

} // namespace avgen::rendering
