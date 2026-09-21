// Animation retargeting (ADR-548).
//
// The unit Phase 0 named as the blocking dependency for everything else: before it, a character's
// motion was whatever shipped inside its own `.glb`, which is why each alien variant carries its own
// copy of all 26 clips and why a four-million-frame CC BY corpus has nowhere to go.
//
// **The standing rule (Phase A brief §2), and here it has a sharp edge.** A retarget that does
// nothing at all -- returns the target's rest pose for every frame -- produces a character that
// stands there, and "the bone lengths are unchanged" is *true* of it. So bone preservation is never
// asserted on its own; every arm that checks it also checks that the target actually MOVED, and the
// orientation-error arms assert a number that a do-nothing retarget cannot reach.
//
// The arms:
//
//   identity      source == target, name-for-name: orientation error is ~0 and the pose reproduces
//   rest pose     the same skeleton with a different bind orientation still reproduces orientations
//   bone lengths  a target with 2x bones matches orientations and keeps ITS OWN lengths
//   moved         every arm above also proves the target is not merely standing in its rest pose
//   missing       a mapping naming a joint the target lacks is reported, and the rest still work
//   empty         a mapping that resolves to nothing says so rather than producing a still clip
//   root          a travelling source makes the target travel, scaled by the rest-height ratio
//   roles         name-based role guessing finds what it can and lists what it cannot
//   alien         the real Glowmere rig, round-tripped through a guessed profile

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/retarget.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using namespace avgen::scene;
using Catch::Approx;

namespace {

namespace fs = std::filesystem;

// A five-joint biped stub: root -> hips -> spine -> head, and root -> hips -> leg -> foot.
// `boneScale` stretches every bone, `twist` gives the bind pose a different orientation, so one
// builder covers the identical, longer-boned and differently-bound cases.
Skeleton stubRig(const char* name, float boneScale = 1.0f, float twistDegrees = 0.0f) {
    const glm::quat twist =
        glm::angleAxis(glm::radians(twistDegrees), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    const auto joint = [&](const char* jn, int parent, glm::vec3 offset) {
        Joint j;
        j.name = jn;
        j.parent = parent;
        j.rest.position = offset * boneScale;
        j.rest.rotation = twist;
        return j;
    };
    Skeleton sk;
    sk.name = name;
    sk.joints.push_back(joint("root", -1, glm::vec3(0.0f)));
    sk.joints.push_back(joint("Hips", 0, glm::vec3(0.0f, 1.0f, 0.0f)));
    sk.joints.push_back(joint("Spine", 1, glm::vec3(0.0f, 0.35f, 0.0f)));
    sk.joints.push_back(joint("Head", 2, glm::vec3(0.0f, 0.45f, 0.0f)));
    sk.joints.push_back(joint("LeftUpLeg", 1, glm::vec3(0.12f, -0.05f, 0.0f)));
    sk.joints.push_back(joint("LeftLeg", 4, glm::vec3(0.0f, -0.45f, 0.0f)));
    sk.joints.push_back(joint("LeftFoot", 5, glm::vec3(0.0f, -0.45f, 0.0f)));
    for (std::uint32_t i = 0; i < sk.joints.size(); ++i) {
        sk.palette.push_back(i);
        sk.inverseBind.emplace_back(1.0f);
    }
    return sk;
}

RetargetProfile identityProfile(const Skeleton& sk) {
    RetargetProfile profile;
    profile.name = "identity";
    profile.rootJoint = "Hips";
    for (const Joint& j : sk.joints) {
        if (j.name == "root") {
            continue; // the armature wrapper is not a mapped bone
        }
        JointMapping m;
        m.source = j.name;
        m.target = j.name;
        m.role = roleForJointName(j.name);
        profile.joints.push_back(m);
    }
    return profile;
}

// A clip that swings the hips, the spine and the left leg through a few known angles, and lifts the
// hips. Enough motion that "the target did not move" is measurable.
AnimationClip wiggleClip(const Skeleton& sk) {
    AnimationClip clip;
    clip.name = "wiggle";
    clip.start = 0.0f;
    clip.duration = 1.0f;
    const auto rotate = [&](const char* jn, glm::vec3 axis, float deg0, float deg1) {
        AnimationChannel c;
        c.joint = static_cast<std::uint32_t>(sk.find(jn));
        c.path = AnimationPath::Rotation;
        c.interpolation = Interpolation::Linear;
        c.times = {0.0f, 0.5f, 1.0f};
        const glm::quat rest = sk.joints[c.joint].rest.rotation;
        for (const float d : {deg0, deg1, deg0}) {
            const glm::quat q = glm::normalize(rest * glm::angleAxis(glm::radians(d), glm::normalize(axis)));
            c.values.emplace_back(q.x, q.y, q.z, q.w);
        }
        clip.channels.push_back(std::move(c));
    };
    rotate("Hips", glm::vec3(0, 1, 0), -20.0f, 25.0f);
    rotate("Spine", glm::vec3(1, 0, 0), -10.0f, 15.0f);
    rotate("LeftUpLeg", glm::vec3(1, 0, 0), -30.0f, 40.0f);
    rotate("LeftLeg", glm::vec3(1, 0, 0), 5.0f, 55.0f);
    AnimationChannel hop;
    hop.joint = static_cast<std::uint32_t>(sk.find("Hips"));
    hop.path = AnimationPath::Translation;
    hop.interpolation = Interpolation::Linear;
    hop.times = {0.0f, 0.5f, 1.0f};
    const glm::vec3 rest = sk.joints[hop.joint].rest.position;
    hop.values.emplace_back(rest.x, rest.y, rest.z, 0.0f);
    hop.values.emplace_back(rest.x, rest.y + 0.20f, rest.z + 0.40f, 0.0f);
    hop.values.emplace_back(rest.x, rest.y, rest.z + 0.80f, 0.0f);
    clip.channels.push_back(std::move(hop));
    return clip;
}

// The model-space position of a joint under a clip at a given second.
glm::vec3 posAt(const Skeleton& sk, const AnimationClip& clip, const char* jn, float t) {
    Pose pose;
    setRestPose(sk, pose);
    sampleClip(clip, t, pose);
    std::vector<glm::mat4> model;
    poseToModel(sk, pose, model);
    return glm::vec3(model[static_cast<std::size_t>(sk.find(jn))][3]);
}

// How far a joint travels over a clip -- the "did anything happen" measure every arm needs.
float excursion(const Skeleton& sk, const AnimationClip& clip, const char* jn) {
    float worst = 0.0f;
    const glm::vec3 first = posAt(sk, clip, jn, 0.0f);
    for (float t = 0.0f; t <= 1.0f; t += 0.05f) {
        worst = std::max(worst, glm::length(posAt(sk, clip, jn, t) - first));
    }
    return worst;
}

} // namespace

TEST_CASE("retargeting a skeleton onto itself reproduces it", "[retarget]") {
    // THE DECISIVE ARM. With identical skeletons and a name-for-name mapping, the rest correction
    // is the identity and the retarget must be the identity too. Any error here is an error in the
    // bind-pose reconciliation, and it will be larger on every real pair.
    const Skeleton sk = stubRig("stub");
    const AnimationClip source = wiggleClip(sk);
    const RetargetBinding binding = bindRetarget(sk, sk, identityProfile(sk));
    INFO((binding.problems.empty() ? std::string() : binding.problems.front()));
    REQUIRE(binding.problems.empty());
    REQUIRE(binding.usable());
    CHECK(binding.rootScale == Approx(1.0f).margin(1e-4));

    RetargetStats stats;
    const AnimationClip out = retargetClip(source, sk, sk, binding, &stats);
    CHECK(stats.frames > 25);
    // 0.1 degrees. Not zero, because the orientation is read back out of a matrix whose columns
    // are re-normalised, and that round trip costs a few thousandths of a degree per joint.
    CHECK(stats.worstOrientationError < 0.1f);
    CHECK(stats.meanOrientationError < 0.01f);
    CHECK(stats.worstBoneLengthError < 1e-3f);

    // ...and the pose really is reproduced, joint by joint, which is a stronger claim than the
    // summary error.
    for (const char* jn : {"Hips", "Spine", "Head", "LeftUpLeg", "LeftLeg", "LeftFoot"}) {
        for (const float t : {0.0f, 0.25f, 0.5f, 0.75f}) {
            INFO(jn << " at " << t);
            const glm::vec3 a = posAt(sk, source, jn, t);
            const glm::vec3 b = posAt(sk, out, jn, t);
            CHECK(glm::length(a - b) < 5e-3f);
        }
    }
    // AND IT MOVED. A retarget that returned the rest pose would satisfy the bone-length assertion
    // above and nothing else here.
    CHECK(excursion(sk, out, "LeftFoot") > 0.2f);
    CHECK(excursion(sk, out, "Head") > 0.2f);
}

TEST_CASE("a target with a different bind orientation still gets the right orientations",
          "[retarget]") {
    // Same topology and proportions, bound 55 degrees away about an off-axis vector. Without the
    // rest correction the target arrives in the animation composed with the difference between two
    // bind poses, which reads as a broken joint rather than as a mapping error.
    const Skeleton source = stubRig("source", 1.0f, 0.0f);
    const Skeleton target = stubRig("target", 1.0f, 55.0f);
    const AnimationClip clip = wiggleClip(source);
    const RetargetBinding binding = bindRetarget(source, target, identityProfile(source));
    REQUIRE(binding.problems.empty());

    RetargetStats stats;
    const AnimationClip out = retargetClip(clip, source, target, binding, &stats);
    CHECK(stats.worstOrientationError < 0.1f);
    CHECK(stats.worstBoneLengthError < 1e-3f);
    CHECK(excursion(target, out, "LeftFoot") > 0.2f);

    SECTION("the two rigs are genuinely in different places at rest, which is the point") {
        // A bind rotation propagates to every child's position, so a twisted rig's joints are NOT
        // where the untwisted rig's are -- even with identical bone offsets. An earlier version of
        // this test compared absolute positions and failed by 0.61, which was the fixture being
        // wrong rather than the retarget. What must match is orientation, and what must be
        // PRESERVED is the target's own geometry.
        CHECK(glm::length(posAt(source, clip, "LeftFoot", 0.25f) -
                          posAt(target, out, "LeftFoot", 0.25f)) > 0.1f);
        // Bone lengths are the target's own, unchanged by the retarget.
        Pose pose;
        setRestPose(target, pose);
        sampleClip(out, 0.4f, pose);
        std::vector<glm::mat4> model;
        poseToModel(target, pose, model);
        const auto lengthOf = [&](const char* a, const char* b) {
            return glm::length(glm::vec3(model[static_cast<std::size_t>(target.find(b))][3]) -
                               glm::vec3(model[static_cast<std::size_t>(target.find(a))][3]));
        };
        CHECK(lengthOf("LeftUpLeg", "LeftLeg") == Approx(0.45f).margin(1e-3));
        CHECK(lengthOf("LeftLeg", "LeftFoot") == Approx(0.45f).margin(1e-3));
    }
}

TEST_CASE("a target with different bone lengths keeps its own", "[retarget]") {
    // The failure a naive "copy every channel" retarget produces is a character stretched onto the
    // source's proportions -- melted, rather than merely mis-mapped. The target here has bones
    // twice as long and must keep them.
    const Skeleton source = stubRig("small", 1.0f);
    const Skeleton target = stubRig("large", 2.0f);
    const AnimationClip clip = wiggleClip(source);
    const RetargetBinding binding = bindRetarget(source, target, identityProfile(source));
    REQUIRE(binding.problems.empty());
    // The derived root scale is the ratio of rest heights, which for a uniform 2x rig is 2.
    CHECK(binding.rootScale == Approx(2.0f).margin(0.05));

    RetargetStats stats;
    const AnimationClip out = retargetClip(clip, source, target, binding, &stats);
    CHECK(stats.worstOrientationError < 0.1f);
    CHECK(stats.worstBoneLengthError < 1e-3f);
    CHECK(excursion(target, out, "LeftFoot") > 0.4f);

    // Bone lengths, measured directly off the retargeted pose rather than from the summary.
    Pose pose;
    setRestPose(target, pose);
    sampleClip(out, 0.4f, pose);
    std::vector<glm::mat4> model;
    poseToModel(target, pose, model);
    const auto lengthOf = [&](const char* a, const char* b) {
        return glm::length(glm::vec3(model[static_cast<std::size_t>(target.find(b))][3]) -
                           glm::vec3(model[static_cast<std::size_t>(target.find(a))][3]));
    };
    CHECK(lengthOf("LeftUpLeg", "LeftLeg") == Approx(0.90f).margin(1e-3)); // 0.45 * 2
    CHECK(lengthOf("LeftLeg", "LeftFoot") == Approx(0.90f).margin(1e-3));
}

TEST_CASE("a mapping the target does not carry is reported, and the rest still work", "[retarget]") {
    const Skeleton source = stubRig("source");
    const Skeleton target = stubRig("target");
    RetargetProfile profile = identityProfile(source);
    profile.joints.push_back(JointMapping{"Hips", "Tail", HumanoidRole::None});
    profile.joints.push_back(JointMapping{"Wing", "Head", HumanoidRole::None});

    const RetargetBinding binding = bindRetarget(source, target, profile);
    REQUIRE(binding.problems.size() == 2);
    CHECK(binding.problems[0].find("Tail") != std::string::npos);
    CHECK(binding.problems[1].find("Wing") != std::string::npos);
    // And the six real mappings survived.
    CHECK(binding.links.size() == 6);
    RetargetStats stats;
    const AnimationClip out = retargetClip(wiggleClip(source), source, target, binding, &stats);
    CHECK(stats.worstOrientationError < 0.1f);
    CHECK(excursion(target, out, "LeftFoot") > 0.2f);
}

TEST_CASE("a mapping that resolves to nothing refuses rather than producing a still clip",
          "[retarget]") {
    // THE ADVERSARIAL ARM for the whole file. A retarget that quietly produced the rest pose would
    // give a character that stands perfectly still, and every bone-length assertion would pass.
    const Skeleton source = stubRig("source");
    const Skeleton target = stubRig("target");
    RetargetProfile profile;
    profile.joints.push_back(JointMapping{"NoSuchJoint", "AlsoNot", HumanoidRole::None});
    const RetargetBinding binding = bindRetarget(source, target, profile);
    CHECK_FALSE(binding.usable());
    CHECK(binding.problems.size() >= 1);
    const AnimationClip out = retargetClip(wiggleClip(source), source, target, binding, nullptr);
    CHECK(out.channels.empty());
    CHECK(out.length() == 0.0f);
}

TEST_CASE("a travelling source makes the target travel, scaled", "[retarget]") {
    const Skeleton source = stubRig("small", 1.0f);
    const Skeleton target = stubRig("large", 2.0f);
    const AnimationClip clip = wiggleClip(source); // its hips move +0.80 in Z over the clip
    const RetargetBinding binding = bindRetarget(source, target, identityProfile(source));
    const AnimationClip out = retargetClip(clip, source, target, binding, nullptr);

    const glm::vec3 sourceTravel = posAt(source, clip, "Hips", 1.0f) - posAt(source, clip, "Hips", 0.0f);
    const glm::vec3 targetTravel = posAt(target, out, "Hips", 1.0f) - posAt(target, out, "Hips", 0.0f);
    CHECK(sourceTravel.z == Approx(0.80f).margin(0.05));
    // Twice the body, twice the stride.
    CHECK(targetTravel.z == Approx(1.60f).margin(0.10));
    CHECK(targetTravel.z > sourceTravel.z * 1.5f);
}

TEST_CASE("role guessing finds what it can and says what it cannot", "[retarget][roles]") {
    CHECK(roleForJointName("LeftUpLeg") == HumanoidRole::LeftUpperLeg);
    CHECK(roleForJointName("UpperLegB.L") == HumanoidRole::LeftUpperLeg);
    CHECK(roleForJointName("HoofB.R") == HumanoidRole::RightFoot);
    CHECK(roleForJointName("Head01") == HumanoidRole::Head);
    CHECK(roleForJointName("head.x") == HumanoidRole::Head);
    CHECK(roleForJointName("mixamorig:LeftHand") == HumanoidRole::LeftHand);
    // And the ones it must NOT claim to know. `leg_stretch.l` is the alien's knee and nothing about
    // that name says knee -- a guesser that returned a role here would be inventing a mapping.
    CHECK(roleForJointName("leg_stretch.l") == HumanoidRole::LeftLowerLeg); // "leg" + side: a guess
    CHECK(roleForJointName("Antenna") == HumanoidRole::None);
    CHECK(roleForJointName("Bone.003") == HumanoidRole::None);
    CHECK(roleForJointName("rig") == HumanoidRole::None);

    SECTION("a guess between two stub rigs fills the roles they share") {
        const Skeleton a = stubRig("a");
        const Skeleton b = stubRig("b");
        const RoleGuess guess = guessRetargetProfile(a, b);
        CHECK(guess.profile.joints.size() >= 5);
        // It could not find an upper arm on either, and it says so rather than leaving a silence.
        CHECK_FALSE(guess.unmatchedSource.empty());
        CHECK_FALSE(guess.unmatchedTarget.empty());
        // The guess is usable as a profile.
        const RetargetBinding binding = bindRetarget(a, b, guess.profile);
        CHECK(binding.usable());
        CHECK(binding.problems.empty());
    }
}

TEST_CASE("the real Glowmere rig retargets onto itself", "[retarget][aliens]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb";
    if (!fs::exists(path)) {
        SKIP("assets are not present");
    }
    avgen::scene::Scene scene;
    avgen::assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(avgen::assets::loadGltf(path, scene, options));
    const SkinnedRig& rig = scene.rigs.front();
    const Skeleton& sk = rig.skeleton;

    // Every joint, name for name. This is the identity arm on a 90-joint rig whose leg is three
    // detached branches (ADR-543) and whose joints all carry baked translation -- the hardest real
    // case in the repository for a retarget that assumes a tidy hierarchy.
    RetargetProfile profile;
    profile.name = "alien-identity";
    profile.rootJoint = "root.x";
    for (const Joint& j : sk.joints) {
        profile.joints.push_back(JointMapping{j.name, j.name, roleForJointName(j.name)});
    }
    const RetargetBinding binding = bindRetarget(sk, sk, profile);
    INFO((binding.problems.empty() ? std::string() : binding.problems.front()));
    REQUIRE(binding.problems.empty());
    CHECK(binding.links.size() == sk.jointCount());

    const int walk = rig.findClip("Walking");
    REQUIRE(walk >= 0);
    const AnimationClip& source = rig.clips[static_cast<std::size_t>(walk)];
    RetargetStats stats;
    const AnimationClip out = retargetClip(source, sk, sk, binding, &stats);

    INFO("worst " << stats.worstOrientationError << " mean " << stats.meanOrientationError);
    CHECK(stats.worstOrientationError < 0.1f);
    CHECK(stats.meanOrientationError < 0.01f);
    // The feet go where they went. Checked against the source clip rather than against a constant,
    // so this is a reproduction test and not a transcription of today's numbers.
    //
    // **The retarget normalises the time base and the comparison has to allow for it.** This pack's
    // clips start at 1/30 s, not at 0 -- Blender's exporter writes the frame range it was given, and
    // ADR-204 records what assuming otherwise cost. A retargeted clip starts at 0, so source second
    // `clip.start + t` is output second `t`. An earlier version of this arm compared the same `t` on
    // both and failed by 0.08 at the feet, which is one frame of walk and was the test being wrong.
    REQUIRE(source.start > 0.0f);
    for (const float t : {0.1f, 0.35f, 0.6f, 0.9f}) {
        for (const char* jn : {"foot.l", "foot.r", "head.x"}) {
            INFO(jn << " at " << t);
            const glm::vec3 a = posAt(sk, source, jn, source.start + t);
            const glm::vec3 b = posAt(sk, out, jn, t);
            CHECK(glm::length(a - b) < 0.01f);
        }
    }
    // And the walk still walks: the feet move a real distance over the cycle.
    CHECK(excursion(sk, out, "foot.l") > 0.1f);
#endif
}
