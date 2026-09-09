#pragma once

// GPU particle systems (ADR-015): compute emit/simulate/compaction passes and an indirect draw
// per scene::ParticleSystem. Pools are allocated per (system index, capacity); everything else
// is driven by the per-frame uniforms, so parameters can change every frame without
// reallocation. Slot assignment and draw order are deterministic (stable prefix-sum compaction,
// no atomics): the same frame sequence produces bit-identical buffers on the same GPU.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::gpu {
class Context;
class GpuTimer;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;
class SplineBuffers;

struct ParticleUniforms {
    glm::mat4 viewProj;
    glm::mat4 prevViewProj; // ADR-035: last frame's, so particles write the velocity target
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
    glm::uvec4 counts; // emitCount, capacity, blend, scan blocks
    glm::uvec4 fieldInfo; // x = field force count (ADR-025), y = spline emitter slot + 1 (0 = none, ADR-026)
    glm::vec4 fieldForces[scene::kMaxFieldForces * 2]; // per force: (mode, slot, strength, mix), (axis.xyz, 0)
};
static_assert(sizeof(ParticleUniforms) == 128 + 16 * 15 + 32 * scene::kMaxFieldForces);

struct ParticleStats {
    std::uint32_t systems = 0;
    std::uint32_t capacity = 0;         // sum of pools
    std::uint32_t emittedThisFrame = 0; // requested spawns (the GPU clamps to the free slots)
    double simulateMs = -1.0;           // GPU time of the compute passes (emit..compaction) of the last measured frame; -1 = none / unavailable
};

// GPU-side pool occupancy after the last update(); alive + dead == capacity.
struct ParticleCounts {
    std::uint32_t alive = 0;
    std::uint32_t dead = 0;
};

class ParticleRenderer {
public:
    ParticleRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;
    // `fieldBlock` is the FieldUniforms buffer, `splineTable` the SplineBuffers buffer and
    // `gridTable` the simulated-grid table fields.wgsl reads at group 0 binding 15 (ADR-032),
    // all bound to the compute passes (zeroed private ones are created when null).
    [[nodiscard]] Result<void> init(wgpu::Buffer fieldBlock = nullptr, wgpu::Buffer splineTable = nullptr,
                                    wgpu::Buffer gridTable = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of particles.wgsl (keeps old on failure)

    // Encodes the compute passes for every enabled system. Call before the scene pass.
    // `fields` resolves the systems' field forces to slots (null = no field forces); `splines`
    // resolves Spline emitters (null or unknown name = the emitter falls back to Point).
    // `prevViewProj` is last frame's view-projection (ADR-035); pass the current one on the first
    // frame and particles simply report zero motion.
    void setPreviousViewProjection(const glm::mat4& prevViewProj) { prevViewProj_ = prevViewProj; }
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                const glm::mat4& view, const glm::mat4& proj, const FieldUniforms* fields = nullptr,
                const SplineBuffers* splines = nullptr);
    // Draws every enabled system into the current render pass (additive/alpha, depth test only).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene);
    // Resets all pools (kills every particle); used on seek/offline restarts.
    void resetAll();
    // Pumps the compute-pass timer after the frame's command buffer was submitted (update() also
    // does this at the start of the next frame).
    void collectTimings();

    [[nodiscard]] const ParticleStats& stats() const { return stats_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

    // Blocking readback of a pool's counters (tests and tools only; waits for the GPU).
    [[nodiscard]] Result<ParticleCounts> readCounts(std::size_t systemIndex);

private:
    struct Pool {
        std::uint32_t capacity = 0;
        std::uint32_t blocks = 0; // scan blocks: ceil(capacity / kScanBlock)
        wgpu::Buffer uniforms;
        wgpu::Buffer particles;
        wgpu::Buffer deadList;
        wgpu::Buffer counters;
        wgpu::Buffer aliveList;
        wgpu::Buffer indirect;
        wgpu::Buffer flags;
        wgpu::Buffer blockSums;
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
    wgpu::Buffer fieldBlock_;
    wgpu::Buffer splineTable_;
    wgpu::Buffer gridTable_;
    std::unique_ptr<gpu::GpuTimer> timer_;
    double lastSimulateMs_ = -1.0;
    bool passThisFrame_ = false;
    bool initialised_ = false;
    glm::mat4 prevViewProj_{1.0f};
    wgpu::BindGroupLayout computeLayout_;
    wgpu::BindGroupLayout renderLayout_;
    wgpu::PipelineLayout computePipelineLayout_;
    wgpu::PipelineLayout renderPipelineLayout_;
    wgpu::ComputePipeline emitPipeline_;
    wgpu::ComputePipeline simulatePipeline_;
    wgpu::ComputePipeline scanReducePipeline_;
    wgpu::ComputePipeline scanTopPipeline_;
    wgpu::ComputePipeline scanScatterPipeline_;
    wgpu::RenderPipeline additivePipeline_;
    wgpu::RenderPipeline alphaPipeline_;
    std::vector<Pool> pools_;
    ParticleStats stats_;

    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;
};

} // namespace avgen::rendering
