#pragma once

// The CPU path tracer (ADR-351).
//
// Phase 1 scope, deliberately small and deliberately correct before it is fast (spec section 76):
// primary rays, a Lambertian BSDF, direct lighting from the scene's lights with shadow rays, the
// analytic sky as the environment and the miss colour, progressive accumulation, and a linear HDR
// framebuffer. No importance sampling, no MIS, no Russian roulette, no indirect bounces beyond
// `maxDepth`, no textures. Those are phases 2 and 3 and each arrives with its own tests.
//
// The output is scene-linear radiance and stops there (spec section 32, section 88): the tracer
// never tone maps and never reaches into the realtime post chain. The colour pipeline is downstream
// of the EXR.

#include "core/error.hpp"
#include "pathtrace/albedo_probe.hpp"
#include "pathtrace/embree_scene.hpp"
#include "pathtrace/lights.hpp"
#include "pathtrace/snapshot.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace avgen::pathtrace {

struct TraceSettings {
    std::uint32_t width = 640;
    std::uint32_t height = 400;
    std::uint32_t samplesPerPixel = 16;

    // Number of surface interactions. 1 = direct lighting only.
    std::uint32_t maxDepth = 4;

    // ---- Phase 3 ---------------------------------------------------------------------------
    //
    // Which estimators contribute. Both on is the normal path; the single-strategy modes exist so a
    // test can assert that light sampling alone, BSDF sampling alone and the MIS combination all
    // converge to the SAME answer. That is the arm that catches MIS weights which do not sum to 1 --
    // a bug that leaves the combined image looking plausible while each strategy is separately wrong.
    enum class Strategy { Mis, LightOnly, BsdfOnly };
    Strategy strategy = Strategy::Mis;

    // Russian roulette start depth. Paths shorter than this are never terminated, so the cheap and
    // important first bounces are always taken. 0 disables roulette entirely.
    std::uint32_t russianRouletteDepth = 3;

    // Deliberately-wrong roulette compensation, for a control arm ONLY. At 1.0 the estimator is
    // unbiased; at anything else it is not, and the energy test must notice. Never set in production.
    float russianRouletteCompensation = 1.0f;

    // Determinism (spec section 27): the image is a pure function of the snapshot and these.
    std::uint64_t seed = 0x853c49e6748fea9bULL;

    // 0 = std::thread::hardware_concurrency(). These are AV Gen's threads, not Embree's.
    unsigned threads = 0;

    // Samples accumulated per pass (spec section 30). The image is rendered in batches so progress
    // is a real count of completed samples rather than an interpolation, and so cancellation lands
    // between batches rather than being checked never. Determinism is unaffected: a sample's seed
    // is (seed, pixel, sampleIndex), so where the batch boundaries fall cannot change its value --
    // a test asserts the same image at one batch and at many.
    std::uint32_t samplesPerBatch = 8;

    // Capture the albedo and normal feature buffers. Cheap (first hit only) but not free, so it is
    // opt-in; the denoiser needs them and turns this on for itself.
    bool captureFeatures = false;

    // Spec section 59. Off by default because it costs a branch per sample; on, a non-finite or
    // negative radiance is counted and clamped away instead of poisoning the accumulation.
    bool debugCheckNonFinite = false;

    // ADR-352's instrument. Diagnostic ONLY: it must never change a pixel, and a bit-identity test
    // is what holds that claim up rather than the comment. Off by default.
    //
    // It is a runtime setting rather than a compile flag deliberately. A compile flag would make
    // the diagnostic unavailable in exactly the build somebody is debugging, and the cost when off
    // is a single predictable branch per shading event -- not worth a second binary.
    AlbedoProbeSettings albedoProbe;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] unsigned resolvedThreads() const;
};

// Scene-linear HDR. Never display-referred, never 8-bit (spec section 32).
struct Framebuffer {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<glm::vec3> radiance;  // accumulated sum, NOT yet divided by sampleCount
    std::uint32_t sampleCount = 0;

    // Feature buffers from the FIRST hit along each path (spec sections 51, 61). They are what lets
    // a denoiser keep an edge it would otherwise smooth away, and they are the first two AOVs.
    // Accumulated and averaged like radiance so they carry the same anti-aliasing.
    // The vocabulary is `app::RenderSettings::aovNames()`'s -- normal, emission, depth, id -- plus
    // `albedo`, which the realtime renderer has no equivalent of and the denoiser requires. It
    // EXTENDS that list rather than inventing a rival one (spec section 31). `velocity` and
    // `shadow` are the two realtime AOVs with no counterpart here yet; see the overview doc.
    std::vector<glm::vec3> albedo;
    std::vector<glm::vec3> normal;    // world space, unit length after resolve
    std::vector<glm::vec3> emission;  // linear radiance emitted by the first surface hit
    std::vector<float> depth;         // VIEW-SPACE metres along the camera's forward axis
    std::vector<float> objectId;      // packPickId(Entity, index), float-encoded; -1 for a miss
    // The worst directional albedo the probe saw anywhere along this pixel's paths. Points straight
    // at the offending REGION of an image, which an aggregate table cannot. Empty unless the probe
    // ran; 0 where nothing was measured.
    std::vector<float> worstAlbedo;
    // Screen-space motion in PIXELS: where this surface point is now, minus where it was one frame
    // earlier, both projected with their own frame's camera. Empty unless the snapshot carried a
    // previous frame. Stored as vec2 packed into a vec3 with z unused, so the EXR writer's planar
    // deinterleave can treat it like the others.
    std::vector<glm::vec3> motion;

    [[nodiscard]] std::vector<glm::vec3> resolvedRadiance() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedAlbedo() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedNormal() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedEmission() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedMotion() const;
    // Depth and id are NOT averaged across samples: a mean of two depths at a silhouette is a
    // distance to nothing, and a mean of two ids is a third object. They take the first sample.
    [[nodiscard]] const std::vector<float>& rawDepth() const { return depth; }
    [[nodiscard]] const std::vector<float>& rawObjectId() const { return objectId; }
    // ADR-372: empty unless `--pt-probe` ran, and written to the AOV EXR as `worstAlbedo.X` when it
    // is not. It was filled and read by nothing, which reads as a capability and is worse than an
    // absent field -- an aggregate table says a material gains energy, this says *where*.
    [[nodiscard]] const std::vector<float>& rawWorstAlbedo() const { return worstAlbedo; }

    void resize(std::uint32_t w, std::uint32_t h);
    // Mean radiance per pixel. Empty if no samples have landed.
    [[nodiscard]] std::vector<float> resolveRgba() const;
    [[nodiscard]] glm::vec3 pixel(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] bool isBlack() const;
    [[nodiscard]] double meanLuminance() const;
};

struct TraceStats {
    std::uint64_t primaryRays = 0;
    std::uint64_t shadowRays = 0;
    std::uint64_t pathsTerminatedByRoulette = 0;
    std::uint64_t bsdfHitsOnLights = 0;
    std::uint64_t nonFiniteSamples = 0;   // only counted when debugCheckNonFinite is on
    std::uint64_t negativeSamples = 0;
    double buildSeconds = 0.0;
    double renderSeconds = 0.0;
};

class PathTracer {
public:
    // `cancel` is polled between scanline blocks; a cancelled render keeps what it accumulated.
    using CancelFn = std::function<bool()>;
    // Called after each completed batch with (samplesDone, samplesTotal). Never called from more
    // than one thread at a time.
    using ProgressFn = std::function<void(std::uint32_t, std::uint32_t)>;

    [[nodiscard]] Result<void> render(const Snapshot& snapshot, const TraceSettings& settings,
                                      Framebuffer& out, CancelFn cancel = {},
                                      ProgressFn progress = {});

    // Reports which stage a caller-visible build step is in, so a job can distinguish scene
    // preparation from acceleration build rather than lumping them together.
    using StageFn = std::function<void(std::string_view)>;
    void setStageCallback(StageFn fn) { stage_ = std::move(fn); }

    [[nodiscard]] const TraceStats& stats() const { return stats_; }
    [[nodiscard]] const AlbedoProbeReport& albedoProbe() const { return probe_; }

private:
    TraceStats stats_{};
    AlbedoProbeReport probe_{};
    StageFn stage_{};
};

// Writes the framebuffer as scene-linear float EXR through the project's existing tinyexr path.
[[nodiscard]] Result<void> writeFramebufferExr(const Framebuffer& fb, const std::filesystem::path& path,
                                               bool half = false);

// Writes the beauty pass plus every captured AOV into ONE multi-layer EXR (spec section 33).
//
// Colour goes in R/G/B/A. Everything else goes in a NAMED LAYER -- `albedo.R/G/B`, `normal.X/Y/Z` --
// so a colour-managed pipeline downstream cannot mistake a normal for a colour and transform it.
// Storing a normal as R/G/B is the mistake section 33 exists to prevent, and it is invisible until
// somebody grades the file.
[[nodiscard]] Result<void> writeFramebufferAovExr(const Framebuffer& fb,
                                                  const std::filesystem::path& path);

// Logs the capability report at render startup (spec section 55).
void logCapabilities(const Snapshot& snapshot);

} // namespace avgen::pathtrace
