#pragma once

// Draws a scene::Scene with WebGPU (ADR-001). Frame = ordered passes: [scene -> HDR target],
// [tonemap -> caller's target]. The caller owns the command encoder so it can append passes
// (UI) and submit; renderToImage() wraps that for tests and offline output.

#include "core/error.hpp"
#include "core/time.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
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

struct RenderStats {
    double gpuFrameMs = -1.0; // -1 when timestamp queries are unavailable
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t entities = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// GPU-side mirrors of the WGSL uniform structs in shaders/common.wgsl and tonemap.wgsl.
struct FrameUniforms {
    glm::mat4 viewProj;
    glm::vec4 cameraPos;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 params;
};
static_assert(sizeof(FrameUniforms) == 128);

struct ObjectUniforms {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 baseColor;
    glm::vec4 emissive;
    glm::vec4 material;
};
static_assert(sizeof(ObjectUniforms) == 176);

struct TonemapUniforms {
    float exposure;
    float pad[3];
};
static_assert(sizeof(TonemapUniforms) == 16);

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

    Result<void> createPipelines();
    Result<wgpu::RenderPipeline> createScenePipeline(const wgpu::ShaderModule& module, bool additive,
                                                     bool depthWrite, wgpu::CullMode cull, const char* label);
    Result<wgpu::RenderPipeline> tonemapPipelineFor(wgpu::TextureFormat format);
    void uploadMeshes(const scene::Scene& scene);
    void ensureTonemapBindGroup();

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::unique_ptr<gpu::GpuTimer> timer_;
    bool initialised_ = false;

    gpu::RenderTarget hdr_;
    wgpu::BindGroupLayout frameLayout_;
    wgpu::BindGroupLayout objectLayout_;
    wgpu::BindGroupLayout tonemapLayout_;
    wgpu::PipelineLayout scenePipelineLayout_;
    wgpu::PipelineLayout tonemapPipelineLayout_;
    wgpu::RenderPipeline litPipeline_;
    wgpu::RenderPipeline gridPipeline_;
    wgpu::ShaderModule tonemapModule_;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> tonemapPipelines_;

    wgpu::Buffer frameUniforms_;
    wgpu::Buffer objectUniforms_;
    wgpu::Buffer tonemapUniforms_;
    wgpu::BindGroup frameBindGroup_;
    wgpu::BindGroup objectBindGroup_;
    wgpu::BindGroup tonemapBindGroup_;
    wgpu::TextureView tonemapBoundView_;

    std::vector<GpuMesh> meshes_;
    std::uint64_t meshVersion_ = ~0ull;
    std::vector<std::uint8_t> objectStaging_;
    RenderStats stats_;
};

} // namespace avgen::rendering
