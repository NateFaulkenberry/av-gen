#pragma once

// The motion database (Phase C §4-§17): what a motion matcher searches.
//
// **Built offline from a `MotionPack`, searched at runtime, and the two are deliberately different
// shapes.** A pack stores clips so they can be played; a database stores *features* so they can be
// compared. Keeping one structure for both would mean either a pack that carries search weights
// nobody playing it needs, or a search that walks an array of clips to answer one query.
//
// **Memory design (§6), stated before the code.** The atomic unit is a sample -- one frame of one
// clip -- and a corpus has millions. So there is no `MotionSample` object: there are parallel
// arrays indexed by sample, and one contiguous `features` array of `dimension * count` floats. A
// query touches the feature array linearly and nothing else, which is the access pattern a linear
// scan is fast at and an array of objects is not.
//
// Measured, per sample, for the 12-dimension default config: **48 bytes of feature** plus **14
// bytes of metadata** = 62 B. A million samples is 62 MB; 100STYLE's 4.78 M frames would be
// 296 MB. Those numbers are why the features are floats in one block rather than a struct per
// frame.
//
// **What the current corpus cannot support, said out loud.** Motion matching's headline feature is
// the *future trajectory*: where the body will be in 0.2, 0.4 and 0.6 seconds. ADR-540 established
// that every locomotion clip in this repository is authored in place, so that trajectory is
// **identically zero** on all of it, and ADR-553 established that the corpus which does travel
// cannot be posed on the Glowmere alien. The trajectory machinery is built because it is correct
// and because 100STYLE exercises it, and the report says plainly where it is degenerate rather
// than letting a weight of zero look like a tuning choice.

#include "scene/motion_analysis.hpp"
#include "scene/motion_pack.hpp"
#include "scene/skeleton.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

// What a clip is, for filtering (§13/§14). A bitmask because a clip is several of these at once --
// a walk is locomotion AND cyclic -- and because a filter is then one AND instruction per sample.
//
// **Tags assist filtering; they do not replace feature matching** (§13). Nothing here is allowed to
// pick a sample. They only remove candidates that cannot be right at all.
enum class MotionTag : std::uint32_t {
    None = 0,
    Locomotion = 1u << 0,
    Idle = 1u << 1,
    Walk = 1u << 2,
    Run = 1u << 3,
    Turn = 1u << 4,
    Airborne = 1u << 5,
    Cyclic = 1u << 6,   // the clip loops and has a phase
    Travelling = 1u << 7, // ADR-552: its root leaves a box the size of its body
    OneShot = 1u << 8,
};
[[nodiscard]] std::uint32_t motionTagsFor(const PackClip& clip, const ClipAnalysis& analysis);
[[nodiscard]] std::string motionTagNames(std::uint32_t tags);

// Which features to extract and how much each matters (§8). **Data-driven on purpose**: §8 forbids
// hardcoding `leftFoot`/`rightFoot`/`pelvis` into the search, because which joints carry a
// character's identity is a property of the character. The alien's feet are `foot.l`/`foot.r`; a
// bull's are `HoofB.L`.
struct MotionFeatureConfig {
    // The joints whose position and velocity enter the feature vector, in the body's own frame.
    // **Not every joint** (§7): a 90-joint alien would give a 540-dimension vector in which the
    // fingers outvote the feet.
    std::vector<std::string> joints;
    // Seconds ahead to sample the future trajectory. Empty means no trajectory term, which is the
    // honest configuration for an in-place corpus.
    std::vector<float> trajectoryTimes;

    // ---- weights (§10) ---------------------------------------------------------------------
    // Every term is weighted and every weight is named. §10: avoid an opaque scoring function.
    float jointPositionWeight = 1.0f;
    float jointVelocityWeight = 0.4f;
    float trajectoryPositionWeight = 1.0f;
    float trajectoryFacingWeight = 0.5f;
    float rootVelocityWeight = 1.0f;
    float phaseWeight = 0.0f;   // 0 by default: a phase term on non-cyclic content is noise
    float contactWeight = 0.0f;

    [[nodiscard]] std::uint32_t dimension() const;
    friend bool operator==(const MotionFeatureConfig&, const MotionFeatureConfig&) = default;
};

// Phase C §10: which term of the cost each feature dimension belongs to.
//
// §10 requires the cost be `poseCost + trajectoryCost + velocityCost + facingCost + phaseCost +
// contactCost + transitionCost`, that **every term have a configurable weight**, and that the
// scoring function not be opaque. Before this existed, none of that was true: five of the seven
// weights in `MotionFeatureConfig` were read by nothing at all, and the two that were read
// (`phaseWeight`, `contactWeight`) were used only as `> 0` presence tests deciding whether to
// *include* the dimension. Setting one to 2.0 rather than 0.5 changed nothing. They were controls
// that did nothing (ADR-558) wearing the costume of a tuning surface.
enum class MotionFeatureGroup : std::uint8_t {
    JointPosition,
    JointVelocity,
    TrajectoryPosition,
    TrajectoryFacing,
    RootVelocity,
    Phase,
    Contact,
    Count,
};
[[nodiscard]] const char* motionFeatureGroupName(MotionFeatureGroup group);

// The group of every dimension, in the order `buildMotionDatabase` writes them. Derived from the
// config rather than stored per sample: it is a fact about the layout, the same for all million
// samples, and storing it per sample would be 1 MB of the same byte repeated.
[[nodiscard]] std::vector<MotionFeatureGroup> motionFeatureLayout(const MotionFeatureConfig& config);

// The weight of each dimension, so a search multiplies rather than consults. Recomputed from the
// config, which is what makes a weight **tunable without rebuilding the database**: the features
// are unchanged, only what they are multiplied by.
[[nodiscard]] std::vector<float> motionFeatureWeights(const MotionFeatureConfig& config);

// §10's explicit breakdown, for the sample a search chose. This is the half that makes the scoring
// function not opaque: "why that sample" has an answer with numbers in it.
struct MotionCostBreakdown {
    float terms[static_cast<std::size_t>(MotionFeatureGroup::Count)] = {};
    float continuity = 0.0f;
    float transition = 0.0f;
    [[nodiscard]] float total() const {
        float sum = continuity + transition;
        for (const float t : terms) {
            sum += t;
        }
        return sum;
    }
    [[nodiscard]] std::string report() const;

    // **Any UI that shows this must show this sentence beside it.** A breakdown answers "what did
    // this cost", not "what changed the decision", and the two differ whenever a term did its work
    // on a candidate that lost. §12's transition penalty is the worked example: with the penalty
    // off the idle wins, with it on the walk wins -- and the winner's `transition` reads **zero**,
    // because the winner never left the motion family and so never paid it. A term can be decisive
    // and read as zero.
    //
    // Provided as a function rather than left in this comment because the person reading a cost
    // breakdown at two in the morning has not read ADR-611, and the place the warning has to be is
    // next to the number.
    [[nodiscard]] static const char* caveat() {
        return "A cost breakdown says what the CHOSEN sample paid, not what decided the match: a "
               "term that reads zero may have been decisive by making another candidate expensive.";
    }
};

// The default for a biped: the two feet and the head, which is what the literature converges on
// and what this rig can actually supply.
[[nodiscard]] MotionFeatureConfig defaultBipedConfig(std::string leftFoot, std::string rightFoot,
                                                     std::string head);

struct MotionDatabaseStats {
    std::uint32_t clips = 0;
    std::uint32_t samples = 0;
    std::uint32_t dimension = 0;
    std::size_t featureBytes = 0;
    std::size_t metadataBytes = 0;
    [[nodiscard]] std::size_t totalBytes() const { return featureBytes + metadataBytes; }
    // How many feature dimensions were **identically constant** across the whole database. A
    // dimension that never varies contributes nothing to any comparison and costs a multiply on
    // every sample of every query -- and on in-place content the whole trajectory block is exactly
    // that. Reported rather than silently carried, because a zero-variance column is the same
    // shape of defect as everything else this project has been catching.
    std::uint32_t deadDimensions = 0;
    // The measured gap between a good match and a typical one, in the cost function's own units.
    // **This is the scale every cost threshold has to be expressed against**, and it is measured
    // at build time rather than assumed because it is a property of the corpus and the weights
    // together -- change either and it moves (ADR-389). §16 uses it as the denominator for search
    // severity; §28's switch margin is a fraction of it.
    float costSpread = 0.0f;
    // **Per feature joint, the spread of its DISTANCE from the body, in model units.**
    //
    // A dead-dimension count is necessary and not sufficient, and this is the statistic that says
    // so. The retargeted 100STYLE pack's foot features vary from frame to frame -- nothing in them
    // is constant -- but ADR-553 established the foot is welded to the pelvis and only the body
    // rotates. A rotating rigid offset varies in x and z while its *length* does not move at all.
    //
    // So: a limb that articulates has a spread here, and a limb that is along for the ride has
    // none, however busy its raw coordinates look. Measured, the alien's own clips give 0.05-0.12
    // and the retargeted corpus gives ~0.000.
    std::vector<float> jointRadiusSpread;
    std::vector<std::string> jointNames;
    std::string report() const;
};

struct MotionDatabase {
    static constexpr std::uint32_t kVersion = 1;

    std::uint32_t version = kVersion;
    std::string name;
    // ADR-550's digest. A database searched against a rig it was not built for is the
    // silent-wrong-character failure, and this is what makes it loud.
    std::string skeletonDigest;
    MotionFeatureConfig config;
    std::uint32_t dimension = 0;

    // ---- per sample, parallel arrays (§5/§6) ------------------------------------------------
    // `features` is `dimension * sampleCount`, sample-major, so one sample's vector is contiguous.
    std::vector<float> features;
    std::vector<std::uint32_t> sampleClip;  // index into the pack's clips
    std::vector<float> sampleTime;          // seconds into that clip
    std::vector<float> samplePhase;         // 0..1, or 0 when the clip has none
    std::vector<std::uint32_t> sampleTags;  // copied from the clip, so filtering touches one array
    // The sample that follows this one in its own clip, or kInvalid at a clip's end. **This is
    // what makes continuation cheap** (§29): playing on is following this index, not searching.
    std::vector<std::uint32_t> sampleNext;

    // ---- normalization (§9) -------------------------------------------------------------------
    // Per dimension, so position in metres and velocity in m/s are comparable. Standardised to
    // zero mean and unit standard deviation -- the convention is stated here because §9 asks for
    // it: a dimension's *spread in this database* is what makes it comparable to another's, and a
    // hand-authored scale per unit would be a second set of weights fighting the first.
    std::vector<float> mean;
    std::vector<float> scale; // 1/stddev, or 1 for a dead dimension

    std::vector<std::string> clipNames;
    MotionDatabaseStats stats;

    [[nodiscard]] std::uint32_t sampleCount() const {
        return static_cast<std::uint32_t>(sampleClip.size());
    }
    [[nodiscard]] const float* featuresFor(std::uint32_t sample) const {
        return features.data() + (static_cast<std::size_t>(sample) * dimension);
    }
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
};

struct MotionDatabaseOptions {
    float sampleRate = 30.0f;   // samples per second of clip
    MotionFeatureConfig config;
};

// Build from a pack. Offline: this walks every frame of every clip and poses the skeleton.
[[nodiscard]] Result<MotionDatabase> buildMotionDatabase(const MotionPack& pack,
                                                         const MotionDatabaseOptions& options);

// ---- the query ---------------------------------------------------------------------------------

struct MotionQuery {
    // The feature vector to match, **already in the body's own frame and unnormalised**. Built by
    // the caller from the character's current state; `normaliseQuery` applies the database's own
    // mean and scale.
    std::vector<float> features;
    // Only samples carrying every bit here are considered (§14). Zero means no filter.
    std::uint32_t requireTags = 0;
    // Samples carrying any bit here are rejected.
    std::uint32_t rejectTags = 0;
    // Where the character is now, so continuity and transition costs have something to be relative
    // to (§11/§12). kInvalid on the first query of a character's life.
    std::uint32_t current = MotionDatabase::kInvalid;
};

struct MotionCostWeights {
    // §11. How much a candidate is penalised for not being the continuation of what is playing.
    // **The most important term beyond a naive nearest neighbour**: without it the search hops
    // between unrelated clips whenever two frames happen to rhyme.
    float continuity = 0.35f;
    // §12. An extra penalty for crossing to a different motion family, over and above continuity.
    // Keyed on tags, never on clip names (§12 is explicit about that).
    float transition = 0.25f;
    // Phase C §11. The flat `continuity` above is **binary**: the one sample that follows the
    // current one pays nothing and everything else pays in full, so a sample two frames later in
    // the same clip is penalised exactly as hard as one from an unrelated clip. §11 names six
    // possible inputs -- current sample, previous sample, source clip, phase, root velocity,
    // transition distance -- and a next-or-not flag uses one of them.
    //
    // This grades the penalty for a candidate **in the same clip** by its transition distance in
    // seconds: skipping a few frames inside a clip costs a little, leaving the clip costs
    // `continuity`.
    //
    // **The default is 0 -- binary -- and that is a measured result, not an unfinished knob.**
    // Graded continuity was implemented, run against the binary version on real Glowmere motion,
    // and **lost**: identical jump rate (0.50/s at every rate from 0.5 to 4.0), and the loop
    // stalled on 13% of steps against the binary's 0%. The reason is structural and is the useful
    // part: a penalty proportional to the distance from the current sample is **zero for the
    // current sample itself**, so standing still is free, and the matcher freezes rather than
    // continues. §11 asks for coherent continuation and a freeze is not continuation.
    //
    // Kept rather than deleted because the negative result is worth more than the absence: a
    // future reader proposing distance-graded continuity can see it was tried, how it was
    // measured, and exactly why it fails. Any working version must penalise *not advancing*, which
    // means the term needs the previous sample as well as the current one -- another of §11's six
    // named inputs, and the direction a second attempt should take. See ADR-610.
    float continuityPerSecond = 0.0f;
};

struct MotionMatch {
    std::uint32_t sample = MotionDatabase::kInvalid;
    float cost = 0.0f;
    MotionCostBreakdown breakdown; // §10: what the cost was made of, for the winner
    std::uint32_t considered = 0; // how many survived filtering and were scored
    std::uint32_t rejected = 0;   // how many the tag filter removed before scoring
    // §16: how many the cheap stage looked at, and how many reached the full cost. Equal to
    // `considered` and to each other for an exhaustive plan.
    std::uint32_t coarseConsidered = 0;
    std::uint32_t fullyScored = 0;
    [[nodiscard]] bool found() const { return sample != MotionDatabase::kInvalid; }
};

// Standardise a raw feature vector in place, using this database's own mean and scale.
void normaliseQuery(const MotionDatabase& db, std::vector<float>& features);

// Phase C §16: candidate filtering -> cheap feature search -> top N -> full cost -> best.
//
// **Read the disclaimer before the implementation.** On this repository's actual content a linear
// scan is comfortably correct: the real Glowmere database is 1,738 samples and a full search costs
// 8.88 us at best, 9.25 us on average and 17.46 us at worst -- **955 characters per frame at
// 60 Hz on the worst case.** Nothing here needs a two-stage search.
//
// What needs it is the scale §6 and §17 ask about. At a million samples one query is **36.5 ms**,
// more than two whole frames, for one character, and the scan is honestly linear (101x for 100x
// the samples). So this exists for a database two orders of magnitude larger than any in the tree,
// and a future reader finding it here should NOT conclude the linear scan was inadequate. It was
// not. The numbers above are the whole of the justification and the whole of the disclaimer, and
// they are stated together on purpose: an optimisation defended by a benchmark that never needed
// it becomes permanent without ever having been justified.
//
// `stride` is the cheap stage. It scores every `stride`-th sample on a **prefix** of the feature
// vector, keeps the best `shortlist`, then evaluates the full weighted cost -- including
// continuity and transition -- on those and on each shortlisted sample's neighbours, so a sample
// the coarse pass stepped over can still win.
struct MotionSearchPlan {
    std::uint32_t stride = 1;      // 1 = exhaustive, which is the linear scan exactly
    std::uint32_t shortlist = 32;  // how many survive the cheap stage
    std::uint32_t prefixDimensions = 0; // 0 = all of them
    // The neighbourhood re-expanded around each shortlisted sample, so striding cannot permanently
    // hide the true best: with `stride` 8 and `neighbourhood` 8 every sample is reachable.
    std::uint32_t neighbourhood = 0;
    [[nodiscard]] bool exhaustive() const { return stride <= 1u; }
};

// The two-stage search. With a default-constructed plan this is `searchMotion` exactly -- same
// samples considered, same answer -- which is what makes the plan a tuning surface rather than a
// second code path that has to be kept in step.
[[nodiscard]] MotionMatch searchMotionStaged(const MotionDatabase& db, const MotionQuery& query,
                                             const MotionCostWeights& weights,
                                             const MotionSearchPlan& plan);

// Linear scan (§15's baseline). Benchmarked against real motion distributions rather than a
// synthetic fixture, for the reason ADR-540 paid to learn: a white-noise fixture made an early-out
// look 2.23x *slower* when on real, near queries it is a 0.80x win. The fixture was wrong, not the
// measurement.
[[nodiscard]] MotionMatch searchMotion(const MotionDatabase& db, const MotionQuery& query,
                                       const MotionCostWeights& weights);

} // namespace avgen::scene
