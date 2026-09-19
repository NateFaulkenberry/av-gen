#pragma once

// Directional-albedo probe (ADR-349, owner's request 2026-09-18).
//
// The glTF metallic-roughness BRDF is kept FAITHFUL to the specification and gains energy at
// grazing angles. The owner chose to keep it that way and asked instead for an instrument that
// watches it: detect and report directional albedo above 1, with the material, the albedo value,
// the view angle and the bounce depth.
//
// ---------------------------------------------------------------------------------------------
// THE HARD CONSTRAINT: this must never modify rendering output.
//
// "Never" here means provably, not intentionally. Two ways a diagnostic changes an image quietly:
//
//   1. **Sampler drift.** If the probe draws from the integrator's `Sampler`, every subsequent
//      shading decision on that path shifts, and the image changes although no shading changed.
//      So the probe carries its OWN `avgen::Rng`, seeded from (probeSeed, pixel, sample, depth),
//      and never touches the integrator's sampler. It also never advances anything the integrator
//      reads.
//   2. **Floating-point drift.** Re-evaluating shading to measure it can reorder arithmetic. The
//      probe therefore calls `evaluateBsdf`, which is a pure function, and writes only into its own
//      accumulator. Nothing it computes flows back into radiance, throughput or path continuation.
//
// The test that matters is not that the probe reports correctly -- it is that the framebuffer is
// BIT-IDENTICAL with the probe on and off, hashed before any tone map or EXR quantisation. That arm
// is primary and it has a control that perturbs the probe and shows the hash move.
// ---------------------------------------------------------------------------------------------
//
// WHAT IS MEASURED, stated plainly because two different things could be:
//
// Directional albedo is an INTEGRAL -- the hemispherical integral of f * cos for a given view
// direction -- not a per-hit quantity. This probe computes the **true integral**, by Monte Carlo
// with its own RNG, at a sampled SUBSET of shading events (one in `hitStride`). It is not the
// cheaper proxy of "throughput grew across a bounce", which measures something related but
// different. The report says which, so its output cannot be misread.

#include "pathtrace/bsdf.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::pathtrace {

inline constexpr std::size_t kProbeMaxDepthBuckets = 16;

struct AlbedoProbeSettings {
    bool enabled = false;
    // One shading event in this many is measured. The integral costs `integralSamples` BSDF
    // evaluations, so measuring every hit would dominate the render; this is why it is optional.
    std::uint32_t hitStride = 97;      // prime, so it does not beat against sample or depth counts
    std::uint32_t integralSamples = 256;
    std::uint64_t seed = 0x5DEECE66DULL; // the probe's own stream, shared with nothing
    float threshold = 1.0f;              // report albedo above this
    // How many standard errors above the threshold an estimate must sit before it counts.
    //
    // This is not conservatism, it is correctness. The albedo is a Monte Carlo estimate, and a
    // noisy estimate compared against a hard threshold produces FALSE POSITIVES: the first version
    // of this probe reported 1674 exceedances on a pure metal, whose true albedo cannot exceed 1.
    // A diagnostic that cries wolf is worse than none, because somebody will spend an afternoon on
    // it. Requiring the estimate to clear the threshold by `sigmaGate` standard errors makes an
    // exceedance a claim about the BRDF rather than about the sample count.
    float sigmaGate = 3.0f;
};

// One material's worst case and its distribution. Aggregate, never a per-hit log: a 1920x1200
// render at 64 spp and depth 8 would otherwise emit hundreds of millions of lines.
struct AlbedoProbeMaterial {
    std::uint32_t materialId = 0;   // the same handle the `id` AOV writes
    std::uint64_t probed = 0;
    std::uint64_t exceedances = 0;
    // The worst CONFIDENT LOWER BOUND, mean - sigmaGate * stdError, not the worst raw estimate.
    //
    // A maximum taken over thousands of Monte Carlo estimates is biased high: the largest of many
    // noisy numbers is an outlier of the estimator, not of the quantity. Reporting the raw maximum
    // gave 2.68 for a material whose true albedo is 1.68 -- a number nobody could act on. A maximum
    // over lower bounds converges to the true worst FROM BELOW, so every figure here is one the
    // BRDF really does reach.
    float worstAlbedo = 0.0f;
    float worstAlbedoEstimate = 0.0f;  // the raw mean at that event, for reference
    float worstViewCos = 0.0f;      // n.v where the worst case happened; near 0 is grazing
    std::uint32_t worstDepth = 0;
    float worstRoughness = 0.0f;
    float worstMetallic = 0.0f;
    glm::vec3 worstBaseColor{0.0f};
    std::array<std::uint64_t, kProbeMaxDepthBuckets> exceedancesByDepth{};
};

struct AlbedoProbeReport {
    std::vector<AlbedoProbeMaterial> materials;
    std::uint64_t hitsProbed = 0;
    std::uint64_t exceedances = 0;
    float worstAlbedo = 0.0f;
    std::uint32_t integralSamples = 0;
    std::uint32_t hitStride = 0;
    float sigmaGate = 0.0f;

    void merge(const AlbedoProbeReport& other);
    [[nodiscard]] bool any() const { return exceedances > 0; }
    [[nodiscard]] std::string format() const;
};

// The hemispherical integral of f * cos for this material and view direction, by Monte Carlo with
// the probe's own generator. Pure: takes no sampler, advances nothing the integrator can see.
struct AlbedoEstimate {
    float mean = 0.0f;
    float stdError = 0.0f;   // standard error OF THE MEAN, so it shrinks as 1/sqrt(samples)
};

[[nodiscard]] AlbedoEstimate estimateDirectionalAlbedo(const SurfaceMaterial& m, const glm::vec3& n,
                                                       const glm::vec3& v, std::uint32_t samples,
                                                       std::uint64_t seed);

// Convenience for tests and callers that only want the value.
[[nodiscard]] float directionalAlbedoAt(const SurfaceMaterial& m, const glm::vec3& n,
                                        const glm::vec3& v, std::uint32_t samples,
                                        std::uint64_t seed);

} // namespace avgen::pathtrace
