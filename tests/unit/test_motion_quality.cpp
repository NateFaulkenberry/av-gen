// Motion quality metrics (Phase B §41) and gates (§44).
//
// **A gate whose metric cannot distinguish good from bad is a gate that always passes**, so every
// metric here is measured on a good clip *and* on a deliberately broken one, and the gate is shown
// rejecting the second. Without the bad arm this file would prove only that the code runs.
//
// The other property under test is that **unmeasured is not zero**. A clip with no contact track
// has a foot slide of zero and a clip with perfect contacts has a foot slide of zero; the first
// must not pass a gate on the strength of the second's number.

#include "scene/motion_quality.hpp"
#include "scene/animation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Skeleton legRig() {
    scene::Skeleton sk;
    sk.name = "walker";
    sk.joints.push_back(scene::Joint{"root.x", -1, scene::Transform{}});
    scene::Transform hip;
    hip.position = glm::vec3(0.0f, 1.0f, 0.0f);
    sk.joints.push_back(scene::Joint{"hip", 0, hip});
    // **The knee is well bent at rest, and that is the third time this fixture family has bitten.**
    // With the knee nearly straight (z = 0.1) the limb spans 1.0198 while the swing carries the
    // foot 0.6 forward, so root-to-tip reaches 1.166 -- ratio **1.1435** -- and the "clean" walk
    // failed its own limb-extension gate. The clip was fine; the fixture's leg had no slack to
    // swing into. At z = 0.35 the limb spans 1.22 and the same swing peaks at 0.955.
    scene::Transform knee;
    knee.position = glm::vec3(0.0f, 0.5f, 0.35f);
    sk.joints.push_back(scene::Joint{"knee", 0, knee});
    scene::Transform foot;
    foot.position = glm::vec3(0.0f, 0.0f, 0.0f);
    sk.joints.push_back(scene::Joint{"foot", 0, foot});
    sk.palette = {0, 1, 2, 3};
    sk.inverseBind.assign(4, glm::mat4(1.0f));
    return sk;
}

// A walk whose foot is genuinely still while it is down, and lifts cleanly while it is not.
// `slideDuringStance` breaks exactly one thing: the foot creeps while planted.
scene::AnimationClip walk(std::string name, float slideDuringStance, float bobDuringStance = 0.0f) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    scene::AnimationChannel foot;
    foot.joint = 3;
    foot.path = scene::AnimationPath::Translation;
    foot.interpolation = scene::Interpolation::Linear;
    const int frames = 31;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (t < 0.5f) {
            // Stance: down, and still unless we are breaking it.
            const float through = t / 0.5f;
            y = bobDuringStance * std::sin(through * 3.14159265f);
            z = slideDuringStance * through;
        } else {
            // Swing: up and forward, which every clip does and no metric should complain about.
            const float through = (t - 0.5f) / 0.5f;
            y = 0.25f * std::sin(through * 3.14159265f);
            z = slideDuringStance + (0.6f * through);
        }
        foot.times.push_back(t);
        foot.values.emplace_back(0.0f, y, z, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels.push_back(std::move(foot));
    return clip;
}

scene::MotionQualityOptions options() {
    scene::MotionQualityOptions o;
    o.contacts = {{"foot", scene::ContactKind::Foot}};
    o.limbs = {{"hip", "knee", "foot"}};
    return o;
}

} // namespace

TEST_CASE("the foot slide metric separates a clean walk from a sliding one",
          "[quality][phaseB][gate]") {
    // **The control arm is the whole test.** A metric that reported 0.004 on both would look
    // healthy and gate nothing.
    const scene::Skeleton sk = legRig();
    const scene::MotionQualityReport clean =
        scene::measureMotionQuality(sk, walk("clean", 0.0f), options());
    const scene::MotionQualityReport sliding =
        scene::measureMotionQuality(sk, walk("sliding", 0.30f), options());

    INFO("clean slide " << clean.footSlide.value << ", sliding " << sliding.footSlide.value);
    REQUIRE(clean.footSlide.measured);
    REQUIRE(sliding.footSlide.measured);
    CHECK(clean.footSlide.value < 0.05f);
    CHECK(sliding.footSlide.value > 0.15f);
    // ...and the separation is large, not a hair. A metric that distinguishes by 3% would be one
    // threshold away from useless.
    CHECK(sliding.footSlide.value > clean.footSlide.value * 4.0f);
}

TEST_CASE("the gate refuses the sliding clip and passes the clean one", "[quality][phaseB][gate]") {
    // §44's consequence. A gate that only logs is the dead-knob family in a new place: the number
    // is right, the message appears, and the bad variant goes into the pack anyway.
    const scene::Skeleton sk = legRig();
    scene::MotionQualityLimits limits;
    limits.maxFootSlide = 0.12f;

    const scene::MotionQualityVerdict good =
        scene::gateMotionQuality(scene::measureMotionQuality(sk, walk("clean", 0.0f), options()), limits);
    INFO(good.report());
    CHECK(good.pass);
    CHECK(good.failures.empty());

    const scene::MotionQualityVerdict bad =
        scene::gateMotionQuality(scene::measureMotionQuality(sk, walk("sliding", 0.30f), options()), limits);
    INFO(bad.report());
    CHECK_FALSE(bad.pass);
    REQUIRE_FALSE(bad.failures.empty());
    // The failure says which metric and by how much, because "rejected" with no number sends
    // whoever generated the variant back to guess.
    CHECK(bad.failures.front().find("foot slide") != std::string::npos);
}

TEST_CASE("contact height catches a foot that bobs through the ground", "[quality][phaseB]") {
    // A foot sliding along the ground and one sinking through it are different defects, and the
    // slide metric cannot tell them apart -- it is horizontal.
    const scene::Skeleton sk = legRig();
    const scene::MotionQualityReport flat =
        scene::measureMotionQuality(sk, walk("flat", 0.0f, 0.0f), options());
    // **The bob has to stay inside the plant band to be measurable at all.** At 0.12 the contact
    // detector correctly refuses to call the foot planted (ADR-546: low AND not changing height),
    // so there is no stance to measure the wander inside and both arms read zero -- the metric
    // looking where the mechanism has no authority, a fifth time. 0.03 is inside the band, so the
    // foot is planted and *then* wanders, which is the defect this metric is for.
    const scene::MotionQualityReport bobbing =
        scene::measureMotionQuality(sk, walk("bobbing", 0.0f, 0.03f), options());
    INFO("flat height wander " << flat.contactHeight.value << ", bobbing "
                               << bobbing.contactHeight.value);
    CHECK(bobbing.contactHeight.value > flat.contactHeight.value + 0.015f);
    // And the slide metric is blind to it, which is why both exist.
    CHECK(bobbing.footSlide.value == Approx(flat.footSlide.value).margin(0.02f));
}

TEST_CASE("unmeasured is not zero, and never fails a gate", "[quality][phaseB][gate]") {
    // **The distinction ADR-551 paid for once already.** A clip with no contact track has a foot
    // slide of zero, and so does a perfect one. If unmeasured metrics gated, every hand-authored
    // clip in this repository would be rejected for lacking a contact track nobody gave it.
    const scene::Skeleton sk = legRig();
    scene::MotionQualityOptions bare;   // no contacts, no limbs
    const scene::MotionQualityReport report =
        scene::measureMotionQuality(sk, walk("untracked", 0.30f), bare);

    CHECK_FALSE(report.footSlide.measured);
    CHECK(report.footSlide.value == Approx(0.0f));
    CHECK_FALSE(report.limbExtension.measured);
    // The same clip that failed the gate above passes it now, because nothing was measured -- and
    // the report says "--" rather than a number.
    const scene::MotionQualityVerdict v = scene::gateMotionQuality(report, {});
    INFO(report.report());
    CHECK(v.pass);
    CHECK(report.report().find("--") != std::string::npos);
}

TEST_CASE("reach and velocity error are honestly unmeasured for a clip alone",
          "[quality][phaseB]") {
    // §41 lists six metrics. Two of them need a live target and a controller's desired velocity,
    // which a clip on a skeleton does not have. Reported as unmeasured rather than filled with
    // zero, because zero is the *good* answer for both and would read as a pass.
    const scene::Skeleton sk = legRig();
    const scene::MotionQualityReport r =
        scene::measureMotionQuality(sk, walk("clip", 0.0f), options());
    CHECK_FALSE(r.reachError.measured);
    CHECK_FALSE(r.velocityError.measured);
    CHECK(r.footSlide.measured);   // ...while the ones that can be measured, are
}

TEST_CASE("limb extension catches a pose asking for a length the bones do not have",
          "[quality][phaseB][gate]") {
    // ADR-553's `reach` probe as a metric. A limb at 1.0 is straight; past it the pose is asking
    // for reach the skeleton cannot supply, and a *generated* variant that does it should not have
    // been generated.
    const scene::Skeleton sk = legRig();
    scene::AnimationClip stretched = walk("stretched", 0.0f);
    // Drag the foot far below the hip: root-to-tip exceeds the limb's own length.
    for (glm::vec4& v : stretched.channels.front().values) {
        v.y -= 1.2f;
    }
    const scene::MotionQualityReport r = scene::measureMotionQuality(sk, stretched, options());
    INFO("limb extension " << r.limbExtension.value);
    REQUIRE(r.limbExtension.measured);
    CHECK(r.limbExtension.value > 1.0f);

    scene::MotionQualityLimits limits;
    const scene::MotionQualityVerdict v = scene::gateMotionQuality(r, limits);
    CHECK_FALSE(v.pass);

    // The control: the unstretched clip stays inside its own limb.
    const scene::MotionQualityReport ok =
        scene::measureMotionQuality(sk, walk("ok", 0.0f), options());
    CHECK(ok.limbExtension.value <= 1.0f);
}
