#pragma once

// The MotionPack (ADR-550, shaped by ADR-542): the one file that crosses the offline/runtime
// boundary.
//
// **What it is for.** Three problems share this answer. There is no way to get motion onto a rig
// without re-importing a whole character (ADR-548 fixed the transfer; this is where the result
// lives). `Composition` deep-copies all 26 clips per node instance at ~3.5 MB each, so clip memory
// scales with the cast. And licensing is the binding constraint on content, so a shipped database
// has to be able to say where it came from.
//
// **The licence is not metadata, it is a required field.** A pack without one does not build. This
// codebase has a standing lesson that an unreported no-op is the defect, and a warning in a build
// log is how research assets ship.
//
// **It is also the training-data format**, eventually (Phase E). Nothing here is shaped around a
// particular neural architecture, and a pack that was *generated* by one must remain readable
// without it -- which is why the optional model lives in its own directory and nothing else refers
// to it.
//
// ---- the layout ---------------------------------------------------------------------------------
//
//   character.motionpack/
//     pack.json      version, skeleton digest, PROVENANCE, and the clip index
//     skeleton.bin   joints: parents, rest transforms, names
//     clips.bin      resampled poses, fixed rate, shared read-only by every instance
//     meta.bin       per clip, per frame: phase
//     features.bin   (Phase C) float32 [frames x D], normalised and weight-folded
//     model/         (Phase E) flat versioned network weights
//
// The last two are named here and not written: ADR-542 says design for versioning and do not
// implement fields that are not needed yet. `PackVersion` is how they arrive without breaking a
// reader.

#include "core/error.hpp"
#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/skeleton.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::scene {

// Bumped when the on-disk layout changes in a way a previous reader could not handle. A reader
// refuses a version it does not know rather than guessing, because a pack half-read is a character
// that animates wrongly instead of not at all.
inline constexpr std::uint32_t kMotionPackVersion = 1;

// ---- provenance ----------------------------------------------------------------------------------

// Whether this motion may be shipped. Deliberately three states and not two: **the third is the
// one that matters.** Phase 0's survey found sources whose terms are genuinely ambiguous -- a
// dataset whose page contradicts itself about BY versus BY-SA, a "free" pack whose licence text
// does not exist -- and the honest answer for those is neither yes nor no.
enum class Redistribution : std::uint8_t {
    // Verified: a derived database built from this may be shipped.
    Allowed,
    // Verified: it may not.
    Forbidden,
    // Not established. **Treated as forbidden by every check**, and reported by name so a human can
    // resolve it. A source arrives here by default, not by accident.
    RequiresReview,
};
[[nodiscard]] const char* redistributionName(Redistribution value);
[[nodiscard]] bool redistributionFromName(std::string_view name, Redistribution& out);

// Where one clip in this pack came from and what may be done with it.
struct Provenance {
    std::string source;        // the corpus: "100STYLE", "alien-scout.glb", "generated"
    std::string sourceFile;    // the file within it
    std::string creator;
    std::string license;       // an SPDX identifier where one exists: "CC-BY-4.0", "CC0-1.0"
    std::string licenseUrl;
    bool attributionRequired = true;
    Redistribution redistribution = Redistribution::RequiresReview;
    bool derivedDataAllowed = false;
    bool trainingAllowed = false;
    // Every transformation applied, in order: "bvh-import scale=0.01", "retarget profile=alien",
    // "resample 30Hz". A derived database whose ancestry cannot be printed is one nobody can clear.
    std::vector<std::string> processing;
    std::string toolVersion;
    std::string notes;

    friend bool operator==(const Provenance&, const Provenance&) = default;
};

// ---- the pack -------------------------------------------------------------------------------------

struct PackClip {
    std::string name;
    float length = 0.0f;
    float sampleRate = 30.0f;
    std::uint32_t frames = 0;
    bool loop = true;
    std::vector<std::string> tags;
    // Analysis, carried rather than recomputed: a pack that stored clips and made the runtime
    // re-derive their contacts would be paying the offline cost at load.
    std::vector<ContactTrack> contacts;
    PhaseTrack phase;
    // Ground-frame travel over the clip, and the speed it implies. ADR-540 had to measure this by
    // hand to find out that nothing in the repository travelled; a pack says so.
    glm::vec3 rootTravel{0.0f};
    float groundSpeed = 0.0f;
    // Index into `MotionPack::provenance`. Per clip, not per pack: a pack may mix a CC0 character's
    // own takes with CC-BY corpus motion, and the answer to "may we ship this" then differs by clip.
    std::uint32_t provenance = 0;
};

struct MotionPack {
    std::uint32_t version = kMotionPackVersion;
    std::string name;
    Skeleton skeleton;
    // A digest of the skeleton this pack was built against: joint count, names, parents and rest
    // transforms. A pack played on a rig that does not match is the silent-wrong-character failure,
    // and this is what makes it loud.
    std::string skeletonDigest;
    std::vector<Provenance> provenance;
    std::vector<PackClip> clips;
    // Parallel to `clips`: the resampled animation. Held as ordinary `AnimationClip`s so that
    // everything downstream -- the player, the layer stack, root motion -- works on pack content
    // with no special case.
    std::vector<AnimationClip> animation;

    [[nodiscard]] std::uint32_t frameCount() const;
    // The worst redistribution state over every clip. A pack is only shippable when every clip is.
    [[nodiscard]] Redistribution redistribution() const;
    [[nodiscard]] int findClip(std::string_view clipName) const;
};

// The digest `MotionPack::skeletonDigest` holds. Exposed because a caller checking a pack against a
// rig needs to compute the rig's side of the comparison.
[[nodiscard]] std::string skeletonDigest(const Skeleton& skeleton);

// ---- building ---------------------------------------------------------------------------------------

struct PackBuildOptions {
    float sampleRate = 30.0f;
    std::vector<ContactJoint> contactJoints;
    ContactSettings contacts;
    std::string toolVersion;
};

// Analyse `clips` against `skeleton` and assemble a pack. `provenance` is required and is checked:
// a build with an empty licence fails rather than warning.
[[nodiscard]] Result<MotionPack> buildMotionPack(std::string name, const Skeleton& skeleton,
                                                 const std::vector<AnimationClip>& clips,
                                                 const Provenance& provenance,
                                                 const PackBuildOptions& options = {});

// ---- validation -------------------------------------------------------------------------------------

struct PackValidation {
    std::uint32_t clips = 0;
    std::uint32_t frames = 0;
    std::uint32_t clipsWithoutContacts = 0;
    std::uint32_t clipsWithoutPhase = 0;
    std::uint32_t clipsNotCyclic = 0;
    std::uint32_t clipsRequiringReview = 0;
    std::uint32_t clipsForbidden = 0;
    std::vector<std::string> errors;   // a pack with any of these must not ship
    std::vector<std::string> warnings; // and these are for a human to look at
    [[nodiscard]] bool ok() const { return errors.empty(); }
    // The human-readable report the Phase A brief asks for.
    [[nodiscard]] std::string report() const;
};

[[nodiscard]] PackValidation validateMotionPack(const MotionPack& pack);

// ---- on disk ------------------------------------------------------------------------------------------

[[nodiscard]] Result<void> writeMotionPack(const MotionPack& pack, const std::filesystem::path& directory);
[[nodiscard]] Result<MotionPack> readMotionPack(const std::filesystem::path& directory);

} // namespace avgen::scene
