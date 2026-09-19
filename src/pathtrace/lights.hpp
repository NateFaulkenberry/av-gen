#pragma once

// Light sampling for MIS (ADR-351 Phase 3, spec sections 43-45).
//
// Multiple importance sampling needs each strategy to be able to answer two questions about the
// SAME direction: "if you had sampled this, what would your PDF have been?" as well as "give me a
// sample". Phase 1's `sampleLight` could only do the second, which is why this is a new file rather
// than an edit: a light sampler without a PDF query cannot participate in MIS at all.
//
// All PDFs here are **solid-angle** densities as seen from the shading point, because that is the
// measure the BSDF's PDF uses and MIS can only combine densities in one measure. Converting an area
// density to solid angle is the dist^2 / cos(theta_light) factor, and forgetting it is the classic
// MIS bug: it is dimensionally invisible and produces weights that are merely wrong rather than
// obviously broken.

#include "pathtrace/snapshot.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace avgen::pathtrace {

// A light is "delta" if it cannot be hit by a ray: a direction or a point with no area. Delta
// lights take MIS weight 1 because no BSDF sample could ever find them, and giving them a weight
// below 1 loses energy that nothing else will supply.
[[nodiscard]] bool isDeltaLight(const scene::PunctualLight& l);

struct LightSample {
    glm::vec3 direction{0.0f};  // from the surface toward the light, normalised
    glm::vec3 radiance{0.0f};   // emitter radiance arriving along `direction`, before the BSDF
    float distance = 0.0f;      // infinite for directional
    float pdf = 0.0f;           // solid-angle density; 0 for delta lights (they have no density)
    bool delta = false;
    bool valid = false;
};

// Samples one light. `u` is a 2-D uniform sample.
[[nodiscard]] LightSample sampleLight(const scene::PunctualLight& light, const glm::vec3& p, glm::vec2 u);

// The solid-angle density this sampler would have had for `direction` from `p`. Zero for delta
// lights and for directions that miss the emitter. This is the half of MIS that Phase 1 lacked.
[[nodiscard]] float lightPdf(const scene::PunctualLight& light, const glm::vec3& p,
                             const glm::vec3& direction, float distance);

// The power (beta = 2) heuristic. Veach's result: it beats the balance heuristic when one strategy
// is much better than the other, which is exactly the glossy-surface-near-a-small-light case.
//
// Written to be robust when one PDF is enormous: squaring two large floats overflows, so the ratio
// is formed first. A naive (a*a)/(a*a+b*b) returns NaN for a = 1e30 and that NaN becomes a pixel.
[[nodiscard]] float powerHeuristic(float pdfA, float pdfB);

// ---- emissive geometry -------------------------------------------------------------------------

struct EmissiveSample {
    glm::vec3 direction{0.0f};
    glm::vec3 radiance{0.0f};
    float distance = 0.0f;
    float pdf = 0.0f;           // solid angle
    std::uint32_t meshIndex = 0;
    std::uint32_t primIndex = 0;
    bool valid = false;
};

// Picks a triangle with probability proportional to its area, then a point uniformly on it.
// `u` chooses the point; `pick` chooses the triangle.
[[nodiscard]] EmissiveSample sampleEmissive(const Snapshot& snap, const glm::vec3& p, glm::vec2 u,
                                            float pick);

// The solid-angle density `sampleEmissive` would have had for a ray that HIT emissive triangle
// (meshIndex, primIndex) at `distance` travelling along `direction`. This is the other half of MIS:
// without it a BSDF ray that lands on an emitter cannot be weighted and its energy is double
// counted.
[[nodiscard]] float emissivePdf(const Snapshot& snap, std::uint32_t meshIndex, std::uint32_t primIndex,
                                const glm::vec3& direction, float distance);

} // namespace avgen::pathtrace
