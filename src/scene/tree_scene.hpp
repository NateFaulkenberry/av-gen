#pragma once

// Assembling a Tree of Life scene: the graph and its meshes become a `scene::Scene` with materials,
// lights, an environment and the fixed camera.
//
// This is GPU-free on purpose. Everything about what the shot IS -- the palette, the emission
// values, the light rig, the framing -- is decided here, where it can be unit-tested and diffed,
// and the renderer is handed an ordinary Scene it has no special knowledge of.
//
// THE EMISSION VALUES ARE ON THE ENGINE'S OWN LADDER. `world::EmissionLadder` is a validated ~200:1
// range with a deliberate 13x gap between "noticeable" (0.295) and "special" (3.94), and Glowmere
// ships bloom at threshold 1.0 precisely so only the rungs above that gap bloom at all. Picking
// numbers freely here would have put the tree on a second, private ladder and broken that contract.
//
// The cookbook's four-render lesson is also honoured rather than rediscovered: a solid dome at
// emissive 9.0 clipped and read as a lamp, and what read as an organism was STRUCTURE -- sixty-four
// thin tubes. The canopy here is thousands of small cards rather than a shell, so it can carry a
// higher value than a solid surface could.

#include "core/error.hpp"
#include "scene/scene.hpp"
#include "scene/tree.hpp"
#include "scene/tree_generator.hpp"
#include "scene/tree_mesh.hpp"

#include <glm/glm.hpp>

#include <array>
#include <string>

namespace avgen::scene {

// The art direction, as numbers. One struct so a look is a diff.
struct TreeLook {
    // Bark. Nearly black and slightly blue: the trunk's job in this shot is to be a silhouette and
    // a support for the veins, not to be lit.
    glm::vec3 barkColor{0.032f, 0.050f, 0.055f};
    float barkRoughness = 0.72f;
    glm::vec3 rootColor{0.026f, 0.038f, 0.044f};

    // The life force. Teal running up the trunk, cooling and brightening outward, so the energy
    // reads as flowing toward the canopy rather than as paint.
    glm::vec3 veinColor{0.16f, 0.78f, 0.88f};
    // Very low. At 0.20 against a near-black base colour the emissive term dominated completely and
    // the whole tree -- trunk, limbs and canopy -- came out one flat teal: the bark stopped being
    // bark. A vein has to be a PATTERN on a dark surface, not a tint over the whole of it, so until
    // there is a material program to carry that pattern the branches keep only enough to catch the
    // eye at grazing angles, where the free emissive Fresnel rim does the work.
    float trunkEmissive = 0.055f;
    float primaryEmissive = 0.040f;
    float secondaryEmissive = 0.028f;
    float tertiaryEmissive = 0.020f;

    // Foliage. Emerald and cyan-green with the gold kept as an accent, per the palette brief --
    // the reference's gold canopy read as gold because it was lit warm, not because the leaves
    // were yellow.
    // Three tints. Deep emerald is the body of the crown, turquoise lifts it, and the gold is an
    // accent rather than the canopy's colour -- the reference's golden crown reads gold because it
    // is lit warm, not because its leaves are yellow.
    std::array<glm::vec3, kFoliageTints> foliageColor{glm::vec3{0.040f, 0.120f, 0.088f},
                                                      glm::vec3{0.036f, 0.118f, 0.128f},
                                                      glm::vec3{0.100f, 0.098f, 0.055f}};
    std::array<glm::vec3, kFoliageTints> foliageEmissiveTint{glm::vec3{0.085f, 0.62f, 0.36f},
                                                             glm::vec3{0.075f, 0.56f, 0.62f},
                                                             glm::vec3{0.60f, 0.44f, 0.14f}};
    std::array<float, kFoliageTints> foliageEmissiveScale{1.0f, 0.86f, 0.50f};
    // 0.55, not 1.65. At 1.65 the canopy clipped to near-white and the palette disappeared -- the
    // cookbook's "a glowing surface reads as paint" failure, arrived at independently. Below the
    // bloom threshold of 1.0 the foliage keeps its colour and the bloom is left for the veins.
    float foliageEmissiveIntensity = 0.85f;
    float foliageRoughness = 0.78f;

    // Ground and sky.
    glm::vec3 groundColor{0.022f, 0.030f, 0.035f};
    float groundRadius = 260.0f;
    glm::vec3 zenith{0.006f, 0.013f, 0.038f};
    glm::vec3 horizon{0.030f, 0.058f, 0.120f};
    glm::vec3 fogColor{0.008f, 0.018f, 0.032f};
    float fogDensity = 0.0075f;
    float volumeDensity = 0.005f;

    // Lighting. A moon key for the silhouette, a violet rim for separation, and almost no fill:
    // the tree is meant to be readable when emission is switched off, but only just.
    float keyIntensity = 0.55f;
    float rimIntensity = 0.62f;
    float fillIntensity = 0.09f;
    bool includeGround = true;
};

// The scene, ready to render. `camera` decides the framing; pass the same `TreeCameraView` the
// candidate search evaluated with, or the tree will have been chosen for a shot nobody takes.
[[nodiscard]] Result<Scene> buildTreeScene(const TreeGraph& graph, const TreeMeshes& meshes,
                                           const TreeCameraView& camera, const TreeLook& look = {});

// Convenience: generate, mesh and assemble in one call.
[[nodiscard]] Result<Scene> buildTreeScene(const TreeParams& params, const TreeCameraView& camera = {},
                                           const TreeLook& look = {}, const TreeMeshSettings& mesh = {});

} // namespace avgen::scene
