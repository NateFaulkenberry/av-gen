#pragma once

// Draws a scene::Scene with WebGPU (ADR-001). Frame = ordered passes: [scene -> HDR target]
// (opaque PBR, skybox, additive grid, blended PBR), [tonemap -> caller's target]. The caller
// owns the command encoder so it can append passes (UI) and submit; renderToImage() wraps that
// for tests and offline output.

#include "core/error.hpp"
#include "core/time.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/texture.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class EnvironmentProcessor;

struct RenderStats {
    double gpuFrameMs = -1.0; // -1 when timestamp queries are unavailable
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t entities = 0;
    std::uint32_t lights = 0;
    std::uint32_t textures = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool ibl = false;
};

constexpr std::uint32_t kMaxLights = 8;

// GPU-side mirrors of the WGSL uniform structs in shaders/common.wgsl and tonemap.wgsl.
struct LightUniform {
    glm::vec4 positionType;
    glm::vec4 directionRange;
    glm::vec4 colorIntensity;
    glm::vec4 cone;
};
static_assert(sizeof(LightUniform) == 64);

struct FrameUniforms {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;
    glm::vec4 params;
    glm::vec4 envParams;
    glm::vec4 skyParams;
    LightUniform lights[kMaxLights];
};
static_assert(sizeof(FrameUniforms) == 128 + 64 + 512);

struct ObjectUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 baseColor;
    glm::vec4 emissive;
    glm::vec4 material;
    glm::vec4 flags;
};
static_assert(sizeof(ObjectUniforms) == 192);

struct TonemapUniforms {
    float exposure;
    float pad[3];
};
static_assert(sizeof(TonemapUniforms) == 16);

// Image-based-lighting inputs shared by the PBR and skybox passes.
struct IblResources {
    wgpu::TextureView irradiance;  // cube
    wgpu::TextureView prefiltered; // cube, mips by roughness
    wgpu::TextureView brdfLut;     // 2D RG
    std::uint32_t prefilteredMips = 1;
    bool valid = false;
};

class SceneRenderer {
public:
    SceneRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~SceneRenderer();

    [[nodiscard]] Result<void> init();
    // (Re)creates the HDR target. Idempotent for equal sizes.
    [[nodiscard]] Result<void> resize(std::uint32_t width, std::uint32_t height);

    // Encodes the scene and tonemap passes. `target` must match the size passed to resize().
    [[nodiscard]] Result<void> render(wgpu::CommandEncoder& encoder, const scene::Scene& scene,
                                      const FrameTime& time, const gpu::TargetView& target);

    // Full frame into a fresh RGBA8 texture, submitted and read back. For tests and offline use.
    [[nodiscard]] Result<gpu::Image8> renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                    std::uint32_t width, std::uint32_t height);

    // Installs image-based lighting produced by EnvironmentProcessor (or clears it).
    void setIbl(const IblResources& ibl);
    [[nodiscard]] const IblResources& ibl() const { return ibl_; }

    [[nodiscard]] const RenderStats& stats() const { return stats_; }
    [[nodiscard]] gpu::GpuTimer& timer() { return *timer_; }
    [[nodiscard]] const gpu::RenderTarget& hdrTarget() const { return hdr_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

    static constexpr std::uint32_t kMaxObjects = 256;
    static constexpr std::uint32_t kObjectStride = 256; // dynamic-offset alignment
    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;

private:
    struct GpuMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;
    };
    enum class LitVariant : std::uint8_t { OpaqueCull, OpaqueNoCull, Blend };

    Result<void> createPipelines();
    Result<wgpu::RenderPipeline> createLitPipeline(const wgpu::ShaderModule& module, LitVariant variant);
    Result<wgpu::RenderPipeline> createGridPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createSkyboxPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> tonemapPipelineFor(wgpu::TextureFormat format);
    Result<wgpu::RenderPipeline> finishPipeline(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    void uploadMeshes(const scene::Scene& scene);
    void uploadTextures(const scene::Scene& scene);
    void ensureTonemapBindGroup();
    void rebuildIblBindGroup();
    const wgpu::BindGroup& materialBindGroup(const scene::Material& material);
    const gpu::GpuTexture& textureOrDefault(const scene::TextureRef& ref, const gpu::GpuTexture& fallback) const;

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::unique_ptr<gpu::GpuTimer> timer_;
    std::unique_ptr<gpu::SamplerCache> samplers_;
    bool initialised_ = false;

    gpu::RenderTarget hdr_;
    wgpu::BindGroupLayout frameLayout_;
    wgpu::BindGroupLayout objectLayout_;
    wgpu::BindGroupLayout materialLayout_;
    wgpu::BindGroupLayout iblLayout_;
    wgpu::BindGroupLayout tonemapLayout_;
    wgpu::PipelineLayout scenePipelineLayout_;
    wgpu::PipelineLayout tonemapPipelineLayout_;
    wgpu::RenderPipeline litOpaqueCull_;
    wgpu::RenderPipeline litOpaqueNoCull_;
    wgpu::RenderPipeline litBlend_;
    wgpu::RenderPipeline gridPipeline_;
    wgpu::RenderPipeline skyboxPipeline_;
    wgpu::ShaderModule tonemapModule_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> tonemapPipelines_;

    wgpu::Buffer frameUniforms_;
    wgpu::Buffer objectUniforms_;
    wgpu::Buffer tonemapUniforms_;
    wgpu::BindGroup frameBindGroup_;
    wgpu::BindGroup objectBindGroup_;
    wgpu::BindGroup tonemapBindGroup_;
    wgpu::BindGroup iblBindGroup_;
    wgpu::TextureView tonemapBoundView_;

    // Defaults for absent textures and IBL.
    gpu::GpuTexture whiteSrgb_;
    gpu::GpuTexture whiteLinear_;
    gpu::GpuTexture flatNormal_;
    gpu::GpuTexture blackCube_;
    wgpu::TextureView blackCubeView_;
    gpu::GpuTexture blackLut_;
    wgpu::Sampler iblSampler_;
    IblResources ibl_;

    std::vector<GpuMesh> meshes_;
    std::uint64_t meshVersion_ = ~0ull;
    std::vector<gpu::GpuTexture> textures_;
    std::uint64_t textureVersion_ = ~0ull;
    std::unordered_map<std::uint64_t, wgpu::BindGroup> materialBindGroups_;
    std::vector<std::uint8_t> objectStaging_;
    RenderStats stats_;
};

} // namespace avgen::rendering
