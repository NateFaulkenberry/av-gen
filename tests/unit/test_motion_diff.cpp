// Phase C §83: diffing two motion databases, by what each sample is rather than where it sits.

#include "scene/motion_database_diff.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {

scene::MotionDatabase build(std::vector<scene::AnimationClip> clips,
                            scene::MotionDatabaseOptions options = testsupport::probeOptions()) {
    auto db = scene::buildMotionDatabase(testsupport::probePack(std::move(clips)), options);
    REQUIRE(db.has_value());
    return std::move(*db);
}

scene::AnimationClip walk() { return testsupport::probeGait("Walking", 0.30f, 1.2f); }
scene::AnimationClip run() { return testsupport::probeGait("Running", 0.75f, 3.0f); }
scene::AnimationClip jog() { return testsupport::probeGait("Jogging", 0.50f, 2.0f); }

} // namespace

TEST_CASE("§83: two builds of the same content are identical", "[motiondiff][phaseC]") {
    const auto d = scene::diffMotionDatabases(build({walk(), run()}), build({walk(), run()}));
    CHECK(d.identical);
    CHECK(d.report().find("identical") != std::string::npos);
}

TEST_CASE("§83: a clip inserted FIRST is one addition, not every later sample changed",
          "[motiondiff][phaseC]") {
    const scene::MotionDatabase a = build({walk(), run()});
    const scene::MotionDatabase b = build({jog(), walk(), run()});
    // The insertion shifted every index and renormalised every stored value; neither is a change.
    REQUIRE(a.sampleCount() == 62);
    REQUIRE(b.sampleCount() == 93);
    const auto d = scene::diffMotionDatabases(a, b);
    INFO(d.report());
    CHECK_FALSE(d.identical);
    CHECK(d.added == 31);
    CHECK(d.removed == 0);
    CHECK(d.changed == 0);
    CHECK(d.unchanged == 62);
    CHECK(d.clipsAdded == std::vector<std::string>{"Jogging"});
    CHECK_FALSE(d.schemaChanged);
    // And the reverse is one removal.
    const auto r = scene::diffMotionDatabases(b, a);
    CHECK(r.removed == 31);
    CHECK(r.clipsRemoved == std::vector<std::string>{"Jogging"});
}

TEST_CASE("§83: an edited clip is reported as changed, and where", "[motiondiff][phaseC]") {
    scene::AnimationClip edited = run();
    for (glm::vec4& v : edited.channels[1].values) {
        v.y += 0.05f; // the left foot lifted 5 cm through the whole run
    }
    const auto d = scene::diffMotionDatabases(build({walk(), run()}), build({walk(), edited}));
    INFO(d.report());
    CHECK(d.changed == 31);
    CHECK(d.unchanged == 31);
    CHECK(d.largestChange > 0.04f);
    CHECK(d.largestChangeAt.rfind("Running", 0) == 0);
    CHECK(d.sourceChanged);
}

TEST_CASE("§83: a schema change is reported, and values across it are not compared",
          "[motiondiff][phaseC]") {
    scene::MotionDatabaseOptions wide = testsupport::probeOptions();
    wide.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    wide.config.rootVelocityWeight = 2.0f;
    const auto d = scene::diffMotionDatabases(build({walk(), run()}), build({walk(), run()}, wide));
    INFO(d.report());
    CHECK(d.schemaChanged);
    CHECK(d.weightsChanged);
    CHECK(d.changed == 0);
    CHECK(d.unchanged == 62);
    CHECK(d.report().find("trajectoryTimes 0.2,0.4 -> 0.2,0.4,0.6") != std::string::npos);
    CHECK(d.report().find("rootVelocityWeight 1 -> 2") != std::string::npos);
}
