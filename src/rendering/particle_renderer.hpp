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

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::gpu {
class Context;
class FrameTimeline;
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
    glm::vec4 cameraPos;    // xyz = eye, w = shutter open seconds (ADR-037, drives the stretch)
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
    glm::vec4 stretch;    // ADR-040: velocityStretch, stretchMax, stretchMin, 0
    glm::vec4 trail;      // history points (0 = off), stride, width, taper
    glm::vec4 trail2;     // tail alpha fraction, tail tint rgb
    glm::vec4 fog;        // volume density, fog height, height falloff, absorption
    glm::vec4 fog2;       // volume max distance, fog coupling, glow strength, 0
    glm::vec4 leaf;       // ADR-370: shape (0 round, 1 leaf), tumble rate, aspect, two-sided depth
    // ADR-370: ADR-055's packed wind field, so `cs_simulate` can sample the same air the tree bends
    // in without the particle pipelines growing a frame bind group they have never had.
    glm::vec4 windDir;
    glm::vec4 windRegion;
    glm::vec4 windGust;
    glm::vec4 windTurb;
    glm::vec4 windMix;    // x = windInfluence, yzw unused
    glm::uvec4 curves;    // size / colour / opacity key counts, glow slot
    glm::uvec4 counts; // emitCount, capacity, blend, scan blocks
    glm::uvec4 fieldInfo; // x = field force count (ADR-025), y = spline emitter slot + 1 (0 = none, ADR-026)
    glm::vec4 fieldForces[scene::kMaxFieldForces * 2]; // per force: (mode, slot, strength, mix), (axis.xyz, 0)
    glm::vec4 sizeKeys[scene::kMaxCurveKeys];    // (t, value, 0, 0)
    glm::vec4 opacityKeys[scene::kMaxCurveKeys]; // (t, value, 0, 0)
    glm::vec4 colorKeys[scene::kMaxCurveKeys];   // (t, r, g, b)
};
static_assert(sizeof(ParticleUniforms) == 128 + 16 * 28 + 32 * scene::kMaxFieldForces + 48 * scene::kMaxCurveKeys);

// Everything the draw needs that is not a per-system parameter (ADR-040). Set once per frame.
struct ParticleFrameContext {
    // ADR-370: the frame's wind, packed by wind::packWind, exactly as FrameUniforms carries it.
    wind::WindUniforms wind{};
    glm::mat4 prevViewProj{1.0f};   // ADR-035, for the velocity target
    glm::vec3 cameraPosition{0.0f}; // ribbons face it; the fog coupling marches from it
    float shutterSeconds = 0.0f;    // shutterAngle / 360 * frame duration (ADR-037)
    // The volumetric atmosphere the particles sit in (scene::Environment). Density 0 = no fog
    // and the coupling costs nothing.
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogHeightFalloff = 0.0f;
    float fogAbsorption = 1.0f;
    float fogMaxDistance = 200.0f;
    // The ADR-035 R32F linear-depth target, resolved by the depth prepass. Null disables the fog
    // coupling for the frame (there is nothing to say what depth the volume composite marched to),
    // and a 1x1 placeholder is bound so the bind group stays valid.
    wgpu::TextureView linearDepth;
};

struct ParticleStats {
    std::uint32_t systems = 0;
    std::uint32_t capacity = 0;         // sum of pools
    std::uint32_t emittedThisFrame = 0; // requested spawns (the GPU clamps to the free slots)
    std::uint32_t ribbonSystems = 0;    // systems drawing trails this frame (ADR-040)
    std::uint32_t glowSystems = 0;      // systems injecting light into the volume (ADR-040)
    std::uint64_t trailBytes = 0;       // history rings currently allocated
    std::uint32_t dispatches = 0;       // compute passes encoded this frame (one per enabled system)
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
    void setPreviousViewProjection(const glm::mat4& prevViewProj) { frame_.prevViewProj = prevViewProj; }
    void setFrameContext(const ParticleFrameContext& context);
    void update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                const glm::mat4& view, const glm::mat4& proj, const FieldUniforms* fields = nullptr,
                const SplineBuffers* splines = nullptr);
    // Draws every enabled system into the current render pass (additive/alpha, depth test only).
    void draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene);
    // Resets all pools (kills every particle); used on seek/offline restarts.
    void resetAll();
    // Pumps the compute-pass timer after the frame's command buffer was submitted (update() also
    // does this at the start of the next frame).
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed.
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    [[nodiscard]] const ParticleStats& stats() const { return stats_; }
    [[nodiscard]] bool initialised() const { return initialised_; }

    // ADR-040: the emissive aggregates volume.wgsl reads, kMaxGlowSystems entries of
    // (centre.xyz, spread radius) and (colour.rgb, power). Valid from init() onwards; slots past
    // glowSystems() are zero, so the volume march can iterate the whole array safely.
    static constexpr std::uint32_t kMaxGlowSystems = 8;
    static constexpr std::uint64_t kGlowBufferSize = kMaxGlowSystems * 32;
    [[nodiscard]] const wgpu::Buffer& glowBuffer() const { return glowBuffer_; }
    [[nodiscard]] std::uint32_t glowSystems() const { return stats_.glowSystems; }

    // Blocking readback of a pool's counters (tests and tools only; waits for the GPU).
    [[nodiscard]] Result<ParticleCounts> readCounts(std::size_t systemIndex);
    // The indirect draw arguments the last compaction wrote: {billboard, ribbon} x
    // {vertexCount, instanceCount, firstVertex, firstInstance} (tests and tools only).
    [[nodiscard]] Result<std::array<std::uint32_t, 8>> readDrawArgs(std::size_t systemIndex);
    // The first `points` history entries of the trail ring, as raw vec4s (tests and tools only).
    [[nodiscard]] Result<std::vector<float>> readTrailHistory(std::size_t systemIndex, std::uint32_t points);
    // The emissive aggregates the volume march reads: per system slot, (centre.xyz, radius) then
    // (colour.rgb, power) (tests and tools only).
    [[nodiscard]] Result<std::vector<float>> readGlow();

private:
    struct Pool {
        std::uint32_t capacity = 0;
        std::uint32_t blocks = 0;        // scan blocks: ceil(capacity / kScanBlock)
        std::uint32_t historyPoints = 0; // ADR-040 trail ring length per particle (0 = off)
        wgpu::Buffer uniforms;
        wgpu::Buffer particles;
        wgpu::Buffer deadList;
        wgpu::Buffer counters;   // dead/alive counts, then the billboard and ribbon DrawArgs
        wgpu::Buffer aliveList;
        wgpu::Buffer scratch;    // per-slot alive flags, then one sum per scan block
        wgpu::Buffer history;    // ADR-040: capacity * historyPoints vec4 (a 1-entry stub when off)
        wgpu::Buffer glowScratch; // per scan block: two vec4 partial sums, then the aggregate
        wgpu::BindGroup computeGroup;
        wgpu::BindGroup renderGroup;
        wgpu::TextureView renderDepthView; // the linear-depth view renderGroup was built against
        double emitCarry = 0.0;
        bool needsReset = true;
        bool trailWarned = false; // the memory budget refusal is logged once per pool
    };

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    void ensurePool(std::size_t index, std::uint32_t capacity, std::uint32_t historyPoints);
    void ensureRenderGroup(Pool& pool);
    void resetPool(Pool& pool);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    wgpu::Buffer fieldBlock_;
    wgpu::Buffer splineTable_;
    wgpu::Buffer gridTable_;
    gpu::FrameTimeline* timeline_ = nullptr;
    double lastSimulateMs_ = -1.0;
    bool passThisFrame_ = false;
    bool initialised_ = false;
    ParticleFrameContext frame_;
    wgpu::Texture depthPlaceholder_;
    wgpu::TextureView depthPlaceholderView_;
    wgpu::Buffer glowBuffer_;
    wgpu::BindGroupLayout computeLayout_;
    wgpu::BindGroupLayout renderLayout_;
    wgpu::PipelineLayout computePipelineLayout_;
    wgpu::PipelineLayout renderPipelineLayout_;
    wgpu::ComputePipeline emitPipeline_;
    wgpu::ComputePipeline simulatePipeline_;
    wgpu::ComputePipeline scanReducePipeline_;
    wgpu::ComputePipeline scanTopPipeline_;
    wgpu::ComputePipeline scanScatterPipeline_;
    wgpu::ComputePipeline glowReducePipeline_;
    wgpu::ComputePipeline glowTopPipeline_;
    wgpu::RenderPipeline additivePipeline_;
    wgpu::RenderPipeline alphaPipeline_;
    wgpu::RenderPipeline ribbonAdditivePipeline_;
    wgpu::RenderPipeline ribbonAlphaPipeline_;
    std::vector<Pool> pools_;
    const scene::Scene* scene_ = nullptr;
    ParticleStats stats_;

    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kDepthFormat = wgpu::TextureFormat::Depth24Plus;
};

} // namespace avgen::rendering
