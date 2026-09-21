// Phase C §21: augmentation by moving the feet and re-solving the legs.
//
// The fixture is a two-legged rig with real two-bone legs (hip, knee, ankle) walking in place:
// each foot is planted for half the cycle and slides back 0.6 m under the hips, which is exactly how
// every Glowmere cycle is authored (ADR-540). The legs are posed by rotation, so a warp that only
// scaled translations would do nothing to them. That is the failure this module exists to avoid.

#include "scene/motion_augment.hpp"
#include "scene/ik.hpp"
#include "scene/motion_coverage.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <tuple>
#include <vector>

using namespace avgen;

namespace {

constexpr float kRate = 30.0f;
constexpr float kSpeed = 1.2f;     // the treadmill: a planted foot slides back at this, model units/s
constexpr float kHipHeight = 0.9f;
constexpr float kThigh = 0.5f;
constexpr float kShin = 0.5f;

// root.x (travel joint) -> pelvis -> {hip.l -> knee.l -> ankle.l, hip.r -> knee.r -> ankle.r}.
scene::Skeleton legRig() {
    scene::Skeleton sk;
    sk.name = "legs";
    const auto add = [&](const char* name, int parent, glm::vec3 at) {
        scene::Joint j;
        j.name = name;
        j.parent = parent;
        j.rest.position = at;
        sk.joints.push_back(j);
    };
    add("root.x", -1, {0.0f, 0.0f, 0.0f});
    add("pelvis", 0, {0.0f, kHipHeight, 0.0f});
    add("hip.l", 1, {0.12f, 0.0f, 0.0f});
    add("knee.l", 2, {0.0f, -kThigh, 0.0f});
    add("ankle.l", 3, {0.0f, -kShin, 0.0f});
    add("hip.r", 1, {-0.12f, 0.0f, 0.0f});
    add("knee.r", 5, {0.0f, -kThigh, 0.0f});
    add("ankle.r", 6, {0.0f, -kShin, 0.0f});
    for (std::uint32_t i = 0; i < sk.joints.size(); ++i) {
        sk.palette.push_back(i);
        sk.inverseBind.push_back(glm::mat4(1.0f));
    }
    return sk;
}

// Where a foot is, relative to its hip, at cycle time u in [0, 1): planted and sliding back for the
// first half, swinging forward and up for the second. Within reach with the knee bent (0.9 of the
// leg at the extremes), as a walking leg is.
glm::vec3 footTarget(float u) {
    if (u < 0.5f) {
        return {0.0f, -0.85f, 0.3f - (kSpeed * u)};
    }
    const float s = (u - 0.5f) / 0.5f;
    return {0.0f, -0.85f + (0.12f * std::sin(3.14159265f * s)), -0.3f + (0.6f * s)};
}

// The in-place walk, with each leg posed by IK so the fixture is rotation-driven like a real rig.
scene::AnimationClip treadmillWalk(const scene::Skeleton& sk) {
    scene::AnimationClip clip;
    clip.name = "Walking";
    clip.start = 0.0f;
    clip.duration = 1.0f;
    std::vector<scene::AnimationChannel> rot;
    for (const std::uint32_t j : {2u, 3u, 5u, 6u}) {
        scene::AnimationChannel c;
        c.joint = j;
        c.path = scene::AnimationPath::Rotation;
        c.interpolation = scene::Interpolation::Linear;
        rot.push_back(c);
    }
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kRate;
        root.times.push_back(t);
        root.values.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
        int k = 0;
        for (const float phase : {0.0f, 0.5f}) {
            const glm::vec3 target = footTarget(std::fmod(t + phase, 1.0f));
            // Two-bone in the hip's frame: straight down at rest, so the solve's two deltas are the
            // hip and knee rotations directly.
            scene::TwoBoneChain chain;
            chain.root = glm::vec3(0.0f);
            chain.mid = glm::vec3(0.0f, -kThigh, 0.0f);
            chain.tip = glm::vec3(0.0f, -kThigh - kShin, 0.0f);
            const auto sol = scene::solveTwoBone(chain, target, glm::vec3(0.0f, -0.5f, 1.0f), true);
            const glm::quat hip = sol.rootDelta;
            const glm::quat knee = sol.midBend;
            for (const glm::quat& q : {hip, knee}) {
                rot[static_cast<std::size_t>(k)].times.push_back(t);
                rot[static_cast<std::size_t>(k)].values.emplace_back(q.x, q.y, q.z, q.w);
                ++k;
            }
        }
    }
    clip.channels.push_back(root);
    for (auto& c : rot) {
        clip.channels.push_back(std::move(c));
    }
    return clip;
}

std::vector<scene::ContactTrack> contacts(const scene::Skeleton& sk) {
    std::vector<scene::ContactTrack> out;
    for (const auto& [name, from, to] : {std::tuple{"ankle.l", 0.0f, 0.5f}, std::tuple{"ankle.r", 0.5f, 1.0f}}) {
        scene::ContactTrack c;
        c.joint = name;
        c.jointIndex = sk.find(name);
        c.kind = scene::ContactKind::Foot;
        // A hair inside the stance, as a detector finds it.
        c.spans.push_back(scene::ContactSpan{from + 0.02f, to - 0.02f, 1.0f});
        out.push_back(c);
    }
    return out;
}

scene::AugmentOptions options(const scene::Skeleton& sk) {
    scene::AugmentOptions o;
    o.sampleRate = kRate;
    o.legs = {{"hip.l", "knee.l", "ankle.l"}, {"hip.r", "knee.r", "ankle.r"}};
    o.contacts = contacts(sk);
    return o;
}

glm::vec3 jointAt(const scene::Skeleton& sk, const scene::AnimationClip& clip, float t, const char* name) {
    scene::Pose pose;
    scene::setRestPose(sk, pose);
    scene::sampleClip(clip, t, pose);
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, pose, model);
    return glm::vec3(model[static_cast<std::size_t>(sk.find(name))][3]);
}

// The body's average velocity over the clip, from the travel joint's first and last position.
glm::vec3 averageVelocity(const scene::Skeleton& sk, const scene::AnimationClip& clip) {
    return (jointAt(sk, clip, clip.duration, "root.x") - jointAt(sk, clip, 0.0f, "root.x")) / clip.length();
}

// The worst horizontal drift of the left ankle across the middle of its stance.
float stanceDrift(const scene::Skeleton& sk, const scene::AnimationClip& clip) {
    float worst = 0.0f;
    const glm::vec3 first = jointAt(sk, clip, 0.05f, "ankle.l");
    for (int i = 2; i <= 13; ++i) {
        const glm::vec3 p = jointAt(sk, clip, static_cast<float>(i) / kRate, "ankle.l");
        worst = std::max(worst, std::hypot(p.x - first.x, p.z - first.z));
    }
    return worst;
}

} // namespace

TEST_CASE("the treadmill fixture is what it claims", "[augment][phaseC]") {
    // The subject exists: in place, and the planted foot slides back at the walking speed.
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    CHECK(glm::length(averageVelocity(sk, walk)) < 1e-4f);
    const float slide = jointAt(sk, walk, 0.1f, "ankle.l").z - jointAt(sk, walk, 0.4f, "ankle.l").z;
    CHECK(std::abs(slide - (kSpeed * 0.3f)) < 0.02f);
}

TEST_CASE("planting turns the treadmill into travel and stills the planted foot", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const scene::AugmentResult planted =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Plant, 0.0f, options(sk));
    INFO(planted.refusal);
    REQUIRE(planted.accepted);
    const glm::vec3 v = averageVelocity(sk, planted.clip);
    WARN(fmt::format("planted: body travels at ({:.3f}, {:.3f}) m/s; left ankle drifts {:.4f} m in stance "
                     "(was {:.4f} in place)",
                     v.x, v.z, stanceDrift(sk, planted.clip), stanceDrift(sk, walk)));
    CHECK(std::abs(v.z - kSpeed) < 0.05f);
    CHECK(std::abs(v.x) < 0.01f);
    CHECK(stanceDrift(sk, planted.clip) < 0.01f);
    CHECK(stanceDrift(sk, walk) > 0.25f);
}

TEST_CASE("a stride variant covers a different speed at the same cadence, feet still planted",
          "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const scene::AugmentResult longer =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Stride, 1.15f, options(sk));
    const scene::AugmentResult shorter =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Stride, 0.7f, options(sk));
    INFO(longer.refusal << " / " << shorter.refusal);
    REQUIRE(longer.accepted);
    REQUIRE(shorter.accepted);
    CHECK(std::abs(averageVelocity(sk, longer.clip).z - (kSpeed * 1.15f)) < 0.06f);
    CHECK(std::abs(averageVelocity(sk, shorter.clip).z - (kSpeed * 0.7f)) < 0.06f);
    CHECK(longer.clip.length() == walk.length()); // same cadence
    CHECK(stanceDrift(sk, longer.clip) < 0.01f);
    CHECK(stanceDrift(sk, shorter.clip) < 0.01f);
}

TEST_CASE("a stride the leg cannot reach is refused, and says why", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const scene::AugmentResult far =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Stride, 2.5f, options(sk));
    WARN(fmt::format("stride x2.5: shortfall {:.1f}% -- {}", far.worstShortfall * 100.0f, far.refusal));
    CHECK_FALSE(far.accepted);
    CHECK(far.worstShortfall > 0.02f);
    CHECK_FALSE(far.refusal.empty());
}

TEST_CASE("a direction variant travels diagonally while the body faces forward", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const float angle = 0.35f; // 20 degrees
    const scene::AugmentResult diagonal =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Direction, angle, options(sk));
    INFO(diagonal.refusal);
    REQUIRE(diagonal.accepted);
    const glm::vec3 v = averageVelocity(sk, diagonal.clip);
    const float travel = std::atan2(v.x, v.z);
    WARN(fmt::format("direction {:+.3f} rad: travels at {:+.3f} rad, {:.3f} m/s; drift {:.4f}", angle, travel,
                     glm::length(v), stanceDrift(sk, diagonal.clip)));
    CHECK(std::abs(travel - angle) < 0.03f);
    CHECK(std::abs(glm::length(v) - kSpeed) < 0.06f);
    // The body faces forward throughout: the heading track is zero and the pelvis is not turned.
    for (const float h : diagonal.heading) {
        CHECK(h == 0.0f);
    }
    CHECK(stanceDrift(sk, diagonal.clip) < 0.01f);
}

TEST_CASE("a turn variant curves the path and turns the body with it", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const float rate = 0.6f; // rad/s
    const scene::AugmentResult turn =
        scene::augmentClip(sk, walk, true, scene::AugmentKind::Turn, rate, options(sk));
    INFO(turn.refusal);
    REQUIRE(turn.accepted);
    // The heading it applied is the one it reports.
    REQUIRE(turn.heading.size() == 31u);
    CHECK(std::abs(turn.heading.back() - rate * 1.0f) < 1e-4f);
    // The path bends: early travel heads +Z, late travel heads rate*t around.
    const glm::vec3 early = jointAt(sk, turn.clip, 0.2f, "root.x") - jointAt(sk, turn.clip, 0.0f, "root.x");
    const glm::vec3 late = jointAt(sk, turn.clip, 1.0f, "root.x") - jointAt(sk, turn.clip, 0.8f, "root.x");
    const float bend = std::atan2(late.x, late.z) - std::atan2(early.x, early.z);
    WARN(fmt::format("turn {:.2f} rad/s: path bends {:.3f} rad over 0.8 s (want {:.3f}); shortfall {:.2f}%",
                     rate, bend, rate * 0.8f, turn.worstShortfall * 100.0f));
    CHECK(std::abs(bend - (rate * 0.8f)) < 0.08f);
}

TEST_CASE("start and stop variants ramp the speed, and never to a standstill", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const auto speedBetween = [&](const scene::AnimationClip& c, float a, float b) {
        return glm::length(jointAt(sk, c, b, "root.x") - jointAt(sk, c, a, "root.x")) / (b - a);
    };
    const scene::AugmentResult start = scene::augmentClip(sk, walk, false, scene::AugmentKind::Start, 0.0f, options(sk));
    const scene::AugmentResult stop = scene::augmentClip(sk, walk, false, scene::AugmentKind::Stop, 0.0f, options(sk));
    INFO(start.refusal << " / " << stop.refusal);
    REQUIRE(start.accepted);
    REQUIRE(stop.accepted);
    const float startEarly = speedBetween(start.clip, 0.0f, 0.2f);
    const float startLate = speedBetween(start.clip, 0.8f, 1.0f);
    const float stopEarly = speedBetween(stop.clip, 0.0f, 0.2f);
    const float stopLate = speedBetween(stop.clip, 0.8f, 1.0f);
    WARN(fmt::format("start {:.2f} -> {:.2f} m/s; stop {:.2f} -> {:.2f} m/s", startEarly, startLate, stopEarly,
                     stopLate));
    CHECK(startLate > 2.0f * startEarly);
    CHECK(stopEarly > 2.0f * stopLate);
    CHECK(startEarly > 0.1f); // the floor: a start begins with a short step, not with both feet together
    CHECK(stopLate > 0.1f);
}

TEST_CASE("a mirror exchanges left and right, and mirroring twice returns the clip", "[augment][phaseC]") {
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const scene::AugmentResult once = scene::augmentClip(sk, walk, true, scene::AugmentKind::Mirror, 0.0f, options(sk));
    REQUIRE(once.accepted);
    // The mirrored left ankle does what the right one did, reflected across x.
    float worst = 0.0f;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const glm::vec3 right = jointAt(sk, walk, t, "ankle.r");
        const glm::vec3 left = jointAt(sk, once.clip, t, "ankle.l");
        worst = std::max(worst, glm::length(left - glm::vec3(-right.x, right.y, right.z)));
    }
    CHECK(worst < 1e-3f);
    const scene::AugmentResult twice =
        scene::augmentClip(sk, once.clip, true, scene::AugmentKind::Mirror, 0.0f, options(sk));
    REQUIRE(twice.accepted);
    float back = 0.0f;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kRate;
        for (const char* j : {"ankle.l", "ankle.r", "knee.l", "knee.r"}) {
            back = std::max(back, glm::length(jointAt(sk, twice.clip, t, j) - jointAt(sk, walk, t, j)));
        }
    }
    WARN(fmt::format("mirror: worst twin error {:.2e}; mirrored twice, worst error {:.2e}", worst, back));
    CHECK(back < 1e-3f);
}

TEST_CASE("§58 counts a turn variant as a turn, the way it turns", "[augment][motioncoverage][phaseC]") {
    // With every feature in the body's frame (§7), a body that turns while it walks keeps its
    // travel straight ahead of it, so a heading difference in its root velocity reads zero, and the
    // categories used to see no turn at all. They now read the path's curvature from the
    // trajectory. Both signs, so a sign error cannot pass.
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip walk = treadmillWalk(sk);
    const auto categoriesOf = [&](float rate) {
        const scene::AugmentResult turn = scene::augmentClip(sk, walk, true, scene::AugmentKind::Turn, rate, options(sk));
        REQUIRE(turn.accepted);
        scene::MotionPack pack;
        pack.name = "turns";
        pack.skeleton = sk;
        pack.skeletonDigest = scene::skeletonDigest(sk);
        scene::PackClip meta;
        meta.name = turn.clip.name;
        meta.loop = false;
        meta.sampleRate = kRate;
        meta.frames = static_cast<std::uint32_t>(turn.heading.size());
        meta.heading = turn.heading;
        pack.clips = {meta};
        pack.animation = {turn.clip};
        scene::MotionDatabaseOptions db;
        db.sampleRate = kRate;
        db.config.joints = {"ankle.l", "ankle.r"};
        db.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
        auto built = scene::buildMotionDatabase(pack, db);
        REQUIRE(built.has_value());
        return scene::measureMotionCategories(*built);
    };
    const scene::MotionCategoryReport left = categoriesOf(1.4f);
    const scene::MotionCategoryReport right = categoriesOf(-1.4f);
    WARN(fmt::format("+1.4 rad/s: left {} right {} samples; -1.4 rad/s: left {} right {}",
                     left.at(scene::MotionCategory::LeftTurn).samples, left.at(scene::MotionCategory::RightTurn).samples,
                     right.at(scene::MotionCategory::LeftTurn).samples, right.at(scene::MotionCategory::RightTurn).samples));
    CHECK(left.at(scene::MotionCategory::LeftTurn).samples > 10u);
    CHECK(left.at(scene::MotionCategory::RightTurn).samples == 0u);
    CHECK(right.at(scene::MotionCategory::RightTurn).samples > 10u);
    CHECK(right.at(scene::MotionCategory::LeftTurn).samples == 0u);
}

// ---- the real scout -------------------------------------------------------------------------------

#include "assets/gltf_loader.hpp"
#include "scene/scene.hpp"

#include <filesystem>

TEST_CASE("§21 on the scout: which variants add coverage, and which are redundant",
          "[augment][aliens][phaseC]") {
    namespace fs = std::filesystem;
    const fs::path glb = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(glb)) {
        SKIP("the scout is not present");
    }
    scene::Scene sc;
    assets::GltfLoadOptions load;
    load.loadImages = false;
    REQUIRE(assets::loadGltf(glb, sc, load).has_value());
    const scene::SkinnedRig& rig = sc.rigs.front();
    scene::Provenance provenance;
    provenance.source = "Glowmere alien pack";
    provenance.license = "CC0-1.0";
    scene::PackBuildOptions packOptions;
    packOptions.contactJoints = {scene::ContactJoint{"foot.l", scene::ContactKind::Foot},
                                 scene::ContactJoint{"foot.r", scene::ContactKind::Foot}};
    auto pack = scene::buildMotionPack("scout", rig.skeleton, rig.clips, provenance, packOptions);
    REQUIRE(pack.has_value());

    scene::AugmentPackOptions options;
    options.augment.legs = {{"thigh_twist.l", "leg_stretch.l", "foot.l"}, {"thigh_twist.r", "leg_stretch.r", "foot.r"}};
    options.database.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");

    // The plan covers every kind. Walking is the source for most, because it is the clip the
    // gaps are next to. The turns are 1.4 rad/s because §58 counts a turn above 1.0. The duplicate
    // stride at the end is there to be refused, as redundant or by its own gate.
    using K = scene::AugmentKind;
    const std::vector<scene::AugmentPlanItem> plan = {
        {"Walking", K::Stride, 1.3f},     {"Walking", K::Stride, 0.7f},    {"Walking", K::Direction, 0.6f},
        {"Walking", K::Direction, -0.6f}, {"Walking", K::Direction, 2.4f}, {"Walking", K::Turn, 1.4f},
        {"Walking", K::Turn, -1.4f},      {"Running", K::Turn, 1.4f},      {"Running", K::Turn, -1.4f},
        {"Walking", K::Start, 0.0f},
        {"Walking", K::Stop, 0.0f},       {"Walking", K::Mirror, 0.0f},    {"Walking", K::Stride, 1.3f},
    };
    auto result = scene::augmentPack(*pack, plan, options);
    REQUIRE(result.has_value());
    WARN(result->report());

    int kept = 0;
    int redundant = 0;
    for (const scene::AugmentDecision& d : result->decisions) {
        kept += d.kept ? 1 : 0;
        redundant += (d.generated && !d.kept) ? 1 : 0;
    }
    // Something was worth adding, and the repeat of an already-kept variant was not.
    CHECK(kept > 0);
    const scene::AugmentDecision& repeat = result->decisions.back();
    CHECK_FALSE(repeat.kept);
    // Every kept variant carries its heading and a provenance naming what it came from.
    for (std::size_t c = pack->clips.size(); c < result->pack.clips.size(); ++c) {
        const scene::PackClip& meta = result->pack.clips[c];
        CHECK_FALSE(meta.heading.empty());
        REQUIRE(meta.provenance < result->pack.provenance.size());
        CHECK(result->pack.provenance[meta.provenance].processing.back().find("§21") != std::string::npos);
    }
    CHECK(result->pack.clips.size() == pack->clips.size() + static_cast<std::size_t>(kept));
}
