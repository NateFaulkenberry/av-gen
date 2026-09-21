// Phase C §57: the database inspector reports what is in a database, measured from its arrays.

#include "scene/motion_database_inspect.hpp"
#include "scene/motion_database_io.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("§57: the inspector's figures are the database's own", "[motioninspect][phaseC]") {
    scene::MotionPack pack = testsupport::probePack();
    // A known contact: the left foot planted for the first half of the walking clip only.
    scene::ContactTrack track;
    track.joint = "foot.l";
    track.spans.push_back(scene::ContactSpan{0.0f, 0.51f, 1.0f});
    pack.clips[0].contacts.push_back(track);
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());

    const scene::MotionDatabaseInspection in = scene::inspectMotionDatabase(*db, &pack);
    INFO(in.report());
    CHECK(in.clips == 2);
    CHECK(in.samples == 62);
    CHECK(in.dimension == 23);
    using G = scene::MotionFeatureGroup;
    CHECK(in.dimensionsByGroup[static_cast<std::size_t>(G::JointPosition)] == 6);
    CHECK(in.dimensionsByGroup[static_cast<std::size_t>(G::JointVelocity)] == 6);
    CHECK(in.dimensionsByGroup[static_cast<std::size_t>(G::TrajectoryPosition)] == 4);
    CHECK(in.dimensionsByGroup[static_cast<std::size_t>(G::TrajectoryFacing)] == 4);
    CHECK(in.dimensionsByGroup[static_cast<std::size_t>(G::RootVelocity)] == 3);
    CHECK(in.trajectoryHorizons == std::vector<float>{0.2f, 0.4f});
    CHECK(in.featureBytes == 62u * 23u * sizeof(float));
    CHECK(in.metadataBytes == 62u * 20u);
    // 16 of 31 walking samples (t = 0 .. 0.5 s, the span ending just past it) of 62 in all.
    REQUIRE(in.contactJoints == std::vector<std::string>{"foot.l"});
    CHECK(in.contactPlanted[0] == 16.0f / 62.0f);
    CHECK(in.build.buildKey == db->build.buildKey);
    CHECK(in.identity == db->identity);
    REQUIRE(in.provenance.size() == 1);
    CHECK(in.provenance[0].find("CC0-1.0") != std::string::npos);
    CHECK(in.searchStructure.find("linear scan over 62 samples") != std::string::npos);
    // The coverage is the §58 analyser's, not a second derivation.
    CHECK(in.categories.at(scene::MotionCategory::Walk).grade ==
          scene::measureMotionCategories(*db).at(scene::MotionCategory::Walk).grade);

    // Without a pack it says what it cannot report rather than inventing it.
    const scene::MotionDatabaseInspection bare = scene::inspectMotionDatabase(*db);
    CHECK(bare.contactJoints.empty());
    CHECK(bare.report().find("no pack given") != std::string::npos);
}

TEST_CASE("§57: the inspector on the real Glowmere database", "[motioninspect][phaseC][aliens]") {
    auto glowmere = testsupport::glowmereMotion();
    if (!glowmere) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::MotionDatabaseInspection in = scene::inspectMotionDatabase(glowmere->db, &glowmere->pack);
    WARN(in.report());
    REQUIRE(in.samples == glowmere->db.sampleCount());
    REQUIRE(in.clips == 26);
    REQUIRE(in.contactJoints.size() == 2);
    for (const float f : in.contactPlanted) {
        CHECK(f > 0.05f);
        CHECK(f < 0.95f);
    }
    CHECK(in.phasedSamples > 0);
    std::uint32_t histogram = 0;
    for (const std::uint32_t n : in.phaseHistogram) {
        histogram += n;
    }
    CHECK(histogram == in.phasedSamples);
}
