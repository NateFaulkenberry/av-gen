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

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace avgen::entity {

struct MatchSettings {
    // The default `minimumContinuation` and the derived halflife below are both stated against
    // this, so changing one cannot leave the other behind.
    static constexpr float kDefaultContinuation = 0.2f;
    // §27. How often to search, in seconds. Between searches the motion continues by following
    // `sampleNext`, which is an array read rather than a scan -- so the cost of motion matching is
    // this frequency times the scan, and not the scan every frame.
    // **Inert whenever it is below `minimumContinuation`**, which at the defaults it is: a search
    // needs `sinceSearch >= searchInterval` AND `sinceSearch >= minimumContinuation`, so the later
    // of the two decides and this one cannot affect anything below 0.2 s. `shadowed()` says so, so
    // the relationship is legible at the value rather than only in a benchmark table.
    float searchInterval = 0.1f;
    // §29. The minimum a newly chosen motion plays before another search may replace it. Without
    // it a character on a threshold re-selects every search and never commits to anything, which
    // is the same flicker `GaitSettings::minDwell` exists to stop one tier up.
    float minimumContinuation = kDefaultContinuation;
    // §28. A new candidate must beat the continuation by this much to be taken. Hysteresis in
    // cost, complementing the hysteresis in time above: they catch different failures, exactly as
    // the speed band and the dwell timer do in `Gait::select`.
    // **A fraction of the database's measured cost spread, not raw cost units.**
    //
    // This was 0.05 in raw units, and the §28 benchmark showed it held *nothing*: `heldByMargin`
    // was 0 at 0.00, 0.05 and 0.50, and only a margin of 100 held anything. Set beside the measured
    // spread -- a typical candidate scores **69.72** worse than the best -- that is not a value a
    // little too low. **It is a units mismatch**: 0.05 against 69.72 is four parts in ten thousand
    // and was never going to hold, while the value that does hold is larger than the entire spread
    // and so holds indiscriminately. No default in raw units could have been right, because the
    // sensible range depends on a scale nobody had measured.
    //
    // That is the fog bank's density defect exactly -- a quantity expressed per-metre when its
    // meaning depends on a size -- and the fix is the one this phase has now derived three times:
    // express it against a scale that is a property of the data, so it lands in the right range by
    // construction and tracks the weights instead of going stale (ADR-389).
    //
    // **Changing the unit makes the default meaningful, not correct.** What its value *should* be
    // still needs a motion-quality metric this phase does not have, and 0.05 here now means "five
    // percent of the good-to-typical gap", which is a number an artist can reason about.
    float switchMargin = 0.05f;
    scene::MotionCostWeights weights;
    // How strongly the request's desired velocity steers the search, against the pose term. This
    // is the one weight that is about intent rather than about the data, which is why it is a
    // setting and not part of `MotionFeatureConfig`.
    float intentWeight = 1.0f;

    // ---- §32: the inertialized transition (ADR-613) -------------------------------------------
    //
    // Seconds for the pose offset a switch introduces to halve. **Zero means no blend**, which is
    // what this provider did for the whole of Phase B and what ADR-612 measured at 0.3566 m of
    // foot teleport per transition.
    //
    // **The default is derived, not chosen**: `derivedInertializeHalflife` balances a decay fast
    // enough to move the foot against one slow enough to still be running when the next switch
    // arrives, and the soonest that can happen here is `minimumContinuation`, because the lock
    // makes searches at least that far apart.
    float inertializeHalflife =
        derivedInertializeHalflife(kDefaultContinuation, kBlendBudgetFrameSeconds);

    // How many interrupted transitions are kept decaying underneath the current one. 1 is the
    // shape `AnimationPlayer` has (ADR-547 keeps one `previous_` state); above 1 is what this seam
    // can afford that a player holding whole poses cannot, because what is kept here is two
    // integers, two times and a clock. **Measured**: the second slot halves the mean at a
    // transition, the third buys 10% more (ADR-613). Clamped to `MotionMemory::kBlendSlots`.
    std::size_t blendSlots = MotionMemory::kBlendSlots;

    // True when `searchInterval` cannot change the behaviour because `minimumContinuation` is the
    // later gate. Not an error -- it is a legitimate configuration -- but it is the difference
    // between "search frequency does not matter" and "this dial is not the one in control", and a
    // person tuning the first will draw the wrong conclusion without being told.
    [[nodiscard]] bool searchIntervalShadowed() const {
        return searchInterval < minimumContinuation;
    }
};

class MatchMotionProvider final : public IMotionProvider {
public:
    MatchMotionProvider() = default;
    MatchMotionProvider(const scene::MotionDatabase* db,
                        const std::vector<scene::AnimationClip>* clips, std::string name)
        : db_(db), clips_(clips), name_(std::move(name)) {}

    void setDatabase(const scene::MotionDatabase* db) { db_ = db; }
    // Phase C §4 requires the database be "shared between character instances", and §6's
    // measurement is why that clause is load-bearing rather than tidy: at a million samples the
    // database is 144.96 MB, so a hundred characters each holding one is 14.5 GB. Exposed so a
    // test can assert the sharing rather than trust that a pointer stays a pointer -- ADR-604 is
    // the same mistake one tier up, where 78% of a scene's rig memory is a second copy.
    [[nodiscard]] const scene::MotionDatabase* database() const { return db_; }
    void setClips(const std::vector<scene::AnimationClip>* clips) { clips_ = clips; }
    void setSettings(MatchSettings settings) { settings_ = settings; }
    [[nodiscard]] const MatchSettings& settings() const { return settings_; }

    [[nodiscard]] std::string_view name() const override { return name_; }

    [[nodiscard]] MotionResult advance(const MotionRequest& request, const MotionMemory& in,
                                       double time, float dt, MotionMemory& next) const override;
    [[nodiscard]] MotionResult pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                    scene::Pose& out) const override;

    // Phase C §68/§69: **the query `advance` would search with**, for `request` from `in`, so a
    // diagnostic explains the decision the provider actually makes rather than one built beside it.
    // The same code path as `advance` (`fillQuery`), not a copy of it. Empty when there is no
    // database. Allocates -- it is for tools and debugging, not the matching loop.
    [[nodiscard]] std::optional<scene::MotionQuery> queryFor(const MotionRequest& request,
                                                             const MotionMemory& in) const;

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
    // A snapshot, by value: the live counters are atomic (below).
    [[nodiscard]] Counters counters() const {
        return Counters{counters_.searches.load(std::memory_order_relaxed),
                        counters_.scored.load(std::memory_order_relaxed),
                        counters_.continued.load(std::memory_order_relaxed),
                        counters_.switches.load(std::memory_order_relaxed),
                        counters_.heldByMargin.load(std::memory_order_relaxed)};
    }
    void resetCounters() { counters_.reset(); }

private:
    void fillQuery(const MotionRequest& request, std::uint32_t current, scene::MotionQuery& query,
                   std::vector<float>& raw) const;

    const scene::MotionDatabase* db_ = nullptr;
    const std::vector<scene::AnimationClip>* clips_ = nullptr;
    MatchSettings settings_;
    std::string name_ = "match";
    // Mutable because `IMotionProvider` is const by contract -- a provider holds no per-character
    // state, and these are diagnostics about the provider rather than about any one character.
    //
    // **Atomic, because one provider serves every character** (§74). These were plain integers
    // incremented from `advance`, which is a data race the moment two characters are advanced on
    // two threads -- the only mutable state in an otherwise shareable object. Relaxed: they are
    // counts, nothing is ordered by them, and a relaxed increment is one uncontended instruction.
    struct RelaxedCount {
        std::atomic<std::uint64_t> value{0};
        RelaxedCount() = default;
        RelaxedCount(const RelaxedCount& other) : value(other.value.load(std::memory_order_relaxed)) {}
        RelaxedCount& operator=(const RelaxedCount& other) {
            value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
        void operator++() { value.fetch_add(1, std::memory_order_relaxed); }
        void operator+=(std::uint64_t n) { value.fetch_add(n, std::memory_order_relaxed); }
        [[nodiscard]] std::uint64_t load(std::memory_order order) const { return value.load(order); }
    };
    struct LiveCounters {
        RelaxedCount searches, scored, continued, switches, heldByMargin;
        void reset() { *this = LiveCounters{}; }
    };
    mutable LiveCounters counters_;
};

} // namespace avgen::entity
