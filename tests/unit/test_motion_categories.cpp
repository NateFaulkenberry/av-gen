// Phase C §58: the coverage report in words -- "Walk: good coverage / Reverse locomotion: poor" --
// measured from the data against thresholds the report prints.
//
// The synthetic packs here are built so the right answer is known in advance for every category:
// a clip that walks forward, one that backs up, one that stands and then sets off, one that curves
// left. A classifier that confused any of them fails a specific assertion.

#include "scene/motion_coverage.hpp"
#include "scene/motion_database.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using namespace avgen;

namespace {

// A clip whose root follows `path(t)` for `seconds`, with the feet swinging, so it has a real pose
// and a real root velocity.
scene::AnimationClip pathClip(std::string name, float seconds,
                              const std::function<glm::vec3(float)>& path) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    const int frames = static_cast<int>(std::lround(seconds * 30.0f)) + 1;
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    scene::AnimationChannel left = root;
    left.joint = 1;
    scene::AnimationChannel right = root;
    right.joint = 2;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        const glm::vec3 p = path(t);
        const float swing = 0.3f * std::sin(6.283185307179586f * t);
        root.times.push_back(t);
        root.values.emplace_back(p, 0.0f);
        left.times.push_back(t);
        left.values.emplace_back(p.x, 0.1f, p.z + swing, 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(p.x, 0.1f, p.z - swing, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = static_cast<float>(frames - 1) / 30.0f;
    clip.channels = {std::move(root), std::move(left), std::move(right)};
    return clip;
}

scene::AnimationClip straight(std::string name, float seconds, float speed) {
    return pathClip(std::move(name), seconds, [speed](float t) { return glm::vec3(0.0f, 0.0f, speed * t); });
}

scene::MotionCategoryReport measure(std::vector<scene::AnimationClip> clips) {
    const scene::MotionPack pack = testsupport::probePack(std::move(clips));
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());
    REQUIRE(db->sampleCount() > 0);
    return scene::measureMotionCategories(*db);
}

using scene::CoverageGrade;
using scene::MotionCategory;

} // namespace

TEST_CASE("§58: each category is graded from what the body does, against printed thresholds",
          "[motioncoverage][phaseC]") {
    // Two walks of three seconds (1.2 and 1.0 m/s) and one second of running at 3 m/s.
    const scene::MotionCategoryReport r = measure({straight("stroll", 3.0f, 1.2f),
                                                   straight("amble", 3.0f, 1.0f),
                                                   straight("dash", 1.0f, 3.0f)});
    INFO(r.report());
    REQUIRE(r.categories.size() == static_cast<std::size_t>(MotionCategory::Count));

    // 180 moving samples = 6 s = 30 commitments from two clips: good.
    CHECK(r.at(MotionCategory::Walk).windows == 30);
    CHECK(r.at(MotionCategory::Walk).clips == 2);
    CHECK(r.at(MotionCategory::Walk).grade == CoverageGrade::Good);
    // 30 samples = 1 s = 5 commitments, one clip: moderate, and no better for being in one take.
    CHECK(r.at(MotionCategory::Run).windows == 5);
    CHECK(r.at(MotionCategory::Run).grade == CoverageGrade::Moderate);
    // Nothing here backs up, strafes, starts or stops.
    CHECK(r.at(MotionCategory::Reverse).grade == CoverageGrade::Poor);
    CHECK(r.at(MotionCategory::Strafe).grade == CoverageGrade::Poor);
    CHECK(r.at(MotionCategory::Start).grade == CoverageGrade::Poor);
    // **The clip-final artefact would otherwise read as three stops**: the builder's velocity is a
    // forward difference clamped to the clip end, so every clip's last sample reads zero.
    CHECK(r.at(MotionCategory::Stop).windows == 0);
    CHECK(r.excludedClipFinal == 3);

    // The thresholds are in the report, not only in the code.
    const std::string text = r.report();
    CHECK(text.find("idle < 0.20 m/s") != std::string::npos);
    CHECK(text.find("walk") != std::string::npos);
}

TEST_CASE("§58: reverse, starts and stops are found where they are and nowhere else",
          "[motioncoverage][phaseC]") {
    // Backs up for three seconds; stands for a second and sets off; walks for a second and stops.
    const scene::MotionCategoryReport r = measure({
        straight("backup", 3.0f, -1.0f),
        pathClip("setoff", 2.0f, [](float t) { return glm::vec3(0.0f, 0.0f, t < 1.0f ? 0.0f : 1.2f * (t - 1.0f)); }),
        pathClip("halt", 2.0f, [](float t) { return glm::vec3(0.0f, 0.0f, 1.2f * std::min(t, 1.0f)); }),
    });
    INFO(r.report());
    CHECK(r.at(MotionCategory::Reverse).windows == 15); // 91 samples less the clip end = 3.0 s
    CHECK(r.at(MotionCategory::Reverse).grade == CoverageGrade::Moderate);
    CHECK(r.at(MotionCategory::Start).windows == 1);
    CHECK(r.at(MotionCategory::Start).grade == CoverageGrade::Limited);
    CHECK(r.at(MotionCategory::Stop).windows == 1);
    CHECK(r.at(MotionCategory::Stop).grade == CoverageGrade::Limited);
    CHECK(r.at(MotionCategory::Walk).clips == 2);
}

TEST_CASE("§58: turns are signed, and a fast turn is its own category", "[motioncoverage][phaseC]") {
    // Round a circle of radius 1 at 1.5 rad/s: 1.5 m/s, heading swinging LEFT (toward +x).
    const auto circle = [](float radius, float omega) {
        return [radius, omega](float t) {
            return glm::vec3(radius * (1.0f - std::cos(omega * t)), 0.0f, radius * std::sin(omega * t));
        };
    };
    const scene::MotionCategoryReport left = measure({pathClip("curve", 2.0f, circle(1.0f, 1.5f))});
    INFO(left.report());
    CHECK(left.at(MotionCategory::LeftTurn).samples > 50);
    CHECK(left.at(MotionCategory::RightTurn).samples == 0);
    CHECK(left.at(MotionCategory::FastLeftTurn).samples == 0); // 1.5 m/s is a walk

    const scene::MotionCategoryReport right = measure({pathClip("curve", 2.0f, circle(1.0f, -1.5f))});
    CHECK(right.at(MotionCategory::RightTurn).samples > 50);
    CHECK(right.at(MotionCategory::LeftTurn).samples == 0);

    // The same curve at twice the radius is 3 m/s: a run, and so a high-speed turn.
    const scene::MotionCategoryReport fast = measure({pathClip("sweep", 2.0f, circle(2.0f, 1.5f))});
    CHECK(fast.at(MotionCategory::FastLeftTurn).samples > 50);
    CHECK(fast.at(MotionCategory::FastLeftTurn).samples == fast.at(MotionCategory::LeftTurn).samples);
}

TEST_CASE("§58: the report on the real Glowmere database", "[motioncoverage][phaseC][aliens]") {
    auto glowmere = testsupport::glowmereMotion();
    if (!glowmere) {
        SKIP("the Glowmere alien is not present");
    }
    const scene::MotionCategoryReport r = scene::measureMotionCategories(glowmere->db);
    WARN(r.report());
    REQUIRE(r.samples == glowmere->db.sampleCount());
    REQUIRE(r.samples > 1000);
    // Every non-final sample lands in exactly one of the speed/direction categories.
    const std::uint32_t classified = r.at(MotionCategory::Idle).samples + r.at(MotionCategory::Walk).samples +
                                     r.at(MotionCategory::Run).samples + r.at(MotionCategory::Strafe).samples +
                                     r.at(MotionCategory::Reverse).samples;
    CHECK(classified + r.excludedClipFinal == r.samples);
    CHECK(r.excludedClipFinal == glowmere->db.clipNames.size());
}

TEST_CASE("§58: a tag the body does not bear out is flagged, sample by sample",
          "[motioncoverage][phaseC]") {
    // A walk that travels, and a "run" authored in place (ADR-540's corpus in miniature).
    const scene::MotionCategoryReport r =
        measure({straight("Walking", 2.0f, 1.2f), straight("Running", 2.0f, 0.0f)});
    INFO(r.report());
    const scene::MotionCategoryCoverage& walk = r.at(MotionCategory::Walk);
    const scene::MotionCategoryCoverage& run = r.at(MotionCategory::Run);
    // The name-inferred tags exist -- the subject of the cross-check is present.
    REQUIRE(walk.taggedSeconds > 1.9f);
    REQUIRE(run.taggedSeconds > 1.9f);
    // The walk is a walk by root velocity on every non-final sample; the run is not a run at all.
    CHECK(walk.taggedAgreeing > 1.9f);
    CHECK(run.taggedAgreeing == 0.0f);
    CHECK(run.grade == CoverageGrade::Poor);
    const std::string text = r.report();
    const std::size_t runLine = text.find("  run ");
    REQUIRE(runLine != std::string::npos);
    CHECK(text.find("not this by root velocity", runLine) != std::string::npos);
    CHECK(text.find("not this by root velocity") == text.find("not this by root velocity", runLine));
}
