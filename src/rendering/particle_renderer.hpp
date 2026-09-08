#pragma once

// GPU particle systems (ADR-015): compute emit/simulate passes and an indirect draw per
// scene::ParticleSystem. Pools are allocated per (system index, capacity); everything else is
// driven by the per-frame uniforms, so parameters can change every frame without reallocation.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct ParticleUniforms {
    glm::mat4 viewProj;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::vec4 emitterPos;
    glm::vec4 extent;
    glm::vec4 direction;
    glm::vec4 speedLife;
    glm::vec4 gravity;
    glm::vec4 turb;
    glm::vec4 attractor;
    glm::vec4 attractor2;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    glm::vec4 sim;
    glm::uvec4 counts;
};
static_assert(sizeof(ParticleUniforms) == 64 + 16 * 14);

struct ParticleStats {
    std::uint32_t systems = 0;
    std::uint32_t capacity = 0;      // sum of pools
    std::uint32_t emittedThisFrame = 0;
};

class ParticleRenderer {
public:
    ParticleRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload(); // hot reload of particles.wgsl (keeps old on failure)

    // Encodes the compute passes for every enabled system. Call before the scene pass.
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                const glm::mat4& view, const glm::mat4& proj);
    // Draws every enabled system into the current render pass (additive/alpha, depth test only).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene);
    // Resets all pools (kills every particle); used on seek/offline restarts.
    void resetAll();

    [[nodiscard]] const ParticleStats& stats() const { return stats_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

private:
    struct Pool {
        std::uint32_t capacity = 0;
        wgpu::Buffer uniforms;
        wgpu::Buffer particles;
        wgpu::Buffer deadList;
        wgpu::Buffer counters;
        wgpu::Buffer aliveList;
        wgpu::Buffer indirect;
        wgpu::BindGroup computeGroup;
        wgpu::BindGroup renderGroup;
        double emitCarry = 0.0;
        bool needsReset = true;
    };

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    void ensurePool(std::size_t index, std::uint32_t capacity);
    void resetPool(Pool& pool);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    bool initialised_ = false;
    wgpu::BindGroupLayout computeLayout_;
    wgpu::BindGroupLayout renderLayout_;
    wgpu::PipelineLayout computePipelineLayout_;
    wgpu::PipelineLayout renderPipelineLayout_;
    wgpu::ComputePipeline resetPipeline_;
    wgpu::ComputePipeline emitPipeline_;
    wgpu::ComputePipeline simulatePipeline_;
    wgpu::RenderPipeline additivePipeline_;
    wgpu::RenderPipeline alphaPipeline_;
    std::vector<Pool> pools_;
    ParticleStats stats_;

    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;
};

} // namespace avgen::rendering
