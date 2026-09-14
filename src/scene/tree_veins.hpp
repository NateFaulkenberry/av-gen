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
    // The vein field is sampled in WORLD SPACE, triplanar, not in the tube's uv.
    //
    // uv was the obvious choice and it is wrong, and the contact sheet is what showed it: a trunk
    // got veins and a twig got a solid glowing rod. uv is normalised per branch, so the pattern's
    // wavelength shrinks with the branch -- and a tertiary tube has four radial sides and a short
    // run, so the noise is very nearly constant across the whole of it and the twig is uniformly on
    // or off. Eight of twelve candidates had limbs reading as bright cyan wires.
    //
    // In world space the wavelength is a fixed distance, so a trunk four metres around carries
    // several veins and a twig a fifth of a metre around carries at most one. Twigs then differ
    // from each other rather than all glowing, which is section 23's "selected secondary branches"
    // arriving as a consequence rather than as a rule.
    // 2.6 cycles per metre, not 0.85. At the longer wavelength the mask made blotches the size of
    // the trunk's own width and the result read as birch bark -- patches, not veins. A vein is thin
    // relative to what carries it, which means a wavelength well under the trunk's circumference
    // and a narrow cut.
    float veinScale = 2.6f;       // cycles per metre, horizontally
    // THE FIELD HAS TO BE ANISOTROPIC OR IT MAKES BLOTCHES, NOT VEINS.
    //
    // fbm is isotropic, so thresholding it gives blobs at every frequency -- at a long wavelength
    // the trunk read as birch bark, and shortening it only made the patches smaller. That was two
    // renders spent on the wrong parameter: the problem was never the scale, it was the shape.
    // Compressing the vertical axis before sampling stretches every feature along the trunk, which
    // is what turns a blob field into a streak field, and it keeps the world-space property that
    // made the switch away from uv worth making.
    float verticalStretch = 13.0f;
    float triplanarSharpness = 4.0f;
    // Where the noise is cut. A narrow window gives thin bright veins; a wide one washes back into
    // the tint this exists to avoid.
    float threshold = 0.600f;
    float edge = 0.022f;
    // The travelling pulse, in world height: energy rising through the whole tree at once rather
    // than each branch pulsing in its own parameterisation, which is what "a travelling wave"
    // means when the thing it travels through is one organism.
    float pulseSpeed = 0.55f;     // metres per second
    float pulseDepth = 0.55f;
    float pulseWavelength = 9.0f; // metres
    glm::vec3 color{0.18f, 0.86f, 0.95f};
    float intensity = 1.9f;
    std::uint32_t seed = 1;
};

// The program. One per tree, shared by every branch tier; the tiers differ in their gain, not in
// their pattern, so they share a program rather than carrying four near-identical copies.
[[nodiscard]] MaterialProgram makeVeinProgram(const std::string& name, const VeinSettings& settings);

// Checks the rule in the header note across a whole scene: nothing may carry a material program AND
// a material emissive that the program's assertion will silently discard.
[[nodiscard]] Result<void> verifyEmissionOwnership(const Scene& scene);

} // namespace avgen::scene
