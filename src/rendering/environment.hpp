#pragma once

// Builds image-based-lighting resources from an equirectangular HDR map (research:
// rendering-techniques.md, assets.md §IBL): source cube with mips, diffuse irradiance cube,
// GGX-prefiltered specular cube (mip = roughness), and the split-sum BRDF LUT. All passes are
// fullscreen fragment passes with fixed sample sequences, so output is deterministic per GPU.

#include "core/error.hpp"
#include "gpu/texture.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct EnvironmentSettings {
    std::uint32_t cubeSize = 256;        // source cube face size (mips down to 1)
    std::uint32_t irradianceSize = 32;
    std::uint32_t prefilteredSize = 128; // 6 mips: roughness 0, 0.2, ... 1
    std::uint32_t prefilteredMips = 6;
    std::uint32_t brdfSize = 128;
    std::uint32_t irradianceSamples = 256;
    std::uint32_t prefilterSamples = 128;
    std::uint32_t brdfSamples = 256;
};

class EnvironmentProcessor {
public:
    EnvironmentProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<void> init();

    // Uploads the equirect map (converted to RGBA16Float with mips) and runs every pass. Blocks
    // until the GPU work is done (tens of milliseconds); intended for load time, not per frame.
    [[nodiscard]] Result<IblResources> process(const scene::TextureData& equirect, const EnvironmentSettings& settings = {});

    // The BRDF LUT is environment-independent; computed once on first use.
    [[nodiscard]] const gpu::GpuTexture& brdfLut() const { return brdf_; }

private:
    struct EnvUniforms {
        std::uint32_t faceIndex;
        std::uint32_t mipLevel;
        std::uint32_t sampleCount;
        std::uint32_t pad0;
        float roughness;
        float sourceMipCount;
        float sourceSize;
        float pad1;
    };
    static_assert(sizeof(EnvUniforms) == 32);

    struct CubeTexture {
        wgpu::Texture texture;
        wgpu::TextureView cubeView;
        std::uint32_t size = 0;
        std::uint32_t mips = 1;
    };

    Result<CubeTexture> createCube(std::uint32_t size, std::uint32_t mips, const char* label);
    Result<wgpu::RenderPipeline> createPipeline(const wgpu::ShaderModule& module, const char* entry,
                                                wgpu::TextureFormat format, const char* label);
    void runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                 const wgpu::BindGroup& bindGroup, const EnvUniforms& uniforms, std::uint32_t slot);
    wgpu::BindGroup makeBindGroup(const wgpu::TextureView& equirect, const wgpu::TextureView& cube);
    Result<void> ensureBrdf(const EnvironmentSettings& settings);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    bool initialised_ = false;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline equirectPipeline_;
    wgpu::RenderPipeline irradiancePipeline_;
    wgpu::RenderPipeline prefilterPipeline_;
    wgpu::RenderPipeline brdfPipeline_;
    wgpu::Sampler sampler_;
    wgpu::Buffer uniforms_; // ring of 256-byte slots, one per pass in a batch
    gpu::GpuTexture brdf_;
    gpu::GpuTexture placeholder2d_;
    CubeTexture placeholderCube_;
    static constexpr std::uint32_t kUniformSlots = 128;
    static constexpr std::uint32_t kUniformStride = 256;
};

} // namespace avgen::rendering
