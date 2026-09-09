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
#include "gpu/transient_pool.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/post_processor.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/shader_layer.hpp"
#include "scene/scene.hpp"
#include "shaders/shader_layers.hpp"

#include "analysis/analyzer.hpp"

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

// Optional per-frame inputs for user shader layers.
struct ShaderFrameInputs {
    const shaders::ShaderLayerSet* layers = nullptr;
    const analysis::AnalysisFrame* frame = nullptr; // for the audio spectrum texture (may be null)
};

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
    ParticleStats particles;
    ProceduralStats procedural; // ADR-023; its draws/triangles are also folded into the totals
    PostStats post;
    std::uint32_t transientTextures = 0;
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
    glm::vec4 cameraRight; // xyz = camera right axis (world), for billboards
    glm::vec4 cameraUp;    // xyz = camera up axis (world)
    glm::vec4 params;
    glm::vec4 envParams;
    glm::vec4 skyParams;
    glm::vec4 fogParams; // rgb = fog colour, w = density (0 = off)
    LightUniform lights[kMaxLights];
};
static_assert(sizeof(FrameUniforms) == 128 + 112 + 512);

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
    float operatorId;
    float vignette;
    float grain;
    float size[2];
    float seed;
    float pad;
};
static_assert(sizeof(TonemapUniforms) == 32);

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
                                      const FrameTime& time, const gpu::TargetView& target,
                                      const ShaderFrameInputs* shaderInputs = nullptr);

    // Full frame into a fresh RGBA8 texture, submitted and read back. For tests and offline use.
    [[nodiscard]] Result<gpu::Image8> renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                    std::uint32_t width, std::uint32_t height,
                                                    const ShaderFrameInputs* shaderInputs = nullptr);
    // Full frame, then the scene-linear HDR image the tonemap pass read (RGBA16F after the post
    // chain, before tone mapping), submitted and read back as floats. For EXR output and tests.
    [[nodiscard]] Result<gpu::ImageF> renderToImageFloat(const scene::Scene& scene, const FrameTime& time,
                                                         std::uint32_t width, std::uint32_t height,
                                                         const ShaderFrameInputs* shaderInputs = nullptr);
    // The RGBA16F texture the tonemap pass sampled in the last render() (the HDR target or the
    // last post output; CopySrc usage), for asynchronous HDR readback. Null before the first frame.
    [[nodiscard]] const wgpu::Texture& hdrOutputTexture() const { return hdrOutput_; }

    // Hot reload of the engine's own WGSL files: rebuilds every pipeline whose shader compiles,
    // keeps the previous pipeline for any that fails, and returns the first error.
    [[nodiscard]] Result<void> reloadEngineShaders();
    [[nodiscard]] std::uint32_t engineShaderReloads() const { return engineReloads_; }
    [[nodiscard]] ShaderStack& shaderStack() { return *shaderStack_; }
    [[nodiscard]] ParticleRenderer& particles() { return *particles_; }
    [[nodiscard]] ProceduralRenderer& procedurals() { return *procedurals_; }
    [[nodiscard]] FieldUniforms& fields() { return *fields_; } // the per-frame field block (ADR-025)
    [[nodiscard]] PostProcessor& post() { return *postProcessor_; }
    [[nodiscard]] gpu::TransientPool& transientPool() { return *pool_; }

    // Installs image-based lighting (normally driven automatically from scene.environment).
    void setIbl(const IblResources& ibl);
    [[nodiscard]] EnvironmentProcessor& environment() { return *environment_; }
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

    // Renders into a fresh RGBA8 texture with CopySrc usage, submits and waits (the sync path).
    Result<wgpu::Texture> renderSubmitted(const scene::Scene& scene, const FrameTime& time, std::uint32_t width,
                                          std::uint32_t height, const ShaderFrameInputs* shaderInputs);
    Result<void> createPipelines();
    Result<wgpu::RenderPipeline> createLitPipeline(const wgpu::ShaderModule& module, LitVariant variant);
    Result<wgpu::RenderPipeline> createGridPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createSkyboxPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> tonemapPipelineFor(wgpu::TextureFormat format);
    Result<wgpu::RenderPipeline> finishPipeline(const wgpu::RenderPipelineDescriptor& desc, const char* label);
    void uploadMeshes(const scene::Scene& scene);
    void uploadTextures(const scene::Scene& scene);
    void updateEnvironment(const scene::Scene& scene);
    void updateSpectrum(const analysis::AnalysisFrame* frame);
    Result<void> ensurePostTargets(std::uint32_t width, std::uint32_t height);
    wgpu::BindGroup tonemapBindGroupFor(const wgpu::TextureView& view);
    void ensureTonemapBindGroup();
    void rebuildIblBindGroup();
    const wgpu::BindGroup& materialBindGroup(const scene::Material& material);
    const gpu::GpuTexture& textureOrDefault(const scene::TextureRef& ref, const gpu::GpuTexture& fallback) const;

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::unique_ptr<gpu::GpuTimer> timer_;
    std::unique_ptr<gpu::SamplerCache> samplers_;
    std::unique_ptr<EnvironmentProcessor> environment_;
    std::unique_ptr<ShaderStack> shaderStack_;
    std::unique_ptr<FieldUniforms> fields_;
    std::unique_ptr<ParticleRenderer> particles_;
    std::unique_ptr<ProceduralRenderer> procedurals_;
    std::unique_ptr<PostProcessor> postProcessor_;
    std::unique_ptr<gpu::TransientPool> pool_;
    glm::mat4 prevViewProj_{1.0f};
    bool havePrevViewProj_ = false;
    gpu::RenderTarget post_[2];      // ping-pong HDR colour targets for post layers
    gpu::GpuTexture spectrum_;       // binCount x 1 RGBA16F audio spectrum for user shaders
    std::size_t spectrumBins_ = 0;
    std::vector<std::uint16_t> spectrumStaging_;
    std::uint32_t engineReloads_ = 0;
    scene::TextureId environmentTexture_ = scene::kInvalidTexture;
    std::uint64_t environmentVersion_ = ~0ull;
    bool initialised_ = false;

    gpu::RenderTarget hdr_;
    wgpu::Texture hdrOutput_;
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
    std::unordered_map<WGPUTextureView, wgpu::BindGroup> tonemapGroups_;

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
