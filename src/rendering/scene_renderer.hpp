#pragma once

// Draws a scene::Scene with WebGPU (ADR-001). Frame = ordered passes:
//
//   1. compute: simulation, particles, procedural effectors/culling, SDF packing
//   2. cluster build (ADR-033): the 16x8x24 froxel grid of light indices
//   3. shadow depth passes (ADR-034): one per cascade / spot map into the shadow atlas
//   4. background: HDR colour cleared, background user-shader layers
//   5. depth prepass: opaque rasterised geometry and raymarched SDFs into the scene depth
//   6. linear depth: the depth buffer resolved to R32F for AO and contact shadows
//   7. GTAO (ADR-034): half-resolution horizon occlusion + bent normal, temporally reprojected
//   8. scene -> HDR + auxiliary targets (ADR-035): opaque PBR, procedural instances, SDF meshes,
//      the SDF raymarch pass, skybox, additive grid, particles, blended PBR
//   9. volumetric atmosphere -> HDR (ADR-032), debug geometry, post chain, tonemap
//
// The scene pass writes five colour targets at once: HDR radiance, normal + roughness, velocity,
// emission and identifiers (ADR-035). The caller owns the command encoder so it can append passes
// (UI) and submit; renderToImage() wraps that for tests and offline output.

#include "core/error.hpp"
#include "core/time.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/texture.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/ao_renderer.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/light_data.hpp"
#include "rendering/material_programs.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/post_processor.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/sdf_renderer.hpp"
#include "rendering/shader_layer.hpp"
#include "rendering/shadow_renderer.hpp"
#include "rendering/simulation.hpp"
#include "rendering/spline_buffers.hpp"
#include "rendering/volume_renderer.hpp"
#include "rendering/debug_draw.hpp"
#include "scene/scene.hpp"
#include "shaders/shader_layers.hpp"

#include "analysis/analyzer.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
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
    SdfStats sdf;               // ADR-027; its draws/triangles are also folded into the totals
    VolumeStats volume;         // ADR-032; the raymarched atmosphere (steps 0 = off)
    ShadowStats shadows;        // ADR-034; cascades and spot maps rendered this frame
    AoStats ao;                 // ADR-034; ground-truth ambient occlusion
    std::uint32_t clusteredLights = 0; // lights that went through the froxel grid (0 = fallback path)
    SimulationStats simulation; // ADR-032; the simulated grid fields stepped this frame
    PostStats post;
    std::uint32_t transientTextures = 0;
};

constexpr std::uint32_t kMaxLights = 8; // the uniform fallback path (ADR-033); clustered has no such limit

// Which auxiliary target the debug view displays over the frame (ADR-035).
enum class AuxDebugView : std::uint8_t { None, Normal, Roughness, Velocity, Emission, Ids, Occlusion, Depth };
[[nodiscard]] const char* auxDebugViewName(AuxDebugView view);

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
    glm::mat4 prevViewProj;   // ADR-035: last frame's, for velocity and temporal reprojection
    glm::vec4 cameraPos;
    glm::vec4 cameraRight;    // xyz = camera right axis (world), for billboards
    glm::vec4 cameraUp;       // xyz = camera up axis (world)
    glm::vec4 cameraForward;  // xyz = camera view axis (world), w = 0; view depth for the froxel grid
    glm::vec4 params;
    glm::vec4 envParams;
    glm::vec4 skyParams;
    glm::vec4 fogParams;      // rgb = fog colour, w = density (0 = off)
    glm::vec4 audio;          // ADR-030 material inputs: rms, bass, mid, treble
    glm::vec4 audioBands;     // lowMid, highMid, spectral centroid, flux
    glm::vec4 beat;           // beat phase 0..1, pulse (1 - phase), onset strength, bar phase
    glm::vec4 clusterParams;  // xyz = froxel grid dimensions, w = 1 when the clustered path is on
    glm::vec4 clusterDepth;   // x = slice scale, y = slice bias, z = near, w = far
    glm::vec4 lightCounts;    // x = directional lights (always shaded), y = total lights, zw = 0
    glm::vec4 shadowParams;   // x = atlas resolution, y = PCF radius (texels), z = contact steps, w = contact length
    glm::vec4 aoParams;       // x = strength, y = 1 when AO is on, zw = the AO texture size
    glm::vec4 targetSize;     // x = width, y = height, z = 1 / width, w = 1 / height
    LightUniform lights[kMaxLights];
};
static_assert(sizeof(FrameUniforms) == 192 + 272 + 512);

struct ObjectUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::mat4 prevModel; // ADR-035: last frame's model matrix, for the velocity target
    glm::vec4 baseColor;
    glm::vec4 emissive;
    glm::vec4 material;
    glm::vec4 flags;
    glm::vec4 ids; // x = object id (its index in the scene's list; the ADR-030 `objectId` input),
                   // y = material id, z = bloom weight of this object's emission, w = 0
};
static_assert(sizeof(ObjectUniforms) == 272);

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
    [[nodiscard]] SdfRenderer& sdfs() { return *sdfs_; } // ADR-027
    [[nodiscard]] VolumeRenderer& volumes() { return *volumes_; } // ADR-032
    [[nodiscard]] Simulation& simulation() { return *simulation_; } // ADR-032
    [[nodiscard]] FieldUniforms& fields() { return *fields_; } // the per-frame field block (ADR-025)
    [[nodiscard]] MaterialPrograms& materialPrograms() { return *materialPrograms_; } // ADR-030
    [[nodiscard]] SplineBuffers& splines() { return *splines_; } // the spline tables (ADR-026)
    [[nodiscard]] PostProcessor& post() { return *postProcessor_; }
    [[nodiscard]] gpu::TransientPool& transientPool() { return *pool_; }

    // Installs image-based lighting (normally driven automatically from scene.environment).
    void setIbl(const IblResources& ibl);
    [[nodiscard]] EnvironmentProcessor& environment() { return *environment_; }
    [[nodiscard]] const IblResources& ibl() const { return ibl_; }

    [[nodiscard]] const RenderStats& stats() const { return stats_; }

    // ---- lighting and quality (ADR-033/034/035) ----
    // Sample counts, resolutions and history lengths only; the scene and its determinism are the
    // same at every tier. Takes effect on the next render().
    void setQuality(QualityTier tier);
    [[nodiscard]] QualityTier quality() const { return tier_; }
    [[nodiscard]] const QualitySettings& qualitySettings() const { return qualitySettings_; }
    // Overrides individual counts without changing the named tier (tests and benchmarks).
    void setQualitySettings(const QualitySettings& settings) { qualitySettings_ = settings; }
    [[nodiscard]] ShadowRenderer& shadows() { return *shadows_; }     // ADR-034
    [[nodiscard]] AoRenderer& ambientOcclusion() { return *ao_; }     // ADR-034
    // Displays one auxiliary target full-screen instead of the shaded frame (ADR-035).
    void setAuxDebugView(AuxDebugView view) { auxDebugView_ = view; }
    [[nodiscard]] AuxDebugView auxDebugView() const { return auxDebugView_; }
    // The auxiliary targets of the last frame (debug tools and tests).
    [[nodiscard]] const wgpu::TextureView& normalRoughnessView() const { return normalRough_.view; }
    [[nodiscard]] const wgpu::TextureView& velocityView() const { return velocity_.view; }
    [[nodiscard]] const wgpu::TextureView& emissionView() const { return emission_.view; }
    [[nodiscard]] const wgpu::TextureView& identifierView() const { return ids_.view; }
    [[nodiscard]] const wgpu::TextureView& linearDepthView() const { return linearDepth_.view; }
    [[nodiscard]] const wgpu::Texture& normalRoughnessTexture() const { return normalRough_.texture; }
    [[nodiscard]] const wgpu::Texture& velocityTexture() const { return velocity_.texture; }
    [[nodiscard]] const wgpu::Texture& emissionTexture() const { return emission_.texture; }
    [[nodiscard]] const wgpu::Texture& identifierTexture() const { return ids_.texture; }
    [[nodiscard]] const wgpu::Texture& linearDepthTexture() const { return linearDepth_.texture; }
    // The packed lights and the froxel grid of the last frame (tests and tools). The cluster buffer
    // holds `kClusterCount` counts followed by `kMaxLightsPerCluster` indices per cluster.
    [[nodiscard]] const wgpu::Buffer& lightBuffer() const { return lightBuffer_; }
    [[nodiscard]] const wgpu::Buffer& clusterBuffer() const { return clusterBuffer_; }

    // Debug drawing (ADR-031): the host fills this before render() and the geometry is drawn over
    // the lit scene. Empty by default, so a frame with no debug geometry is encoded as before.
    [[nodiscard]] DebugDraw& debugDraw() { return *debug_; }
    void setDebugDepthTest(bool on) { debugDepthTest_ = on; }
    [[nodiscard]] gpu::GpuTimer& timer() { return *timer_; }
    [[nodiscard]] const gpu::RenderTarget& hdrTarget() const { return hdr_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

    static constexpr std::uint32_t kMaxObjects = 256;
    static constexpr std::uint32_t kObjectStride = 512; // dynamic-offset alignment (256) x 2
    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;
    // Auxiliary targets (ADR-035). Normal + roughness packs an octahedral normal in rg, the
    // roughness in b and a flag byte in a; identifiers pack the object id in the low 16 bits and
    // the material id in the high 16.
    static constexpr wgpu::TextureFormat kNormalFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kVelocityFormat = wgpu::TextureFormat::RG16Float;
    static constexpr wgpu::TextureFormat kEmissionFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kIdFormat = wgpu::TextureFormat::R32Uint;
    static constexpr wgpu::TextureFormat kLinearDepthFormat = wgpu::TextureFormat::R32Float;
    static constexpr std::uint32_t kAuxTargetCount = 4;

private:
    struct GpuMesh {
        wgpu::Buffer vertices;
        wgpu::Buffer indices;
        std::uint32_t indexCount = 0;
    };
    enum class LitVariant : std::uint8_t { OpaqueCull, OpaqueNoCull, Blend };
    // One auxiliary colour target: its texture, its view and nothing else.
    struct AuxTarget {
        wgpu::Texture texture;
        wgpu::TextureView view;
        [[nodiscard]] bool valid() const { return texture != nullptr; }
    };

    // Renders into a fresh RGBA8 texture with CopySrc usage, submits and waits (the sync path).
    Result<wgpu::Texture> renderSubmitted(const scene::Scene& scene, const FrameTime& time, std::uint32_t width,
                                          std::uint32_t height, const ShaderFrameInputs* shaderInputs);
    Result<void> createPipelines();
    Result<void> createAuxTargets(std::uint32_t width, std::uint32_t height);
    Result<void> createLightResources();
    Result<wgpu::RenderPipeline> createDepthOnlyPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createLinearDepthPipeline(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> createAuxDebugPipeline(const wgpu::ShaderModule& module);
    void rebuildFrameBindGroups();
    // Packs this frame's lights, uploads them and encodes the cluster build.
    void updateLights(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const glm::mat4& view,
                      float aspect, FrameUniforms& frame);
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
    std::unique_ptr<gpu::GpuTimer> shadowTimer_; // the depth-only shadow passes on their own
    std::unique_ptr<gpu::SamplerCache> samplers_;
    std::unique_ptr<EnvironmentProcessor> environment_;
    std::unique_ptr<ShaderStack> shaderStack_;
    std::unique_ptr<FieldUniforms> fields_;
    std::unique_ptr<MaterialPrograms> materialPrograms_;
    std::unique_ptr<SplineBuffers> splines_;
    std::unique_ptr<ParticleRenderer> particles_;
    std::unique_ptr<ProceduralRenderer> procedurals_;
    std::unique_ptr<SdfRenderer> sdfs_;
    std::unique_ptr<VolumeRenderer> volumes_;
    std::unique_ptr<DebugDraw> debug_;
    bool debugDepthTest_ = true;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<ShadowRenderer> shadows_; // ADR-034
    std::unique_ptr<AoRenderer> ao_;          // ADR-034
    std::unique_ptr<PostProcessor> postProcessor_;
    std::unique_ptr<gpu::TransientPool> pool_;
    glm::mat4 prevViewProj_{1.0f};
    bool havePrevViewProj_ = false;
    QualityTier tier_ = QualityTier::Realtime;
    QualitySettings qualitySettings_ = QualitySettings::forTier(QualityTier::Realtime);
    AuxDebugView auxDebugView_ = AuxDebugView::None;
    gpu::RenderTarget post_[2];      // ping-pong HDR colour targets for post layers
    gpu::GpuTexture spectrum_;       // binCount x 1 RGBA16F audio spectrum for user shaders
    std::size_t spectrumBins_ = 0;
    std::vector<std::uint16_t> spectrumStaging_;
    std::uint32_t engineReloads_ = 0;
    scene::TextureId environmentTexture_ = scene::kInvalidTexture;
    std::uint64_t environmentVersion_ = ~0ull;
    bool initialised_ = false;

    gpu::RenderTarget hdr_;
    AuxTarget normalRough_;
    AuxTarget velocity_;
    AuxTarget emission_;
    AuxTarget ids_;
    AuxTarget linearDepth_;
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
    wgpu::RenderPipeline depthOnlyPipeline_;   // entities and meshed SDFs, depth prepass and shadows
    wgpu::RenderPipeline linearDepthPipeline_; // Depth24Plus -> R32Float view-space distance
    wgpu::RenderPipeline auxDebugPipeline_;
    wgpu::BindGroupLayout auxDebugLayout_;
    wgpu::BindGroup auxDebugGroup_;
    wgpu::Buffer auxDebugUniforms_;
    wgpu::ShaderModule pbrModule_;
    wgpu::ShaderModule tonemapModule_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> tonemapPipelines_;

    wgpu::Buffer frameUniforms_;
    wgpu::Buffer lightBuffer_;        // group 0 binding 1: the packed scene lights
    wgpu::Buffer clusterBuffer_;      // group 0 binding 2: froxel counts then index lists
    wgpu::Buffer clusterParams_;      // the cluster build pass's own uniforms
    wgpu::BindGroupLayout clusterLayout_;
    wgpu::ComputePipeline clusterPipeline_;
    wgpu::BindGroup clusterBindGroup_;
    wgpu::ShaderModule clusterModule_;
    gpu::GpuTexture linearDepthDefault_; // 1x1 stand-in before the first frame's prepass
    gpu::GpuTexture ltc1_;            // group 0 bindings 8/9: the linearly-transformed-cone table
    gpu::GpuTexture ltc2_;
    wgpu::Sampler ltcSampler_;
    std::vector<GpuLight> lightStaging_;
    std::vector<std::uint32_t> clusterStaging_;
    std::vector<const scene::PunctualLight*> lightOrder_;
    wgpu::Buffer objectUniforms_;
    wgpu::Buffer tonemapUniforms_;
    wgpu::BindGroup frameBindGroup_;
    // The same group with the shadow atlas and the AO target replaced by placeholders, for the
    // passes that write them (a pass may not sample what it renders into).
    wgpu::BindGroup frameBindGroupAux_;
    std::array<wgpu::BindGroup, kMaxShadowViews> shadowFrameGroups_{};
    wgpu::BindGroupLayout linearDepthLayout_;
    wgpu::BindGroup linearDepthGroup_;
    wgpu::TextureView linearDepthBoundView_;
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
    // Previous-frame model matrices by entity name, so velocity survives reordering (ADR-035).
    std::unordered_map<std::string, glm::mat4> prevModels_;
    std::unordered_map<std::string, glm::mat4> prevModelsNext_;
    RenderStats stats_;
};

} // namespace avgen::rendering
