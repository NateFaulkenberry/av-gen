#pragma once

// BVH import (ADR-549): the format every mocap corpus ships in, and the one AV Gen could not read.
//
// **Why this exists.** Phase 0's licensing survey found exactly one corpus that can be shipped in a
// commercial product and is large enough to matter: **100STYLE, CC BY 4.0, four million frames of
// stylized locomotion** with the starts, stops and turns this repository's own content does not
// have (ADR-542 §8.3). It ships as BVH. ACCAD (CC BY 3.0) ships BVH. CMU's usable conversions ship
// BVH. AV Gen reads glTF and nothing else, so the licensing gate that Phase 0 found to be open led
// to a door with no handle.
//
// **Offline.** A BVH is a text file with one float per channel per frame; a 4-million-frame corpus
// is gigabytes of ASCII. Nothing here belongs on a load path, let alone a frame path. It produces a
// `Skeleton` and an `AnimationClip`, which is the same currency `retargetClip` already takes.
//
// **What BVH is, and the two things about it that bite.**
//
//   * A joint's channel *order* is declared per joint and is not fixed. `Zrotation Xrotation
//     Yrotation` is the common one and `Xrotation Yrotation Zrotation` also occurs; applying them
//     in the wrong order is a rig that looks almost right and is wrong wherever two axes are both
//     non-zero.
//   * The rotations are **intrinsic, applied in the declared order**, and the OFFSET is the bone.
//     There is no bind pose in the file beyond the offsets, so the rest pose is "every joint at its
//     offset with no rotation" -- which is the T-pose or A-pose the file was captured in.
//
// Units are whatever the capture used. 100STYLE and CMU are in centimetres; `BvhLoadOptions::scale`
// is how a caller says so, and getting it wrong is not subtle -- a 170 cm human arrives 170 units
// tall in a world where the alien is 1.66.

#include "core/error.hpp"
#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::assets {

struct BvhLoadOptions {
    // Multiplied into every offset and every translation channel. 0.01 for a corpus in centimetres.
    float scale = 1.0f;
    // The name given to the clip. Empty means the file's stem.
    std::string clipName;
    // Joint name prefix, so two skeletons loaded into one scene do not collide.
    std::string namePrefix;
};

struct BvhClip {
    scene::Skeleton skeleton;
    scene::AnimationClip clip;
    float frameSeconds = 0.0f;  // the file's own frame time
    std::uint32_t frames = 0;
    std::uint32_t channels = 0;
    // Joints declared `End Site` -- they have an offset and no channels. Kept in the skeleton,
    // because a foot's end site is where the toe is and a contact detector wants it, and reported
    // because a caller mapping joints by name needs to know they are unnamed in the file.
    std::uint32_t endSites = 0;
    std::vector<std::string> warnings;
    // The distinct rotation channel orders the file declares, as they appear: "ZXY", "XYZ". A
    // corpus with more than one is a corpus a reader that assumed an order is wrong about -- and
    // the same six numbers under ZXY and XYZ are poses five units apart, so it is not a small
    // error. Reported per file because "the corpus uses ZXY" is a claim about every file in it.
    std::vector<std::string> rotationOrders;
};

// Reads `path` as a BVH. Fails on a malformed hierarchy or a frame count that disagrees with the
// data; a recoverable oddity produces a warning and a clip.
[[nodiscard]] Result<BvhClip> loadBvh(const std::filesystem::path& path, const BvhLoadOptions& options = {});

// The same, from text already in memory -- which is what the tests use, so a fixture is visible in
// the test rather than sitting in a file beside it.
[[nodiscard]] Result<BvhClip> parseBvh(std::string_view text, const BvhLoadOptions& options = {});

} // namespace avgen::assets
