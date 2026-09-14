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
#include "scene/tree_foliage.hpp"
#include "scene/tree_generator.hpp"
#include "scene/tree_mesh.hpp"
#include "scene/tree_rig.hpp"
#include "scene/tree_veins.hpp"

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
    // The veins are a MaterialProgram now, so these are gains on ITS output: the program asserts
    // emission and the material's own lane is discarded by the shader. See tree_veins.hpp for why
    // that is a decision rather than an accident. The values below are only reached when the veins
    // are switched off.
    VeinSettings veins{};
    bool veinsEnabled = true;
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
                                                      glm::vec3{0.135f, 0.110f, 0.045f}};
    std::array<glm::vec3, kFoliageTints> foliageEmissiveTint{glm::vec3{0.085f, 0.62f, 0.36f},
                                                             glm::vec3{0.075f, 0.56f, 0.62f},
                                                             glm::vec3{0.92f, 0.58f, 0.16f}};
    std::array<float, kFoliageTints> foliageEmissiveScale{1.0f, 0.86f, 0.92f};
    // 0.55, not 1.65. At 1.65 the canopy clipped to near-white and the palette disappeared -- the
    // cookbook's "a glowing surface reads as paint" failure, arrived at independently. Below the
    // bloom threshold of 1.0 the foliage keeps its colour and the bloom is left for the veins.
    // Lowered again, and the reason is section 28: the tree has to stay readable with emission
    // disabled, which means the FORM has to come from light falling on the canopy rather than from
    // the canopy glowing. Every previous reduction here made the picture better.
    float foliageEmissiveIntensity = 0.62f;
    float foliageRoughness = 0.78f;
    LeafSpraySettings leafSpray{};
    // The cutoff. High enough that the mask's soft edge does not leave a halo of half-leaves, low
    // enough that the mip chain does not erode the spray to nothing at distance -- the renderer's
    // alpha-coverage preservation is what makes the second half of that true.
    float foliageAlphaCutoff = 0.42f;

    // --- The environment, and the only job it has --------------------------------------------
    //
    // A referent, not furniture. The hero has read as an eight-metre tree in every render so far,
    // and nothing in the frame has ever disagreed: an empty ground plane has no size. A handful of
    // ordinary trees at a distance, each a few metres tall, is what makes thirty metres legible --
    // it is the cue the reference images all use and the cheapest one there is. Everything past
    // that is Glowmere Valley, which the brief rules out.
    int distantTrees = 14;
    float distantNear = 46.0f;
    float distantFar = 135.0f;
    float distantHeightMin = 5.0f;
    float distantHeightMax = 11.0f;
    // Kept out of a wedge behind the hero so they never crowd its silhouette.
    float distantClearAngle = 0.55f;

    // Ground and sky.
    // Darker than the sky it meets. The ground was rendering brighter than the horizon and drew a
    // pale band across the frame, which is the same mistake the bioluminescence cookbook records
    // for fog: anything behind the subject that is brighter than the background works against the
    // silhouette rather than for it.
    glm::vec3 groundColor{0.009f, 0.012f, 0.015f};
    float groundRadius = 260.0f;
    glm::vec3 zenith{0.006f, 0.013f, 0.038f};
    glm::vec3 horizon{0.030f, 0.058f, 0.120f};
    // Cool. The mist was reading warm-brown against a teal tree because the ground's albedo came
    // through it; the fog's own colour is what decides that, and a warm haze under a cyan canopy
    // fights the palette everywhere the two meet.
    glm::vec3 fogColor{0.006f, 0.016f, 0.034f};
    float fogDensity = 0.0075f;
    float volumeDensity = 0.024f;
    // The mist sits low and thick enough to swallow the far trees' feet, which is what turns a row
    // of silhouettes into distance rather than a row of silhouettes.
    float fogHeight = 4.5f;
    float fogHeightFalloff = 0.17f;

    // Lighting. A moon key for the silhouette, a violet rim for separation, and almost no fill:
    // the tree is meant to be readable when emission is switched off, but only just.
    // Raised with the emission lowered: the canopy now has to be lit rather than lit-from-within,
    // and a rim-led rig with almost no key gives a glowing shell no internal gradient.
    // 0.68. At 0.95 the key lit the atmosphere as well as the tree: the sky and mist came up, the
    // contrast between a dark tree and a dark ground collapsed, and the bark's veins stopped
    // reading because the bark itself was no longer dark. The canopy does need more key than a
    // rim-led rig gives it, but the amount that helps the canopy is well below the amount that
    // starts lighting the air between the camera and everything else.
    float keyIntensity = 0.68f;
    float rimIntensity = 0.62f;
    float fillIntensity = 0.06f;
    // UNDER-LIGHT. A crown is not one shell, it is layers with light falling between them, and a
    // rig lit only from above and behind gives the underside nothing at all -- so the canopy's
    // lower half goes flat and the layering the foliage volume now has is invisible from below.
    // A wide, weak disk under the crown separates those layers. Glowmere's bioluminescent rig does
    // the same thing with a disk at elevation -40 and it is the single cue that reads as "lit from
    // within the foliage" rather than "lit from the sky".
    float underIntensity = 1.7f;
    glm::vec3 underColor{0.30f, 0.92f, 0.72f};
    bool includeGround = true;
};

// Everything the runtime needs to draw and animate one tree: the scene, plus the rig and the graph
// the animator reads. Returned together because they are only valid as a set -- the scene's meshes
// are skinned against this rig's joint indices and nothing else's.
struct TreeSceneBuild {
    Scene scene;
    TreeGraph graph;
    TreeRig rig;
    TreeMeshes meshes;
    std::uint32_t triangles = 0;
};

// The scene, ready to render. `camera` decides the framing; pass the same `TreeCameraView` the
// candidate search evaluated with, or the tree will have been chosen for a shot nobody takes.
[[nodiscard]] Result<Scene> buildTreeScene(const TreeGraph& graph, const TreeMeshes& meshes,
                                           const TreeCameraView& camera, const TreeLook& look = {});

// Convenience: generate, mesh and assemble in one call.
[[nodiscard]] Result<Scene> buildTreeScene(const TreeParams& params, const TreeCameraView& camera = {},
                                           const TreeLook& look = {}, const TreeMeshSettings& mesh = {});

// The animatable form: generates, meshes, rigs, skins and assembles. Use this when the tree has to
// move; `buildTreeScene` above is the still.
[[nodiscard]] Result<TreeSceneBuild> buildAnimatedTree(const TreeParams& params,
                                                       const TreeCameraView& camera = {},
                                                       const TreeLook& look = {},
                                                       const TreeMeshSettings& mesh = {},
                                                       const TreeRigSettings& rigSettings = {});

} // namespace avgen::scene
