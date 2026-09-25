#pragma once

// GPU particle systems (ADR-015): compute emit/simulate/compaction passes and an indirect draw
// per scene::ParticleSystem. Pools are allocated per (system index, capacity); everything else
// is driven by the per-frame uniforms, so parameters can change every frame without
// reallocation. Slot assignment and draw order are deterministic (stable prefix-sum compaction,
// no atomics): the same frame sequence produces bit-identical buffers on the same GPU.

#include "core/error.hpp"
#include "core/time.hpp"
#include "scene/scatter_anchors.hpp"
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
    // ADR-360: x = FrameTime::frameNonce(), the spawn RNG's key. Keyed to the timeline second
    // rather than to frames-since-render-start, so a live frame, an offline render and a re-render
    // of a section all seed the same spawns at the same second. yzw = 0.
    glm::uvec4 nonce;
    glm::vec4 stretch;    // ADR-040: velocityStretch, stretchMax, stretchMin, 0
    glm::vec4 trail;      // history points (0 = off), stride, width, taper
    glm::vec4 trail2;     // tail alpha fraction, tail tint rgb
    glm::vec4 fog;        // volume density, fog height, height falloff, absorption
    glm::vec4 fog2;       // volume max distance, fog coupling, glow strength, 0
    // ADR-568 (§7): the height layer's shape. Here because this pass estimates its own
    // transmittance through the SAME layer the march integrates (ADR-567), and a reader
    // left on the old model is a third atmosphere in the same frame.
    glm::vec4 fog3;       // fogUpperDensity, fogHeightCurve, fogGroundFollow (ADR-715; 0 without a terrain), horizonDensity (ADR-705)
    // ADR-715: where the terrain height texture (render group binding 12) sits in the world, the
    // same two lanes `FrameUniforms::terrainMap0/1` carry -- so this estimate follows the ground the
    // march and the surface fog follow, rather than being the third atmosphere ADR-567 warns of.
    glm::vec4 terrain0;
    glm::vec4 terrain1;
    glm::vec4 leaf;       // ADR-370: shape (0 round, 1 leaf), tumble rate, aspect, two-sided depth
    // ADR-370: ADR-055's packed wind field, so `cs_simulate` can sample the same air the tree bends
    // in without the particle pipelines growing a frame bind group they have never had.
    glm::vec4 windDir;
    glm::vec4 windRegion;
    glm::vec4 windGust;
    glm::vec4 windTurb;
    glm::vec4 windMix;    // x = windInfluence, yzw unused
    // ---- ADR-520 ----
    glm::vec4 volume;   // volumeFollow.xyz (per-axis 0..1), w = 1 when the box wraps
    glm::vec4 collide;  // response (0 none, 1 kill, 2 bounce, 3 splash), height, restitution, splashSize
    glm::vec4 collide2; // splashLifetime, ringThickness, dragSizeBias, 0
    glm::vec4 pulse;    // rate (Hz, 0 = off), depth, sync, sharpness
    glm::vec4 cluster;  // clusterCount (0 = off), clusterRadius, pauseRate, pauseFraction
    glm::vec4 scatter;  // scatterStrength, HG anisotropy, sizeVariance, sizeSkew
    glm::vec4 sun;      // xyz = unit direction *towards* the key light, w = 1 when it is usable
    glm::vec4 sunColor; // rgb = the key light's colour times its intensity, w = 0
    glm::uvec4 curves;    // size / colour / opacity key counts, glow slot
    glm::uvec4 counts; // emitCount, capacity, blend, scan blocks
    glm::uvec4 fieldInfo; // x = field force count (ADR-025), y = spline emitter slot + 1 (0 = none, ADR-026)
    glm::vec4 fieldForces[scene::kMaxFieldForces * 2]; // per force: (mode, slot, strength, mix), (axis.xyz, 0)
    glm::vec4 sizeKeys[scene::kMaxCurveKeys];    // (t, value, 0, 0)
    glm::vec4 opacityKeys[scene::kMaxCurveKeys]; // (t, value, 0, 0)
    glm::vec4 colorKeys[scene::kMaxCurveKeys];   // (t, r, g, b)
    // Scatter-anchored clusters (scene::ScatterAnchor): x = anchors in the table this frame,
    // y = 1 when the system is anchored at all, zw = 0. The table is the crown centres the CPU
    // chose for this camera (scene/scatter_anchors.hpp), xyz = centre, w = 1.
    glm::vec4 anchorInfo;
    glm::vec4 anchors[scene::kMaxScatterAnchors];
};
static_assert(sizeof(ParticleUniforms) == 128 + 16 * 41 + 32 * scene::kMaxFieldForces + 48 * scene::kMaxCurveKeys +
                                              16 * scene::kMaxScatterAnchors);

// Everything the draw needs that is not a per-system parameter (ADR-040). Set once per frame.
struct ParticleFrameContext {
    // ADR-370: the frame's wind, packed by wind::packWind, exactly as FrameUniforms carries it.
    wind::WindUniforms wind{};
    // ADR-360's bounded, opt-in warm-up, in frames. 0 -- the default -- is the behaviour this
    // renderer has always had: a seek, and the head of a render range, start with empty pools and
    // the field blooms in over one particle lifetime. Non-zero runs that many emit/simulate steps
    // at the timeline seconds immediately BEFORE the frame about to be drawn, so the pools hold
    // what a render that had played up to here would hold. Capped at kMaxWarmUpFrames, and
    // deliberately not the default: it costs that many extra compute submits on every seek, and
    // ADR-360 chose not to put that in front of a scrub click.
    std::uint32_t warmUpFrames = 0;
    // ADR-382: the quality tier's multiplier on every system's spawn rate. Capacity is deliberately
    // not scaled -- changing it destroys and recreates the pool (ADR-015), so a tier change would
    // empty every system mid-shot.
    float spawnScale = 1.0f;
    glm::mat4 prevViewProj{1.0f};   // ADR-035, for the velocity target
    glm::vec3 cameraPosition{0.0f}; // ribbons face it; the fog coupling marches from it
    float shutterSeconds = 0.0f;    // shutterAngle / 360 * frame duration (ADR-037)
    // The volumetric atmosphere the particles sit in (scene::Environment). Density 0 = no fog
    // and the coupling costs nothing.
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogHeightFalloff = 0.0f;
    float fogUpperDensity = 0.0f;   // ADR-568
    float fogHeightCurve = 0.0f;    // ADR-568
    // ADR-715: the ground follow, already 0 when there is no terrain, and the terrain's baked
    // height with its placement. A null view binds a placeholder that is never read.
    float fogGroundFollow = 0.0f;
    glm::vec4 terrainMap0{0.0f};
    glm::vec4 terrainMap1{0.0f};
    wgpu::TextureView terrainHeight;
    float horizonDensity = 0.0f;    // ADR-705: the march's distance factor, so this estimate agrees
    float fogAbsorption = 1.0f;
    float fogMaxDistance = 200.0f;
    // The ADR-035 R32F linear-depth target, resolved by the depth prepass. Null disables the fog
    // coupling for the frame (there is nothing to say what depth the volume composite marched to),
    // and a 1x1 placeholder is bound so the bind group stays valid.
    wgpu::TextureView linearDepth;
    // ADR-520: the key light, for the scattering phase function. A unit vector pointing *towards*
    // it, in the sky's own convention (scene::Environment::sunDirection). All-zero switches the
    // phase term off rather than dividing by a zero-length vector, which is why `scatterStrength`
    // alone is not enough to enable it: a scene with no key light has no forward direction to
    // scatter along, and inventing one would make dust that blazes at a light that is not there.
    glm::vec3 sunDirection{0.0f};
    glm::vec3 sunColor{0.0f}; // already multiplied by intensity
};

struct ParticleStats {
    std::uint32_t systems = 0;
    std::uint32_t capacity = 0;         // sum of pools
    std::uint32_t emittedThisFrame = 0; // requested spawns (the GPU clamps to the free slots)
    std::uint32_t ribbonSystems = 0;    // systems drawing trails this frame (ADR-040)
    std::uint32_t glowSystems = 0;      // systems injecting light into the volume (ADR-040)
    std::uint64_t trailBytes = 0;       // history rings currently allocated
    std::uint32_t dispatches = 0;       // compute passes encoded this frame (one per enabled system)
    std::uint32_t anchors = 0;          // scatter anchors in use this frame, over every anchored system
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
    // Resets all pools (kills every particle); used on seek/offline restarts. The next update()
    // then runs the warm-up, if ParticleFrameContext::warmUpFrames asked for one.
    void resetAll();
    // ADR-360: the ceiling on the warm-up. Four seconds at 60 fps, which is longer than any
    // particle lifetime this engine's authoring range allows, so asking for more cannot buy
    // anything and can only stall a seek.
    static constexpr std::uint32_t kMaxWarmUpFrames = 240;
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
        wgpu::TextureView renderTerrainView; // ADR-715: and the terrain height view
        bool needsReset = true;
        // A disabled pool is SKIPPED, not stepped, so its particles do not age while it is
        // off -- they are frozen, not drained. Re-enabling the system somewhere else thaws
        // them at the new position: Glowmere's tractor beam resumed a five-second pool 210 m
        // from where it was hidden, as stray particles in unrelated shots. This remembers the
        // previous frame's state so the transition into disabled can empty the pool once.
        bool wasEnabled = false;
        bool trailWarned = false; // the memory budget refusal is logged once per pool
        // Scatter anchors: every gated instance of the named layers, rebuilt when the objects'
        // structure moves (`anchorVersion`), and the camera's selection from it each frame.
        std::vector<scene::ScatterAnchorPoint> anchorPoints;
        std::uint64_t anchorVersion = 0;
        bool anchorBuilt = false;
    };

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    // ADR-360: encodes and submits `frame_.warmUpFrames` emit/simulate steps ending just before
    // `time`, each in its own command buffer. Its own buffer per step is not an optimisation
    // oversight: the per-system uniforms live in ONE buffer per pool, and queue writes are ordered
    // against submits rather than interleaved inside an encoder, so W steps sharing one encoder
    // would all read the last uniform written.
    void runWarmUp(const scene::Scene& scene, const FrameTime& time, const glm::mat4& view,
                   const glm::mat4& proj, const FieldUniforms* fields, const SplineBuffers* splines);
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
    bool warmUpPending_ = false; // ADR-360: a reset happened; the next update() warms if asked
    bool warming_ = false; // inside runWarmUp: no stats, no timeline marks, no recursion
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
