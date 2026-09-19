// Two-bone foot IK (ADR-344): the solver on its own, then the layer on the rigs that have a leg.
//
// ADR-182 sets the shape of the evidence, and for a solver it has a specific sharp edge: **"the
// foot reached the target" is satisfied by a solver that puts the foot anywhere the target is, by
// any route, including through the body.** A leg inverted at the knee reaches the target exactly.
// So every reach arm here is paired with a control that a wrong-but-reaching solver fails:
//
//   reach          the tip lands ON the target                   (a solver that does nothing fails)
//   identity       a target the tip is already at moves nothing  (a solver that moves it anywhere fails)
//   pole, twice    the knee lands on the side it was told to, and on the OTHER side when told the
//                  other way, with the tip on the target both times
//                                                                (a solver that ignores the pole fails)
//   unreachable    a target three limb-lengths away clamps to the limb's own extension, stays
//                  finite, and lands on the ray to the target    (a solver that "reaches" explodes)
//   straight       a straight chain with no pole is refused, and the SAME chain with a pole solves
//                                                                (a solver that guesses a plane fails)
//   not-a-chain    the alien rig's three leg-ish names bind to nothing and write nothing
//                                                                (a solver that trusts names fails)
//   footAlign      four hooves tilt 18 degrees on an 18-degree bank, measured against WORLD up --
//                  against the slope normal the arm would be asking whether the layer did what it
//                  had just done -- and with the alignment off the same four come out at 18, 27,
//                  27 and 56                                     (a plant with no sole fails)
//
// Two more that are not solver arms at all and earn their place anyway: the bind-pose extension
// measurement, which is the fact that decides what this layer can be used *for* on this content,
// and a save-and-reload of the shipped lab scene, because a layer kind with its own key set is
// exactly the thing a shared writer turns into a file that will not load.
//
// Bands, not floors. "The hoof went down" passes on a hoof that went to the centre of the earth;
// every assertion below is an interval with a top as well as a bottom.

#include "assets/asset_registry.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/ik.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

fs::path farmDir() { return fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm"; }
fs::path alienDir() { return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens"; }

// A textbook leg standing in the +Y-up model space this engine poses in: hip at the origin, knee a
// metre below and bent forward (+Z), foot a metre below that. Lengths are deliberately unequal so
// that `minReach` is non-zero and the fold limit is a real limit.
scene::TwoBoneChain textbookLeg() {
    return scene::TwoBoneChain{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -0.95f, 0.30f),
                               glm::vec3(0.0f, -1.80f, 0.0f)};
}

float dist(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b); }

// Which side of the root->target axis the knee came out on, measured against a direction. Positive
// means the knee leans that way. This is the number a pole is *for*, and it is the one that tells a
// forward-bending stifle from a backward-bending hock.
float kneeSide(const scene::TwoBoneSolution& sol, const glm::vec3& root, const glm::vec3& target,
               const glm::vec3& direction) {
    const glm::vec3 axis = glm::normalize(target - root);
    const glm::vec3 arm = (sol.mid - root) - glm::dot(sol.mid - root, axis) * axis;
    return glm::dot(arm, direction);
}

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool finite(const glm::quat& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
}

std::vector<glm::vec3> jointPositions(const scene::Skeleton& sk, const scene::Pose& pose) {
    std::vector<glm::mat4> model;
    scene::poseToModel(sk, pose, model);
    std::vector<glm::vec3> out(model.size());
    for (std::size_t i = 0; i < model.size(); ++i) {
        out[i] = glm::vec3(model[i][3]);
    }
    return out;
}

glm::vec3 jointAt(const scene::Skeleton& sk, const scene::Pose& pose, const char* name) {
    const int i = sk.find(name);
    REQUIRE(i >= 0);
    return jointPositions(sk, pose)[static_cast<std::size_t>(i)];
}

// One rig, loaded straight out of its GLB with no composition, no clock and no entity in the way.
// Everything a foot layer needs is in the rig, which is itself the ADR-260 `PoseOnly` claim
// restated: if this test needed an entity, the layer would be reaching for one.
struct Rig {
    scene::Scene s;
    scene::SkinnedRig* rig = nullptr;

    explicit Rig(const fs::path& file) {
        const auto summary = assets::loadGltf(file, s, {});
        REQUIRE(summary.has_value());
        REQUIRE(s.rigs.size() == 1);
        rig = &s.rigs.front();
        scene::setRestPose(rig->skeleton, rig->pose);
    }
};

scene::PoseLayer bullRearLeft() {
    scene::PoseLayer layer;
    layer.name = "rear-left";
    layer.kind = scene::PoseLayerKind::Foot;
    layer.chainRoot = "UpperLegB.L";
    layer.chainMid = "LowerLegB.L";
    layer.chainTip = "HoofB.L";
    layer.weight = 1.0f;
    return layer;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The solver, with no rig anywhere near it
// ------------------------------------------------------------------------------------------------

TEST_CASE("a two-bone solve reaches its target without changing the limb", "[ik][skeleton]") {
    const scene::TwoBoneChain leg = textbookLeg();
    const float l1 = dist(leg.root, leg.mid);
    const float l2 = dist(leg.mid, leg.tip);

    SECTION("reach: a target inside the limb's range is met exactly") {
        // Forward, down and to one side, so all three of the solve's steps have work to do.
        const glm::vec3 target(0.40f, -1.30f, 0.55f);
        REQUIRE(glm::length(target - leg.root) < (l1 + l2) * 0.99f);
        REQUIRE(glm::length(target - leg.root) > std::abs(l1 - l2));
        const auto sol = scene::solveTwoBone(leg, target, glm::vec3(0.0f), false);
        CHECK(sol.status == scene::IkStatus::Solved);
        CHECK(dist(sol.tip, target) < 1e-4f);
        // Rigid, not elastic. "The tip reached the target" also passes on a solver that stretched
        // the bones to get there, and a stretched bone is a stretched mesh.
        CHECK(dist(leg.root, sol.mid) == Approx(l1).margin(1e-5));
        CHECK(dist(sol.mid, sol.tip) == Approx(l2).margin(1e-5));
        // The knee is bent, not locked and not folded flat -- a band, because "the knee has an
        // angle" is true of every number.
        CHECK(sol.kneeAngle > 0.2f);
        CHECK(sol.kneeAngle < 3.0f);
    }

    SECTION("identity: a target where the tip already is moves nothing at all") {
        // The control for the reach arm. A solver that moves the foot *anywhere* passes the reach
        // assertion on a target it happens to cover; this one it cannot pass, because the only
        // answer is no rotation at all.
        const auto sol = scene::solveTwoBone(leg, leg.tip, glm::vec3(0.0f), false);
        CHECK(sol.status == scene::IkStatus::Solved);
        CHECK(dist(sol.tip, leg.tip) < 1e-5f);
        CHECK(dist(sol.mid, leg.mid) < 1e-5f);
        CHECK(glm::angle(glm::normalize(sol.rootDelta)) < 1e-4f);
        CHECK(glm::angle(glm::normalize(sol.midBend)) < 1e-4f);
    }

    SECTION("determinism: the same question twice is the same answer, bit for bit") {
        const glm::vec3 target(0.40f, -1.30f, 0.55f);
        const glm::vec3 pole(0.0f, -0.9f, 2.0f);
        const auto a = scene::solveTwoBone(leg, target, pole, true);
        const auto b = scene::solveTwoBone(leg, target, pole, true);
        CHECK(a.rootDelta.x == b.rootDelta.x);
        CHECK(a.rootDelta.y == b.rootDelta.y);
        CHECK(a.rootDelta.z == b.rootDelta.z);
        CHECK(a.rootDelta.w == b.rootDelta.w);
        CHECK(a.midBend.w == b.midBend.w);
        CHECK(a.tip.x == b.tip.x);
        CHECK(a.tip.y == b.tip.y);
        CHECK(a.tip.z == b.tip.z);
    }
}

TEST_CASE("the pole decides which side the knee comes out on", "[ik][skeleton]") {
    // THE control arm that a numerically-correct-but-visually-broken solver fails. Both solves
    // reach the same target; they differ only in the knee hint, and the knee has to come out on
    // opposite sides. A solver that ignored the pole would pass every reach assertion in this file
    // and put every hock on a herd of bulls the wrong way round.
    const scene::TwoBoneChain leg = textbookLeg();
    const glm::vec3 target(0.0f, -1.55f, 0.35f);
    const glm::vec3 forward(0.0f, 0.0f, 1.0f);

    const auto ahead = scene::solveTwoBone(leg, target, leg.root + glm::vec3(0.0f, -0.9f, 3.0f), true);
    const auto behind = scene::solveTwoBone(leg, target, leg.root + glm::vec3(0.0f, -0.9f, -3.0f), true);

    CHECK(ahead.status == scene::IkStatus::Solved);
    CHECK(behind.status == scene::IkStatus::Solved);
    // Both still land on the target: the pole must not be bought with the reach.
    CHECK(dist(ahead.tip, target) < 1e-4f);
    CHECK(dist(behind.tip, target) < 1e-4f);
    // And they came out on opposite sides, each by a real distance rather than a rounding error.
    // The limb is 1.8 m long and the target is 1.59 m away, so the knee's offset from the axis is
    // of the order of 0.4 m; a band of 0.1..0.9 admits the geometry and excludes both zero and
    // anything that has left the animal.
    const float forwardSide = kneeSide(ahead, leg.root, target, forward);
    const float backwardSide = kneeSide(behind, leg.root, target, forward);
    CHECK(forwardSide > 0.10f);
    CHECK(forwardSide < 0.90f);
    CHECK(backwardSide < -0.10f);
    CHECK(backwardSide > -0.90f);
    // Symmetric, because the target is in the plane the two poles straddle.
    CHECK(forwardSide == Approx(-backwardSide).margin(1e-3));

    SECTION("no pole keeps the plane the chain was already bent in") {
        // The default, and the reason it is the default: the animation has already decided which
        // way this knee goes, and the solver has no better opinion. `textbookLeg` bends forward.
        const auto kept = scene::solveTwoBone(leg, target, glm::vec3(0.0f), false);
        CHECK(kept.status == scene::IkStatus::Solved);
        CHECK(kneeSide(kept, leg.root, target, forward) > 0.10f);
    }
}

TEST_CASE("a two-bone solve refuses what it cannot do instead of exploding", "[ik][skeleton]") {
    const scene::TwoBoneChain leg = textbookLeg();
    const float l1 = dist(leg.root, leg.mid);
    const float l2 = dist(leg.mid, leg.tip);
    const float span = l1 + l2;

    SECTION("unreachable: a target three limb-lengths away clamps and says so") {
        const glm::vec3 target = leg.root + glm::vec3(0.0f, -3.0f * span, 0.0f);
        const auto sol = scene::solveTwoBone(leg, target, glm::vec3(0.0f, -1.0f, 1.0f), true);
        CHECK(sol.status == scene::IkStatus::Clamped);
        CHECK(finite(sol.tip));
        CHECK(finite(sol.rootDelta));
        CHECK(finite(sol.midBend));
        // It went as far as it could and no further: the achieved distance is the extension limit,
        // in a band that excludes both "did not move" and "stretched to reach".
        CHECK(sol.achieved == Approx(span).margin(1e-3));
        CHECK(sol.achieved < sol.requested);
        CHECK(sol.achieved > span * 0.95f);
        // And it is pointing AT the target, not merely short of it somewhere. The tip, the root and
        // the target are collinear.
        const glm::vec3 toTip = glm::normalize(sol.tip - leg.root);
        const glm::vec3 toTarget = glm::normalize(target - leg.root);
        CHECK(glm::length(glm::cross(toTip, toTarget)) < 1e-3f);
        // Still a limb, still the same bones.
        CHECK(dist(leg.root, sol.mid) == Approx(l1).margin(1e-4));
        CHECK(dist(sol.mid, sol.tip) == Approx(l2).margin(1e-4));
    }

    SECTION("too close: a target inside the fold limit clamps the other way") {
        const float minReach = std::abs(l1 - l2);
        REQUIRE(minReach > 0.01f); // the fixture's bones are unequal on purpose
        const glm::vec3 target = leg.root + glm::vec3(0.0f, -minReach * 0.25f, 0.0f);
        const auto sol = scene::solveTwoBone(leg, target, glm::vec3(0.0f, -1.0f, 1.0f), true);
        CHECK(sol.status == scene::IkStatus::Clamped);
        CHECK(sol.achieved == Approx(minReach).margin(1e-4));
        CHECK(sol.achieved > sol.requested);
        CHECK(finite(sol.tip));
    }

    SECTION("a zero-length bone is not a chain") {
        scene::TwoBoneChain broken = leg;
        broken.mid = broken.root;
        const auto sol = scene::solveTwoBone(broken, glm::vec3(0.5f, -1.0f, 0.0f), glm::vec3(0.0f), false);
        CHECK(sol.status == scene::IkStatus::DegenerateBone);
        CHECK(glm::angle(sol.rootDelta) == Approx(0.0f).margin(1e-6));
        CHECK(glm::angle(sol.midBend) == Approx(0.0f).margin(1e-6));
    }

    SECTION("a target on top of the root has no direction") {
        const auto sol = scene::solveTwoBone(leg, leg.root, glm::vec3(0.0f), false);
        CHECK(sol.status == scene::IkStatus::DegenerateTarget);
        CHECK(glm::angle(sol.rootDelta) == Approx(0.0f).margin(1e-6));
    }

    SECTION("a straight chain with no pole is refused, and the same chain with one is not") {
        // The control pair for the bend plane. A solver that invented an axis here would pass the
        // first half; the point is that it must not.
        const scene::TwoBoneChain straight{glm::vec3(0.0f), glm::vec3(0.0f, -0.95f, 0.0f),
                                           glm::vec3(0.0f, -1.80f, 0.0f)};
        const glm::vec3 target(0.0f, -1.40f, 0.30f);

        const auto refused = scene::solveTwoBone(straight, target, glm::vec3(0.0f), false);
        CHECK(refused.status == scene::IkStatus::DegenerateBend);
        CHECK(glm::angle(refused.rootDelta) == Approx(0.0f).margin(1e-6));
        CHECK(glm::angle(refused.midBend) == Approx(0.0f).margin(1e-6));

        const auto told = scene::solveTwoBone(straight, target, glm::vec3(0.0f, -0.9f, 3.0f), true);
        CHECK(told.status == scene::IkStatus::Solved);
        CHECK(dist(told.tip, target) < 1e-4f);
        CHECK(kneeSide(told, straight.root, target, glm::vec3(0.0f, 0.0f, 1.0f)) > 0.05f);
        // Told the other way, it goes the other way. Two arms, opposite answers, one difference.
        const auto other = scene::solveTwoBone(straight, target, glm::vec3(0.0f, -0.9f, -3.0f), true);
        CHECK(other.status == scene::IkStatus::Solved);
        CHECK(dist(other.tip, target) < 1e-4f);
        CHECK(kneeSide(other, straight.root, target, glm::vec3(0.0f, 0.0f, 1.0f)) < -0.05f);
    }
}

TEST_CASE("planting drops a foot vertically onto a plane, never along the normal", "[ik][skeleton]") {
    // A 30-degree bank. Sliding the foot down the surface normal instead of straight down would
    // move it sideways across the terrain -- half a hoof's width here, for nothing.
    const float k = std::sqrt(0.75f);
    const glm::vec3 normal = glm::normalize(glm::vec3(0.5f, k, 0.0f));
    const glm::vec3 origin(0.0f, 2.0f, 0.0f);
    const glm::vec3 foot(1.0f, 5.0f, -0.4f);

    const glm::vec3 planted = scene::plantOnPlane(foot, origin, normal, 0.0f);
    // Straight down: x and z untouched, exactly.
    CHECK(planted.x == foot.x);
    CHECK(planted.z == foot.z);
    // And on the plane.
    CHECK(glm::dot(planted - origin, normal) == Approx(0.0f).margin(1e-5));
    // One metre along +X on a 30-degree bank is tan(30) = 0.5774 m -- of *fall*, not rise. A
    // surface normal leaning towards +X is a surface sloping away from it, and getting that
    // backwards is a whole herd standing on the wrong side of the hill.
    CHECK(planted.y == Approx(2.0f - 0.57735f).margin(1e-4));

    SECTION("the offset lifts the joint off the surface by exactly what it was given") {
        const glm::vec3 lifted = scene::plantOnPlane(foot, origin, normal, 0.25f);
        CHECK(lifted.y - planted.y == Approx(0.25f).margin(1e-6));
    }

    SECTION("a vertical wall has no answer and the foot is left where it was") {
        const glm::vec3 wall(1.0f, 0.0f, 0.0f);
        const glm::vec3 same = scene::plantOnPlane(foot, origin, wall, 0.0f);
        CHECK(same.x == foot.x);
        CHECK(same.y == foot.y);
        CHECK(same.z == foot.z);
    }
}

// ------------------------------------------------------------------------------------------------
// The layer, on the rigs that have a leg
// ------------------------------------------------------------------------------------------------

TEST_CASE("a foot layer moves one hoof and nothing else on the bull", "[ik][layers][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb")) {
        SKIP("assets/farm is not present");
    }
    Rig bull(farmDir() / "bull.glb");
    scene::Skeleton& sk = bull.rig->skeleton;
    const scene::Pose rest = bull.rig->pose;
    const glm::vec3 hoofRest = jointAt(sk, rest, "HoofB.L");
    const glm::vec3 otherRest = jointAt(sk, rest, "HoofB.R");
    const glm::vec3 hipRest = jointAt(sk, rest, "Hip");

    // 0.12 m ABOVE where the hoof stands, and the direction is the whole point (ADR-344 SS5). This
    // pack binds its hind leg at 98.5% of its own span -- 4 mm of straightening left in a 0.90 m
    // leg -- so a foot layer on a farm animal can raise a foot onto higher ground and essentially
    // cannot lower one onto lower ground. Which is how foot IK is *supposed* to be used: the body
    // drops to the lowest contact and the solver lifts the rest, and the body drop is grounding's
    // job, one level up, because a pose layer structurally cannot move a body.
    const glm::vec3 target = hoofRest + glm::vec3(0.0f, 0.12f, 0.0f);

    SECTION("arm A: the hoof lands on the target and the body does not move") {
        scene::PoseLayer layer = bullRearLeft();
        layer.target = target;
        layer.hasTarget = true;
        const auto problems = bull.rig->layers.bind({layer}, sk, bull.rig->clips);
        INFO((problems.empty() ? std::string() : problems.front()));
        CHECK(problems.empty());

        bull.rig->pose = rest;
        const auto stats = bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
        CHECK(stats.applied == 1);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::Applied);
        CHECK(bull.rig->layers.ikStatuses().front() == scene::IkStatus::Solved);

        const auto after = jointPositions(sk, bull.rig->pose);
        const glm::vec3 hoof = after[static_cast<std::size_t>(sk.find("HoofB.L"))];
        CHECK(dist(hoof, target) < 1e-4f);
        // ADR-260 `PoseOnly`, and the half of it this layer has to earn: the *body* stays exactly
        // where the animation and the grounding put it. Not "nearly" -- a foot IK that moved the
        // hips would be root motion wearing a different hat.
        const glm::vec3 hip = after[static_cast<std::size_t>(sk.find("Hip"))];
        CHECK(dist(hip, hipRest) == 0.0f);
        // And the other three legs are untouched, to the bit.
        CHECK(dist(after[static_cast<std::size_t>(sk.find("HoofB.R"))], otherRest) == 0.0f);
        CHECK(dist(after[static_cast<std::size_t>(sk.find("HoofF.L"))],
                   jointAt(sk, rest, "HoofF.L")) == 0.0f);
        // The bones did not stretch. Measured hip-of-the-*leg* to knee: the pelvis-to-knee
        // distance is supposed to change, because that is the leg swinging.
        const glm::vec3 upper = after[static_cast<std::size_t>(sk.find("UpperLegB.L"))];
        const glm::vec3 knee = after[static_cast<std::size_t>(sk.find("LowerLegB.L"))];
        CHECK(dist(upper, knee) ==
              Approx(dist(jointAt(sk, rest, "UpperLegB.L"), jointAt(sk, rest, "LowerLegB.L")))
                  .margin(1e-4));
        CHECK(dist(knee, hoof) ==
              Approx(dist(jointAt(sk, rest, "LowerLegB.L"), hoofRest)).margin(1e-4));
    }

    SECTION("arm B (control): the same layer at weight zero moves nothing at all") {
        // The layer is the cause. Asserted as an exact zero rather than a small number, so a layer
        // that "nearly" did nothing cannot pass it.
        scene::PoseLayer layer = bullRearLeft();
        layer.target = target;
        layer.hasTarget = true;
        layer.weight = 0.0f;
        CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        const auto stats = bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
        CHECK(stats.applied == 0);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::Inactive);
        const auto after = jointPositions(sk, bull.rig->pose);
        CHECK(dist(after[static_cast<std::size_t>(sk.find("HoofB.L"))], hoofRest) == 0.0f);
    }

    SECTION("arm C (control): the same layer on the other leg moves the other hoof and not this one") {
        // The chain is the cause, not the layer. Delete the chain resolution and this passes
        // vacuously only if arm A also stops passing, which is the point of running both.
        scene::PoseLayer layer = bullRearLeft();
        layer.chainRoot = "UpperLegB.R";
        layer.chainMid = "LowerLegB.R";
        layer.chainTip = "HoofB.R";
        layer.target = otherRest + glm::vec3(0.0f, 0.12f, 0.0f);
        layer.hasTarget = true;
        CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 1);
        const auto after = jointPositions(sk, bull.rig->pose);
        CHECK(dist(after[static_cast<std::size_t>(sk.find("HoofB.R"))], layer.target) < 1e-4f);
        CHECK(dist(after[static_cast<std::size_t>(sk.find("HoofB.L"))], hoofRest) == 0.0f);
    }

    SECTION("arm D: an unreachable target clamps, reports it, and does not leave the animal") {
        scene::PoseLayer layer = bullRearLeft();
        layer.target = hoofRest - glm::vec3(0.0f, 5.0f, 0.0f); // five metres into the ground
        layer.hasTarget = true;
        layer.poleDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 1);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::Clamped);
        CHECK(bull.rig->layers.ikStatuses().front() == scene::IkStatus::Clamped);
        const auto after = jointPositions(sk, bull.rig->pose);
        const glm::vec3 hoof = after[static_cast<std::size_t>(sk.find("HoofB.L"))];
        CHECK(finite(hoof));
        // It went *towards* the target and stopped at the end of the leg. The band is the leg
        // itself: the hoof is at the hip's full span and no further, which is the honest answer to
        // "stand five metres down there" and is nothing like five metres.
        const glm::vec3 hip = after[static_cast<std::size_t>(sk.find("Hip"))];
        const glm::vec3 upper = after[static_cast<std::size_t>(sk.find("UpperLegB.L"))];
        const float span = dist(jointAt(sk, rest, "UpperLegB.L"), jointAt(sk, rest, "LowerLegB.L")) +
                           dist(jointAt(sk, rest, "LowerLegB.L"), hoofRest);
        CHECK(dist(upper, hoof) == Approx(span).margin(2e-3));
        CHECK(dist(hoof, layer.target) > 4.0f); // it did not reach, and does not pretend to
        CHECK(glm::dot(hoof - hoofRest, glm::vec3(0.0f, -1.0f, 0.0f)) > 0.0f); // but it tried
        CHECK(dist(hip, hipRest) == 0.0f);
    }

    SECTION("arm E: a target the hoof is already at is a solve that changes nothing") {
        // The rig-level twin of the solver's identity arm, and the one that would catch a writeback
        // that was subtly wrong about parent frames: every matrix in the chain is recomputed, and
        // the answer still has to be the pose that went in.
        scene::PoseLayer layer = bullRearLeft();
        layer.target = hoofRest;
        layer.hasTarget = true;
        CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 1);
        const auto after = jointPositions(sk, bull.rig->pose);
        for (std::size_t j = 0; j < after.size(); ++j) {
            INFO(sk.joints[j].name);
            CHECK(dist(after[j], jointPositions(sk, rest)[j]) < 1e-5f);
        }
    }
#endif
}

TEST_CASE("a foot layer is a pure function of the pose and the target", "[ik][layers][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb")) {
        SKIP("assets/farm is not present");
    }
    // ADR-091 wants scrub == play, and a layer that carried state would be the thing that broke it.
    // Three ways of asking the same question, all of which must give the identical pose:
    //   * the same base pose applied twice
    //   * the same base pose at a wildly different sample second
    //   * a pose reached by "seeking" (one apply) against one reached by "playing" (many applies
    //     at ascending seconds, all re-seeded from the same base, which is what `evaluate` does)
    Rig bull(farmDir() / "bull.glb");
    scene::Skeleton& sk = bull.rig->skeleton;
    const scene::Pose rest = bull.rig->pose;

    scene::PoseLayer layer = bullRearLeft();
    layer.target = jointAt(sk, rest, "HoofB.L") - glm::vec3(0.03f, 0.11f, 0.02f);
    layer.hasTarget = true;
    layer.poleDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());

    const auto evaluate = [&](double second) {
        bull.rig->pose = rest;
        bull.rig->layers.apply(sk, bull.rig->clips, second, bull.rig->pose);
        return bull.rig->pose;
    };

    const scene::Pose seek = evaluate(37.5);
    const scene::Pose again = evaluate(37.5);
    scene::Pose played;
    for (int i = 0; i <= 375; ++i) {
        played = evaluate(static_cast<double>(i) * 0.1);
    }
    const scene::Pose atZero = evaluate(0.0);

    REQUIRE(seek.size() == again.size());
    for (std::size_t j = 0; j < seek.size(); ++j) {
        INFO(sk.joints[j].name);
        // Bit-identical, not close. A tolerance here would hide exactly the drift it exists to find.
        CHECK(seek.local[j].position == again.local[j].position);
        CHECK(seek.local[j].rotation == again.local[j].rotation);
        CHECK(seek.local[j].position == played.local[j].position);
        CHECK(seek.local[j].rotation == played.local[j].rotation);
        // And the second is not an input at all to this layer kind, which is the strongest form of
        // the promise: there is no clock for a scrub to be in the wrong place on.
        CHECK(seek.local[j].position == atZero.local[j].position);
        CHECK(seek.local[j].rotation == atZero.local[j].rotation);
    }
#endif
}

TEST_CASE("every farm rig carries a solvable two-bone leg and the alien rig does not",
          "[ik][layers][farm][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb") || !fs::exists(alienDir() / "alien-scout.glb")) {
        SKIP("assets are not present");
    }
    // The finding that decided the shape of this unit, as an assertion rather than a paragraph.
    //
    // The farm pack spells the same anatomy three ways and every one of them is a chain. The alien
    // pack's leg is not a chain at all: `foot.l` is a direct child of `root.x`, the thigh is a
    // separate branch under the same parent, and `leg_stretch.l` is a sibling of `root.x` entirely.
    // Three names that all exist, under two different parents, describing nothing that bends.
    struct Leg {
        const char* file;
        const char* root;
        const char* mid;
        const char* tip;
    };
    static constexpr std::array<Leg, 9> kLegs{{
        {"bull", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"cow", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"horse", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"sheep", "UpperLegB.L", "LowerLegB.L", "FootB.L"},  // AnkleB.L rides along between them
        {"goat", "UpperLegB.L", "LowerLegB.L", "FootB.L"},
        {"pig", "UpperLegB.L", "LowerLegB.L", "FootB.L"},
        {"chicken", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
        {"rooster", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
        {"chick", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
    }};

    for (const Leg& leg : kLegs) {
        INFO(leg.file);
        Rig animal(farmDir() / (std::string(leg.file) + ".glb"));
        scene::Skeleton& sk = animal.rig->skeleton;
        scene::PoseLayer layer;
        layer.name = "rear-left";
        layer.kind = scene::PoseLayerKind::Foot;
        layer.chainRoot = leg.root;
        layer.chainMid = leg.mid;
        layer.chainTip = leg.tip;
        layer.weight = 1.0f;
        const scene::Pose rest = animal.rig->pose;
        const glm::vec3 foot = jointAt(sk, rest, leg.tip);
        // A tenth of the animal's own leg, raised, so a chick and a horse are asked for the same
        // thing in their own units rather than the same number of metres -- and raised rather than
        // lowered because this pack binds with no downward headroom at all (see the bind-pose
        // extension test below, which is where that number lives).
        const float legLength = dist(jointAt(sk, rest, leg.root), jointAt(sk, rest, leg.mid)) +
                                dist(jointAt(sk, rest, leg.mid), foot);
        layer.target = foot + glm::vec3(0.0f, legLength * 0.10f, 0.0f);
        layer.hasTarget = true;

        const auto problems = animal.rig->layers.bind({layer}, sk, animal.rig->clips);
        INFO((problems.empty() ? std::string() : problems.front()));
        CHECK(problems.empty());
        animal.rig->pose = rest;
        CHECK(animal.rig->layers.apply(sk, animal.rig->clips, 0.0, animal.rig->pose).applied == 1);
        CHECK(animal.rig->layers.results().front() == scene::LayerResolution::Applied);
        CHECK(dist(jointAt(sk, animal.rig->pose, leg.tip), layer.target) < 1e-4f);
    }

    SECTION("the alien's three leg-ish names bind to a complaint and write nothing") {
        Rig alien(alienDir() / "alien-scout.glb");
        scene::Skeleton& sk = alien.rig->skeleton;
        // Every one of these joints exists. That is the trap.
        REQUIRE(sk.find("thigh_stretch.l") >= 0);
        REQUIRE(sk.find("leg_stretch.l") >= 0);
        REQUIRE(sk.find("foot.l") >= 0);

        scene::PoseLayer layer;
        layer.name = "alien-left";
        layer.kind = scene::PoseLayerKind::Foot;
        layer.chainRoot = "thigh_stretch.l";
        layer.chainMid = "leg_stretch.l";
        layer.chainTip = "foot.l";
        layer.weight = 1.0f;
        layer.target = jointAt(sk, alien.rig->pose, "foot.l") - glm::vec3(0.0f, 0.2f, 0.0f);
        layer.hasTarget = true;

        const auto problems = alien.rig->layers.bind({layer}, sk, alien.rig->clips);
        REQUIRE_FALSE(problems.empty());
        INFO(problems.front());
        CHECK(problems.front().find("is not below") != std::string::npos);

        const scene::Pose before = alien.rig->pose;
        const auto stats = alien.rig->layers.apply(sk, alien.rig->clips, 0.0, alien.rig->pose);
        CHECK(stats.applied == 0);
        CHECK(alien.rig->layers.results().front() == scene::LayerResolution::NoChain);
        for (std::size_t j = 0; j < before.size(); ++j) {
            CHECK(alien.rig->pose.local[j].position == before.local[j].position);
            CHECK(alien.rig->pose.local[j].rotation == before.local[j].rotation);
        }
    }
#endif
}

TEST_CASE("foot planting lands four hooves on one slope and lays the soles on it",
          "[ik][layers][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb")) {
        SKIP("assets/farm is not present");
    }
    // The case the whole unit is for: a bull standing across a bank. One plane of intent, four
    // chains, each foot dropping onto the ground that is under *it* rather than all four converging
    // on one point -- which is the difference between planting and teleporting.
    Rig bull(farmDir() / "bull.glb");
    scene::Skeleton& sk = bull.rig->skeleton;
    const scene::Pose rest = bull.rig->pose;

    // An 18-degree bank, placed so that every hoof has to come *up* onto it. That is not a
    // convenience: the pack binds its legs at 98-100% of their own span, so a plant that asked any
    // hoof to go down would be asking for a leg that does not exist. The arm below asserts exactly
    // that case and what the layer says about it.
    //
    // The normal leans towards +X, so the surface falls towards +X; the plane is pinned through the
    // point that puts it at or above the highest hoof across the animal's whole width.
    const glm::vec3 normal = glm::normalize(glm::vec3(std::sin(0.3142f), std::cos(0.3142f), 0.0f));
    static constexpr std::array<const char*, 4> kFeet{{"HoofB.L", "HoofB.R", "HoofF.L", "HoofF.R"}};
    float lift = -1e9f;
    for (const char* f : kFeet) {
        const glm::vec3 p = jointAt(sk, rest, f);
        // The height the plane through the origin would give this foot, relative to the foot.
        lift = std::max(lift, p.y - (-normal.x * p.x - normal.z * p.z) / normal.y);
    }
    const glm::vec3 planePoint(0.0f, lift + 0.03f, 0.0f);

    std::vector<scene::PoseLayer> layers;
    static constexpr std::array<std::array<const char*, 3>, 4> kChains{{
        {"UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"UpperLegB.R", "LowerLegB.R", "HoofB.R"},
        {"UpperLegF.L", "LowerLegF.L", "HoofF.L"},
        {"UpperLegF.R", "LowerLegF.R", "HoofF.R"},
    }};
    for (const auto& c : kChains) {
        scene::PoseLayer layer;
        layer.name = c[2];
        layer.kind = scene::PoseLayerKind::Foot;
        layer.chainRoot = c[0];
        layer.chainMid = c[1];
        layer.chainTip = c[2];
        layer.groundPoint = planePoint;
        layer.groundNormal = normal;
        layer.hasGround = true;
        layer.footAlign = 1.0f;
        layer.weight = 1.0f;
        layers.push_back(layer);
    }
    const auto problems = bull.rig->layers.bind(layers, sk, bull.rig->clips);
    INFO((problems.empty() ? std::string() : problems.front()));
    CHECK(problems.empty());

    bull.rig->pose = rest;
    const auto stats = bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
    CHECK(stats.applied == 4);

    float spread = 0.0f;
    for (std::size_t i = 0; i < kFeet.size(); ++i) {
        INFO(kFeet[i]);
        CHECK(bull.rig->layers.results()[i] == scene::LayerResolution::Applied);
        const glm::vec3 before = jointAt(sk, rest, kFeet[i]);
        const glm::vec3 after = jointAt(sk, bull.rig->pose, kFeet[i]);
        // Planted means "standing on this slope the way it stands on the flat", not "the joint is
        // on the plane": a hoof joint is not the sole of the hoof, and on this rig it rests 0.120
        // model units above the ground (0.097 on the front leg). The plant preserves that height,
        // read out of the rest pose, so the residual along the normal is exactly it -- and a plant
        // that dropped the joint onto the plane instead would bury a quarter of every hoof.
        const float standsAt = jointAt(sk, rest, kFeet[i]).y;
        CHECK(standsAt > 0.05f); // the fixture's premise, so a re-export cannot make this vacuous
        CHECK(standsAt < 0.20f);
        CHECK(glm::dot(after - planePoint, normal) == Approx(standsAt * normal.y).margin(2e-4));
        // Straight down, not sideways: a plant must not slide the hoof across the ground.
        CHECK(after.x == Approx(before.x).margin(1e-4));
        CHECK(after.z == Approx(before.z).margin(1e-4));
        spread = std::max(spread, std::abs(after.y - before.y));
    }
    // A control against a plant that did nothing: an 18-degree bank across a bull 0.76 m wide must
    // have moved at least one hoof by a centimetre, and none of them by more than a leg.
    CHECK(spread > 0.01f);
    CHECK(spread < 0.60f);

    SECTION("a plane that asks a hoof to go down says Clamped rather than pretending") {
        // The finding, as an arm. Dropped a quarter of a metre under the animal, the plane is
        // below three of the four hooves and out of every one of their reach; the layer reports it
        // per foot instead of silently leaving the hoof somewhere plausible.
        std::vector<scene::PoseLayer> low = layers;
        for (scene::PoseLayer& l : low) {
            l.groundPoint = glm::vec3(0.0f, planePoint.y - 0.30f, 0.0f);
        }
        CHECK(bull.rig->layers.bind(low, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 4);
        int clamped = 0;
        for (std::size_t i = 0; i < kFeet.size(); ++i) {
            INFO(kFeet[i]);
            const glm::vec3 after = jointAt(sk, bull.rig->pose, kFeet[i]);
            CHECK(finite(after));
            if (bull.rig->layers.results()[i] == scene::LayerResolution::Clamped) {
                ++clamped;
                // Clamped means "at the end of my leg", and the residual is what a body drop would
                // have to make up. It is bounded by the leg, not by the ask.
                CHECK(glm::dot(after - low[i].groundPoint, normal) > 0.0f);
                CHECK(glm::dot(after - low[i].groundPoint, normal) < 0.50f);
            }
        }
        CHECK(clamped >= 3);
    }

    SECTION("the soles tilt onto the slope by the bank angle, and do not when footAlign is zero") {
        // `footAlign` is the per-foot twin of `entity::GroundSettings::slopeAlign`, and this is the
        // control that says it is doing the work rather than the solve.
        //
        // Measured against **world up**, not against the slope normal. Against the normal this arm
        // would be circular -- the layer rotates the sole onto the normal and the test would be
        // asking whether it did what it just did. Against world up it is a prediction with a
        // number in it: an 18-degree bank must tilt each hoof 18 degrees away from where it stood,
        // and the tilt must be *towards* the bank.
        //
        // The direction measured is the tip joint's rest-pose up, which on this pack is not any
        // cardinal axis of the joint: the hoof joints run along the bone, and a bull's hind hoof
        // has its +Y 42.8 degrees from vertical. Reading it out of the rest pose is what the layer
        // does and the reason it does it.
        std::vector<glm::mat4> restModel;
        scene::poseToModel(sk, rest, restModel);
        const auto soleUpOf = [&](const char* joint) {
            const auto j = static_cast<std::size_t>(sk.find(joint));
            return glm::normalize(glm::inverse(glm::mat3(restModel[j])) * glm::vec3(0.0f, 1.0f, 0.0f));
        };
        const auto tiltFromVertical = [&](const scene::Pose& pose, const char* joint) {
            std::vector<glm::mat4> model;
            scene::poseToModel(sk, pose, model);
            const auto j = static_cast<std::size_t>(sk.find(joint));
            const glm::vec3 up = glm::normalize(glm::mat3(model[j]) * soleUpOf(joint));
            return std::acos(std::clamp(up.y, -1.0f, 1.0f)) * 57.29578f;
        };
        // 0.3142 rad = 18.0 degrees, which is the bank this fixture is built on. All four hooves,
        // each solved by its own layer with no knowledge of the others, land on the same angle.
        float alignedLow = 1e9f;
        float alignedHigh = -1e9f;
        for (const char* f : kFeet) {
            INFO(f);
            const float tilt = tiltFromVertical(bull.rig->pose, f);
            CHECK(tilt == Approx(18.0f).margin(0.5f));
            alignedLow = std::min(alignedLow, tilt);
            alignedHigh = std::max(alignedHigh, tilt);
        }

        std::vector<scene::PoseLayer> flat = layers;
        for (scene::PoseLayer& l : flat) {
            l.footAlign = 0.0f;
        }
        CHECK(bull.rig->layers.bind(flat, sk, bull.rig->clips).empty());
        bull.rig->pose = rest;
        bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
        // With the alignment off the hooves still plant -- and they come out at four *different*
        // angles, none of them the bank, because the tip joint rides whatever rotations the hip
        // and the knee needed to reach the target. On this fixture that is 27, 27 and 56 degrees
        // against the aligned 18, and the 56 is a front hoof turned over on its edge.
        //
        // This is the thing "the foot reached the target" cannot see, and it is why `footAlign`
        // exists rather than being an option: a plant without it is a foot in the right place at
        // the wrong angle, which reads as a broken leg and passes every distance assertion above.
        float flatLow = 1e9f;
        float flatHigh = -1e9f;
        for (const char* f : kFeet) {
            const float tilt = tiltFromVertical(bull.rig->pose, f);
            flatLow = std::min(flatLow, tilt);
            flatHigh = std::max(flatHigh, tilt);
        }
        CHECK(alignedHigh - alignedLow < 1.0f);  // aligned: four feet, one angle
        CHECK(flatHigh - flatLow > 10.0f);       // unaligned: four feet, four angles
        CHECK(flatHigh - flatLow < 120.0f);      // and still an animal, not a scatter
        CHECK(flatHigh > 30.0f);
    }
#endif
}

TEST_CASE("a foot layer says so when it is handed a mask or half a chain", "[ik][layers][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb")) {
        SKIP("assets/farm is not present");
    }
    Rig bull(farmDir() / "bull.glb");
    scene::Skeleton& sk = bull.rig->skeleton;

    SECTION("a name this rig does not carry") {
        scene::PoseLayer layer = bullRearLeft();
        layer.chainTip = "Hoof01.L"; // the name a Mixamo-shaped rig would use
        const auto problems = bull.rig->layers.bind({layer}, sk, bull.rig->clips);
        REQUIRE_FALSE(problems.empty());
        CHECK(problems.front().find("Hoof01.L") != std::string::npos);
        bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::NoChain);
    }

    SECTION("a chain of three names this rig carries none of is still NoChain") {
        // Not `NoJoints`. A foot layer's mask is synthesised from its chain, so an empty one is a
        // limb this rig does not have rather than a mask that missed, and the two answers send a
        // reader to different places.
        scene::PoseLayer layer = bullRearLeft();
        layer.chainRoot = "LeftUpLeg";
        layer.chainMid = "LeftLeg";
        layer.chainTip = "LeftFoot"; // a Mixamo rig's names, on a Blender-derived bull
        const auto problems = bull.rig->layers.bind({layer}, sk, bull.rig->clips);
        CHECK(problems.size() >= 3);
        bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::NoChain);
    }

    SECTION("an authored mask is reported rather than silently ignored") {
        // Half a knee does not reach half a target, so a mask on a foot layer could only ever be a
        // no-op. An unreported no-op is the failure this module exists to stop repeating.
        scene::PoseLayer layer = bullRearLeft();
        layer.mask.joints = {"Head01"};
        layer.target = jointAt(sk, bull.rig->pose, "HoofB.L");
        layer.hasTarget = true;
        const auto problems = bull.rig->layers.bind({layer}, sk, bull.rig->clips);
        REQUIRE_FALSE(problems.empty());
        CHECK(problems.front().find("ignores") != std::string::npos);
        // And it still works, because the chain is what drives it.
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 1);
    }

    SECTION("a layer with neither a target nor a ground plane has been asked for nothing") {
        scene::PoseLayer layer = bullRearLeft();
        CHECK(bull.rig->layers.bind({layer}, sk, bull.rig->clips).empty());
        const scene::Pose before = bull.rig->pose;
        CHECK(bull.rig->layers.apply(sk, bull.rig->clips, 0.0, bull.rig->pose).applied == 0);
        CHECK(bull.rig->layers.results().front() == scene::LayerResolution::NoTarget);
        for (std::size_t j = 0; j < before.size(); ++j) {
            CHECK(bull.rig->pose.local[j].position == before.local[j].position);
        }
    }
#endif
}

TEST_CASE("a farm leg stands nearly straight, which is what bounds what foot IK can do here",
          "[ik][layers][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(farmDir() / "bull.glb")) {
        SKIP("assets/farm is not present");
    }
    // The measurement that decides how this layer may be used, asserted rather than asserted-about.
    //
    // Every rig in `assets/farm` is modelled standing, and standing on these rigs means the hip,
    // the knee and the hoof are very nearly collinear: hip-to-hoof is 97.9% to 100.0% of
    // upper + lower, and the bull's *front* leg is at 100.0% with a knee angle of 177.7 degrees.
    // Two consequences, and both of them are the shape of the feature:
    //
    //   1. A foot layer on this pack can RAISE a foot and cannot LOWER one. There are four
    //      millimetres of straightening left in a bull's 0.90 m hind leg. Foot planting on a slope
    //      therefore has to be the standard two-part move -- the body drops to the lowest contact,
    //      the solver lifts every other foot onto the surface -- and the body drop belongs to
    //      `entity::GroundFollower`, one level up, because a pose layer structurally cannot move a
    //      body (ADR-260).
    //   2. `extension` cannot default below 1. At 0.99 the solver would report a bull's front hoof
    //      as out of reach while it stood exactly where the artist put it.
    //
    // The Walk clip is sampled too, because "the bind pose is straight" would be a curiosity if the
    // animation bent the knees; it does not bend them by much, and the band says how much.
    struct Leg {
        const char* file;
        const char* root;
        const char* mid;
        const char* tip;
    };
    static constexpr std::array<Leg, 9> kLegs{{
        {"bull", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"cow", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"horse", "UpperLegB.L", "LowerLegB.L", "HoofB.L"},
        {"sheep", "UpperLegB.L", "LowerLegB.L", "FootB.L"},
        {"goat", "UpperLegB.L", "LowerLegB.L", "FootB.L"},
        {"pig", "UpperLegB.L", "LowerLegB.L", "FootB.L"},
        {"chicken", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
        {"rooster", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
        {"chick", "UpperLeg.L", "LowerLeg.L", "Foot.L"},
    }};

    float leastBindExtension = 2.0f;
    float mostWalkFold = 0.0f;
    for (const Leg& leg : kLegs) {
        INFO(leg.file);
        Rig animal(farmDir() / (std::string(leg.file) + ".glb"));
        scene::Skeleton& sk = animal.rig->skeleton;
        const auto extensionOf = [&](const scene::Pose& pose) {
            const glm::vec3 a = jointAt(sk, pose, leg.root);
            const glm::vec3 b = jointAt(sk, pose, leg.mid);
            const glm::vec3 c = jointAt(sk, pose, leg.tip);
            return dist(a, c) / (dist(a, b) + dist(b, c));
        };
        const float bind = extensionOf(animal.rig->pose);
        CHECK(bind > 0.97f);
        CHECK(bind <= 1.0f);
        leastBindExtension = std::min(leastBindExtension, bind);

        // And across the whole walk cycle, which is the pose that actually renders.
        REQUIRE(animal.rig->clips.size() == 1);
        const scene::AnimationClip& walk = animal.rig->clips.front();
        float least = 2.0f;
        scene::Pose pose;
        for (int i = 0; i <= 40; ++i) {
            scene::setRestPose(sk, pose);
            scene::sampleClip(walk, walk.start + walk.length() * (static_cast<float>(i) / 40.0f), pose);
            least = std::min(least, extensionOf(pose));
        }
        // The walk does bend the knee, and not by much: nowhere near enough to give a standing
        // animal room to lower a foot. A band, so that "the clip animates the leg" is not enough to
        // pass and "the clip folds it double" would not be either.
        CHECK(least > 0.80f);
        CHECK(least < bind);
        mostWalkFold = std::max(mostWalkFold, bind - least);
    }
    // Across the nine, the tightest bind pose and the deepest the walk ever folds. Stated as bands
    // so that a re-export that changed the pack's rest pose would be visible here rather than as a
    // mysterious herd of clamped feet.
    CHECK(leastBindExtension > 0.97f);
    CHECK(leastBindExtension < 0.995f);
    CHECK(mostWalkFold > 0.01f);
    CHECK(mostWalkFold < 0.20f);
#endif
}

TEST_CASE("a foot layer survives a scene save and reload", "[ik][layers][scene]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path source = fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "footik" /
                            "foot-ik-lab.scene.json";
    if (!fs::exists(source) || !fs::exists(farmDir() / "bull.glb")) {
        SKIP("the foot IK lab scene or the farm assets are not present");
    }
    // The shipped lab scene, loaded, serialised back out, and loaded again. Round-tripping is where
    // a layer kind with its own key set goes wrong quietly: written through the shared path this
    // one came out with `"joints": []` and `"clip": ""`, which is a file that does not load,
    // produced by a save of a file that did. The second load is what catches it -- checking the
    // JSON alone would pass on a writer that emitted keys the parser happens to ignore.
    assets::AssetRegistry registry(source.parent_path());
    auto loaded = scene::Composition::loadFile(source, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());

    const nlohmann::json written = (*loaded)->toJson();
    const fs::path round = fs::temp_directory_path() / "avgen-footik-roundtrip.scene.json";
    std::ofstream(round) << written.dump(1);
    auto again = scene::Composition::loadFile(round, registry);
    INFO((again.has_value() ? std::string() : again.error().message));
    REQUIRE(again.has_value());
    fs::remove(round);

    // Four foot layers, with their chains, on the far side of the trip.
    const auto layersOf = [](const scene::Composition& comp) {
        std::vector<scene::PoseLayer> out;
        for (const auto& node : comp.nodes()) {
            for (const scene::PoseLayer& layer : node->animation.layers) {
                out.push_back(layer);
            }
        }
        return out;
    };
    const std::vector<scene::PoseLayer> before = layersOf(**loaded);
    const std::vector<scene::PoseLayer> after = layersOf(**again);
    REQUIRE(before.size() == 4);
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        INFO(before[i].name);
        CHECK(after[i].name == before[i].name);
        CHECK(after[i].kind == scene::PoseLayerKind::Foot);
        CHECK(after[i].drive == scene::PoseLayerDrive::Ground);
        CHECK(after[i].chainRoot == before[i].chainRoot);
        CHECK(after[i].chainMid == before[i].chainMid);
        CHECK(after[i].chainTip == before[i].chainTip);
        CHECK(after[i].footAlign == before[i].footAlign);
        CHECK(after[i].extension == before[i].extension);
        CHECK_FALSE(after[i].chainTip.empty());
    }
#endif
}
