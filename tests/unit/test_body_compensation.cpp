// Reachable contact solving (ADR-544): the stage that runs before a limb, and decides whether the
// body has to move at all.
//
// **The standing rule this file exists under.** ADR-543 was found because a null-request control
// arm scored a broken implementation as perfect: asked to put a foot where the foot already was,
// an implementation that does nothing is exactly right. So every arm here asks for something the
// current state does NOT satisfy, and requires movement toward a known answer. The one arm that
// does ask for nothing (`NotNeeded`) is paired with an assertion that the translation is *exactly*
// zero -- a solver that always returns a small nudge fails it -- and it is never the only evidence.
//
// The arms:
//
//   not-needed    every demand already inside its limb; the translation is bit-zero
//   exact         one demand, short by a known amount; the body moves by exactly that amount
//   alien         the real 0.0902 m the Glowmere leg needs for a 10 cm step down (ADR-543)
//   limited       a demand further than the limits allow; the body goes to the limit and SAYS so
//   impossible    two limbs pulling apart; no translation helps, and the status distinguishes that
//                 from a limit that could be raised
//   compliance    a frozen axis is not moved along, and the shortfall is reported rather than hidden
//   many          four limbs at once; the answer satisfies all four, which one limb's answer does not
//   determinism   the same demands twice give bit-identical translations

#include "scene/ik.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using avgen::scene::BodyCompensation;
using avgen::scene::BodyCompensationLimits;
using avgen::scene::BodyCompensationStatus;
using avgen::scene::ReachDemand;
using avgen::scene::solveBodyCompensation;
using Catch::Approx;

namespace {

// How far short a limb is, given a body translation. The tests assert against this rather than
// against the solver's own report wherever possible, so a solver that mis-reports its own success
// cannot pass by agreeing with itself.
float shortfall(const ReachDemand& d, const glm::vec3& t) {
    return glm::length(d.target - (d.root + t)) - d.reach;
}

} // namespace

TEST_CASE("a body with every limb in reach does not move at all", "[ik][compensation]") {
    // The null arm. It is here to catch a solver that always nudges, and it is deliberately not
    // the only arm in this file: on its own it is satisfied by a function that returns zero.
    const std::array<ReachDemand, 2> demands{{
        {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.5f, 0.0f), 0.8f},
        {glm::vec3(0.2f, 1.0f, 0.0f), glm::vec3(0.2f, 0.6f, 0.0f), 0.8f},
    }};
    const BodyCompensation out = solveBodyCompensation(demands);
    CHECK(out.status == BodyCompensationStatus::NotNeeded);
    CHECK(out.translation == glm::vec3(0.0f)); // exactly, not approximately
    CHECK(out.unreachableBefore == 0);
    CHECK(out.shortfallBefore == 0.0f);
    CHECK(out.sweeps == 0);

    SECTION("and neither does a body with no limbs asking for anything") {
        const BodyCompensation none = solveBodyCompensation({});
        CHECK(none.status == BodyCompensationStatus::NotNeeded);
        CHECK(none.translation == glm::vec3(0.0f));
    }
}

TEST_CASE("one limb short by a known amount moves the body by exactly that amount",
          "[ik][compensation]") {
    // THE POSITIVE ARM. The hip is at y=1, the limb reaches 0.80, and the foot is wanted at y=0.10
    // -- 0.90 away, so it is short by exactly 0.10. The body must come down 0.10 and no further.
    ReachDemand d;
    d.root = glm::vec3(0.0f, 1.0f, 0.0f);
    d.target = glm::vec3(0.0f, 0.10f, 0.0f);
    d.reach = 0.80f;
    REQUIRE(shortfall(d, glm::vec3(0.0f)) == Approx(0.10f).margin(1e-6));

    const std::array<ReachDemand, 1> demands{d};
    const BodyCompensation out = solveBodyCompensation(demands);

    CHECK(out.status == BodyCompensationStatus::Solved);
    CHECK(out.shortfallBefore == Approx(0.10f).margin(1e-5));
    CHECK(out.unreachableBefore == 1);
    // It moved DOWN by the shortfall. A solver that returns zero fails the first line; one that
    // over-corrects fails the second; one that moves sideways fails the third and fourth.
    CHECK(out.translation.y == Approx(-0.10f).margin(1e-5));
    CHECK(out.translation.x == Approx(0.0f).margin(1e-6));
    CHECK(out.translation.z == Approx(0.0f).margin(1e-6));
    // And the limb can now reach, measured independently of what the solver claims.
    CHECK(shortfall(d, out.translation) <= 1e-5f);
    CHECK(out.unreachableAfter == 0);
    CHECK(out.shortfallAfter <= 1e-5f);
}

TEST_CASE("the Glowmere alien's measured leg needs the hip drop ADR-543 measured", "[ik][compensation]") {
    // The numbers are the real ones from `tools/motion_probe.cpp` against `alien-scout.glb`, in the
    // rig's own model units: hip at y=0.7785, foot at y=0.1377, limb 0.2819 + 0.3736 = 0.6555. The
    // leg binds at 98.5% extension with 0.0098 of slack, so a 10 cm step down is 0.0902 out of
    // reach. This is the arm that ties the primitive to the character it was built for.
    ReachDemand leg;
    leg.root = glm::vec3(0.1125f, 0.7785f, 0.0330f);
    leg.target = glm::vec3(0.1356f, 0.1377f - 0.10f, -0.0436f);
    leg.reach = 0.2819f + 0.3736f;

    const std::array<ReachDemand, 1> demands{leg};
    BodyCompensationLimits limits; // the defaults: 0.25 down, 0.05 up, 0.05 lateral
    const BodyCompensation out = solveBodyCompensation(demands, limits);

    CHECK(out.status == BodyCompensationStatus::Solved);
    CHECK(out.shortfallBefore == Approx(0.0896f).margin(2e-3)); // the probe's 0.0896 short
    CHECK(out.translation.y < -0.08f);
    CHECK(out.translation.y > -0.10f);
    CHECK(shortfall(leg, out.translation) <= 1e-5f);

    SECTION("and a raise needs no compensation at all, because up is where the slack is not") {
        ReachDemand raise = leg;
        raise.target = glm::vec3(0.1356f, 0.1377f + 0.15f, -0.0436f);
        const std::array<ReachDemand, 1> up{raise};
        const BodyCompensation none = solveBodyCompensation(up, limits);
        CHECK(none.status == BodyCompensationStatus::NotNeeded);
        CHECK(none.translation == glm::vec3(0.0f));
    }
}

TEST_CASE("a demand beyond the body's limits goes to the limit and says it is still short",
          "[ik][compensation]") {
    // A step down three times the limb's length. No amount of hip drop this character is allowed
    // will reach it, and the honest answer is "I went as far as I may, and I am still 1.95 short"
    // -- not a silent clamp, and not a body folded through the floor.
    ReachDemand d;
    d.root = glm::vec3(0.0f, 1.0f, 0.0f);
    d.target = glm::vec3(0.0f, -2.0f, 0.0f);
    d.reach = 0.80f;
    BodyCompensationLimits limits;
    limits.maxDown = 0.25f;

    const std::array<ReachDemand, 1> demands{d};
    const BodyCompensation out = solveBodyCompensation(demands, limits);

    CHECK(out.status == BodyCompensationStatus::Limited);
    // It DID move -- all the way to the limit, not part way and not at all.
    CHECK(out.translation.y == Approx(-0.25f).margin(1e-5));
    CHECK(out.shortfallBefore == Approx(2.20f).margin(1e-4));
    CHECK(out.shortfallAfter == Approx(1.95f).margin(1e-4));
    CHECK(out.unreachableAfter == 1);
    // The reported shortfall is the real one, checked independently.
    CHECK(shortfall(d, out.translation) == Approx(out.shortfallAfter).margin(1e-5));
}

TEST_CASE("two limbs pulling apart is impossible rather than merely limited", "[ik][compensation]") {
    // One foot wanted far below, the other far above, both beyond their limbs. No single body
    // translation satisfies both, and raising the limits would not change that. The status has to
    // tell those two cases apart, because one of them is a tuning problem and the other is not.
    const std::array<ReachDemand, 2> demands{{
        {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0.50f},
        {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 2.0f, 0.0f), 0.50f},
    }};
    BodyCompensationLimits limits;
    limits.maxDown = 5.0f; // deliberately generous: the limits are not what is stopping it
    limits.maxUp = 5.0f;
    limits.maxLateral = 5.0f;

    const BodyCompensation out = solveBodyCompensation(demands, limits);
    CHECK(out.status == BodyCompensationStatus::Impossible);
    CHECK(out.unreachableBefore == 2);
    CHECK(out.unreachableAfter >= 1);
    // It is nowhere near a limit -- that is what makes it impossible rather than limited.
    CHECK(std::abs(out.translation.y) < 4.0f);
}

TEST_CASE("a frozen axis is not moved along and the shortfall is reported", "[ik][compensation]") {
    // Lateral compliance of zero: the demand is entirely sideways, so the body must not move and
    // must not pretend it succeeded.
    ReachDemand d;
    d.root = glm::vec3(0.0f, 1.0f, 0.0f);
    d.target = glm::vec3(1.0f, 1.0f, 0.0f);
    d.reach = 0.50f;
    BodyCompensationLimits limits;
    limits.compliance = glm::vec3(0.0f, 1.0f, 0.0f);

    const std::array<ReachDemand, 1> demands{d};
    const BodyCompensation out = solveBodyCompensation(demands, limits);
    CHECK(out.status == BodyCompensationStatus::Limited);
    CHECK(out.translation.x == Approx(0.0f).margin(1e-6));
    CHECK(out.shortfallAfter == Approx(0.50f).margin(1e-4));

    SECTION("and with that axis freed, the same demand is satisfied") {
        BodyCompensationLimits free = limits;
        free.compliance = glm::vec3(1.0f, 1.0f, 1.0f);
        free.maxLateral = 1.0f;
        const BodyCompensation moved = solveBodyCompensation(demands, free);
        CHECK(moved.status == BodyCompensationStatus::Solved);
        CHECK(moved.translation.x == Approx(0.50f).margin(1e-4));
        CHECK(shortfall(d, moved.translation) <= 1e-5f);
    }
}

TEST_CASE("four limbs at once get an answer that satisfies all four", "[ik][compensation]") {
    // The case a single-limb solver gets wrong: each foot alone would ask for a different drop, and
    // the body has only one. The answer must satisfy the *worst* of them, and the arm checks every
    // limb independently rather than trusting the summary.
    std::vector<ReachDemand> demands;
    const std::array<float, 4> drops{0.02f, 0.06f, 0.11f, 0.04f};
    for (std::size_t i = 0; i < drops.size(); ++i) {
        ReachDemand d;
        d.root = glm::vec3(static_cast<float>(i) * 0.3f, 1.0f, 0.0f);
        d.reach = 0.80f;
        d.target = glm::vec3(static_cast<float>(i) * 0.3f, 1.0f - 0.80f - drops[i], 0.0f);
        demands.push_back(d);
    }
    BodyCompensationLimits limits;
    limits.maxDown = 0.5f;

    const BodyCompensation out = solveBodyCompensation(demands, limits);
    CHECK(out.status == BodyCompensationStatus::Solved);
    CHECK(out.unreachableBefore == 4);
    CHECK(out.unreachableAfter == 0);
    // Deep enough for the worst foot...
    CHECK(out.translation.y <= -0.11f + 1e-4f);
    // ...and not deeper than the worst foot needed, which is the half a "just drop a lot" solver
    // would fail.
    CHECK(out.translation.y >= -0.13f);
    for (const ReachDemand& d : demands) {
        INFO(d.root.x);
        CHECK(shortfall(d, out.translation) <= 1e-5f);
    }
}

TEST_CASE("the same demands twice give bit-identical translations", "[ik][compensation]") {
    // ADR-541: this runs inside pose evaluation, which two renders of the same frame must agree on.
    // There is no seed here and no accumulator, and this is the arm that keeps it that way.
    std::vector<ReachDemand> demands;
    for (int i = 0; i < 5; ++i) {
        ReachDemand d;
        d.root = glm::vec3(0.11f * static_cast<float>(i), 1.0f, 0.07f * static_cast<float>(i));
        // 0.15 below what the limb can reach: inside the default 0.25 of allowed hip drop, so the
        // arm below asserts `Solved`. It asked for 0.97 on the first draft, which is past the
        // limit, and the answer was `Limited` -- a wrong fixture, not a wrong solver.
        d.target = glm::vec3(0.11f * static_cast<float>(i), 0.15f, 0.07f * static_cast<float>(i));
        d.reach = 0.7f + 0.01f * static_cast<float>(i);
        demands.push_back(d);
    }
    const BodyCompensation a = solveBodyCompensation(demands);
    const BodyCompensation b = solveBodyCompensation(demands);
    CHECK(a.translation.x == b.translation.x);
    CHECK(a.translation.y == b.translation.y);
    CHECK(a.translation.z == b.translation.z);
    CHECK(a.sweeps == b.sweeps);
    CHECK(a.status == b.status);
    // And it actually did something, so the equality above is not two zeroes agreeing.
    CHECK(a.status == BodyCompensationStatus::Solved);
    CHECK(a.translation.y < -1e-3f);
}

// ---- and now on the real character ---------------------------------------------------------------
//
// Everything above is arithmetic. This is the Glowmere alien, loaded through the real importer,
// posed through the real `PoseLayerStack`, with the real solver -- the arm that says the primitive
// and the rig actually meet.

#include "assets/gltf_loader.hpp"
#include "scene/pose_layers.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <filesystem>

namespace {

namespace fs = std::filesystem;

fs::path alienPath() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
#else
    return {};
#endif
}

glm::vec3 jointPos(const avgen::scene::Skeleton& sk, const avgen::scene::Pose& pose, const char* name) {
    std::vector<glm::mat4> model;
    avgen::scene::poseToModel(sk, pose, model);
    const int j = sk.find(name);
    return j < 0 ? glm::vec3(0.0f) : glm::vec3(model[static_cast<std::size_t>(j)][3]);
}

} // namespace

TEST_CASE("the alien's foot reaches ground its leg alone cannot, by lowering the body",
          "[ik][compensation][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(alienPath())) {
        SKIP("assets are not present");
    }
    avgen::scene::Scene scene;
    avgen::assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(avgen::assets::loadGltf(alienPath(), scene, options));
    REQUIRE_FALSE(scene.rigs.empty());
    avgen::scene::SkinnedRig& rig = scene.rigs.front();
    const avgen::scene::Skeleton& sk = rig.skeleton;

    avgen::scene::PoseLayer layer;
    layer.name = "left-foot";
    layer.kind = avgen::scene::PoseLayerKind::Foot;
    layer.chainRoot = "thigh_twist.l";
    layer.chainMid = "leg_stretch.l";
    layer.chainTip = "foot.l";
    layer.weight = 1.0f;
    layer.footAlign = 0.0f;

    const avgen::scene::Pose rest = restPose(sk);
    const glm::vec3 foot0 = jointPos(sk, rest, "foot.l");
    const glm::vec3 head0 = jointPos(sk, rest, "head.x");
    // A 10 cm step down: the exact case ADR-543 measured as 0.0896 out of reach.
    layer.target = foot0 - glm::vec3(0.0f, 0.10f, 0.0f);
    layer.hasTarget = true;

    SECTION("without compensation the leg clamps and the foot stops short") {
        rig.layers.setBodyCompensation({}); // disabled
        REQUIRE(rig.layers.bind({layer}, sk, rig.clips).empty());
        rig.pose = rest;
        const auto stats = rig.layers.apply(sk, rig.clips, 0.0, rig.pose);
        CHECK(stats.bodyCompensations == 0);
        CHECK(rig.layers.results().front() == avgen::scene::LayerResolution::Clamped);
        const glm::vec3 landed = jointPos(sk, rig.pose, "foot.l");
        // It went down -- a clamp is not a refusal -- but not far enough.
        CHECK(landed.y < foot0.y);
        CHECK(landed.y > layer.target.y + 0.05f);
        // And the head did not move, because nothing moved the body.
        CHECK(glm::length(jointPos(sk, rig.pose, "head.x") - head0) < 1e-5f);
    }

    SECTION("with compensation the body drops and the foot ARRIVES") {
        avgen::scene::BodyCompensationSpec spec;
        spec.enabled = true; // joint empty -> the skeleton root, which on this rig is the armature
        rig.layers.setBodyCompensation(spec);
        REQUIRE(rig.layers.bind({layer}, sk, rig.clips).empty());
        REQUIRE(rig.layers.bodyJoint() == 0);
        rig.pose = rest;
        const auto stats = rig.layers.apply(sk, rig.clips, 0.0, rig.pose);

        CHECK(stats.bodyCompensations == 1);
        CHECK(stats.detachedChains == 1); // still the detached chain of ADR-543
        const avgen::scene::BodyCompensation& body = rig.layers.bodyCompensation();
        CHECK(body.status == avgen::scene::BodyCompensationStatus::Solved);
        // The probe's number, arrived at by the engine rather than by hand.
        CHECK(body.translation.y == Approx(-0.0902f).margin(3e-3));

        // THE POSITIVE ARM: the foot is where it was asked to be, which it was NOT in the section
        // above with the same target and the same solver.
        const glm::vec3 landed = jointPos(sk, rig.pose, "foot.l");
        CHECK(glm::length(landed - layer.target) < 2e-3f);
        CHECK(rig.layers.results().front() == avgen::scene::LayerResolution::Applied);
        CHECK(rig.layers.ikStatuses().front() == avgen::scene::IkStatus::Solved);

        // And the body really did come with it: the head is lower by the compensation, which is
        // the difference between "the leg stretched" and "the character crouched".
        const glm::vec3 head1 = jointPos(sk, rig.pose, "head.x");
        CHECK(head1.y == Approx(head0.y + body.translation.y).margin(1e-4));
        CHECK(head1.y < head0.y - 0.05f);
    }

    SECTION("a foot it can already reach moves no body at all") {
        avgen::scene::BodyCompensationSpec spec;
        spec.enabled = true;
        rig.layers.setBodyCompensation(spec);
        REQUIRE(rig.layers.bind({layer}, sk, rig.clips).empty());
        rig.layers.layers().front().target = foot0 + glm::vec3(0.0f, 0.10f, 0.0f);
        rig.pose = rest;
        const auto stats = rig.layers.apply(sk, rig.clips, 0.0, rig.pose);
        CHECK(stats.bodyCompensations == 0);
        CHECK(rig.layers.bodyCompensation().status ==
              avgen::scene::BodyCompensationStatus::NotNeeded);
        CHECK(glm::length(jointPos(sk, rig.pose, "head.x") - head0) < 1e-5f);
        // ...and the foot still arrived, so "no compensation" is not "no solve".
        CHECK(glm::length(jointPos(sk, rig.pose, "foot.l") -
                          rig.layers.layers().front().target) < 1e-3f);
    }
#endif
}

// ---- and it has to survive a file ----------------------------------------------------------------

#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

TEST_CASE("body compensation survives a scene save and reload", "[ik][compensation][scene]") {
    // The scene below places the alien. Without the gitignored asset there is no node to read, and
    // `nodes().front()` on the empty list crashed the whole binary on CI rather than skipping.
    if (!fs::exists(alienPath())) {
        SKIP("alien asset missing: " << alienPath().string());
    }
    // Same hazard `test_foot_ik.cpp`'s round-trip arm exists for: a block with its own key set is
    // exactly the thing a shared writer turns into a file that will not load. Checking the JSON
    // alone would pass on a writer that emits keys the parser ignores, so the second *load* is the
    // assertion, and the values are compared on the far side of it.
    const nlohmann::json doc = {
        {"format", "avgen-scene"},
        {"version", 1},
        {"nodes",
         nlohmann::json::array(
             {{{"name", "walker"},
               {"kind", "gltf"},
               {"asset", "../../assets/aliens/alien-scout.glb"},
               {"animation",
                {{"state", "Idle"},
                 {"bodyCompensation",
                  {{"enabled", true},
                   {"joint", "rig"},
                   {"maxDown", 0.31f},
                   {"maxUp", 0.07f},
                   {"maxLateral", 0.02f},
                   {"compliance", nlohmann::json::array({0.2f, 1.0f, 0.3f})},
                   {"iterations", 12}}}}}}})}};

    const fs::path first = fs::temp_directory_path() / "avgen-bodycomp-a.scene.json";
    std::ofstream(first) << doc.dump(1);
    // The registry root the relative asset path above is resolved against.
    avgen::assets::AssetRegistry registry(fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs");
    auto loaded = avgen::scene::Composition::loadFile(first, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());

    const nlohmann::json written = (*loaded)->toJson();
    const fs::path round = fs::temp_directory_path() / "avgen-bodycomp-b.scene.json";
    std::ofstream(round) << written.dump(1);
    auto again = avgen::scene::Composition::loadFile(round, registry);
    INFO((again.has_value() ? std::string() : again.error().message));
    REQUIRE(again.has_value());
    fs::remove(first);
    fs::remove(round);

    const auto specOf = [](const avgen::scene::Composition& comp) {
        return comp.nodes().front()->animation.bodyCompensation;
    };
    const avgen::scene::BodyCompensationSpec before = specOf(**loaded);
    const avgen::scene::BodyCompensationSpec after = specOf(**again);
    // The values arrived in the first place -- an arm that only compared before to after would
    // pass on a parser that dropped every field on both sides.
    CHECK(before.enabled);
    CHECK(before.joint == "rig");
    CHECK(before.limits.maxDown == Approx(0.31f));
    CHECK(before.limits.iterations == 12u);
    CHECK(before.limits.compliance.z == Approx(0.3f));
    // ...and they are the same on the far side of the trip.
    CHECK(after == before);

    SECTION("a negative limit is refused rather than silently clamped") {
        nlohmann::json bad = doc;
        bad["nodes"][0]["animation"]["bodyCompensation"]["maxDown"] = -1.0f;
        const fs::path p = fs::temp_directory_path() / "avgen-bodycomp-bad.scene.json";
        std::ofstream(p) << bad.dump(1);
        auto result = avgen::scene::Composition::loadFile(p, registry);
        fs::remove(p);
        CHECK_FALSE(result.has_value());
    }
}
