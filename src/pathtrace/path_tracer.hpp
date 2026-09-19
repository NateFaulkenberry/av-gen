#pragma once

// The CPU path tracer (ADR-344).
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
#include "pathtrace/embree_scene.hpp"
#include "pathtrace/lights.hpp"
#include "pathtrace/snapshot.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
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

    // Capture the albedo and normal feature buffers. Cheap (first hit only) but not free, so it is
    // opt-in; the denoiser needs them and turns this on for itself.
    bool captureFeatures = false;

    // Spec section 59. Off by default because it costs a branch per sample; on, a non-finite or
    // negative radiance is counted and clamped away instead of poisoning the accumulation.
    bool debugCheckNonFinite = false;

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
    std::vector<glm::vec3> albedo;
    std::vector<glm::vec3> normal;   // world space, unit length after resolve

    [[nodiscard]] std::vector<glm::vec3> resolvedRadiance() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedAlbedo() const;
    [[nodiscard]] std::vector<glm::vec3> resolvedNormal() const;

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

    [[nodiscard]] Result<void> render(const Snapshot& snapshot, const TraceSettings& settings,
                                      Framebuffer& out, CancelFn cancel = {});

    [[nodiscard]] const TraceStats& stats() const { return stats_; }

private:
    TraceStats stats_{};
};

// Writes the framebuffer as scene-linear float EXR through the project's existing tinyexr path.
[[nodiscard]] Result<void> writeFramebufferExr(const Framebuffer& fb, const std::filesystem::path& path,
                                               bool half = false);

// Logs the capability report at render startup (spec section 55).
void logCapabilities(const Snapshot& snapshot);

} // namespace avgen::pathtrace
