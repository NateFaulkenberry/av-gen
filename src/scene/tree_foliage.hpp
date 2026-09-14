#pragma once

// The foliage primitive: a leaf spray on an alpha-cut card, and the texture that makes it one.
//
// WHY THE CARD HAD TO CHANGE RATHER THAN BE TUNED AGAIN. The canopy has been through both failures
// on one axis. Large overlapping cards gave a solid shell that hid the limbs; small scattered cards
// gave something open enough to read the limbs through and moth-eaten at the silhouette, because a
// 0.15 m opaque rectangle at the showcase distance is a few pixels of flat colour and a canopy of
// them is a spray of flakes. Every lever between those -- count, size, spacing, elongation, cluster
// scale, normals -- was exercised, and the midpoint is not the answer: both ends are made of
// rectangles, and a rectangle has no interior.
//
// What has interior is a card that is ITSELF a small branchlet: a spine with seven leaves on it,
// cut out of the card by an alpha mask. Then a card can be large enough to read as mass without
// reading as a rectangle, its silhouette edge is leaf-shaped rather than straight, and overlapping
// cards resolve into layers instead of into a lattice. Eight of them per cluster is ~56 leaves,
// at a quarter of the triangles the flakes cost.
//
// The texture is generated rather than imported, for two reasons that are both about this project
// rather than about taste: the tree is a pure function of its parameters and an imported asset
// would break that, and a procedural node's material JSON cannot express alpha cutout at all -- it
// is only reachable because the scene assembler builds `Material` in C++.

#include "scene/scene_types.hpp"

#include <cstdint>
#include <string>

namespace avgen::scene {

struct LeafSpraySettings {
    std::uint32_t resolution = 256;
    int leaves = 11;
    // The spine the leaves hang off, as a fraction of the card. A straight spine gives a feather;
    // a curved one gives a branchlet, and the difference is visible at the silhouette.
    float spineCurve = 0.28f;
    float leafLength = 0.235f;  // of the card
    float leafWidth = 0.082f;
    float leafSpread = 0.62f;   // radians either side of the spine
    // Leaves are darker at the base and toward the midrib. This is the "internal structure" half:
    // it is invisible at fifty metres and it is what a viewer sees when they look INTO the crown.
    float baseDarkening = 0.45f;
    float midribDarkening = 0.30f;
    float tipLightening = 0.22f;
    std::uint32_t seed = 1;
};

// An RGBA8 sRGB texture: alpha is the cutout, rgb is a value variation multiplied onto the
// material's base colour. One texture serves every tint, because the tint lives in the material.
[[nodiscard]] TextureData makeLeafSprayTexture(const LeafSpraySettings& settings);

// The fraction of a card that survives the alpha cutoff.
//
// The evaluator needs this and did not have it. Its rasteriser draws each foliage cluster as a
// SOLID DISC, which was right when a cluster was a ball of opaque flakes and is wrong now: a spray
// card is mostly empty, so a solid disc over-reports the canopy's coverage by about three times.
// Every silhouette metric is computed on that mask, so `openness` and `structureVisible` were both
// measuring a canopy denser than the one being rendered -- and the proof is that swapping the whole
// foliage primitive changed not one band's variance by a thousandth. A measurement that cannot
// notice the thing you changed is not measuring it.
[[nodiscard]] float leafSprayCoverage(const LeafSpraySettings& settings, float alphaCutoff);

} // namespace avgen::scene
