#pragma once

// ADR-1140 / ADR-1141: the CPU reference for the latent SDF force and the particle density volume.
//
// `shaders/particles.wgsl` (`cs_latent`) and `shaders/particle_density.wgsl` (`cs_density_splat`,
// `cs_density_resolve`) are transliterations of these functions: the same formulas in the same order.
// They exist so the GPU passes can be compared against something that is not themselves
// (tests/rendering/test_particle_latent_gpu.cpp), and so the binding curve -- the one number an author
// reasons about ("at coherence 0.3, how much of the matter is held?") -- is checkable without a GPU.

#include "scene/particles.hpp"
#include "spatial/sdf.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::scene {

// The particle's own binding threshold: mix(width, 1 - width, fract(seed * 61)). Kept inside
// [width, 1 - width] so coherence 1 binds every particle fully and coherence 0 binds none.
[[nodiscard]] float latentTheta(float seed, float width);
// b = smoothstep(theta - width, theta + width, coherence), WGSL's smoothstep exactly.
[[nodiscard]] float latentBinding(float theta, float coherence, float width);
// K = strength * (18 + 70 C^2), capped at 0.8 / dt^2 so the semi-implicit step stays stable when a
// frame is long (dt <= 0 leaves it uncapped: a zero step moves nothing anyway).
[[nodiscard]] float latentStiffness(float strength, float coherence, float dt);
// The tetrahedral-difference epsilon, in the SDF object's local units: 1e-3 of its bounds' diagonal.
[[nodiscard]] float latentGradientEpsilon(const glm::vec3& boundsMin, const glm::vec3& boundsMax);

struct LatentSample {
    float distance = 0.0f;    // mean of the four taps
    glm::vec3 normal{0.0f};   // normalised tetrahedral gradient (+Y when degenerate), local space
    glm::vec3 projection{0.0f}; // p - normal * distance, local space
};
// Four evaluations of the packed program around `pLocal` (sdf.wgsl's interpreter, evaluatePacked here).
[[nodiscard]] LatentSample latentProject(std::span<const spatial::SdfNodeGpu> program, const glm::vec3& pLocal,
                                         double time, float epsilon, const spatial::FieldSet* fields = nullptr);
// ADR-1145: the same over the tree itself -- the reference for the compiled force (sdfCompileWgsl's field).
[[nodiscard]] LatentSample latentProject(const spatial::SdfTree& tree, const glm::vec3& pLocal, double time,
                                         float epsilon, const spatial::FieldSet* fields = nullptr);

// Everything one cs_latent step reads that is not the particle.
struct LatentStep {
    glm::mat4 model{1.0f};   // SDF local -> world
    glm::mat4 inverse{1.0f}; // world -> SDF local
    glm::mat4 normal{1.0f};  // transpose(inverse(model)), for the release impulse's direction
    float coherence = 1.0f;
    float prevCoherence = 1.0f;
    float width = 0.08f;
    float strength = 1.0f;
    float release = 12.0f;
    float epsilon = 1e-3f;
    double time = 0.0;
    float dt = 1.0f / 60.0f;
};
// The velocity after one cs_latent step, WITHOUT the flow term (which reads the curl noise, a
// function this mirror does not carry). `flow` must be 0 for a parity comparison.
[[nodiscard]] glm::vec3 latentVelocityStep(const LatentStep& step, std::span<const spatial::SdfNodeGpu> program,
                                           const glm::vec3& position, const glm::vec3& velocity, float seed,
                                           const spatial::FieldSet* fields = nullptr);
// ADR-1145: the compiled force's reference (the tree evaluated directly).
[[nodiscard]] glm::vec3 latentVelocityStep(const LatentStep& step, const spatial::SdfTree& tree,
                                           const glm::vec3& position, const glm::vec3& velocity, float seed,
                                           const spatial::FieldSet* fields = nullptr);

// ---- ADR-1141: the density volume ---------------------------------------------------------------

// Trilinear cloud-in-cell into a res^3 u32 grid over [lo, hi]: each particle adds
// u32(w_corner * kDensityFixedScale + 0.5) to its eight surrounding cells (cell centres sit at
// lo + (i + 0.5) * cell). A particle whose 2x2x2 footprint leaves the grid is skipped, as on the GPU.
void splatDensity(std::span<const glm::vec3> positions, const glm::vec3& lo, const glm::vec3& hi, int res,
                  std::vector<std::uint32_t>& grid);
// The 3x3x3 binomial blur ((1 2 1)^3 / 64, edges clamped) of the splat, scaled to particles per cell
// times `weight`: what the GPU resolve writes into the r channel of the texture.
[[nodiscard]] std::vector<float> resolveDensity(std::span<const std::uint32_t> grid, int res, float weight);

} // namespace avgen::scene
