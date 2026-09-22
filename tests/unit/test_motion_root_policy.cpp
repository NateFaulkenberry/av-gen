// Phase C §71: one authoritative movement result. The simulation owns where the body is and which
// way it faces; the matcher supplies a pose in the body's own frame.
//
// §71: "Avoid motion matching moves root + MotionController moves root + physics moves root, all
// simultaneously." The entity already moves the body (its behaviours and ADR-620's limiter), and
// ADR-337 transfers an opted-in clip's root motion into that simulation. The matcher is the third
// candidate. Posing a travelling clip as authored walked the rig away from the entity over the clip
// and snapped it back at the wrap, and a turning clip turned the body on top of the entity's own
// turn. So a clip whose travel is real is posed with its travel and heading removed, and an in-place
// clip is posed exactly as authored.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

using namespace avgen;

namespace {

glm::vec3 jointIn(const scene::Skeleton& sk, const scene::Pose& pose, const char* name, float* yaw = nullptr) {
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, pose, model);
    const glm::mat4& m = model[static_cast<std::size_t>(sk.find(name))];
    if (yaw != nullptr) {
        const glm::vec3 f = glm::vec3(m * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        *yaw = std::atan2(f.x, f.z);
    }
    return glm::vec3(m[3]);
}

scene::Pose posed(const entity::MatchMotionProvider& matcher, const scene::MotionDatabase& db,
                  const scene::Skeleton& sk, std::uint32_t sample) {
    entity::MotionMemory memory;
    memory.selection = sample;
    memory.localTime = db.sampleTime[sample];
    memory.generation = 1;
    memory.database = db.identity;
    scene::Pose pose;
    REQUIRE(matcher.pose(memory, sk, pose).ok());
    return pose;
}

} // namespace

TEST_CASE("§71 a travelling clip is posed in the body's frame, and so is a turning one",
          "[rootpolicy][matching][phaseC]") {
    const scene::MotionPack pack = testsupport::goldenPack();
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());
    entity::MatchMotionProvider matcher(&*db, &pack.animation, "match");
    const auto sampleOf = [&](const char* clip, int frame) {
        for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
            if (db->clipNames[db->sampleClip[s]] == clip && s > 0 && db->sampleClip[s - 1] == db->sampleClip[s]) {
                if (--frame <= 0) {
                    return s;
                }
            }
        }
        FAIL("no such sample");
        return 0u;
    };
    // `Walk` at 0.8 s: authored 0.96 m down the path. `TurnLeft` at 0.8 s: authored 1.2 rad around.
    const std::uint32_t walk = sampleOf("Walk", 24);
    const std::uint32_t turn = sampleOf("TurnLeft", 24);
    scene::Pose raw;
    scene::setRestPose(pack.skeleton, raw);
    scene::sampleClip(pack.animation[db->sampleClip[walk]], db->sampleTime[walk], raw);
    // The subject exists: as authored, the walk's root is far down its path.
    REQUIRE(jointIn(pack.skeleton, raw, "root.x").z > 0.9f);

    float yaw = 0.0f;
    const glm::vec3 w = jointIn(pack.skeleton, posed(matcher, *db, pack.skeleton, walk), "root.x");
    const glm::vec3 t = jointIn(pack.skeleton, posed(matcher, *db, pack.skeleton, turn), "root.x", &yaw);
    WARN(fmt::format("walk root in the body frame ({:.4f}, {:.4f}); turn root ({:.4f}, {:.4f}) facing {:.4f} rad",
                     w.x, w.z, t.x, t.z, yaw));
    CHECK(std::hypot(w.x, w.z) < 1e-3f);
    CHECK(std::hypot(t.x, t.z) < 1e-3f);
    CHECK(std::abs(yaw) < 1e-3f);
    // And the feet still stride: removing the travel did not remove the motion.
    const scene::Pose walkPose = posed(matcher, *db, pack.skeleton, walk);
    CHECK(glm::length(jointIn(pack.skeleton, walkPose, "foot.l") - jointIn(pack.skeleton, walkPose, "foot.r")) > 0.2f);
}

TEST_CASE("§71 an in-place clip is posed exactly as authored", "[rootpolicy][matching][phaseC]") {
    // The other arm: Glowmere's in-place cycles keep their authored root sway, bit for bit.
    scene::MotionPack pack;
    pack.name = "inplace";
    // A body with a height, so "in place" means what it does on a real rig: a root that stays
    // inside a box the size of the body (ADR-552). The golden rig is all at the origin and has none.
    pack.skeleton = testsupport::goldenRig();
    pack.skeleton.joints.push_back(scene::Joint{"head", 0, scene::Transform{glm::vec3(0.0f, 1.6f, 0.0f)}});
    pack.skeleton.palette.push_back(3u);
    pack.skeleton.inverseBind.push_back(glm::mat4(1.0f));
    pack.skeletonDigest = scene::skeletonDigest(pack.skeleton);
    scene::AnimationClip clip;
    clip.name = "Sway";
    clip.start = 0.0f;
    clip.duration = 1.0f;
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    for (int i = 0; i <= 30; ++i) {
        const float s = static_cast<float>(i) / 30.0f;
        root.times.push_back(s);
        root.values.emplace_back(0.03f * std::sin(6.2831853f * s), 0.0f, 0.02f * std::cos(6.2831853f * s), 0.0f);
    }
    clip.channels = {root};
    scene::PackClip meta;
    meta.name = clip.name;
    meta.loop = true;
    meta.sampleRate = 30.0f;
    meta.frames = 31;
    pack.animation = {clip};
    pack.clips = {meta};
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());
    REQUIRE(db->clipTravels.at(0) == 0u);
    entity::MatchMotionProvider matcher(&*db, &pack.animation, "match");
    for (std::uint32_t s = 0; s < db->sampleCount(); s += 4) {
        scene::Pose raw;
        scene::setRestPose(pack.skeleton, raw);
        scene::sampleClip(pack.animation[0], db->sampleTime[s], raw);
        const scene::Pose got = posed(matcher, *db, pack.skeleton, s);
        CHECK(got.local[0].position == raw.local[0].position);
    }
}
