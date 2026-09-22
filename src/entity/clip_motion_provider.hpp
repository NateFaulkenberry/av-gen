#pragma once

// The clip provider (ADR-541 corollary 1): the bottom of the fallback chain, and the one that must
// not fail.
//
// **This is not a wrapper around `AnimationPlayer`.** It could not be: the player keeps its current
// state, its start time and its inertialization scratch inside itself, and ADR-541's whole point is
// that a provider keeps nothing. So this is a second, smaller implementation of clip playback whose
// entire state is the `MotionMemory` handed to it -- which is what makes it replayable by
// `EntityWorld::seek` and testable without a world.
//
// It is deliberately less capable than `AnimationPlayer`. It plays one clip at a rate, wraps it,
// and reports its phase. What it gives the chain is the guarantee that *something* always poses
// the body.
//
// **It used to say, here, that it does not blend "because those are the player's job and
// duplicating them here would be a second answer to a question already settled (ADR-547)". That
// was wrong, and the way it was wrong is worth keeping.** The argument holds only while the player
// is still consulted. It is not: when a provider poses a body, `SkinnedRig::evaluate` takes the
// external pose and `AnimationPlayer::evaluate` is never called, so there is no first answer for
// this to be a second one to. Measured on the Glowmere alien at a gait change: **0.4484 m of foot
// teleport on average, 8.8x the 0.0510 m bar, worst 0.6410 m** -- worse than the matcher's
// original defect, and on the only provider the product actually installs (ADR-613).
//
// **The obligation it is held to** is ADR-541's: for the case they both cover -- a looping clip
// playing at a steady rate -- this provider and `AnimationPlayer` must produce the same pose. The
// test asserts that joint by joint rather than trusting it.

#include "entity/gait.hpp"
#include "entity/motion_provider.hpp"

#include <string>
#include <vector>

namespace avgen::entity {

// One piece of playable content, as the provider sees it. The mapping from intent to content lives
// here rather than in a behaviour, which is rule R4: a request names an intent and the pack decides
// what that means.
struct ClipEntry {
    std::uint32_t clip = 0;      // index into the clips this provider was given
    MovementMode mode = MovementMode::Ground;
    // Matched exactly, and "" matches only a request that asks for no style. NOT a wildcard: see
    // `select` for why a catch-all would make a missing style pack invisible.
    std::string style;
    // The speed this clip's stride was authored for, m/s. Zero means "no opinion" -- not "0 m/s" --
    // and is what an idle has. It is authored rather than derived because it cannot be derived:
    // every locomotion clip in this repository is in place and its root nets zero (ADR-540).
    float authoredSpeed = 0.0f;
    bool loop = true;
};

class ClipMotionProvider final : public IMotionProvider {
public:
    ClipMotionProvider() = default;
    // Non-owning. The clips outlive the provider by construction: they belong to the rig or to a
    // MotionPack, both of which are loaded before any character is posed.
    ClipMotionProvider(const std::vector<scene::AnimationClip>* clips, std::string name)
        : clips_(clips), name_(std::move(name)) {}

    void setClips(const std::vector<scene::AnimationClip>* clips) { clips_ = clips; }

    // §32/ADR-613. Seconds for the pose offset a clip change introduces to halve; zero is the old
    // unblended behaviour. **Derived from the soonest one change can follow another**, which for
    // this provider is the gait tier's minimum dwell -- `Gait::select` will not change its mind
    // faster than that, so nothing downstream needs to blend faster than that either. A caller
    // whose gait settings differ should pass its own.
    void setInertializeHalflife(float seconds) { inertializeHalflife_ = seconds; }
    [[nodiscard]] float inertializeHalflife() const { return inertializeHalflife_; }
    void setBlendSlots(std::size_t slots) { blendSlots_ = slots; }
    void addEntry(ClipEntry entry) { entries_.push_back(std::move(entry)); }
    void clearEntries() { entries_.clear(); }
    [[nodiscard]] const std::vector<ClipEntry>& entries() const { return entries_; }

    [[nodiscard]] std::string_view name() const override { return name_; }

    [[nodiscard]] MotionResult advance(const MotionRequest& request, const MotionMemory& in,
                                       double time, float dt, MotionMemory& next) const override;
    [[nodiscard]] MotionResult pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                    scene::Pose& out) const override;

    // Which entry this request resolves to, or -1. Exposed because "which clip did it pick" is the
    // first question of any bug report about this provider, and re-deriving it in a debug view
    // would be a second implementation of the selection rule.
    [[nodiscard]] int select(const MotionRequest& request) const;

private:
    const std::vector<scene::AnimationClip>* clips_ = nullptr;
    std::vector<ClipEntry> entries_;
    std::string name_ = "clip";
    float inertializeHalflife_ =
        derivedInertializeHalflife(GaitSettings{}.minDwell, kBlendBudgetFrameSeconds);
    std::size_t blendSlots_ = MotionMemory::kBlendSlots;
};

} // namespace avgen::entity
