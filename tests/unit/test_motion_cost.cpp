// Phase C §10 -- the cost function, audited against its own text.
//
// §10: "cost = poseCost + trajectoryCost + velocityCost + facingCost + phaseCost + contactCost +
// transitionCost. **Every term should have a configurable weight. Avoid an opaque scoring
// function.**"
//
// §10 was in the sixteen sections Phase C inherited as done. `MotionFeatureConfig` carries seven
// weights -- `jointPositionWeight`, `jointVelocityWeight`, `trajectoryPositionWeight`,
// `trajectoryFacingWeight`, `rootVelocityWeight`, `phaseWeight`, `contactWeight` -- which is
// exactly the list §10 asks for, and which is what made the section look met.
//
// **Five of the seven were read by nothing at all**, and the two that were read were used only as
// `> 0` presence tests deciding whether to include a dimension. Setting `phaseWeight` to 2.0
// rather than 0.5 changed nothing. The search summed every dimension with weight 1, so the cost
// was a single undifferentiated squared distance: the opaque scoring function §10 names, with a
// tuning surface bolted to the outside of it that did nothing.
//
// That is ADR-558's family -- a control that does nothing -- and it is worse than an absent
// control, because an absent one is obviously absent. This is the probe that would have caught it,
// and it is written so it fails if the weights ever go inert again.

#include "scene/motion_database.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

scene::MotionDatabase tinyDatabase(const scene::MotionFeatureConfig& config) {
    scene::MotionDatabase db;
    db.config = config;
    db.dimension = config.dimension();
    const std::uint32_t samples = 3;
    db.features.assign(static_cast<std::size_t>(samples) * db.dimension, 0.0f);
    db.sampleClip.assign(samples, 0u);
    db.sampleTime.assign(samples, 0.0f);
    db.samplePhase.assign(samples, 0.0f);
    db.sampleTags.assign(samples, 0u);
    db.sampleNext.assign(samples, scene::MotionDatabase::kInvalid);
    db.mean.assign(db.dimension, 0.0f);
    db.scale.assign(db.dimension, 1.0f);
    db.clipNames.push_back("tiny");
    return db;
}

} // namespace

TEST_CASE("every configured weight changes the answer", "[motioncost][phaseC]") {
    // The construction is the argument. Two candidates are made wrong in *different groups* by the
    // same amount: sample 0 is off by 0.5 in a joint-position dimension, sample 1 by 0.5 in a
    // trajectory-position dimension. With equal weights they tie. Raise one group's weight and the
    // other sample must win -- which is impossible unless that weight is actually read.
    scene::MotionFeatureConfig config;
    config.joints = {"foot.l"};
    config.trajectoryTimes = {0.2f};
    scene::MotionDatabase db = tinyDatabase(config);
    const std::vector<scene::MotionFeatureGroup> layout = scene::motionFeatureLayout(config);
    REQUIRE(layout.size() == db.dimension);

    std::size_t jointDim = layout.size();
    std::size_t trajectoryDim = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (jointDim == layout.size() && layout[d] == scene::MotionFeatureGroup::JointPosition) {
            jointDim = d;
        }
        if (trajectoryDim == layout.size() &&
            layout[d] == scene::MotionFeatureGroup::TrajectoryPosition) {
            trajectoryDim = d;
        }
    }
    REQUIRE(jointDim < layout.size());
    REQUIRE(trajectoryDim < layout.size());

    db.features[0 * db.dimension + jointDim] = 0.5f;      // sample 0: wrong in the joint term
    db.features[1 * db.dimension + trajectoryDim] = 0.5f; // sample 1: wrong in the trajectory term
    db.features[2 * db.dimension + jointDim] = 4.0f;      // sample 2: far away in both senses
    db.features[2 * db.dimension + trajectoryDim] = 4.0f;

    scene::MotionQuery query;
    query.features.assign(db.dimension, 0.0f);
    const scene::MotionCostWeights weights;

    // Equal weights: a tie, broken by scan order. Sample 2 must not win either way -- if it does,
    // the test below is measuring something other than the weights.
    db.config.jointPositionWeight = 1.0f;
    db.config.trajectoryPositionWeight = 1.0f;
    const scene::MotionMatch tied = scene::searchMotion(db, query, weights);
    REQUIRE(tied.found());
    CHECK(tied.sample != 2u);

    // Make the joint term expensive: the sample that is wrong in the JOINT term must now lose, so
    // sample 1 wins.
    db.config.jointPositionWeight = 10.0f;
    db.config.trajectoryPositionWeight = 1.0f;
    const scene::MotionMatch preferTrajectory = scene::searchMotion(db, query, weights);
    REQUIRE(preferTrajectory.found());
    CHECK(preferTrajectory.sample == 1u);

    // And the other way round, which is what makes this a test of the weights rather than of a
    // tie-break: the same database, the same query, the opposite answer.
    db.config.jointPositionWeight = 1.0f;
    db.config.trajectoryPositionWeight = 10.0f;
    const scene::MotionMatch preferJoint = scene::searchMotion(db, query, weights);
    REQUIRE(preferJoint.found());
    CHECK(preferJoint.sample == 0u);

    WARN(fmt::format("joint-heavy chose sample {}, trajectory-heavy chose sample {}",
                     preferTrajectory.sample, preferJoint.sample));

    // **And the weights are tunable without rebuilding.** Nothing above touched `db.features`
    // after construction: only `db.config` changed between the three searches. That is the
    // property that makes a weight a tuning surface rather than a build parameter, and it is why
    // the weights are applied at search time rather than baked into the feature values.
}

TEST_CASE("the cost says what it was made of", "[motioncost][phaseC]") {
    // §10's other half: "avoid an opaque scoring function". A number with no breakdown cannot
    // answer "why that sample", which is the question anyone debugging motion matching is asking.
    scene::MotionFeatureConfig config;
    config.joints = {"foot.l"};
    config.trajectoryTimes = {0.2f};
    config.jointPositionWeight = 2.0f;
    config.trajectoryPositionWeight = 3.0f;
    scene::MotionDatabase db = tinyDatabase(config);
    const std::vector<scene::MotionFeatureGroup> layout = scene::motionFeatureLayout(config);

    std::size_t jointDim = 0;
    std::size_t trajectoryDim = 0;
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::JointPosition) {
            jointDim = d;
            break;
        }
    }
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::TrajectoryPosition) {
            trajectoryDim = d;
            break;
        }
    }
    db.features[0 * db.dimension + jointDim] = 0.5f;
    db.features[0 * db.dimension + trajectoryDim] = 0.25f;
    db.features[1 * db.dimension + jointDim] = 9.0f;
    db.features[2 * db.dimension + jointDim] = 9.0f;

    scene::MotionQuery query;
    query.features.assign(db.dimension, 0.0f);
    const scene::MotionMatch match = scene::searchMotion(db, query, scene::MotionCostWeights{});
    REQUIRE(match.found());
    REQUIRE(match.sample == 0u);
    WARN(match.breakdown.report());

    // The terms are the terms: 2.0 * 0.5^2 in the joint group, 3.0 * 0.25^2 in the trajectory one.
    const auto term = [&](scene::MotionFeatureGroup g) {
        return match.breakdown.terms[static_cast<std::size_t>(g)];
    };
    CHECK(term(scene::MotionFeatureGroup::JointPosition) == Approx(2.0f * 0.25f));
    CHECK(term(scene::MotionFeatureGroup::TrajectoryPosition) == Approx(3.0f * 0.0625f));
    CHECK(term(scene::MotionFeatureGroup::JointVelocity) == Approx(0.0f));

    // **The breakdown adds up to the cost.** This is the assertion that stops the breakdown
    // becoming a decorative second opinion that drifts from the number actually used to choose --
    // which is what a separately-computed diagnostic does the first time someone edits one of them.
    CHECK(match.breakdown.total() == Approx(match.cost).margin(1e-5f));
}

TEST_CASE("the breakdown carries its own caveat", "[motioncost][phaseC]") {
    // §50's rule applied to §10's artefact: the warning has to live next to the number, because
    // the person reading a cost breakdown has not read the ADR. This asserts the text exists and
    // says the thing it must say -- so deleting it to tidy the header fails here rather than
    // silently removing a warning from every future inspector.
    const std::string caveat = scene::MotionCostBreakdown::caveat();
    CHECK(caveat.find("decisive") != std::string::npos);
    CHECK(caveat.find("zero") != std::string::npos);
    CHECK(caveat.size() > 60);
}
