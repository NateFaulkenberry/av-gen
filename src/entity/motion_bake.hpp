#pragma once

// Phase C §72: offline baking -- a motion-matching session turned into an ordinary clip.
//
//   motion matching session -> selected samples -> continuous clip -> baked animation
//
// **What is baked is the provider's own output**: each step runs `advance` exactly as the entity
// does, then `pose`, and the pose -- inertialized blends included -- is keyed. So a baked clip
// played back is the session as it was presented, frame for frame, and it can go anywhere a clip
// can: a MotionPack, the clip player, an export, a cinematic that must not re-run a search.
//
// **Not a prerequisite for anything** (§72). Runtime matching never bakes; this is a tool.
//
// **What it does not bake: world root motion.** The keys are the skeleton's local transforms as
// the provider poses them -- the travel joint's authored translation included -- not the
// character's position in the world, which the entity integrates after the provider (ADR-337, and
// §71's policy). A baked in-place corpus is in place; a baked travelling one carries each source
// clip's own root track, including the jump where the matcher switched clips.

#include "core/error.hpp"
#include "entity/motion_provider.hpp"
#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace avgen::entity {

struct MotionBakeOptions {
    std::string name = "baked";
    float sampleRate = 30.0f;   // steps per second; one key per step
    float seconds = 5.0f;
};

struct MotionBakeResult {
    scene::AnimationClip clip;
    // What each step settled on, so a bake can be audited against the session that made it.
    std::vector<MotionMemory> memories;
    std::uint32_t steps = 0;
    std::uint32_t declined = 0; // steps whose `advance` or `pose` did not produce; keyed from rest
};

using MotionScript = std::function<MotionRequest(std::uint32_t step, double time)>;

// Run `provider` for `options.seconds` at `options.sampleRate`, asking `script` for each step's
// request, and key every pose. Deterministic: the same provider, database, script and options give
// a bit-identical clip (ADR-360 / §73).
[[nodiscard]] Result<MotionBakeResult> bakeMotionSession(const IMotionProvider& provider,
                                                         const scene::Skeleton& skeleton,
                                                         const MotionScript& script,
                                                         const MotionBakeOptions& options = {});

} // namespace avgen::entity
