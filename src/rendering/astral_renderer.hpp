#pragma once

// THE ASTRAL FORGE (ADR-1221): the second Environment on ADR-1200's seam. The prototype's renderer
// (prototypes/astral-forge/, iterations 1-4 + v2), moved into the engine:
//   * 2M particles at a fixed 60 Hz step, bound by coherence to a latent SDF anatomy (shaders/astral/sim.wgsl);
//   * a u32 fixed-point density splat after every step, resolved into a blurred rgba16f volume and an occupancy
//     grid; a latent cache framed on the shot;
//   * a half-resolution march, then a full-resolution refine and shade of the iso-surface as engraved, tempered
//     metal (shaders/astral/surface.wgsl);
//   * flakes splatted in compute with glints, near flakes as shards;
//   * a combine pass into the scene's HDR target and depth buffer -- AV Gen's bloom, tonemap and every output
//     path follow.
// State is a function of time: the simulation steps at 60 Hz whatever the frame rate, every step conducted at its
// own second, so play at 30 or 60 fps simulates the same steps. A seek re-simulates: from the song's start at the
// offline tier (exact: a `--range` lands where play does), over `preroll` seconds otherwise (ADR-360's contract).
// The live quality ladder's `astralTier` (ADR-1222) scales the particles simulated, the god-ray taps and shards.

#include "rendering/environment_renderer.hpp"
#include "scene/astral_forge.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>

namespace avgen::gpu {
class Context;
}

namespace avgen::rendering {

struct AstralStats {
    std::uint32_t particlesActive = 0; // simulated this frame (the tier's prefix)
    std::uint32_t steps = 0;           // simulation steps this frame (catch-up and pre-roll included)
    std::uint32_t tier = 0;            // the astralTier rendered at
    bool reset = false;                // the god was re-initialised this frame (a seek, a new block)
    float coherence = 0.0f;
    int arch = 0;
};

class AstralRenderer final : public EnvironmentRenderer {
public:
    AstralRenderer() = default;
    AstralRenderer(const AstralRenderer&) = delete;
    AstralRenderer& operator=(const AstralRenderer&) = delete;
    ~AstralRenderer() override;

    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat depthFormat);

    [[nodiscard]] std::string_view name() const override { return "astral"; }
    [[nodiscard]] bool wants(const scene::Scene& scene) const override;
    void encode(const EnvironmentFrame& frame) override;
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders) override;

    [[nodiscard]] const AstralStats& stats() const { return stats_; }
    // Forces the next frame to re-simulate (SceneRenderer::resetTemporalHistory: a seek it was told about).
    void markDiscontinuity() { discontinuity_ = true; }

    // Mirrors `Frame` in shaders/astral/common.wgsl (vec4s and mat4s only).
    struct FrameU {
        glm::mat4 viewProj, invViewProj;
        glm::vec4 cam, camFwd, screen;
        glm::vec4 ent0, ent1, ent2, ent3;
        glm::vec4 fold0, fold1;
        glm::vec4 grid0, grid1;
        glm::vec4 sim;
        glm::vec4 bands[8];
        glm::vec4 rig, audio0, audio1, look, flags, entity, misc, fp0, fp1, fp2, ext, it2, cbox, it3, pal0, pal1, pal2,
            pal3, pal4, atm0, lg0;
    };

private:
    static constexpr int kSlots = 4;           // uniform slots: simulation steps in one submit
    static constexpr int kCacheRes = 128;      // CACHE_RES in latent_cache.wgsl
    static constexpr std::uint64_t kShardMax = 262144; // SHARD_MAX in flakes.wgsl
    static constexpr double kSimRate = 60.0;

    struct Config {
        std::uint32_t particles = 0;
        int gridRes = 0;
        float gridSize = 0.0f;
        bool operator==(const Config&) const = default;
    };
    struct Camera {
        glm::mat4 viewProj{1.0f};
        glm::vec3 eye{0.0f}, target{0.0f, 0.0f, -1.0f};
        float fovY = 0.5f;
        std::uint32_t width = 1, height = 1;
    };

    [[nodiscard]] Result<void> createPipelines(gpu::ShaderLibrary& shaders);
    void ensureState(const Config& c);
    void ensureTargets(std::uint32_t width, std::uint32_t height);
    void rebuildGroups();

    [[nodiscard]] astral::State conductAt(const scene::AstralForge& a, double t) const;
    void fillFrame(FrameU& f, const scene::AstralForge& a, const astral::State& s, const astral::State& sPrev,
                   double tPrev, double t, std::uint64_t step, const Camera& cam) const;
    // Steps the simulation to `t` (fixed 60 Hz), splatting the density after each step; records into `enc`, or
    // into submits of its own when the backlog exceeds the uniform slots. Returns the frame's uniform slot.
    int simulateTo(wgpu::CommandEncoder& enc, const scene::AstralForge& a, double t, const Camera& cam);
    void initParticles(const scene::AstralForge& a, double t, const Camera& cam);
    void dispatch1D(wgpu::ComputePassEncoder& cp, std::uint32_t n) const;

    gpu::Context* context_ = nullptr;
    gpu::ShaderLibrary* shaders_ = nullptr;
    bool pipelineFailed_ = false;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth24Plus;

    // pipelines
    wgpu::BindGroupLayout frameLayout_, simLayout_, resLayout_, coarseLayout_, cacheLayout_, flakeLayout_, surfLayout_,
        shardLayout_, postLayout_;
    wgpu::ComputePipeline initPipe_, stepPipe_, splatPipe_, resPipe_, coarsePipe_, cachePipe_, flakePipe_;
    wgpu::RenderPipeline surfPipe_, halfPipe_, shardPipe_, combinePipe_;
    wgpu::Sampler sampler_;

    // simulation state (per Config)
    Config config_{};
    std::array<wgpu::Buffer, kSlots> frameBufs_;
    std::array<wgpu::BindGroup, kSlots> frameGroups_;
    wgpu::Buffer P_, V_, A_, TG_, TD_, grid_;
    std::uint64_t stateBytes_ = 0, gridBytes_ = 0;
    wgpu::Texture densTex_, coarseTex_, cacheTex_, cacheTexCoarse_;
    wgpu::TextureView densView_, coarseView_, cacheView_, cacheViewCoarse_;

    // screen targets (per HDR size)
    std::uint32_t width_ = 0, height_ = 0;
    wgpu::Buffer accum_, shardBuf_, shardArgs_, postBuf_, renderBuf_;
    wgpu::BindGroup renderGroup_;
    gpu::FrameTimeline* timeline_ = nullptr;
    std::uint32_t tier_ = 1;
    std::uint64_t accumBytes_ = 0;
    wgpu::Texture halfTex_, surfColor_, surfDepth_, shardDepth_, dummy2D_;
    wgpu::TextureView halfView_, surfColorView_, surfDepthView_, shardDepthView_, dummy2DView_;
    wgpu::BindGroup simGroup_, resGroup_, coarseGroup_, cacheGroup_, flakeGroup_, surfGroup_, halfGroup_, shardGroup_,
        combineGroup_;

    // time
    bool initialised_ = false;
    bool discontinuity_ = true;
    double simT_ = 0.0;
    std::uint64_t step_ = 0;
    double lastFrameTime_ = -1e9;
    std::uint32_t lastEpoch_ = 0;
    std::uint32_t activeN_ = 0;
    astral::State lastStepState_{};
    astral::State liveFrom_{};   // live mode: the state at the previous frame (sub-steps interpolate from it)
    double liveFromT_ = 0.0;
    AstralStats stats_;
};

} // namespace avgen::rendering
