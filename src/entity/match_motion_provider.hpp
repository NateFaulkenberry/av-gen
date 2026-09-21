#pragma once

// Motion matching as a provider (Phase C §26-§35), plugged into ADR-541's chain ahead of the clip
// player.
//
// **Why this fits `advance`/`pose` without a skeleton, which is the part that looks impossible.**
// ADR-556 says `advance` may not touch a skeleton, and a motion-matching query obviously needs the
// character's current pose. The resolution is that the current pose **is a database sample**: the
// character is playing frame N of clip C, so the pose half of the query is that sample's own
// feature vector, already extracted, already normalised, already in the database. Only the half
// that describes *intent* -- where the body wants to go and how fast -- comes from the request.
//
// That is not a trick to satisfy the interface; it is how motion matching is actually formulated.
// The query is "something that looks like what I am doing now and goes where I want to go", and
// the first clause is a lookup rather than a computation.
//
// **What this cannot do on the current content, measured.** Phase C's headline feature is the
// future trajectory, and the database reports every feature joint's distance-from-body spread as
// **0.0000** on the retargeted corpus (ADR-553: the feet are welded to the pelvis) while the
// alien's own clips give 0.087-0.098. So this provider searches the alien's own 1,738 samples,
// where the legs articulate, and the corpus that travels is a search benchmark rather than
// playable content until positional retargeting exists.

#include "entity/motion_provider.hpp"
#include "scene/motion_database.hpp"

#include <string>
#include <vector>

namespace avgen::entity {

struct MatchSettings {
    // §27. How often to search, in seconds. Between searches the motion continues by following
    // `sampleNext`, which is an array read rather than a scan -- so the cost of motion matching is
    // this frequency times the scan, and not the scan every frame.
    float searchInterval = 0.1f;
    // §29. The minimum a newly chosen motion plays before another search may replace it. Without
    // it a character on a threshold re-selects every search and never commits to anything, which
    // is the same flicker `GaitSettings::minDwell` exists to stop one tier up.
    float minimumContinuation = 0.2f;
    // §28. A new candidate must beat the continuation by this much to be taken. Hysteresis in
    // cost, complementing the hysteresis in time above: they catch different failures, exactly as
    // the speed band and the dwell timer do in `Gait::select`.
    float switchMargin = 0.05f;
    scene::MotionCostWeights weights;
    // How strongly the request's desired velocity steers the search, against the pose term. This
    // is the one weight that is about intent rather than about the data, which is why it is a
    // setting and not part of `MotionFeatureConfig`.
    float intentWeight = 1.0f;
};

class MatchMotionProvider final : public IMotionProvider {
public:
    MatchMotionProvider() = default;
    MatchMotionProvider(const scene::MotionDatabase* db,
                        const std::vector<scene::AnimationClip>* clips, std::string name)
        : db_(db), clips_(clips), name_(std::move(name)) {}

    void setDatabase(const scene::MotionDatabase* db) { db_ = db; }
    void setClips(const std::vector<scene::AnimationClip>* clips) { clips_ = clips; }
    void setSettings(MatchSettings settings) { settings_ = settings; }
    [[nodiscard]] const MatchSettings& settings() const { return settings_; }

    [[nodiscard]] std::string_view name() const override { return name_; }

    [[nodiscard]] MotionResult advance(const MotionRequest& request, const MotionMemory& in,
                                       double time, float dt, MotionMemory& next) const override;
    [[nodiscard]] MotionResult pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                    scene::Pose& out) const override;

    // How many searches this provider has run, and how many samples it scored. **Counted by the
    // provider rather than inferred**, because "is the matcher actually searching, or has it been
    // following `sampleNext` since frame one" is the question a matcher that quietly stopped
    // working would otherwise answer with a perfectly smooth animation.
    struct Counters {
        std::uint64_t searches = 0;
        std::uint64_t scored = 0;
        std::uint64_t continued = 0;   // frames that followed the chain instead of searching
        std::uint64_t switches = 0;    // searches that actually changed the motion
        std::uint64_t heldByMargin = 0; // searches whose winner did not beat the margin
    };
    [[nodiscard]] const Counters& counters() const { return counters_; }
    void resetCounters() { counters_ = Counters{}; }

private:
    const scene::MotionDatabase* db_ = nullptr;
    const std::vector<scene::AnimationClip>* clips_ = nullptr;
    MatchSettings settings_;
    std::string name_ = "match";
    // Mutable because `IMotionProvider` is const by contract -- a provider holds no per-character
    // state, and these are diagnostics about the provider rather than about any one character.
    mutable Counters counters_;
};

} // namespace avgen::entity
