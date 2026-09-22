// Phase C §46: the evaluation harness, checked on the golden corpus before it is trusted anywhere.
//
// Three arms. With the ground-truth clip in the database the matcher can reproduce it, so every
// error must be near its floor: the harness's own control. With the clip left out, the rest of the
// corpus stands in and the errors rise: the harness can see a difference. And with the request's
// terms weighted to nothing, the velocity error rises further: the harness can see a worse
// matcher, not only a smaller corpus.

#include "entity/motion_evaluation.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

using namespace avgen;

TEST_CASE("§46 the evaluation harness: floor, left out, and a broken matcher", "[evaluation][phaseC]") {
    const scene::MotionPack pack = testsupport::goldenPack();
    const std::size_t walk = 1; // "Walk"
    REQUIRE(pack.clips[walk].name == "Walk");
    entity::MotionEvaluationOptions options;
    options.seconds = 0.9f;

    auto all = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(all.has_value());
    const entity::MotionEvaluation floor = entity::evaluateMatcher(pack, walk, *all, pack.animation, options);

    scene::MotionPack without = pack;
    without.clips.erase(without.clips.begin() + static_cast<std::ptrdiff_t>(walk));
    without.animation.erase(without.animation.begin() + static_cast<std::ptrdiff_t>(walk));
    auto rest = scene::buildMotionDatabase(without, testsupport::goldenOptions());
    REQUIRE(rest.has_value());
    const entity::MotionEvaluation leftOut = entity::evaluateMatcher(pack, walk, *rest, without.animation, options);

    scene::MotionDatabase blind = *all;
    blind.config.trajectoryPositionWeight = 0.0f;
    blind.config.rootVelocityWeight = 0.0f;
    blind.config.trajectoryFacingWeight = 0.0f;
    const entity::MotionEvaluation broken = entity::evaluateMatcher(pack, walk, blind, pack.animation, options);

    WARN(fmt::format("{}\n{}\n{}", floor.report(), leftOut.report(), broken.report()));
    REQUIRE(floor.steps > 40u);
    CHECK(floor.velocityError < 0.05f);
    CHECK(floor.trajectoryError < 0.05f);
    CHECK(leftOut.velocityError > floor.velocityError);
    CHECK(broken.velocityError > floor.velocityError + 0.1f);
}

#include "assets/gltf_loader.hpp"
#include "scene/scene.hpp"

#include <filesystem>

TEST_CASE("§46 on the scout: how well the rest of the corpus stands in for each locomotion clip",
          "[evaluation][aliens][phaseC]") {
    const std::filesystem::path glb = std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!std::filesystem::exists(glb)) {
        SKIP("the scout is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions load;
    load.loadImages = false;
    REQUIRE(assets::loadGltf(glb, sc, load).has_value());
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.license = "CC0-1.0";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    auto pack = scene::buildMotionPack("scout", sc.rigs.front().skeleton, sc.rigs.front().clips, provenance, packOptions);
    REQUIRE(pack.has_value());
    scene::MotionDatabaseOptions dbOptions;
    dbOptions.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    auto all = scene::buildMotionDatabase(*pack, dbOptions);
    REQUIRE(all.has_value());
    entity::MotionEvaluationOptions options;
    options.seconds = 2.0f;
    std::string report = "included (the floor), then left out:\n";
    int rose = 0;
    int clips = 0;
    for (std::size_t c = 0; c < pack->clips.size(); ++c) {
        const std::string& name = pack->clips[c].name;
        if (name.find("Walk") == std::string::npos && name.find("Run") == std::string::npos) {
            continue;
        }
        const entity::MotionEvaluation in = entity::evaluateMatcher(*pack, c, *all, pack->animation, options);
        scene::MotionPack without = *pack;
        without.clips.erase(without.clips.begin() + static_cast<std::ptrdiff_t>(c));
        without.animation.erase(without.animation.begin() + static_cast<std::ptrdiff_t>(c));
        auto rest = scene::buildMotionDatabase(without, dbOptions);
        REQUIRE(rest.has_value());
        const entity::MotionEvaluation out = entity::evaluateMatcher(*pack, c, *rest, without.animation, options);
        report += "  " + in.report() + "\n  " + out.report() + "\n";
        ++clips;
        rose += out.poseError > in.poseError ? 1 : 0;
    }
    WARN(report);
    REQUIRE(clips > 0);
    // **On the scout the "floor" is not a floor, and that is the finding.** With the clip in the
    // database, `Walking`, `Walking_crouch` and `Walking_injured` score exactly what they score
    // with it left out: the matcher never picks the clip whose velocity it was given, because a
    // request that carries only a velocity cannot tell one walk from another (they all move at
    // about 1–1.5 m/s in place). What the harness can show is that removing a clip that IS distinct
    // (`Running`, `Walking_low_grav`) makes it worse. That is asserted, and the rest is reported.
    CHECK(rose >= 2);
}
