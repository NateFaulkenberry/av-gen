#pragma once

// Builds image-based-lighting resources from an equirectangular HDR map (research:
// rendering-techniques.md, assets.md §IBL): source cube with mips, diffuse irradiance cube,
// GGX-prefiltered specular cube (mip = roughness), and the split-sum BRDF LUT. All passes are
// fullscreen fragment passes with fixed sample sequences, so output is deterministic per GPU.
//
// `processSky` (ADR-036) feeds the same chain from the analytic sky in scene/sky.hpp instead of a
// map, so a scene with no HDR environment still lights metals correctly. It is built once and
// cached by the sky's hash; the renderer only rebuilds when a parameter actually changes.

#include "core/error.hpp"
#include "gpu/texture.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "scene/sky.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// How many times this process has run the full IBL chain -- cube, irradiance, GGX prefilter -- and
// blocked the main thread on it. `process`/`processSky` are documented as load-time work; this is
// how "not per frame" stops being a hope and becomes a number a run can be judged against.
[[nodiscard]] std::uint64_t environmentBuildCount() noexcept;

// ADR-1070: one procedural-sky IBL build in flight (EnvironmentProcessor::beginSky / advanceSky).
struct SkyBuildJob {
    struct Pass {
        const wgpu::RenderPipeline* pipeline = nullptr;
        wgpu::TextureView target;
        wgpu::BindGroup group;
        std::array<std::uint8_t, 112> uniforms{}; // an EnvUniforms
        double cost = 0.0;
    };
    std::vector<Pass> passes;
    std::size_t next = 0;
    IblResources result;
    scene::SkyRuntime sky{};
    EnvironmentSettings settings{};
    double totalCost = 0.0;
    [[nodiscard]] bool active() const { return !passes.empty() && next < passes.size(); }
};

class EnvironmentProcessor {
public:
    EnvironmentProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<void> init();

    // Uploads the equirect map (converted to RGBA16Float with mips) and runs every pass. Blocks
    // until the GPU work is done (tens of milliseconds); intended for load time, not per frame.
    [[nodiscard]] Result<IblResources> process(const scene::TextureData& equirect, const EnvironmentSettings& settings = {});

    // Same chain, fed by the analytic procedural sky rather than a map (ADR-036). Blocks like
    // `process` does; call it when the sky's hash changes, not per frame.
    [[nodiscard]] Result<IblResources> processSky(const scene::SkyRuntime& sky,
                                                  const EnvironmentSettings& settings = {});

    // ADR-1070: the same chain as `processSky`, run a slice a frame without blocking. `beginSky`
    // allocates the new cubes and records every pass (nothing is submitted); `advanceSky` encodes
    // passes until their cost reaches `budget` (texels written x samples taken; at least one pass)
    // and submits them WITHOUT waiting, and returns true once the last pass is submitted. The IBL in
    // `job.result` is then complete as far as anything submitted after it can tell: WebGPU runs a
    // queue's submissions in order, so the frame that binds it reads finished cubes. The cubes the
    // renderer is drawing meanwhile are untouched (the job writes new ones), so the lighting on
    // screen is the previous sky until the swap.
    using SkyJob = SkyBuildJob;
    [[nodiscard]] Result<void> beginSky(SkyJob& job, const scene::SkyRuntime& sky,
                                        const EnvironmentSettings& settings = {});
    [[nodiscard]] bool advanceSky(SkyJob& job, double budget);

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
        float faceSize;         // width of the face being written (fs_sky uses it for the disc)
        glm::vec4 skyZenith;    // rgb, w = hazeWidth
        glm::vec4 skyHorizon;   // rgb, w = sunAngularRadius
        glm::vec4 skyGround;    // rgb, w = sunGlowWidth
        glm::vec4 skySun;       // rgb = sun colour * intensity, w = overall intensity
        glm::vec4 skySunDir;    // xyz = unit direction towards the sun
    };
    static_assert(sizeof(EnvUniforms) == 32 + 80);
    static_assert(sizeof(EnvUniforms) == sizeof(SkyJob::Pass::uniforms));

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
    // Shared tail of `process` and `processSky`: irradiance + prefiltered specular from a
    // finished source cube.
    Result<IblResources> filterCube(const CubeTexture& sourceCube, const EnvironmentSettings& settings);
    // ADR-1070: the sky chain as a list of passes, shared by `processSky` (run blocking, as before)
    // and `beginSky` (run a slice a frame).
    Result<void> recordSky(SkyJob& job, const scene::SkyRuntime& sky, const EnvironmentSettings& settings);
    void recordFilter(SkyJob& job, const CubeTexture& sourceCube, const CubeTexture& irradiance,
                      const CubeTexture& prefiltered, const EnvironmentSettings& settings);
    void addPass(SkyJob& job, const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                 const wgpu::BindGroup& group, const EnvUniforms& uniforms, double cost);
    // Every remaining pass, submitting and waiting whenever the uniform ring fills and once at the
    // end: exactly the submission pattern the chain always had.
    void runBlocking(SkyJob& job);
    // Encodes one recorded pass into `encoder` at uniform slot `slot`.
    void encodePass(wgpu::CommandEncoder& encoder, const SkyJob::Pass& pass, std::uint32_t slot);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    bool initialised_ = false;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline equirectPipeline_;
    wgpu::RenderPipeline skyPipeline_;
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
