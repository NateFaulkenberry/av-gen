#pragma once

// The life force: a vein pattern on the bark, as a `MaterialProgram`.
//
// WHY A PROGRAM AND NOT AN EMISSIVE VALUE. Measured, last session: emissive 0.20 over a near-black
// base made the trunk, the limbs and the canopy one flat teal, and the bark stopped being bark. A
// tint has no edges, so it cannot read as a vein however it is scaled -- turn it down and it
// vanishes, turn it up and the whole surface glows. A vein is a PATTERN ON A DARK SURFACE, which
// means a per-fragment mask, which means a material program.
//
// WHO OWNS EMISSION. This is the decision the Glowmere agent's regression came from, so it is made
// here in writing rather than discovered.
//
// In this engine a program does not modulate emission, it REPLACES it:
// `matEmissive = vec4(program.emission, 1.0)` in pbr_shade.wgsl, and the material's own
// `emissiveIntensity` lane is discarded. So emission is an ASSERTION BY THE PROGRAM, never an input
// to it, and there is no way to author it as an input short of changing the shader.
//
// The rule adopted here, stated so it can be checked: **a part whose material names a program owns
// its entire emission contract -- the ladder rung and the audio response both.** Concretely:
//
//   * the branch tiers carry the vein program, and their audio-driven gain is written to
//     `MaterialProgram::emissionIntensity`, which is the multiplier the program's output passes
//     through;
//   * the foliage carries NO program, so its `Material::emissiveIntensity` still means what it
//     meant in phase 9 and the routes built there are untouched;
//   * `verifyEmissionOwnership` asserts no part is in both states at once -- a program *and* a
//     non-default material emissive that nothing will ever read. That is the exact shape of the
//     regression this note exists to prevent, and it is a test rather than a convention.

#include "core/error.hpp"
#include "scene/material_program.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct VeinSettings {
    // The pattern. Veins run ALONG the branch, so the noise is sampled with a high frequency around
    // the tube's u and a low one along its v: fast variation around, slow along, which is a streak.
    float aroundFrequency = 9.0f;
    // Not near-zero. At 0.42 a vein at a given u persisted the whole length of the trunk and read
    // as a painted stripe rather than as something grown; the pattern has to meander along the
    // branch as well as vary around it.
    float alongFrequency = 1.15f;
    // Where the noise is cut. A narrow window gives thin bright veins; a wide one washes back into
    // the tint this exists to avoid.
    float threshold = 0.465f;
    float edge = 0.075f;
    // The travelling pulse: energy moving up the tree, as a cosine in v displaced by time.
    float pulseSpeed = 0.085f;
    float pulseDepth = 0.55f;   // 0 = a static vein, 1 = it blinks out between pulses
    float pulseWavelength = 0.7f;
    glm::vec3 color{0.18f, 0.86f, 0.95f};
    float intensity = 1.6f;
    std::uint32_t seed = 1;
};

// The program. One per tree, shared by every branch tier; the tiers differ in their gain, not in
// their pattern, so they share a program rather than carrying four near-identical copies.
[[nodiscard]] MaterialProgram makeVeinProgram(const std::string& name, const VeinSettings& settings);

// Checks the rule in the header note across a whole scene: nothing may carry a material program AND
// a material emissive that the program's assertion will silently discard.
[[nodiscard]] Result<void> verifyEmissionOwnership(const Scene& scene);

} // namespace avgen::scene
